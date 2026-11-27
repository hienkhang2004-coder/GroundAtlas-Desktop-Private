#include "planning.h"
#include "earcut.hpp"
#include "geoimport.h"
#include "sitegeometry.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ga {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Deg = 180.0 / kPi;

// WGS84 ellipsoid
constexpr double kA = 6378137.0;
constexpr double kF = 1.0 / 298.257223563;
constexpr double kE2 = 2.0 * kF - kF * kF;
constexpr double kEp2 = kE2 / (1.0 - kE2);

// Official BTNMT 7-parameter Helmert shift: VN-2000 -> WGS84 (Decision 05/2007/QD-BTNMT)
constexpr double kDx = -191.90441429;
constexpr double kDy = -39.30318279;
constexpr double kDz = -111.45032835;
constexpr double kRxRad = (-0.00928836 / 3600.0) * kDeg2Rad;
constexpr double kRyRad = (0.01975479 / 3600.0) * kDeg2Rad;
constexpr double kRzRad = (-0.00427372 / 3600.0) * kDeg2Rad;
constexpr double kDs = 0.252906278e-6;

double meridionalArc(double latRad) {
 const double e4 = kE2 * kE2, e6 = e4 * kE2;
 return kA * ((1.0 - kE2 / 4.0 - 3.0 * e4 / 64.0 - 5.0 * e6 / 256.0) * latRad
              - (3.0 * kE2 / 8.0 + 3.0 * e4 / 32.0 + 45.0 * e6 / 1024.0) * std::sin(2.0 * latRad)
              + (15.0 * e4 / 256.0 + 45.0 * e6 / 1024.0) * std::sin(4.0 * latRad)
              - (35.0 * e6 / 3072.0) * std::sin(6.0 * latRad));
}

bool pointInGeoPoly(const QPointF &lonLat, const QJsonArray &ring) {
 if (ring.size() < 3) return false;
 QPolygonF poly;
 poly.reserve(ring.size());
 for (const auto &v : ring) {
  const auto a = v.toArray();
  if (a.size() >= 2) poly.append({a[0].toDouble(), a[1].toDouble()});
 }
 return poly.containsPoint(lonLat, Qt::OddEvenFill);
}

QJsonObject loadHanoiMasterplanJson(const QString &planningDir) {
 const QStringList candidates = {
  planningDir + "/hanoi_masterplan_2045.json",
  QDir::cleanPath(planningDir + "/../planning/hanoi_masterplan_2045.json"),
 };
 for (const auto &p : candidates) {
  QFile f(p);
  if (f.open(QIODevice::ReadOnly)) {
   const auto doc = QJsonDocument::fromJson(f.readAll());
   if (doc.isObject()) return doc.object();
  }
 }
 return {};
}

QPolygonF geoRingToLocal(const QJsonArray &coords, const Site &site) {
 QPolygonF out;
 out.reserve(coords.size());
 for (const auto &v : coords) {
  const auto a = v.toArray();
  if (a.size() >= 2) out.append(localPoint(a[0].toDouble(), a[1].toDouble(), site));
 }
 return out;
}

bool intersectsSiteBounds(const QPolygonF &poly, double radius) {
 if (poly.isEmpty()) return false;
 const QRectF siteRect(-radius * 1.15, -radius * 1.15, radius * 2.3, radius * 2.3);
 return poly.boundingRect().intersects(siteRect);
}

double distPointToSegment(const QPointF &p, const QPointF &a, const QPointF &b) {
 const QPointF ab = b - a;
 const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
 if (l2 < 1e-8) return QLineF(p, a).length();
 const double t = std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / l2, 0.0, 1.0);
 return QLineF(p, a + ab * t).length();
}

double minDistToPolyline(const QPointF &p, const QPolygonF &line) {
 double best = 1e9;
 for (int i = 0; i + 1 < line.size(); ++i) {
  best = std::min(best, distPointToSegment(p, line[i], line[i + 1]));
 }
 return best;
}

double parseHeightHint(const QString &text, double fallback) {
 const QRegularExpression re("(\\d+(?:\\.\\d+)?)\\s*(m|met|tầng|tang|fl|f)\\b", QRegularExpression::CaseInsensitiveOption);
 const auto m = re.match(text);
 if (!m.hasMatch()) return fallback;
 const double val = m.captured(1).toDouble();
 const QString unit = m.captured(2).toLower();
 if (unit.startsWith('t') || unit.startsWith('f')) return std::clamp(val * 3.5, 4.0, 260.0);
 return std::clamp(val, 4.0, 260.0);
}

PlanningCrs resolveEffectiveCrs(PlanningCrs requested, const QVector<QPointF> &samplePts) {
 if (requested != PlanningCrs::Auto) return requested;
 if (samplePts.isEmpty()) return PlanningCrs::Wgs84;
 double maxAbsX = 0.0, maxAbsY = 0.0;
 double avgX = 0.0, avgY = 0.0;
 for (const auto &p : samplePts) {
  maxAbsX = std::max(maxAbsX, std::abs(p.x()));
  maxAbsY = std::max(maxAbsY, std::abs(p.y()));
  avgX += p.x(); avgY += p.y();
 }
 avgX /= samplePts.size();
 avgY /= samplePts.size();
 if (maxAbsX <= 180.0 && maxAbsY <= 90.0 && avgX > 95.0 && avgY > 5.0) return PlanningCrs::Wgs84;
 if (avgX >= 250000.0 && avgX <= 850000.0 && avgY >= 1000000.0 && avgY <= 2700000.0) return PlanningCrs::Vn2000Hanoi3;
 return PlanningCrs::SiteLocal;
}

QPointF projectToSiteLocal(const QPointF &rawPt, PlanningCrs crs, const Site &site) {
 if (crs == PlanningCrs::Wgs84) return localPoint(rawPt.x(), rawPt.y(), site);
 if (crs == PlanningCrs::Vn2000Hanoi3) {
  const QPointF ll = vn2000ToWgs84(rawPt.x(), rawPt.y(), 105.0, 0.9999);
  return localPoint(ll.x(), ll.y(), site);
 }
 if (crs == PlanningCrs::Vn2000National6) {
  const QPointF ll = vn2000ToWgs84(rawPt.x(), rawPt.y(), 105.0, 0.9996);
  return localPoint(ll.x(), ll.y(), site);
 }
 return rawPt;
}

} // namespace

// ── 1. Official TT 04/2022/TT-BXD & QCVN 01:2021/BXD Zoning Catalog ──

QVector<PlanningZoneDef> planningZoneCatalog() {
 return {
  {"DI_SAN", "Khu di sản · Bảo tồn (H1-1)", "Heritage Conservation", QColor("#d9ab62"), QColor("#8c5f18"), 16.0, 55},
  {"ODT",    "Đất ở đô thị (ODT / NO)",     "Urban Residential",     QColor("#f4a498"), QColor("#c45548"), 24.0, 65},
  {"HH",     "Đất hỗn hợp (HH · TM-VP-Ở)",  "Mixed-Use Corridor",    QColor("#f28d42"), QColor("#b85314"), 55.0, 60},
  {"CC",     "Đất công cộng đô thị (CC)",   "Civic & Public",        QColor("#e85a5a"), QColor("#a32424"), 32.0, 45},
  {"GD",     "Đất giáo dục · Trường học",   "Education",             QColor("#f2ca58"), QColor("#ab8016"), 18.0, 40},
  {"YT",     "Đất y tế · Bệnh viện (YT)",   "Healthcare",            QColor("#de7cb5"), QColor("#94346c"), 32.0, 40},
  {"CX",     "Cây xanh · Công viên (CX)",   "Parks & Green Space",   QColor("#69bf73"), QColor("#2b7836"), 0.0,  5},
  {"MN",     "Mặt nước · Sông hồ (MN)",     "Water & River Axis",    QColor("#65b7dd"), QColor("#256e94"), 0.0,  0},
  {"GT",     "Giao thông · Mở đường · Metro","Transit & Widening",   QColor("#e11d48"), QColor("#881337"), 0.0,  0},
  {"CN",     "Công nghiệp · Công nghệ cao", "Industry & Tech",       QColor("#a892ca"), QColor("#5f468a"), 24.0, 50},
  {"HTKT",   "Hạ tầng kỹ thuật · Bãi đỗ xe","Utilities & Parking",   QColor("#9aa3af"), QColor("#475569"), 12.0, 40},
 };
}

PlanningZoneDef classifyPlanningCode(const QString &rawText) {
 const QString s = rawText.trimmed().toUpper();
 const auto cat = planningZoneCatalog();
 auto byCode = [&](const QString &c) {
  for (const auto &d : cat) if (d.code == c) return d;
  return cat[1];
 };
 if (s.contains("DI_SAN") || s.contains("DISAN") || s.contains("DI SẢN") || s.contains("BAO_TON") || s.contains("BẢO TỒN") || s.contains("HERITAGE") || s.contains("HISTORIC") || s.startsWith("DT")) return byCode("DI_SAN");
 if (s.startsWith("HH") || s.contains("HON_HOP") || s.contains("HỖN HỢP") || s.contains("MIXED") || s.contains("COMMERCIAL") || s.contains("OFFICE") || s.contains("TM-DV") || s.contains("TMDV")) return byCode("HH");
 if (s.startsWith("CC") || s.contains("CONG_CONG") || s.contains("CÔNG CỘNG") || s.contains("CIVIC") || s.contains("PUBLIC") || s.contains("GOVERNMENT")) return byCode("CC");
 if (s.startsWith("GD") || s.startsWith("TH") || s.startsWith("MG") || s.contains("GIAO_DUC") || s.contains("GIÁO DỤC") || s.contains("SCHOOL") || s.contains("UNIVERSITY") || s.contains("TRƯỜNG")) return byCode("GD");
 if (s.startsWith("YT") || s.startsWith("BV") || s.contains("Y_TE") || s.contains("Y TẾ") || s.contains("HOSPITAL") || s.contains("HEALTH")) return byCode("YT");
 if (s.startsWith("CX") || s.startsWith("CV") || s.contains("CAY_XANH") || s.contains("CÂY XANH") || s.contains("CONG_VIEN") || s.contains("CÔNG VIÊN") || s.contains("PARK") || s.contains("GREEN") || s.contains("FOREST")) return byCode("CX");
 if (s.startsWith("MN") || s.contains("MAT_NUOC") || s.contains("MẶT NƯỚC") || s.contains("WATER") || s.contains("RIVER") || s.contains("LAKE") || s.contains("SÔNG") || s.contains("HỒ")) return byCode("MN");
 if (s.startsWith("GT") || s.contains("GIAO_THONG") || s.contains("GIAO THÔNG") || s.contains("METRO") || s.contains("ROAD") || s.contains("CHI_GIOI") || s.contains("CHỈ GIỚI")) return byCode("GT");
 if (s.startsWith("CN") || s.contains("CONG_NGHIEP") || s.contains("CÔNG NGHIỆP") || s.contains("INDUSTRIAL")) return byCode("CN");
 if (s.startsWith("HT") || s.startsWith("BDX") || s.startsWith("BĐX") || s.contains("PARKING") || s.contains("UTILITY")) return byCode("HTKT");
 return byCode("ODT");
}

// ── 2. Exact Ellipsoidal VN-2000 <-> WGS84 Conversion ──

QPointF vn2000ToWgs84(double easting, double northing, double centralMeridianDeg, double scaleFactor) {
 const double x = easting - 500000.0;
 const double y = northing;
 const double M = y / scaleFactor;
 const double e1 = (1.0 - std::sqrt(1.0 - kE2)) / (1.0 + std::sqrt(1.0 - kE2));
 const double mu = M / (kA * (1.0 - kE2 / 4.0 - 3.0 * kE2 * kE2 / 64.0 - 5.0 * kE2 * kE2 * kE2 / 256.0));
 const double phi1 = mu
  + (3.0 * e1 / 2.0 - 27.0 * std::pow(e1, 3) / 32.0) * std::sin(2.0 * mu)
  + (21.0 * e1 * e1 / 16.0 - 55.0 * std::pow(e1, 4) / 32.0) * std::sin(4.0 * mu)
  + (151.0 * std::pow(e1, 3) / 96.0) * std::sin(6.0 * mu)
  + (1097.0 * std::pow(e1, 4) / 512.0) * std::sin(8.0 * mu);

 const double sinP = std::sin(phi1), cosP = std::cos(phi1), tanP = std::tan(phi1);
 const double C1 = kEp2 * cosP * cosP;
 const double T1 = tanP * tanP;
 const double N1 = kA / std::sqrt(1.0 - kE2 * sinP * sinP);
 const double R1 = kA * (1.0 - kE2) / std::pow(1.0 - kE2 * sinP * sinP, 1.5);
 const double D = x / (N1 * scaleFactor);

 const double latVn = phi1 - (N1 * tanP / R1) * (D * D / 2.0
  - (5.0 + 3.0 * T1 + 10.0 * C1 - 4.0 * C1 * C1 - 9.0 * kEp2) * std::pow(D, 4) / 24.0
  + (61.0 + 90.0 * T1 + 298.0 * C1 + 45.0 * T1 * T1 - 252.0 * kEp2 - 3.0 * C1 * C1) * std::pow(D, 6) / 720.0);
 const double lonVn = centralMeridianDeg * kDeg2Rad + (D
  - (1.0 + 2.0 * T1 + C1) * std::pow(D, 3) / 6.0
  + (5.0 - 2.0 * C1 + 28.0 * T1 - 3.0 * C1 * C1 + 8.0 * kEp2 + 24.0 * T1 * T1) * std::pow(D, 5) / 120.0) / cosP;

 // Geodetic VN-2000 -> ECEF -> 7-param Helmert -> Geodetic WGS84
 const double sL = std::sin(latVn), cL = std::cos(latVn);
 const double N = kA / std::sqrt(1.0 - kE2 * sL * sL);
 const double X = N * cL * std::cos(lonVn);
 const double Y = N * cL * std::sin(lonVn);
 const double Z = N * (1.0 - kE2) * sL;

 const double s = 1.0 + kDs;
 const double Xw = kDx + s * (X - kRzRad * Y + kRyRad * Z);
 const double Yw = kDy + s * (kRzRad * X + Y - kRxRad * Z);
 const double Zw = kDz + s * (-kRyRad * X + kRxRad * Y + Z);

 const double p = std::hypot(Xw, Yw);
 double latW = std::atan2(Zw, p * (1.0 - kE2));
 for (int i = 0; i < 6; ++i) {
  const double nw = kA / std::sqrt(1.0 - kE2 * std::sin(latW) * std::sin(latW));
  latW = std::atan2(Zw + kE2 * nw * std::sin(latW), p);
 }
 const double lonW = std::atan2(Yw, Xw);
 return {lonW * kRad2Deg, latW * kRad2Deg};
}

QPointF wgs84ToVn2000(double lon, double lat, double centralMeridianDeg, double scaleFactor) {
 const double latR = lat * kDeg2Rad, lonR = lon * kDeg2Rad;
 const double sL = std::sin(latR), cL = std::cos(latR);
 const double N = kA / std::sqrt(1.0 - kE2 * sL * sL);
 const double Xw = N * cL * std::cos(lonR);
 const double Yw = N * cL * std::sin(lonR);
 const double Zw = N * (1.0 - kE2) * sL;

 // Inverse Helmert WGS84 -> VN-2000
 const double s = 1.0 - kDs;
 const double X = -kDx + s * (Xw + kRzRad * Yw - kRyRad * Zw);
 const double Y = -kDy + s * (-kRzRad * Xw + Yw + kRxRad * Zw);
 const double Z = -kDz + s * (kRyRad * Xw - kRxRad * Yw + Zw);

 const double p = std::hypot(X, Y);
 double latV = std::atan2(Z, p * (1.0 - kE2));
 for (int i = 0; i < 6; ++i) {
  const double nv = kA / std::sqrt(1.0 - kE2 * std::sin(latV) * std::sin(latV));
  latV = std::atan2(Z + kE2 * nv * std::sin(latV), p);
 }
 const double lonV = std::atan2(Y, X);

 const double sinP = std::sin(latV), cosP = std::cos(latV), tanP = std::tan(latV);
 const double Nv = kA / std::sqrt(1.0 - kE2 * sinP * sinP);
 const double T = tanP * tanP;
 const double C = kEp2 * cosP * cosP;
 const double A = cosP * (lonV - centralMeridianDeg * kDeg2Rad);
 const double M = meridionalArc(latV);

 const double easting = 500000.0 + scaleFactor * Nv * (A + (1.0 - T + C) * std::pow(A, 3) / 6.0
  + (5.0 - 18.0 * T + T * T + 72.0 * C - 58.0 * kEp2) * std::pow(A, 5) / 120.0);
 const double northing = scaleFactor * (M + Nv * tanP * (A * A / 2.0
  + (5.0 - T + 9.0 * C + 4.0 * C * C) * std::pow(A, 4) / 24.0
  + (61.0 - 58.0 * T + T * T + 600.0 * C - 330.0 * kEp2) * std::pow(A, 6) / 720.0));
 return {easting, northing};
}

// ── 3. Planning Image Overlay Persistence ──

QJsonObject PlanningImageOverlay::toJson() const {
 return {
  {"enabled", enabled},
  {"image_path", imagePath},
  {"opacity", opacity},
  {"center_x", centerX},
  {"center_y", centerY},
  {"width_m", widthM},
  {"rotation_deg", rotationDeg},
 };
}

PlanningImageOverlay PlanningImageOverlay::fromJson(const QJsonObject &v) {
 PlanningImageOverlay o;
 o.enabled = v["enabled"].toBool(false);
 o.imagePath = v["image_path"].toString();
 o.opacity = std::clamp(v["opacity"].toDouble(0.56), 0.05, 1.0);
 o.centerX = v["center_x"].toDouble(0.0);
 o.centerY = v["center_y"].toDouble(0.0);
 o.widthM = std::clamp(v["width_m"].toDouble(1000.0), 20.0, 20000.0);
 o.rotationDeg = v["rotation_deg"].toDouble(0.0);
 if (!o.imagePath.isEmpty() && QFileInfo::exists(o.imagePath)) {
  o.image.load(o.imagePath);
 }
 return o;
}

// ── 4. Hanoi Subdivision Detection (QĐ 1668/QĐ-TTg & QĐ 6075/QĐ-UBND) ──

HanoiSubdivisionInfo detectHanoiSubdivision(double lat, double lon, const QString &planningDir) {
 HanoiSubdivisionInfo info;
 const auto root = loadHanoiMasterplanJson(planningDir);
 if (root.isEmpty()) return info;
 const QPointF ll(lon, lat);
 for (const auto &sv : root["subdivisions"].toArray()) {
  const auto o = sv.toObject();
  if (pointInGeoPoly(ll, o["ring"].toArray())) {
   info.matched = true;
   info.code = o["code"].toString();
   info.nameVi = o["name_vi"].toString();
   info.nameEn = o["name_en"].toString();
   info.decision = o["decision"].toString();
   info.maxHeightNote = o["max_height_note"].toString();
   info.densityNote = o["density_note"].toString();
   info.orientationVi = o["orientation_vi"].toString();
   info.orientationEn = o["orientation_en"].toString();
   info.defaultMaxHeightM = o["max_height_m"].toDouble(24.0);
   return info;
  }
 }
 return info;
}

// ── 5. AutoCAD DXF + GeoJSON VN-2000 Planning Vector Importer ──

PlanningCustomData importPlanningVectorFile(const QString &filePath,
                                            const Site &site,
                                            PlanningCrs crs,
                                            QString *errorOut) {
 PlanningCustomData out;
 const QFileInfo fi(filePath);
 if (!fi.exists() || !fi.isFile()) {
  if (errorOut) *errorOut = "Planning file not found.";
  return out;
 }
 out.sourceName = fi.fileName();
 out.sourcePath = fi.absoluteFilePath();
 const QString ext = fi.suffix().toLower();

 struct RawPoly { QString layer, label; QPolygonF pts; bool closed = false; double heightHint = 0.0; int bcrHint = 0; };
 QVector<RawPoly> rawPolys;
 QVector<QPair<QPointF, QString>> dxfLabels;
 QVector<QPointF> allSamples;

 if (ext == "geojson" || ext == "json") {
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) {
   if (errorOut) *errorOut = "Cannot open GeoJSON planning file.";
   return out;
  }
  const auto root = QJsonDocument::fromJson(f.readAll()).object();
  const auto feats = root["features"].toArray();
  for (const auto &fv : feats) {
   const auto fo = fv.toObject();
   const auto props = fo["properties"].toObject();
   const auto geom = fo["geometry"].toObject();
   const QString gType = geom["type"].toString();
   QString codeStr;
   for (const char *k : {"code", "ky_hieu", "loai_dat", "zoning", "landuse", "layer", "name", "chuc_nang"}) {
    if (!props[QLatin1String(k)].toString().isEmpty()) { codeStr = props[QLatin1String(k)].toString(); break; }
   }
   const QString lbl = props["label"].toString(props["name"].toString(codeStr));
   double hHint = props["max_height"].toDouble(props["chieu_cao"].toDouble(0.0));
   if (hHint <= 0.0 && props.contains("tang_cao")) hHint = props["tang_cao"].toDouble() * 3.5;
   if (hHint <= 0.0) hHint = parseHeightHint(lbl, 0.0);
   const int bcrHint = props["bcr"].toInt(props["mdxd"].toInt(0));

   auto pushCoords = [&](const QJsonArray &arr, bool closed) {
    QPolygonF ring;
    for (const auto &cv : arr) {
     const auto c = cv.toArray();
     if (c.size() >= 2) {
      const QPointF pt(c[0].toDouble(), c[1].toDouble());
      ring.append(pt);
      if (allSamples.size() < 400) allSamples.append(pt);
     }
    }
    if (ring.size() >= (closed ? 3 : 2)) rawPolys.append({codeStr, lbl, ring, closed, hHint, bcrHint});
   };
   if (gType == "Polygon") {
    const auto rings = geom["coordinates"].toArray();
    if (!rings.isEmpty()) pushCoords(rings[0].toArray(), true);
   } else if (gType == "MultiPolygon") {
    for (const auto &pv : geom["coordinates"].toArray()) {
     const auto rings = pv.toArray();
     if (!rings.isEmpty()) pushCoords(rings[0].toArray(), true);
    }
   } else if (gType == "LineString") {
    pushCoords(geom["coordinates"].toArray(), false);
   } else if (gType == "MultiLineString") {
    for (const auto &lv : geom["coordinates"].toArray()) pushCoords(lv.toArray(), false);
   }
  }
 } else if (ext == "dxf") {
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
   if (errorOut) *errorOut = "Cannot open DXF planning file.";
   return out;
  }
  QTextStream in(&f);
  QString curEnt, curLayer, curText;
  QPolygonF curPts;
  bool curClosed = false;
  double curX = 0.0, curY = 0.0, x2 = 0.0, y2 = 0.0;
  bool hasX = false, hasY = false, inEntities = false;

  auto flushEntity = [&]() {
   if (curEnt == "LWPOLYLINE" && curPts.size() >= 2) {
    const bool cl = curClosed || (curPts.size() >= 3 && QLineF(curPts.first(), curPts.last()).length() < 0.5);
    rawPolys.append({curLayer, curLayer, curPts, cl, parseHeightHint(curLayer, 0.0), 0});
   } else if (curEnt == "LINE" && hasX && hasY) {
    QPolygonF seg{QPointF(curX, curY), QPointF(x2, y2)};
    rawPolys.append({curLayer, curLayer, seg, false, 0.0, 0});
   } else if ((curEnt == "TEXT" || curEnt == "MTEXT") && !curText.trimmed().isEmpty()) {
    dxfLabels.append({QPointF(curX, curY), curText.trimmed()});
   }
   curEnt.clear(); curLayer.clear(); curText.clear();
   curPts.clear(); curClosed = false; hasX = hasY = false;
  };

  while (!in.atEnd()) {
   const QString codeLine = in.readLine().trimmed();
   if (in.atEnd()) break;
   const QString valLine = in.readLine().trimmed();
   const int code = codeLine.toInt();

   if (code == 2 && valLine == "ENTITIES") { inEntities = true; continue; }
   if (inEntities && code == 0 && valLine == "ENDSEC") { flushEntity(); break; }
   if (!inEntities) continue;

   if (code == 0) {
    flushEntity();
    curEnt = valLine.toUpper();
   } else if (code == 8) {
    curLayer = valLine;
   } else if (code == 1 && (curEnt == "TEXT" || curEnt == "MTEXT")) {
    curText = valLine;
   } else if (code == 70 && curEnt == "LWPOLYLINE") {
    curClosed = (valLine.toInt() & 1) != 0;
   } else if (code == 10) {
    curX = valLine.toDouble(); hasX = true;
   } else if (code == 20) {
    curY = valLine.toDouble(); hasY = true;
    if (curEnt == "LWPOLYLINE" && hasX) {
     curPts.append({curX, curY});
     if (allSamples.size() < 400) allSamples.append({curX, curY});
     hasX = false;
    } else if ((curEnt == "LINE" || curEnt == "TEXT" || curEnt == "MTEXT") && allSamples.size() < 400) {
     allSamples.append({curX, curY});
    }
   } else if (code == 11) {
    x2 = valLine.toDouble();
   } else if (code == 21) {
    y2 = valLine.toDouble();
   }
  }
  flushEntity();
 } else {
  if (errorOut) *errorOut = "Unsupported planning file format (use .dxf or .geojson).";
  return out;
 }

 const PlanningCrs effCrs = resolveEffectiveCrs(crs, allSamples);
 out.crs = effCrs;

 // Project DXF labels to site local metres so we can associate zone codes inside polygons!
 QVector<QPair<QPointF, QString>> localLabels;
 localLabels.reserve(dxfLabels.size());
 for (const auto &dl : dxfLabels) {
  localLabels.append({projectToSiteLocal(dl.first, effCrs, site), dl.second});
 }

 for (const auto &rp : rawPolys) {
  QPolygonF localRing;
  localRing.reserve(rp.pts.size());
  for (const auto &pt : rp.pts) localRing.append(projectToSiteLocal(pt, effCrs, site));
  if (!intersectsSiteBounds(localRing, std::max(800.0, site.radius * 3.0))) continue;

  QString codeHint = rp.layer;
  QString lbl = rp.label;
  double hHint = rp.heightHint;
  if (rp.closed && localRing.size() >= 3) {
   for (const auto &ll : localLabels) {
    if (localRing.containsPoint(ll.first, Qt::OddEvenFill)) {
     codeHint = ll.second + " " + codeHint;
     lbl = ll.second;
     if (hHint <= 0.0) hHint = parseHeightHint(ll.second, 0.0);
     break;
    }
   }
   const auto zDef = classifyPlanningCode(codeHint);
   PlanningVectorZone z;
   z.code = zDef.code;
   z.label = lbl.isEmpty() ? zDef.vi : lbl;
   z.ring = localRing;
   z.fill = zDef.fill;
   z.stroke = zDef.stroke;
   z.maxHeightM = (hHint > 0.0) ? hHint : zDef.defaultMaxHeightM;
   z.maxBcrPct = (rp.bcrHint > 0) ? rp.bcrHint : zDef.defaultMaxBcrPct;
   z.source = (ext == "dxf") ? "imported_dxf" : "imported_geojson";
   out.zones.append(std::move(z));
  } else if (localRing.size() >= 2) {
   PlanningCorridor cor;
   cor.id = "custom_" + QString::number(out.corridors.size());
   cor.name = lbl.isEmpty() ? "Chỉ giới đường đỏ (nhập)" : lbl;
   cor.kind = "road_widening";
   cor.polyline = localRing;
   cor.color = QColor("#e11d48");
   cor.widthM = 12.0;
   cor.status = out.sourceName;
   out.corridors.append(std::move(cor));
  }
 }

 if (out.empty() && errorOut) {
  *errorOut = QString::fromUtf8("Không tìm thấy đối tượng quy hoạch nằm trong vùng khu đất (thử đổi hệ tọa độ VN-2000 / WGS84).");
 }
 return out;
}

// ── 6. Compute Future Planning Overlay for All 4 Planning Levels ──

FuturePlanningOverlay computeFuturePlanningOverlay(const Project &project,
                                                   const QString &levelId,
                                                   const QString &planningDir,
                                                   const PlanningCustomData *customData,
                                                   const PlotDesign *plot,
                                                   double opacity) {
 FuturePlanningOverlay out;
 out.levelId = levelId;
 out.opacity = std::clamp(opacity, 0.15, 1.0);
 out.subdivision = detectHanoiSubdivision(project.site.lat, project.site.lon, planningDir);

 const auto root = loadHanoiMasterplanJson(planningDir);
 const double r = project.site.radius;

 // 1. Load relevant Hanoi Masterplan 2045 corridors (Metro, Ring Roads, Bridges, Red River Axis)
 if (!root.isEmpty()) {
  for (const auto &cv : root["corridors"].toArray()) {
   const auto o = cv.toObject();
   const QPolygonF poly = geoRingToLocal(o["coords"].toArray(), project.site);
   if (!intersectsSiteBounds(poly, r * 1.4)) continue;
   PlanningCorridor c;
   c.id = o["id"].toString();
   c.name = o["name"].toString();
   c.kind = o["kind"].toString();
   c.polyline = poly;
   c.color = QColor(o["color"].toString("#7c3aed"));
   c.widthM = o["width_m"].toDouble(12.0);
   c.underground = o["underground"].toBool(false);
   c.status = o["status"].toString();
   out.corridors.append(std::move(c));
  }
  for (const auto &sv : root["stations"].toArray()) {
   const auto o = sv.toObject();
   const QPointF pt = localPoint(o["lon"].toDouble(), o["lat"].toDouble(), project.site);
   if (std::hypot(pt.x(), pt.y()) > r * 1.35) continue;
   PlanningStation st;
   st.code = o["code"].toString();
   st.name = o["name"].toString();
   st.line = o["line"].toString();
   st.pos = pt;
   st.color = QColor(o["color"].toString("#7c3aed"));
   st.underground = o["underground"].toBool(true);
   out.stations.append(std::move(st));
  }
 }

 // 2. Extract live OSM proposed / under-construction features from the open project
 int osmProposedCount = 0;
 for (const auto &f : project.features) {
  const QString hw = f.tags["highway"].toString().toLower();
  const QString rw = f.tags["railway"].toString().toLower();
  const QString lu = f.tags["landuse"].toString().toLower();
  const QString bd = f.tags["building"].toString().toLower();
  const bool isProposedWay = (hw == "proposed" || hw == "construction" || rw == "proposed" || rw == "construction" ||
                              f.tags.contains("proposed:highway") || f.tags.contains("proposed:railway"));
  if (isProposedWay && !f.rings.isEmpty() && f.rings[0].size() >= 2) {
   PlanningCorridor c;
   c.id = "osm_" + f.id;
   c.name = f.tags["name"].toString(hw == "construction" ? "Đường đang thi công (OSM)" : "Tuyến đề xuất (OSM)");
   c.kind = "osm_proposed";
   c.polyline = f.rings[0];
   c.color = (rw == "proposed" || rw == "construction") ? QColor("#7c3aed") : QColor("#e11d48");
   c.widthM = 10.0;
   c.status = "OpenStreetMap proposed/construction";
   out.corridors.append(std::move(c));
   ++osmProposedCount;
  } else if ((lu == "construction" || bd == "construction") && f.geometry == Geometry::Polygon && !f.rings.isEmpty()) {
   const auto def = classifyPlanningCode("HH");
   out.zones.append({def.code, "Đang xây dựng (OSM)", f.rings[0], def.fill, def.stroke, 36.0, 55, "osm_inferred"});
   ++osmProposedCount;
  }
 }

 // 3. Merge custom user-imported VN-2000 DXF / GeoJSON planning data (highest priority!)
 const bool hasCustom = (customData && !customData->empty());
 if (hasCustom) {
  for (const auto &z : customData->zones) out.zones.append(z);
  for (const auto &c : customData->corridors) out.corridors.append(c);
 }

 // 4. Load bundled Hanoi reference zoning envelopes for the open site (e.g. H1-1B Hoàn Kiếm, Sông Hồng...)
 if (levelId != "qhc-2045" && !root.isEmpty()) {
  for (const auto &zv : root["zoning_envelopes"].toArray()) {
   const auto o = zv.toObject();
   const QPolygonF poly = geoRingToLocal(o["ring"].toArray(), project.site);
   if (!intersectsSiteBounds(poly, r * 1.15)) continue;
   const auto def = classifyPlanningCode(o["code"].toString());
   PlanningVectorZone z;
   z.code = def.code;
   z.label = o["label"].toString(def.vi);
   z.ring = poly;
   z.fill = def.fill;
   z.stroke = def.stroke;
   z.maxHeightM = o["max_height_m"].toDouble(def.defaultMaxHeightM);
   z.maxBcrPct = o["max_bcr_pct"].toInt(def.defaultMaxBcrPct);
   z.source = "hanoi_masterplan";
   out.zones.append(std::move(z));
  }
 }

 // 5. For QHPK 1/2000 & QHCT 1/500: if no bundled/custom zoning covers all parcels, classify OSM landuse/buildings into TT 04/2022/TT-BXD
 if (levelId == "qhpk-2000" || (levelId == "qhct-500" && out.zones.isEmpty())) {
  for (const auto &f : project.features) {
   if (f.geometry != Geometry::Polygon || f.rings.isEmpty() || f.rings[0].size() < 3) continue;
   QString tagHint;
   if (f.tags.contains("importLayer") && f.tags["importLayer"].toString() == "planning") {
    tagHint = f.tags["zoning"].toString(f.tags["name"].toString("HH"));
   } else if (f.tags.contains("historic") || f.tags.contains("heritage")) {
    tagHint = "DI_SAN";
   } else if (f.tags["water"].isString() || f.tags["natural"].toString() == "water") {
    tagHint = "MN";
   } else if (f.tags["leisure"].toString() == "park" || f.tags["leisure"].toString() == "garden" ||
              f.tags["landuse"].toString() == "grass" || f.tags["landuse"].toString() == "forest") {
    tagHint = "CX";
   } else if (f.tags["amenity"].toString() == "school" || f.tags["amenity"].toString() == "university" || f.tags["building"].toString() == "school") {
    tagHint = "GD";
   } else if (f.tags["amenity"].toString() == "hospital" || f.tags["amenity"].toString() == "clinic") {
    tagHint = "YT";
   } else if (f.tags["amenity"].toString() == "townhall" || f.tags["building"].toString() == "civic" || f.tags["building"].toString() == "public" || f.tags["building"].toString() == "government") {
    tagHint = "CC";
   } else if (f.tags["building"].toString() == "commercial" || f.tags["building"].toString() == "office" || f.tags["building"].toString() == "hotel" || f.tags["landuse"].toString() == "commercial") {
    tagHint = "HH";
   } else if (f.tags["landuse"].toString() == "residential") {
    tagHint = "ODT";
   } else {
    continue;
   }
   const auto def = classifyPlanningCode(tagHint);
   double maxH = def.defaultMaxHeightM;
   if (out.subdivision.matched && out.subdivision.defaultMaxHeightM > 0.0 && maxH > 0.0) {
    maxH = std::min(maxH, out.subdivision.defaultMaxHeightM);
   }
   out.zones.append({def.code, def.vi, f.rings[0], def.fill, def.stroke, maxH, def.defaultMaxBcrPct, "osm_inferred"});
  }
 }

 // If Design Plot is active in QHCT 1/500, also add a statutory 3D envelope right on the user's Design Plot!
 if (levelId == "qhct-500" && plot && plot->active) {
  const auto def = classifyPlanningCode("HH");
  const double capH = out.subdivision.matched ? std::max(16.0, out.subdivision.defaultMaxHeightM) : 36.0;
  out.zones.append({"HH", QString("Lô đồ án · Khung QH ≤ %1 m").arg(qRound(capH)), plot->polygon(), def.fill, def.stroke, capH, 60, "plot_envelope"});
 }

 // 6. Clip 3D-visible planning polygons & corridors cleanly to the site boundary so 3D envelopes sit flush on the site disk!
 {
  QVector<PlanningVectorZone> clippedZones;
  clippedZones.reserve(out.zones.size());
  for (const auto &z : out.zones) {
   Feature f;
   f.geometry = Geometry::Polygon;
   f.rings = {z.ring};
   for (const auto &part : clipToSite(f, project.site)) {
    if (!part.rings.isEmpty() && part.rings[0].size() >= 3) {
     auto cz = z;
     cz.ring = part.rings[0];
     clippedZones.append(std::move(cz));
    }
   }
  }
  out.zones = std::move(clippedZones);

  QVector<PlanningCorridor> clippedCors;
  clippedCors.reserve(out.corridors.size());
  for (const auto &c : out.corridors) {
   Feature f;
   f.geometry = Geometry::Line;
   f.rings = {c.polyline};
   for (const auto &part : clipToSite(f, project.site)) {
    if (!part.rings.isEmpty() && part.rings[0].size() >= 2) {
     auto cc = c;
     cc.polyline = part.rings[0];
     clippedCors.append(std::move(cc));
    }
   }
  }
  out.corridors = std::move(clippedCors);
 }

 // 7. Mode-specific configuration, Diff comparison & Legends
 const QString subPrefix = out.subdivision.matched
  ? QString("%1 (%2)").arg(out.subdivision.code, out.subdivision.decision)
  : QString::fromUtf8("Quy hoạch mở rộng");

 if (levelId == "qhc-2045") {
  out.show3DCorridors = true;
  out.titleVi = QString::fromUtf8("QH Chung Hà Nội 2045 · Metro & Hạ tầng (1/25.000)");
  out.titleEn = "Hanoi Master Plan 2045 · Metro & Corridors (1:25k)";
  out.summaryVi = QString::fromUtf8("%1 · %2 tuyến hạ tầng · %3 ga Metro · %4 OSM đề xuất")
   .arg(subPrefix).arg(out.corridors.size()).arg(out.stations.size()).arg(osmProposedCount);
  out.summaryEn = QString("%1 · %2 corridors · %3 Metro stations · %4 OSM proposed")
   .arg(subPrefix).arg(out.corridors.size()).arg(out.stations.size()).arg(osmProposedCount);
  out.legendItems = {
   {QColor("#7c3aed"), "Metro ngầm / tương lai"},
   {QColor("#e11d48"), "Đường Vành đai / Cầu mới"},
   {QColor("#0284c7"), "Trục cảnh quan Sông Hồng"},
  };
 } else if (levelId == "qhpk-2000") {
  out.titleVi = QString::fromUtf8("QH Phân khu · Sử dụng đất TT 04/2022 (1/2.000)");
  out.titleEn = "Zoning Plan · TT 04/2022 Land Use (1:2k)";
  out.summaryVi = QString::fromUtf8("%1 · %2 ô quy hoạch%3")
   .arg(subPrefix).arg(out.zones.size()).arg(hasCustom ? QString::fromUtf8(" (+ file nhập)") : QString());
  out.summaryEn = QString("%1 · %2 planning zones%3")
   .arg(subPrefix).arg(out.zones.size()).arg(hasCustom ? " (+ imported)" : "");
  out.legendItems = {
   {QColor("#d9ab62"), "Di sản"},
   {QColor("#f4a498"), "ODT (Ở)"},
   {QColor("#f28d42"), "HH (Hỗn hợp)"},
   {QColor("#e85a5a"), "CC"},
   {QColor("#69bf73"), "CX (Cây xanh)"},
   {QColor("#65b7dd"), "MN (Mặt nước)"},
  };
 } else if (levelId == "qhct-500") {
  out.show3DEnvelopes = true;
  out.show3DCorridors = true;
  out.titleVi = QString::fromUtf8("QH Chi tiết · Khung chiều cao 3D (1/500)");
  out.titleEn = "Detailed Plan · 3D Height Control Envelope (1:500)";
  out.summaryVi = QString::fromUtf8("%1 · %2 · %3 khung khống chế 3D")
   .arg(subPrefix, out.subdivision.matched ? out.subdivision.maxHeightNote : QString::fromUtf8("Khống chế chiều cao"), QString::number(out.zones.size()));
  out.summaryEn = QString("%1 · %2 3D height envelopes").arg(subPrefix).arg(out.zones.size());
  out.legendItems = {
   {QColor("#d9ab62"), "Khung bảo tồn ≤16m"},
   {QColor("#f28d42"), "Khung hỗn hợp 32–60m"},
   {QColor("#7c3aed"), "Hành lang Metro"},
  };
 } else if (levelId == "compare") {
  out.show3DCorridors = true;
  int impactCount = 0, headroomCount = 0, heritageCount = 0;
  for (const auto &f : project.features) {
   if (!f.building() || f.geometry != Geometry::Polygon || f.rings.isEmpty()) continue;
   const QPointF cen = f.bounds().center();
   bool inCorridor = false;
   for (const auto &c : out.corridors) {
    const double buf = c.underground ? 14.0 : (c.widthM * 0.75 + 6.0);
    if (minDistToPolyline(cen, c.polyline) <= buf) { inCorridor = true; break; }
   }
   for (const auto &st : out.stations) {
    if (QLineF(cen, st.pos).length() <= 38.0) { inCorridor = true; break; }
   }
   if (inCorridor) {
    out.diffBuildings.append({f.rings[0], QColor(225, 29, 72, 185)}); // Red: inside future corridor/station zone
    ++impactCount;
    continue;
   }
   // Check height headroom vs subdivision/zoning envelope
   double capH = out.subdivision.matched ? out.subdivision.defaultMaxHeightM : 28.0;
   bool isHeritage = false;
   for (const auto &z : out.zones) {
    if (z.ring.containsPoint(cen, Qt::OddEvenFill)) {
     if (z.code == "DI_SAN") isHeritage = true;
     if (z.maxHeightM > 0.0) capH = z.maxHeightM;
     break;
    }
   }
   const double curH = f.knownHeight() ? f.height : 9.0;
   if (isHeritage) {
    out.diffBuildings.append({f.rings[0], QColor(201, 155, 86, 130)}); // Ochre: heritage keep
    ++heritageCount;
   } else if (curH + 6.0 < capH) {
    out.diffBuildings.append({f.rings[0], QColor(245, 158, 11, 135)}); // Amber: redevelopment headroom
    ++headroomCount;
   }
  }
  out.titleVi = QString::fromUtf8("Đối chiếu Hiện trạng ↔ Quy hoạch tương lai");
  out.titleEn = "Existing vs. Future Planning Diff";
  out.summaryVi = QString::fromUtf8("%1 · %2 ảnh hưởng hành lang · %3 dư địa tái thiết · %4 bảo tồn")
   .arg(subPrefix).arg(impactCount).arg(headroomCount).arg(heritageCount);
  out.summaryEn = QString("%1 · %2 corridor-impacted · %3 headroom · %4 heritage")
   .arg(subPrefix).arg(impactCount).arg(headroomCount).arg(heritageCount);
  out.legendItems = {
   {QColor("#e11d48"), "Hành lang Metro / Mở đường"},
   {QColor("#f59e0b"), "Dư địa tầng cao tương lai"},
   {QColor("#c99b56"), "Vùng bảo tồn di sản"},
  };
 }

 return out;
}

// ── 7. 3D Statutory Height Control Envelopes & Metro Alignments in OpenGL 3.3 ──

void appendPlanning3DToMesh(Mesh &mesh,
                            const FuturePlanningOverlay &planning,
                            const std::function<double(const QPointF &)> &terrainZ) {
 if (!planning.valid()) return;

 auto addLine3 = [&](const QPointF &a, double az, const QPointF &b, double bz, const QColor &col) {
  mesh.lines.append({float(a.x()), float(a.y()), float(az), 0, 0, 0, float(col.redF()), float(col.greenF()), float(col.blueF()), float(col.alphaF()), 0.0f});
  mesh.lines.append({float(b.x()), float(b.y()), float(bz), 0, 0, 0, float(col.redF()), float(col.greenF()), float(col.blueF()), float(col.alphaF()), 0.0f});
 };
 auto addTriT = [&](const QPointF &a, double az, const QPointF &b, double bz, const QPointF &c, double cz, const QVector3D &n, const QColor &col) {
  const float r = float(col.redF()), g = float(col.greenF()), bl = float(col.blueF()), al = float(col.alphaF());
  mesh.transparent.append({float(a.x()), float(a.y()), float(az), n.x(), n.y(), n.z(), r, g, bl, al, 0.0f});
  mesh.transparent.append({float(b.x()), float(b.y()), float(bz), n.x(), n.y(), n.z(), r, g, bl, al, 0.0f});
  mesh.transparent.append({float(c.x()), float(c.y()), float(cz), n.x(), n.y(), n.z(), r, g, bl, al, 0.0f});
 };

 // 1. Translucent 3D Height Control Envelopes (QHCT 1/500)
 if (planning.show3DEnvelopes) {
  for (const auto &z : planning.zones) {
   if (z.ring.size() < 3 || z.maxHeightM < 2.0) continue;
   const QPointF cen = z.ring.boundingRect().center();
   const double baseZ = terrainZ ? terrainZ(cen) : 0.0;
   const double topZ = baseZ + z.maxHeightM;

   QColor wallCol = z.fill;
   wallCol.setAlphaF(float(std::clamp(planning.opacity * 0.30, 0.10, 0.36)));
   QColor roofCol = z.fill.lighter(115);
   roofCol.setAlphaF(float(std::clamp(planning.opacity * 0.24, 0.08, 0.30)));
   QColor edgeCol = z.stroke;
   edgeCol.setAlphaF(0.92f);

   // Walls + glowing wireframe top & vertical edges
   const int n = z.ring.size();
   for (int i = 0; i < n; ++i) {
    const QPointF a = z.ring[i], b = z.ring[(i + 1) % n];
    if (QLineF(a, b).length() < 0.1) continue;
    QVector3D norm(float(b.y() - a.y()), float(a.x() - b.x()), 0.0f);
    if (norm.lengthSquared() > 1e-6f) norm.normalize();
    addTriT(a, baseZ, b, baseZ, b, topZ, norm, wallCol);
    addTriT(a, baseZ, b, topZ, a, topZ, norm, wallCol);
    addLine3(a, topZ + 0.15, b, topZ + 0.15, edgeCol);
    addLine3(a, baseZ + 0.2, a, topZ + 0.15, edgeCol);
   }

   // Triangulated top cap
   using Pt = std::array<double, 2>;
   std::vector<std::vector<Pt>> poly(1);
   for (int i = 0; i < n; ++i) {
    if (i == n - 1 && z.ring[i] == z.ring[0]) continue;
    poly[0].push_back({z.ring[i].x(), z.ring[i].y()});
   }
   if (poly[0].size() >= 3) {
    const auto inds = mapbox::earcut<uint32_t>(poly);
    for (size_t i = 0; i + 2 < inds.size(); i += 3) {
     const QPointF p0(poly[0][inds[i]][0], poly[0][inds[i]][1]);
     const QPointF p1(poly[0][inds[i + 1]][0], poly[0][inds[i + 1]][1]);
     const QPointF p2(poly[0][inds[i + 2]][0], poly[0][inds[i + 2]][1]);
     addTriT(p0, topZ, p1, topZ, p2, topZ, {0, 0, 1}, roofCol);
    }
   }
  }
 }

 // 2. Elevated / Floating Glowing 3D Metro & Ring-Road Corridors + Station Beacons
 if (planning.show3DCorridors) {
  for (const auto &c : planning.corridors) {
   if (c.polyline.size() < 2) continue;
   const double elev = c.underground ? 1.4 : 2.2;
   QColor ribCol = c.color;
   ribCol.setAlphaF(0.68f);
   const double halfW = std::clamp(c.widthM * 0.38, 2.5, 9.0);
   for (int i = 0; i + 1 < c.polyline.size(); ++i) {
    const QPointF a = c.polyline[i], b = c.polyline[i + 1];
    const QPointF d = b - a;
    const double len = std::hypot(d.x(), d.y());
    if (len < 0.2) continue;
    const QPointF perp(-d.y() * halfW / len, d.x() * halfW / len);
    const double za = (terrainZ ? terrainZ(a) : 0.0) + elev;
    const double zb = (terrainZ ? terrainZ(b) : 0.0) + elev;
    addTriT(a + perp, za, b + perp, zb, b - perp, zb, {0, 0, 1}, ribCol);
    addTriT(a + perp, za, b - perp, zb, a - perp, za, {0, 0, 1}, ribCol);
    addLine3(a, za + 0.3, b, zb + 0.3, c.color);
   }
  }
  for (const auto &st : planning.stations) {
   const double gz = terrainZ ? terrainZ(st.pos) : 0.0;
   const double rad = 11.0;
   const double h = 22.0;
   QColor col = st.color;
   col.setAlphaF(0.62f);
   for (int k = 0; k < 12; ++k) {
    const double a0 = k * 2.0 * kPi / 12.0, a1 = (k + 1) * 2.0 * kPi / 12.0;
    const QPointF p0 = st.pos + QPointF(std::cos(a0) * rad, std::sin(a0) * rad);
    const QPointF p1 = st.pos + QPointF(std::cos(a1) * rad, std::sin(a1) * rad);
    addLine3(p0, gz + 1.5, p1, gz + 1.5, st.color);
    addLine3(p0, gz + h, p1, gz + h, st.color);
    if (k % 3 == 0) addLine3(p0, gz + 1.5, p0, gz + h, col);
   }
  }
 }
}

} // namespace ga
