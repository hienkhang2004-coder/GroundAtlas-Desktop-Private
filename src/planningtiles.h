#pragma once
#include "project.h"
#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QVector>

class QNetworkReply;
class QPainter;
class QRectF;

namespace ga {

struct HanoiTileLayerInfo {
 QString id;
 QString vi;
 QString en;
 QString urlTemplate;
 int minZoom = 10;
 int maxZoom = 17;
 bool hasBounds = false;
 double west = -180.0;
 double south = -85.0;
 double east = 180.0;
 double north = 85.0;
 double focusLat = 0.0;
 double focusLon = 0.0;
 QString localTilesDir;
};

struct HanoiTileComposite {
 QString layerId;
 int zoom = 16;
 int tilesTotal = 0;
 int tilesLoaded = 0;
 int tilesAvailable = 0;
 double siteLat = 0.0;
 double siteLon = 0.0;
 QPointF topLeft;
 QPointF topRight;
 QPointF bottomRight;
 QPointF bottomLeft;
 QImage image;

 bool valid() const { return !image.isNull() && !layerId.isEmpty() && tilesAvailable > 0; }
};

QVector<HanoiTileLayerInfo> hanoiPlanningLayers();
HanoiTileLayerInfo hanoiPlanningLayerInfo(const QString &layerId);
QString normalizePlanningLayerId(const QString &layerId);
bool isSupportedPlanningLayer(const QString &layerId);
QString hanoiPlanningTileTemplate(const QString &layerId);
void setCustomPlanningTileTemplate(const QString &urlTemplate);
QString customPlanningTileTemplate();
bool isLikelyInHanoiArea(double lat, double lon);
bool isLikelyInHaLongArea(double lat, double lon);
QString satelliteBasemapStyleJson();
void drawBasemapCornerSwitcher(QPainter &p, const QRectF &card, bool isSatelliteActive, bool hovered, bool darkUi, bool english);

class HanoiPlanningTileFetcher : public QObject {
 Q_OBJECT
public:
 explicit HanoiPlanningTileFetcher(QString cacheDirectory, QObject *parent = nullptr);
 ~HanoiPlanningTileFetcher() override;

 void fetchForSite(const Site &site, const QString &layerId = QStringLiteral("qhpk"));
 void cancel();

signals:
 void fetchProgress(int done, int total);
 void compositeReady(ga::HanoiTileComposite composite);

private:
 struct Job;
 QString cacheDirectory_;
 QNetworkAccessManager network_;
 quint64 generation_ = 0;
 QVector<QPointer<QNetworkReply>> activeReplies_;

 void finalizeJob(const std::shared_ptr<Job> &job);
};

} // namespace ga
