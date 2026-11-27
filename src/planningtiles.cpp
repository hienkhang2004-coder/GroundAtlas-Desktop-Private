#include "planningtiles.h"
#include "sitegeometry.h"
#include <QCache>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <memory>

namespace ga {
namespace {

int lonToTileX(double lon, int z) {
 const int n = 1 << z;
 return std::clamp(int(std::floor((lon + 180.0) / 360.0 * double(n))), 0, n - 1);
}

int latToTileY(double lat, int z) {
 const int n = 1 << z;
 const double clamped = std::clamp(lat, -85.05112878, 85.05112878);
 const double rad = clamped * M_PI / 180.0;
 const double y = (1.0 - std::asinh(std::tan(rad)) / M_PI) * 0.5 * double(n);
 return std::clamp(int(std::floor(y)), 0, n - 1);
}

double tileXToLon(int x, int z) {
 return double(x) / double(1 << z) * 360.0 - 180.0;
}

double tileYToLat(int y, int z) {
 const double n = M_PI - 2.0 * M_PI * double(y) / double(1 << z);
 return std::atan(std::sinh(n)) * 180.0 / M_PI;
}

QString &customTemplateStorage() {
 static QString tmpl;
 return tmpl;
}

// Ultra-low-latency local HTTP XYZ tile server for bundled planning pyramids (e.g. Ha Long CVDD).
// Serves sparse transparent PNG tiles from RAM cache (< 0.05 ms) and replies HTTP 204 No Content
// for empty tiles so MapLibre Native allocates zero GPU textures and suffers zero zoom stutter.
class LocalPlanningTileServer : public QObject {
public:
 explicit LocalPlanningTileServer(QString rootDir, QObject *parent = nullptr)
  : QObject(parent), rootDir_(std::move(rootDir)), ramCache_(40 * 1024 * 1024) {
  server_.listen(QHostAddress::LocalHost, 0);
  connect(&server_, &QTcpServer::newConnection, this, [this] {
   while (QTcpSocket *sock = server_.nextPendingConnection()) {
    sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    auto buf = std::make_shared<QByteArray>();
    connect(sock, &QTcpSocket::readyRead, this, [this, sock, buf] {
     buf->append(sock->readAll());
     if (buf->size() > 16384) {
      sock->disconnectFromHost();
      return;
     }
     for (;;) {
      const int hdrEnd = buf->indexOf("\r\n\r\n");
      if (hdrEnd < 0) break;
      const QByteArray req = buf->left(hdrEnd);
      buf->remove(0, hdrEnd + 4);
      handleRequest(sock, req);
     }
    });
    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
   }
  });
 }

 quint16 port() const { return server_.isListening() ? server_.serverPort() : 0; }

private:
 void handleRequest(QTcpSocket *sock, const QByteArray &req) {
  const int nl = req.indexOf("\r\n");
  const QByteArray firstLine = (nl >= 0) ? req.left(nl) : req;
  const QList<QByteArray> parts = firstLine.split(' ');
  const bool keepAlive = !req.contains("Connection: close") && !req.contains("connection: close")
   && (parts.size() >= 3 && parts[2].contains("1.1"));
  const QByteArray connHdr = keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";

  if (parts.size() < 2 || (parts[0] != "GET" && parts[0] != "HEAD")) {
   sock->write("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
   sock->disconnectFromHost();
   return;
  }

  static const QRegularExpression rx(QStringLiteral("^/([a-z0-9_-]+)/([0-9]{1,2})/([0-9]{1,8})/([0-9]{1,8})\\.png(?:\\?.*)?$"));
  const QString path = QString::fromUtf8(parts[1]);
  const auto m = rx.match(path);
  if (!m.hasMatch()) {
   sock->write("HTTP/1.1 204 No Content\r\nCache-Control: public, max-age=86400, immutable\r\n" + connHdr + "\r\n");
   if (!keepAlive) sock->disconnectFromHost();
   return;
  }

  const QString relKey = m.captured(1) + QLatin1Char('/') + m.captured(2) + QLatin1Char('/') + m.captured(3) + QLatin1Char('/') + m.captured(4) + QStringLiteral(".png");
  QByteArray payload;
  if (const auto *cached = ramCache_.object(relKey)) {
   payload = *cached;
  } else {
   QFile f(rootDir_ + QLatin1Char('/') + relKey);
   if (f.open(QIODevice::ReadOnly)) {
    payload = f.readAll();
   }
   // Cache both hits and misses (empty QByteArray) so sparse areas never re-stat disk.
   ramCache_.insert(relKey, new QByteArray(payload), std::max(64, int(payload.size())));
  }

  if (payload.isEmpty()) {
   sock->write("HTTP/1.1 204 No Content\r\nCache-Control: public, max-age=86400, immutable\r\n" + connHdr + "\r\n");
  } else {
   QByteArray hdr;
   hdr.reserve(160);
   hdr += "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: ";
   hdr += QByteArray::number(payload.size());
   hdr += "\r\nCache-Control: public, max-age=86400, immutable\r\n";
   hdr += connHdr;
   hdr += "\r\n";
   sock->write(hdr);
   if (parts[0] == "GET") sock->write(payload);
  }
  if (!keepAlive) sock->disconnectFromHost();
 }

 QString rootDir_;
 QTcpServer server_;
 QCache<QString, QByteArray> ramCache_;
};

QString localPlanningTileUrl(const QString &slug) {
 static QPointer<LocalPlanningTileServer> s_server;
 if (!s_server && QCoreApplication::instance()) {
  s_server = new LocalPlanningTileServer(appDataPath(QStringLiteral("planning-tiles")), QCoreApplication::instance());
 }
 if (s_server && s_server->port() > 0) {
  return QStringLiteral("http://127.0.0.1:%1/%2/{z}/{x}/{y}.png").arg(s_server->port()).arg(slug);
 }
 return QUrl::fromLocalFile(appDataPath(QStringLiteral("planning-tiles/") + slug)).toString() + QStringLiteral("/{z}/{x}/{y}.png");
}

} // namespace

void setCustomPlanningTileTemplate(const QString &urlTemplate) {
 const QString trimmed = urlTemplate.trimmed();
 const QUrl u(trimmed);
 if (trimmed.isEmpty()) {
  customTemplateStorage().clear();
  return;
 }
 if (u.isValid() && (u.scheme() == "https" || (u.scheme() == "http" && (u.host() == "127.0.0.1" || u.host() == "localhost")))
     && trimmed.contains("{z}") && trimmed.contains("{x}") && trimmed.contains("{y}")) {
  customTemplateStorage() = trimmed;
 }
}

QString customPlanningTileTemplate() {
 return customTemplateStorage();
}

QVector<HanoiTileLayerInfo> hanoiPlanningLayers() {
 return {
  {
   QStringLiteral("qhpk"),
   QStringLiteral("QH Phân khu · Sử dụng đất (Hà Nội)"),
   QStringLiteral("Hanoi Subdivision Land-Use Zoning"),
   QStringLiteral("https://land.vietpalm.group/data/qhpk/{z}/{x}/{y}.png"),
   10, 17,
   false, -180.0, -85.0, 180.0, 85.0,
   21.0288, 105.8525,
   {}
  },
  {
   QStringLiteral("khsdd"),
   QStringLiteral("Kế hoạch sử dụng đất đến 2030 (Hà Nội)"),
   QStringLiteral("Hanoi Land-Use Plan 2030"),
   QStringLiteral("https://land.vietpalm.group/data/khsdd/{z}/{x}/{y}.png"),
   10, 17,
   false, -180.0, -85.0, 180.0, 85.0,
   21.0288, 105.8525,
   {}
  },
  {
   QStringLiteral("qhct"),
   QStringLiteral("QH Chi tiết dự án 1/500 (Hà Nội)"),
   QStringLiteral("Hanoi Detailed Project Plans (1:500)"),
   QStringLiteral("https://land.vietpalm.group/data/qhct/{z}/{x}/{y}.png"),
   10, 17,
   false, -180.0, -85.0, 180.0, 85.0,
   21.0288, 105.8525,
   {}
  },
  {
   QStringLiteral("halong_cvdd"),
   QStringLiteral("QH Hạ Long · CV Đại Dương & Cảng tàu QT (2025)"),
   QStringLiteral("Ha Long · Ocean Park & Cruise Port Plan (2025)"),
   localPlanningTileUrl(QStringLiteral("halong-cvdd")),
   12, 18,
   true, 107.021872, 20.930336, 107.092800, 20.975907,
   20.953340, 107.055938,
   appDataPath(QStringLiteral("planning-tiles/halong-cvdd"))
  },
  {
   QStringLiteral("custom"),
   QStringLiteral("Nguồn XYZ Quy hoạch tùy chỉnh (URL)"),
   QStringLiteral("Custom Planning XYZ Tile URL"),
   customTemplateStorage(),
   8, 18,
   false, -180.0, -85.0, 180.0, 85.0,
   0.0, 0.0,
   {}
  }
 };
}

bool isSupportedPlanningLayer(const QString &layerId) {
 return layerId == "qhpk" || layerId == "khsdd" || layerId == "qhct"
  || layerId == "halong_cvdd" || layerId == "custom";
}

QString normalizePlanningLayerId(const QString &layerId) {
 return isSupportedPlanningLayer(layerId) ? layerId : QStringLiteral("qhpk");
}

HanoiTileLayerInfo hanoiPlanningLayerInfo(const QString &layerId) {
 const QString norm = normalizePlanningLayerId(layerId);
 const auto layers = hanoiPlanningLayers();
 for (const auto &info : layers) {
  if (info.id == norm) return info;
 }
 return layers.first();
}

QString hanoiPlanningTileTemplate(const QString &layerId) {
 const auto info = hanoiPlanningLayerInfo(layerId);
 if (!info.urlTemplate.isEmpty()) return info.urlTemplate;
 return hanoiPlanningLayers().first().urlTemplate;
}

bool isLikelyInHanoiArea(double lat, double lon) {
 return lat >= 20.52 && lat <= 21.40 && lon >= 105.25 && lon <= 106.05;
}

bool isLikelyInHaLongArea(double lat, double lon) {
 return lat >= 20.89 && lat <= 21.02 && lon >= 106.98 && lon <= 107.14;
}

struct HanoiPlanningTileFetcher::Job {
 quint64 generation = 0;
 Site site;
 QString layerId;
 int zoom = 16;
 int xMin = 0, xMax = 0, yMin = 0, yMax = 0;
 int tilesTotal = 0;
 int tilesDone = 0;
 int tilesAvailable = 0;
 QImage canvas;
};

HanoiPlanningTileFetcher::HanoiPlanningTileFetcher(QString cacheDirectory, QObject *parent)
 : QObject(parent), cacheDirectory_(std::move(cacheDirectory)) {}

HanoiPlanningTileFetcher::~HanoiPlanningTileFetcher() {
 cancel();
}

void HanoiPlanningTileFetcher::cancel() {
 ++generation_;
 for (const auto &reply : activeReplies_) {
  if (reply) {
   disconnect(reply, nullptr, this, nullptr);
   reply->abort();
   reply->deleteLater();
  }
 }
 activeReplies_.clear();
}

void HanoiPlanningTileFetcher::fetchForSite(const Site &site, const QString &layerId) {
 cancel();
 const QString normalized = normalizePlanningLayerId(layerId);
 const auto info = hanoiPlanningLayerInfo(normalized);
 const QString urlTmpl = info.urlTemplate;
 if (urlTmpl.isEmpty() && info.localTilesDir.isEmpty()) { emit compositeReady({}); return; }

 // Compute exact site geographic bbox
 const auto boundary = site.boundary.isEmpty() ? primitiveBoundary(site) : site.boundary;
 double west = 180.0, east = -180.0, south = 85.0, north = -85.0;
 for (const auto &pt : boundary) {
  const auto ll = geographicPoint(pt, site);
  west = std::min(west, ll.x());
  east = std::max(east, ll.x());
  south = std::min(south, ll.y());
  north = std::max(north, ll.y());
 }

 // Pick highest crisp native zoom (up to info.maxZoom) keeping tile grid <= 6x6 (36 tiles)
 const double envR = envelopeRadius(site);
 int z = info.maxZoom;
 if (envR > 1350.0) z = std::min(info.maxZoom, 15);
 else if (envR > 650.0) z = std::min(info.maxZoom, 16);
 else if (envR > 360.0) z = std::min(info.maxZoom, 17);
 z = std::clamp(z, info.minZoom, info.maxZoom);

 int xMin = lonToTileX(west, z), xMax = lonToTileX(east, z);
 int yMin = latToTileY(north, z), yMax = latToTileY(south, z);
 while (z > info.minZoom && ((xMax - xMin + 1) > 6 || (yMax - yMin + 1) > 6)) {
  --z;
  xMin = lonToTileX(west, z);
  xMax = lonToTileX(east, z);
  yMin = latToTileY(north, z);
  yMax = latToTileY(south, z);
 }

 const int cols = xMax - xMin + 1;
 const int rows = yMax - yMin + 1;
 const int total = cols * rows;
 if (cols <= 0 || rows <= 0 || total > 64) {
  emit compositeReady({});
  return;
 }

 auto job = std::make_shared<Job>();
 job->generation = generation_;
 job->site = site;
 job->layerId = normalized;
 job->zoom = z;
 job->xMin = xMin;
 job->xMax = xMax;
 job->yMin = yMin;
 job->yMax = yMax;
 job->tilesTotal = total;
 job->canvas = QImage(cols * 256, rows * 256, QImage::Format_ARGB32_Premultiplied);
 job->canvas.fill(Qt::transparent);

 // Instant direct-disk fast path for bundled local XYZ pyramids (zero HTTP, < 5ms composite)
 if (!info.localTilesDir.isEmpty()) {
  QPainter p(&job->canvas);
  for (int ty = yMin; ty <= yMax; ++ty) {
   for (int tx = xMin; tx <= xMax; ++tx) {
    const QString path = info.localTilesDir + QStringLiteral("/%1/%2/%3.png").arg(z).arg(tx).arg(ty);
    if (QFile::exists(path)) {
     QImage tile(path);
     if (!tile.isNull()) {
      const int dx = (tx - xMin) * 256;
      const int dy = (ty - yMin) * 256;
      if (tile.width() == 256 && tile.height() == 256) p.drawImage(dx, dy, tile);
      else p.drawImage(dx, dy, tile.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
      ++job->tilesAvailable;
     }
    }
    ++job->tilesDone;
   }
  }
  p.end();
  emit fetchProgress(job->tilesDone, job->tilesTotal);
  finalizeJob(job);
  return;
 }

 const QString tileDir = cacheDirectory_.isEmpty()
  ? QString()
  : (cacheDirectory_ + "/qh-tiles/" + normalized);
 if (!tileDir.isEmpty()) QDir().mkpath(tileDir);

 int pendingNetwork = 0;
 for (int ty = yMin; ty <= yMax; ++ty) {
  for (int tx = xMin; tx <= xMax; ++tx) {
   const int dx = (tx - xMin) * 256;
   const int dy = (ty - yMin) * 256;
   const QString cacheFile = tileDir.isEmpty()
    ? QString()
    : (tileDir + QString("/%1_%2_%3.png").arg(z).arg(tx).arg(ty));

   if (!cacheFile.isEmpty() && QFile::exists(cacheFile)) {
    QImage cached(cacheFile);
    if (!cached.isNull()) {
     QPainter p(&job->canvas);
     p.drawImage(dx, dy, cached.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
     ++job->tilesAvailable;
     ++job->tilesDone;
     continue;
    }
   }

   QString urlStr = urlTmpl;
   urlStr.replace("{z}", QString::number(z));
   urlStr.replace("{x}", QString::number(tx));
   urlStr.replace("{y}", QString::number(ty));
   QNetworkRequest req{QUrl(urlStr)};
   req.setTransferTimeout(12000);
   req.setRawHeader("User-Agent", "GroundAtlasDesktop/2.2");
   req.setRawHeader("Accept", "image/png,image/*");

   ++pendingNetwork;
   QNetworkReply *reply = network_.get(req);
   activeReplies_.append(reply);
   connect(reply, &QNetworkReply::finished, this, [this, job, reply, dx, dy, cacheFile] {
    reply->deleteLater();
    if (job->generation != generation_) return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status == 200) {
     const QByteArray bytes = reply->readAll();
     QImage tile;
     if (tile.loadFromData(bytes, "PNG") && !tile.isNull()) {
      QPainter p(&job->canvas);
      p.drawImage(dx, dy, tile.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
      ++job->tilesAvailable;
      if (!cacheFile.isEmpty()) {
       QSaveFile sf(cacheFile);
       if (sf.open(QIODevice::WriteOnly)) {
        sf.write(bytes);
        sf.commit();
       }
      }
     }
    }
    ++job->tilesDone;
    emit fetchProgress(job->tilesDone, job->tilesTotal);
    if (job->tilesDone >= job->tilesTotal) {
     finalizeJob(job);
    }
   });
  }
 }

 emit fetchProgress(job->tilesDone, job->tilesTotal);
 if (pendingNetwork == 0) {
  finalizeJob(job);
 }
}

void HanoiPlanningTileFetcher::finalizeJob(const std::shared_ptr<Job> &job) {
 if (job->generation != generation_) return;
 activeReplies_.clear();

 HanoiTileComposite out;
 out.layerId = job->layerId;
 out.zoom = job->zoom;
 out.tilesTotal = job->tilesTotal;
 out.tilesLoaded = job->tilesDone;
 out.tilesAvailable = job->tilesAvailable;
 out.siteLat = job->site.lat;
 out.siteLon = job->site.lon;

 if (job->tilesAvailable > 0) {
  const double west = tileXToLon(job->xMin, job->zoom);
  const double east = tileXToLon(job->xMax + 1, job->zoom);
  const double north = tileYToLat(job->yMin, job->zoom);
  const double south = tileYToLat(job->yMax + 1, job->zoom);
  out.topLeft = localPoint(west, north, job->site);
  out.topRight = localPoint(east, north, job->site);
  out.bottomRight = localPoint(east, south, job->site);
  out.bottomLeft = localPoint(west, south, job->site);
  out.image = std::move(job->canvas);
 }
 emit compositeReady(std::move(out));
}

} // namespace ga
