#include "urbananalytics.h"
#include "analysis.h"
#include <QHash>
#include <QJsonArray>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <queue>

namespace ga {
namespace {

struct BldgBox {
 QRectF rect;
 double height;
 const Feature *feat;
};

QVector<BldgBox> collectBuildings(const Project &project) {
 QVector<BldgBox> out;
 out.reserve(project.features.size() / 2);
 for (const auto &f : project.features) {
  if (!f.building() || f.geometry != Geometry::Polygon || f.rings.isEmpty()) continue;
  const double h = f.knownHeight() ? std::max(3.0, f.height) : 4.5;
  out.append({f.bounds(), h, &f});
 }
 return out;
}

bool pointInsideBuilding(const QPointF &p, const BldgBox &b) {
 if (!b.rect.contains(p)) return false;
 if (!b.feat->rings[0].containsPoint(p, Qt::OddEvenFill)) return false;
 for (int i = 1; i < b.feat->rings.size(); ++i) {
  if (b.feat->rings[i].containsPoint(p, Qt::OddEvenFill)) return false;
 }
 return true;
}

QColor sampleRamp(double t, const QVector<QColor> &stops) {
 if (stops.isEmpty()) return Qt::gray;
 t = std::clamp(t, 0.0, 1.0);
 const double pos = t * (stops.size() - 1);
 const int idx = std::min(int(stops.size()) - 2, std::max(0, int(pos)));
 const double f = pos - idx;
 const auto &a = stops[idx];
 const auto &b = stops[idx + 1];
 return QColor::fromRgbF(
  a.redF() * (1 - f) + b.redF() * f,
  a.greenF() * (1 - f) + b.greenF() * f,
  a.blueF() * (1 - f) + b.blueF() * f,
  a.alphaF() * (1 - f) + b.alphaF() * f);
}

bool rayHitsAnySegment(const QPointF &o, const QPointF &dir, double maxDist, const QPolygonF &ring, double &outT) {
 bool hit = false;
 const int n = ring.size();
 for (int i = 0; i < n; ++i) {
  const QPointF p1 = ring[i];
  const QPointF p2 = ring[(i + 1) % n];
  const QPointF e = p2 - p1;
  const double denom = dir.x() * e.y() - dir.y() * e.x();
  if (std::abs(denom) < 1e-9) continue;
  const QPointF q = p1 - o;
  const double t = (q.x() * e.y() - q.y() * e.x()) / denom;
  const double u = (q.x() * dir.y() - q.y() * dir.x()) / denom;
  if (t > 0.25 && t < maxDist && u >= 0.0 && u <= 1.0) {
   if (t < outT) { outT = t; hit = true; }
  }
 }
 return hit;
}

double pointSegmentDist(const QPointF &p, const QPointF &a, const QPointF &b) {
 const QPointF ab = b - a;
 const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
 if (l2 < 1e-12) return QLineF(p, a).length();
 const double t = std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / l2, 0.0, 1.0);
 return QLineF(p, a + ab * t).length();
}

void rdpRecurse(const QPolygonF &pts, int first, int last, double tol, QVector<bool> &keep) {
 double maxD = 0.0;
 int idx = first;
 for (int i = first + 1; i < last; ++i) {
  const double d = pointSegmentDist(pts[i], pts[first], pts[last]);
  if (d > maxD) { maxD = d; idx = i; }
 }
 if (maxD > tol && idx != first && idx != last) {
  keep[idx] = true;
  rdpRecurse(pts, first, idx, tol, keep);
  rdpRecurse(pts, idx, last, tol, keep);
 }
}

// Sample a bundled WGS84 environmental JSON grid into a local-metres N x N grid
bool sampleBundledField(const QJsonObject &field, const Site &site, const QRectF &bounds, int N,
                        QVector<double> &outVals, double &minV, double &maxV, double &sumV, int &countV) {
 const int fw = field["width"].toInt(), fh = field["height"].toInt();
 const auto bbox = field["bbox"].toArray();
 const auto rows = field["grid"].toArray();
 if (fw <= 1 || fh <= 1 || bbox.size() != 4 || rows.size() != fh) return false;
 const double west = bbox[0].toDouble(), south = bbox[1].toDouble(), east = bbox[2].toDouble(), north = bbox[3].toDouble();
 if (east <= west || north <= south) return false;

 QVector<QVector<double>> src(fh, QVector<double>(fw, std::numeric_limits<double>::quiet_NaN()));
 for (int y = 0; y < fh; ++y) {
  const auto row = rows[y].toArray();
  if (row.size() != fw) return false;
  for (int x = 0; x < fw; ++x) {
   if (row[x].isDouble() && std::isfinite(row[x].toDouble())) src[y][x] = row[x].toDouble();
  }
 }

 outVals.assign(N * N, -1e9);
 minV = 1e9; maxV = -1e9; sumV = 0; countV = 0;
 const double dx = bounds.width() / N, dy = bounds.height() / N;
 const QPolygonF &sitePoly = site.boundary;
 for (int gy = 0; gy < N; ++gy) {
  const double wy = bounds.bottom() - (gy + 0.5) * dy;
  for (int gx = 0; gx < N; ++gx) {
   const double wx = bounds.left() + (gx + 0.5) * dx;
   if (!sitePoly.containsPoint({wx, wy}, Qt::OddEvenFill)) continue;
   const QPointF ll = geographicPoint({wx, wy}, site);
   const double fx = (ll.x() - west) / (east - west) * (fw - 1);
   const double fy = (north - ll.y()) / (north - south) * (fh - 1);
   if (fx < 0 || fx > fw - 1 || fy < 0 || fy > fh - 1) continue;
   const int x0 = std::clamp(int(std::floor(fx)), 0, fw - 1);
   const int y0 = std::clamp(int(std::floor(fy)), 0, fh - 1);
   const int x1 = std::min(fw - 1, x0 + 1);
   const int y1 = std::min(fh - 1, y0 + 1);
   const double tx = fx - x0, ty = fy - y0;
   const double v00 = src[y0][x0], v10 = src[y0][x1], v01 = src[y1][x0], v11 = src[y1][x1];
   double val = std::numeric_limits<double>::quiet_NaN();
   if (std::isfinite(v00) && std::isfinite(v10) && std::isfinite(v01) && std::isfinite(v11)) {
    val = (v00 * (1 - tx) + v10 * tx) * (1 - ty) + (v01 * (1 - tx) + v11 * tx) * ty;
   } else if (std::isfinite(v00)) val = v00;
   else if (std::isfinite(v10)) val = v10;
   else if (std::isfinite(v01)) val = v01;
   else if (std::isfinite(v11)) val = v11;
   if (!std::isfinite(val)) continue;
   outVals[gy * N + gx] = val;
   minV = std::min(minV, val);
   maxV = std::max(maxV, val);
   sumV += val;
   ++countV;
  }
 }
 return countV > 0;
}

} // namespace

QPolygonF simplifyPolyline(const QPolygonF &pts, double tolerance) {
 if (pts.size() <= 3 || tolerance <= 0.0) return pts;
 QVector<bool> keep(pts.size(), false);
 keep.first() = true;
 keep.last() = true;
 rdpRecurse(pts, 0, pts.size() - 1, tolerance, keep);
 QPolygonF out;
 out.reserve(pts.size());
 for (int i = 0; i < pts.size(); ++i) if (keep[i]) out.append(pts[i]);
 return out;
}

QPolygonF PlotDesign::polygon() const {
 if (isPolygon && customRing.size() >= 3) return customRing;
 const double rad = rotationDeg * M_PI / 180.0;
 const double c = std::cos(rad), s = std::sin(rad);
 const double hw = width * 0.5, hl = length * 0.5;
 const QPointF local[4] = {{-hw, -hl}, {hw, -hl}, {hw, hl}, {-hw, hl}};
 QPolygonF poly;
 for (const auto &pt : local) {
  poly.append(QPointF(centerX + pt.x() * c - pt.y() * s, centerY + pt.x() * s + pt.y() * c));
 }
 return poly;
}

double PlotDesign::areaM2() const {
 const QPolygonF p = polygon();
 return p.size() >= 3 ? std::abs(ringArea(p)) : 0.0;
}

double PlotDesign::perimeterM() const {
 const QPolygonF p = polygon();
 if (p.size() < 2) return 0.0;
 double perim = 0.0;
 for (int i = 0; i < p.size(); ++i) perim += QLineF(p[i], p[(i + 1) % p.size()]).length();
 return perim;
}

QJsonObject PlotDesign::toJson() const {
 QJsonArray ring;
 for (const auto &pt : customRing) ring.append(QJsonArray{pt.x(), pt.y()});
 return {
  {"active", active}, {"is_polygon", isPolygon}, {"cx", centerX}, {"cy", centerY},
  {"width", width}, {"length", length}, {"rotation", rotationDeg},
  {"ring", ring}, {"dimensions", showDimensions}, {"color", strokeColor.name()},
 };
}

PlotDesign PlotDesign::fromJson(const QJsonObject &v) {
 PlotDesign p;
 p.active = v["active"].toBool(false);
 p.isPolygon = v["is_polygon"].toBool(false);
 p.centerX = v["cx"].toDouble(0);
 p.centerY = v["cy"].toDouble(0);
 p.width = std::clamp(v["width"].toDouble(50), 4.0, 2000.0);
 p.length = std::clamp(v["length"].toDouble(80), 4.0, 2000.0);
 p.rotationDeg = std::clamp(v["rotation"].toDouble(0), -180.0, 180.0);
 for (const auto &item : v["ring"].toArray()) {
  const auto a = item.toArray();
  if (a.size() == 2 && a[0].isDouble() && a[1].isDouble()) p.customRing.append({a[0].toDouble(), a[1].toDouble()});
 }
 p.showDimensions = v["dimensions"].toBool(true);
 const QString col = v["color"].toString();
 if (QColor::isValidColorName(col)) p.strokeColor = QColor(col);
 return p;
}

AnalyticalOverlay computeAnalyticalOverlay(const Project &project,
                                           const QString &kind,
                                           const SolarState &solar,
                                           double windFromDeg,
                                           QPointF viewpoint,
                                           const QString &envDir,
                                           const QString &climateFile) {
 AnalyticalOverlay out;
 if (kind.isEmpty() || project.features.isEmpty()) return out;
 out.id = kind;
 out.viewpoint = viewpoint;
 out.windDirDeg = windFromDeg;
 out.sunState = solar;

 const QRectF bounds = project.site.boundary.boundingRect();
 out.worldBounds = bounds;
 const auto bldgs = collectBuildings(project);
 const QPolygonF &sitePoly = project.site.boundary;

 // A. 2D Polar Sun-Path Diagram overlaid on site
 if (kind == "sunpath-2d") {
  out.showSunpath2d = true;
  out.titleVi = "Biểu đồ đường đi mặt trời 2D (Sun-Path)";
  out.titleEn = "2D Polar Sun-Path Diagram";
  out.unit = "°";
  const auto sp = solarPosition(project.site.lat, solar.dayOfYear, solar.hour);
  out.legendStops = {QColor("#e59824"), QColor("#c47a44"), QColor("#3b82f6")};
  out.summaryVi = QString("Vĩ độ %1° · Mặt trời cao %2° · Phương vị %3° (Hạ chí / Phân / Đông chí).")
                   .arg(project.site.lat, 0, 'f', 2).arg(sp.altitudeDeg, 0, 'f', 1).arg(sp.azimuthDeg, 0, 'f', 1);
  out.summaryEn = QString("Lat %1° · Sun Alt %2° · Az %3° (Summer / Equinox / Winter arcs).")
                   .arg(project.site.lat, 0, 'f', 2).arg(sp.altitudeDeg, 0, 'f', 1).arg(sp.azimuthDeg, 0, 'f', 1);
  return out;
 }

 // B. 2D Crisp Solar Shadows at selected date/hour
 if (kind == "shadow-2d") {
  out.titleVi = "Bóng đổ công trình theo giờ";
  out.titleEn = "Hourly Building Shadow Projection";
  out.unit = "h";
  const auto sp = solarPosition(project.site.lat, solar.dayOfYear, solar.hour);
  if (sp.aboveHorizon) {
   for (const auto &b : bldgs) {
    if (!b.feat->knownHeight()) continue;
    const auto hull = buildingShadowHull(b.feat->rings[0], b.height, sp);
    if (hull.size() >= 3) out.shadowPolygons.append(hull);
   }
  }
  out.legendStops = {QColor(242, 238, 228, 220), QColor(92, 112, 138, 210), QColor(32, 46, 68, 220)};
  out.summaryVi = QString("%1 khối đổ bóng · Độ cao mặt trời %2° · Phương vị %3°.")
                   .arg(out.shadowPolygons.size()).arg(sp.altitudeDeg, 0, 'f', 1).arg(sp.azimuthDeg, 0, 'f', 1);
  out.summaryEn = QString("%1 cast building shadows · Sun Alt %2° · Az %3°.")
                   .arg(out.shadowPolygons.size()).arg(sp.altitudeDeg, 0, 'f', 1).arg(sp.azimuthDeg, 0, 'f', 1);
  return out;
 }

 // C. TMY Climate Wind Rose overlaid on site
 if (kind == "wind-rose") {
  out.titleVi = "Hoa gió khí hậu (TMY 16 hướng)";
  out.titleEn = "Climate Wind Rose (16-Sector TMY)";
  out.unit = "%";
  out.windRose16.assign(16, 0.03);
  QString stationName = "TMY Reference";
  if (!climateFile.isEmpty()) {
   const auto st = nearestClimate(climateFile, project.site);
   if (!st.isEmpty()) {
    stationName = st["station"].toString("TMY");
    const auto rose = st["climate"].toObject()["wind_rose"].toArray();
    if (rose.size() >= 8) {
     out.windRose16.assign(16, 0.0);
     double maxVal = 0.001;
     for (int i = 0; i < rose.size(); ++i) {
      const double v = rose[i].isObject() ? rose[i].toObject()["share"].toDouble(rose[i].toObject()["freq"].toDouble(0.05)) : rose[i].toDouble(0.05);
      const int idx = (rose.size() == 8) ? (i * 2) : (i % 16);
      out.windRose16[idx] = std::max(out.windRose16[idx], v);
      maxVal = std::max(maxVal, v);
     }
     if (rose.size() == 8) {
      for (int i = 1; i < 16; i += 2) out.windRose16[i] = 0.5 * (out.windRose16[i - 1] + out.windRose16[(i + 1) % 16]);
     }
     for (double &v : out.windRose16) v /= maxVal;
    }
   }
  } else {
   // Default monsoon SE + NE bimodal Hanoi profile
   const QVector<double> fallback = {0.32, 0.58, 0.78, 0.45, 0.36, 0.65, 1.0, 0.72, 0.42, 0.25, 0.18, 0.14, 0.16, 0.22, 0.28, 0.30};
   out.windRose16 = fallback;
  }
  out.windStationLabel = stationName;
  out.legendStops = {QColor("#bae6fd"), QColor("#38bdf8"), QColor("#0284c7"), QColor("#0c4a6e")};
  out.summaryVi = QString("Trạm khí hậu: %1 · Phân bố 16 hướng gió thịnh hành quanh năm.").arg(stationName);
  out.summaryEn = QString("Climate station: %1 · 16-sector prevailing annual wind distribution.").arg(stationName);
  return out;
 }

 // D. 360° Isovist Viewshed
 if (kind == "viewshed") {
  out.titleVi = "Trường nhìn 360° (Isovist)";
  out.titleEn = "360° Isovist Viewshed";
  out.unit = "m";
  const double maxR = std::max(120.0, project.site.radius * 0.95);
  const int rays = 144;
  double sumDist = 0;
  out.isovistPolygon.reserve(rays);
  for (int i = 0; i < rays; ++i) {
   const double ang = i * 2.0 * M_PI / rays;
   const QPointF dir(std::cos(ang), std::sin(ang));
   double bestT = maxR;
   for (const auto &b : bldgs) {
    const QPointF bc = b.rect.center() - viewpoint;
    const double proj = bc.x() * dir.x() + bc.y() * dir.y();
    const double br = std::hypot(b.rect.width(), b.rect.height()) * 0.55;
    if (proj + br < 0 || proj - br > bestT) continue;
    const double perp = std::abs(bc.x() * (-dir.y()) + bc.y() * dir.x());
    if (perp > br) continue;
    rayHitsAnySegment(viewpoint, dir, bestT, b.feat->rings[0], bestT);
   }
   sumDist += bestT;
   out.isovistPolygon.append(viewpoint + dir * bestT);
  }
  out.minVal = 0;
  out.maxVal = maxR;
  out.meanVal = sumDist / rays;
  out.legendStops = {QColor(224, 132, 78, 90), QColor(196, 86, 50, 210)};
  out.summaryVi = QString("Tầm nhìn trung bình %1 m (tối đa %2 m) quanh tâm khu đất.").arg(out.meanVal, 0, 'f', 0).arg(maxR, 0, 'f', 0);
  out.summaryEn = QString("Mean sightline %1 m (max %2 m) from site origin.").arg(out.meanVal, 0, 'f', 0).arg(maxR, 0, 'f', 0);
  return out;
 }

 // E. Traffic Congestion / Corridor Load
 if (kind == "congestion") {
  out.titleVi = "Áp lực & ùn tắc giao thông";
  out.titleEn = "Traffic Pressure & Congestion";
  out.unit = "LoS";
  int high = 0, med = 0, low = 0;
  for (const auto &f : project.features) {
   if (f.layer != "roads" || f.geometry != Geometry::Line || f.rings.isEmpty()) continue;
   const QString hw = f.tags["highway"].toString();
   double load = 0.25;
   QColor col("#16a34a");
   if (hw == "motorway" || hw == "trunk" || hw == "primary") { load = 0.92; col = QColor("#dc2626"); ++high; }
   else if (hw == "secondary") { load = 0.68; col = QColor("#ea580c"); ++med; }
   else if (hw == "tertiary") { load = 0.45; col = QColor("#eab308"); ++med; }
   else { ++low; }
   const auto &line = f.rings[0];
   for (int i = 0; i + 1 < line.size(); ++i) {
    out.walkSegments.append({line[i], line[i + 1], load * 15.0, col});
   }
  }
  out.legendStops = {QColor("#16a34a"), QColor("#eab308"), QColor("#ea580c"), QColor("#dc2626")};
  out.summaryVi = QString("%1 tuyến áp lực cao · %2 tuyến trung bình · %3 tuyến thông thoáng.").arg(high).arg(med).arg(low);
  out.summaryEn = QString("%1 high-load corridors · %2 moderate · %3 free-flow streets.").arg(high).arg(med).arg(low);
  return out;
 }

 // F. Network Walkshed 5 / 10 / 15 min (Dijkstra on real road graph)
 if (kind == "walkshed") {
  out.titleVi = "Tiếp cận đi bộ 5–10–15 phút (Walkshed)";
  out.titleEn = "5–10–15 min Network Walkshed";
  out.unit = "phút / min";
  QHash<qint64, int> nodeMap;
  QVector<QPointF> nodes;
  QVector<QVector<QPair<int, double>>> adj;
  auto getNode = [&](const QPointF &p) {
   const qint64 kx = qRound64(p.x() / 6.0);
   const qint64 ky = qRound64(p.y() / 6.0);
   const qint64 key = (kx << 32) ^ (ky & 0xffffffffLL);
   auto it = nodeMap.find(key);
   if (it != nodeMap.end()) return it.value();
   const int idx = nodes.size();
   nodeMap.insert(key, idx);
   nodes.append(p);
   adj.emplaceBack();
   return idx;
  };
  for (const auto &f : project.features) {
   if (f.layer != "roads" || f.geometry != Geometry::Line || f.rings.isEmpty()) continue;
   const auto &line = f.rings[0];
   for (int i = 0; i + 1 < line.size(); ++i) {
    const int u = getNode(line[i]);
    const int v = getNode(line[i + 1]);
    if (u == v) continue;
    const double d = QLineF(nodes[u], nodes[v]).length();
    adj[u].append({v, d});
    adj[v].append({u, d});
   }
  }
  if (!nodes.isEmpty()) {
   int start = 0;
   double bestD = 1e18;
   for (int i = 0; i < nodes.size(); ++i) {
    const double d = QLineF(nodes[i], viewpoint).length();
    if (d < bestD) { bestD = d; start = i; }
   }
   QVector<double> dist(nodes.size(), 1e18);
   dist[start] = bestD;
   using State = QPair<double, int>;
   std::priority_queue<State, std::vector<State>, std::greater<State>> pq;
   pq.push({bestD, start});
   const double maxMeters = 1200.0;
   while (!pq.empty()) {
    const auto [d, u] = pq.top();
    pq.pop();
    if (d > dist[u] || d > maxMeters) continue;
    for (const auto &[v, w] : adj[u]) {
     if (d + w < dist[v] && d + w <= maxMeters) {
      dist[v] = d + w;
      pq.push({dist[v], v});
     }
    }
   }
   int seg5 = 0, seg10 = 0, seg15 = 0;
   for (int u = 0; u < nodes.size(); ++u) {
    if (dist[u] > maxMeters) continue;
    for (const auto &[v, w] : adj[u]) {
     if (v <= u || dist[v] > maxMeters) continue;
     const double mins = (dist[u] + dist[v]) * 0.5 / 80.0;
     out.walkSegments.append({nodes[u], nodes[v], mins, QColor()});
     if (mins <= 5.0) ++seg5;
     else if (mins <= 10.0) ++seg10;
     else ++seg15;
    }
   }
   out.minVal = 0;
   out.maxVal = 15;
   out.meanVal = double(seg5 + seg10 + seg15);
   out.legendStops = {QColor("#1e8e5a"), QColor("#d99026"), QColor("#c74c38")};
   out.summaryVi = QString("%1 đoạn trong 5p · %2 đoạn 10p · %3 đoạn 15p (4,8 km/h).").arg(seg5).arg(seg10).arg(seg15);
   out.summaryEn = QString("%1 segments in 5m · %2 in 10m · %3 in 15m (4.8 km/h).").arg(seg5).arg(seg10).arg(seg15);
  }
  return out;
 }

 // G. 2D Wind Streamlines around buildings
 if (kind == "wind-flow") {
  out.titleVi = "Trường luồng gió 2D (CFD sơ bộ)";
  out.titleEn = "2D Wind Flow Streamlines (Preview)";
  out.unit = "m/s";
  const double toRad = (windFromDeg + 180.0) * M_PI / 180.0;
  const QPointF windDir(std::sin(toRad), std::cos(toRad));
  const QPointF perp(-windDir.y(), windDir.x());
  const double r = std::max(100.0, project.site.radius);
  const int numLines = 28;
  const int steps = 54;
  const double stepLen = (2.0 * r) / steps;
  for (int li = 0; li < numLines; ++li) {
   const double offset = -r * 0.92 + (1.84 * r) * double(li) / (numLines - 1);
   QPointF p = -windDir * r + perp * offset;
   QPolygonF trace;
   for (int st = 0; st <= steps; ++st) {
    if (sitePoly.containsPoint(p, Qt::OddEvenFill)) trace.append(p);
    QPointF vel = windDir;
    for (const auto &b : bldgs) {
     const QPointF c = b.rect.center();
     const double br = std::max(8.0, std::hypot(b.rect.width(), b.rect.height()) * 0.45);
     const QPointF d = p - c;
     const double d2 = d.x() * d.x() + d.y() * d.y();
     if (d2 > br * br * 9.0 || d2 < 1.0) continue;
     const double dist = std::sqrt(d2);
     const double push = std::clamp((br * 1.45 - dist) / (br * 1.45), 0.0, 1.0);
     const double side = (d.x() * perp.x() + d.y() * perp.y()) >= 0 ? 1.0 : -1.0;
     vel += perp * (side * push * 0.95) + (d / dist) * (push * 0.45);
    }
    const double vl = std::hypot(vel.x(), vel.y());
    if (vl > 1e-4) vel /= vl;
    p += vel * stepLen;
   }
   if (trace.size() >= 3) out.streamlines.append(trace);
  }
  out.legendStops = {QColor("#67e8f9"), QColor("#0284c7"), QColor("#1d4ed8")};
  out.summaryVi = QString("Luồng gió thổi từ hướng %1° (%2 đường dòng tránh khối nhà).").arg(qRound(windFromDeg)).arg(out.streamlines.size());
  out.summaryEn = QString("Wind streamlines from %1° (%2 obstacle-deflected streamlines).").arg(qRound(windFromDeg)).arg(out.streamlines.size());
  return out;
 }

 // H. Real Bundled Satellite / Terrain Environmental Grids (env:gedtm30, env:terrain, env:slope, env:flood, env:landsat, env:sentinel, env:canopy, env:population)
 if (kind.startsWith("env:")) {
  const QString envId = kind.mid(4);
  const QString lookupId = (envId == "slope" || envId == "terrain") ? QString("gedtm30") : envId;
  QJsonObject matchedField;
  if (!envDir.isEmpty()) {
   for (const auto &f : environmentFields(envDir, project.site)) {
    const QString fid = f["id"].toString();
    const QString src = f["source"].toString().toLower();
    if (fid == lookupId || src.contains(lookupId)) { matchedField = f; break; }
   }
   // Fallback: match by filename inside hoankiem/halong directories
   if (matchedField.isEmpty()) {
    for (const auto &f : environmentFields(envDir, project.site)) {
     const QString u = f["unit"].toString().toLower();
     if (lookupId == "gedtm30" && (f["source"].toString().contains("GEDTM", Qt::CaseInsensitive) || u == "m")) { matchedField = f; break; }
     if (lookupId == "landsat" && f["source"].toString().contains("Landsat", Qt::CaseInsensitive)) { matchedField = f; break; }
     if (lookupId == "sentinel" && f["source"].toString().contains("Sentinel", Qt::CaseInsensitive)) { matchedField = f; break; }
     if (lookupId == "flood" && f["source"].toString().contains("Flood", Qt::CaseInsensitive)) { matchedField = f; break; }
     if (lookupId == "canopy" && f["source"].toString().contains("Canopy", Qt::CaseInsensitive)) { matchedField = f; break; }
     if (lookupId == "population" && f["source"].toString().contains("WorldPop", Qt::CaseInsensitive)) { matchedField = f; break; }
    }
   }
  }

  const int N = 96;
  QVector<double> vals;
  double minV = 0, maxV = 1, sumV = 0;
  int countV = 0;
  const bool loaded = !matchedField.isEmpty() && sampleBundledField(matchedField, project.site, bounds, N, vals, minV, maxV, sumV, countV);

  if (envId == "gedtm30" || envId == "terrain") {
   out.titleVi = (envId == "terrain") ? "Đồng mức & Cao độ địa hình (DEM)" : "Cao độ địa hình (GEDTM30)";
   out.titleEn = (envId == "terrain") ? "Terrain Contours & Elevation (DEM)" : "Ground Elevation (GEDTM30)";
   out.unit = "m";
   out.legendStops = {QColor(54, 114, 148, 200), QColor(118, 178, 134, 200), QColor(228, 214, 134, 205), QColor(198, 134, 86, 210), QColor(142, 78, 66, 215)};
  } else if (envId == "slope") {
   out.titleVi = "Độ dốc địa hình (°)";
   out.titleEn = "Terrain Slope (°)";
   out.unit = "°";
   out.legendStops = {QColor(231, 237, 218, 195), QColor(204, 215, 172, 200), QColor(233, 212, 144, 205), QColor(220, 163, 117, 210), QColor(186, 78, 66, 220)};
   if (loaded) {
    // Convert elevation grid to slope degrees using central differences
    QVector<double> slopeVals(N * N, -1e9);
    const double cellX = bounds.width() / N, cellY = bounds.height() / N;
    minV = 1e9; maxV = -1e9; sumV = 0; countV = 0;
    for (int y = 1; y + 1 < N; ++y) {
     for (int x = 1; x + 1 < N; ++x) {
      const double c = vals[y * N + x];
      const double l = vals[y * N + x - 1], r = vals[y * N + x + 1];
      const double u = vals[(y - 1) * N + x], d = vals[(y + 1) * N + x];
      if (c < -1e8 || l < -1e8 || r < -1e8 || u < -1e8 || d < -1e8) continue;
      const double dzdx = (r - l) / (2.0 * cellX);
      const double dzdy = (u - d) / (2.0 * cellY);
      const double deg = std::atan(std::hypot(dzdx, dzdy)) * 180.0 / M_PI;
      slopeVals[y * N + x] = deg;
      minV = std::min(minV, deg); maxV = std::max(maxV, deg); sumV += deg; ++countV;
     }
    }
    vals = std::move(slopeVals);
   }
  } else if (envId == "flood") {
   out.titleVi = "Nguy cơ ngập · Kịch bản RP100";
   out.titleEn = "River Flood Hazard (RP100)";
   out.unit = "m";
   out.legendStops = {QColor(219, 234, 254, 150), QColor(96, 165, 250, 200), QColor(37, 99, 235, 215), QColor(30, 58, 138, 230)};
  } else if (envId == "landsat") {
   out.titleVi = "Nhiệt độ bề mặt (LST / Landsat)";
   out.titleEn = "Land Surface Temperature (Landsat)";
   out.unit = "°C";
   out.legendStops = {QColor(59, 130, 186, 195), QColor(156, 206, 162, 195), QColor(245, 212, 114, 205), QColor(226, 112, 62, 215), QColor(158, 36, 48, 225)};
  } else if (envId == "sentinel") {
   out.titleVi = "Thảm thực vật · Chỉ số NDVI (Sentinel-2)";
   out.titleEn = "Vegetation Index · NDVI (Sentinel-2)";
   out.unit = "NDVI";
   out.legendStops = {QColor(186, 146, 112, 185), QColor(226, 220, 174, 190), QColor(138, 196, 118, 200), QColor(42, 136, 72, 215), QColor(18, 84, 48, 225)};
  } else if (envId == "canopy") {
   out.titleVi = "Chiều cao tán cây (Canopy Height)";
   out.titleEn = "Tree Canopy Height";
   out.unit = "m";
   out.legendStops = {QColor(220, 236, 212, 165), QColor(134, 196, 128, 195), QColor(52, 142, 78, 210), QColor(22, 88, 52, 225)};
  } else if (envId == "population") {
   out.titleVi = "Mật độ dân số (WorldPop)";
   out.titleEn = "Population Density (WorldPop)";
   out.unit = "ng/ha";
   out.legendStops = {QColor(253, 242, 248, 165), QColor(244, 114, 182, 195), QColor(190, 24, 93, 215), QColor(112, 26, 72, 230)};
  }

  if (loaded && countV > 0) {
   QImage img(N, N, QImage::Format_ARGB32);
   img.fill(Qt::transparent);
   out.minVal = minV; out.maxVal = maxV; out.meanVal = sumV / countV;
   const double span = std::max(1e-4, maxV - minV);
   for (int gy = 0; gy < N; ++gy) {
    for (int gx = 0; gx < N; ++gx) {
     const double v = vals[gy * N + gx];
     if (v < -1e8) continue;
     img.setPixelColor(gx, gy, sampleRamp((v - minV) / span, out.legendStops));
    }
   }
   out.heatmap = img;

   // Extract crisp contour segments for env:terrain
   if (envId == "terrain" && span > 0.5) {
    const double step = (span > 25.0) ? 5.0 : (span > 10.0 ? 2.0 : 1.0);
    const double dx = bounds.width() / N, dy = bounds.height() / N;
    for (double level = std::ceil(minV / step) * step; level < maxV; level += step) {
     for (int y = 0; y + 1 < N; ++y) {
      for (int x = 0; x + 1 < N; ++x) {
       const double v00 = vals[y * N + x], v10 = vals[y * N + x + 1];
       const double v01 = vals[(y + 1) * N + x], v11 = vals[(y + 1) * N + x + 1];
       if (v00 < -1e8 || v10 < -1e8 || v01 < -1e8 || v11 < -1e8) continue;
       QVector<QPointF> hits;
       const double x0 = bounds.left() + (x + 0.5) * dx, x1 = x0 + dx;
       const double y0 = bounds.bottom() - (y + 0.5) * dy, y1 = y0 - dy;
       auto checkEdge = [&](double va, double vb, QPointF pa, QPointF pb) {
        if ((va <= level && vb > level) || (vb <= level && va > level)) {
         const double t = (level - va) / (vb - va);
         hits.append(pa + (pb - pa) * t);
        }
       };
       checkEdge(v00, v10, {x0, y0}, {x1, y0});
       checkEdge(v10, v11, {x1, y0}, {x1, y1});
       checkEdge(v11, v01, {x1, y1}, {x0, y1});
       checkEdge(v01, v00, {x0, y1}, {x0, y0});
       if (hits.size() >= 2) out.contours.append(QPolygonF{hits[0], hits[1]});
      }
     }
    }
   }
   out.summaryVi = QString("TB %1 · Min %2 · Max %3 %4 (%5).")
                    .arg(out.meanVal, 0, 'f', 1).arg(minV, 0, 'f', 1).arg(maxV, 0, 'f', 1).arg(out.unit, matchedField["source"].toString("Local Grid"));
   out.summaryEn = QString("Mean %1 · Min %2 · Max %3 %4 (%5).")
                    .arg(out.meanVal, 0, 'f', 1).arg(minV, 0, 'f', 1).arg(maxV, 0, 'f', 1).arg(out.unit, matchedField["source"].toString("Local Grid"));
  } else {
   out.summaryVi = "Chưa có lưới vệ tinh đi kèm cho tọa độ khu đất này (sẵn có tại Hoàn Kiếm & Hạ Long).";
   out.summaryEn = "No bundled satellite grid for this location (available for Hoan Kiem & Ha Long).";
  }
  return out;
 }

 // I. Grid-based C++ architectural & environmental heatmaps
 const int N = 72;
 QImage img(N, N, QImage::Format_ARGB32);
 img.fill(Qt::transparent);

 QVector<SolarPosition> daySuns;
 if (kind == "sunlight-hours" || kind == "solar-radiation" || kind == "utci") {
  for (int h = 6; h <= 18; ++h) {
   const auto sp = solarPosition(project.site.lat, solar.dayOfYear, double(h));
   if (sp.aboveHorizon) daySuns.append(sp);
  }
 }

 QVector<QPair<QPolygonF, double>> roadLevels;
 QVector<QRectF> greenBoxes, waterBoxes, bizBoxes;
 if (kind == "traffic-noise") {
  for (const auto &f : project.features) {
   if (f.layer != "roads" || f.geometry != Geometry::Line || f.rings.isEmpty()) continue;
   const QString hw = f.tags["highway"].toString();
   double baseDb = 58.0;
   if (hw == "motorway" || hw == "trunk") baseDb = 76.0;
   else if (hw == "primary") baseDb = 72.0;
   else if (hw == "secondary") baseDb = 68.0;
   else if (hw == "tertiary") baseDb = 64.0;
   else if (hw == "pedestrian" || hw == "footway") baseDb = 50.0;
   roadLevels.append({f.rings[0], baseDb});
  }
 } else if (kind == "utci") {
  for (const auto &f : project.features) {
   if (f.layer == "green") greenBoxes.append(f.bounds());
   else if (f.layer == "water") waterBoxes.append(f.bounds());
  }
 } else if (kind == "business") {
  for (const auto &f : project.features) {
   if (f.tags.contains("shop") || f.tags.contains("office") || f.tags["amenity"].toString() == "cafe"
       || f.tags["amenity"].toString() == "restaurant" || f.tags["building"].toString() == "commercial"
       || f.tags["building"].toString() == "retail") {
    bizBoxes.append(f.bounds());
   }
  }
 }

 QVector<double> gridVals(N * N, -1e9);
 double minV = 1e9, maxV = -1e9, sumV = 0;
 int countV = 0;
 const double dx = bounds.width() / N;
 const double dy = bounds.height() / N;

 for (int gy = 0; gy < N; ++gy) {
  const double wy = bounds.bottom() - (gy + 0.5) * dy;
  for (int gx = 0; gx < N; ++gx) {
   const double wx = bounds.left() + (gx + 0.5) * dx;
   const QPointF pt(wx, wy);
   if (!sitePoly.containsPoint(pt, Qt::OddEvenFill)) continue;

   double val = 0.0;
   if (kind == "sunlight-hours" || kind == "solar-radiation") {
    double hours = 0.0, radKwh = 0.0;
    for (const auto &sp : daySuns) {
     bool shaded = false;
     const double tanAlt = std::tan(std::max(5.0, sp.altitudeDeg) * M_PI / 180.0);
     const double hLen = std::hypot(double(sp.direction.x()), double(sp.direction.y()));
     const QPointF toSun = hLen > 1e-4 ? QPointF(sp.direction.x() / hLen, sp.direction.y() / hLen) : QPointF(0, 0);
     for (const auto &b : bldgs) {
      if (pointInsideBuilding(pt, b)) continue;
      const double maxReach = b.height / tanAlt;
      const QPointF rel = b.rect.center() - pt;
      const double proj = rel.x() * toSun.x() + rel.y() * toSun.y();
      const double br = std::hypot(b.rect.width(), b.rect.height()) * 0.5;
      if (proj < -br || proj > maxReach + br) continue;
      const double perp = std::abs(rel.x() * (-toSun.y()) + rel.y() * toSun.x());
      if (perp <= br * 0.85 && proj > 0 && b.height / std::max(1.0, proj) >= tanAlt) {
       shaded = true;
       break;
      }
     }
     if (!shaded) { hours += 1.0; radKwh += 0.85 * std::sin(sp.altitudeDeg * M_PI / 180.0); }
     else { radKwh += 0.14 * std::sin(sp.altitudeDeg * M_PI / 180.0); }
    }
    val = (kind == "sunlight-hours") ? hours : radKwh;
   } else if (kind == "svf") {
    double openSum = 0.0;
    const int dirs = 16;
    for (int d = 0; d < dirs; ++d) {
     const double a = d * 2.0 * M_PI / dirs;
     const QPointF u(std::cos(a), std::sin(a));
     double maxSlope = 0.0;
     for (const auto &b : bldgs) {
      const QPointF rel = b.rect.center() - pt;
      const double proj = rel.x() * u.x() + rel.y() * u.y();
      if (proj <= 2.0 || proj > 180.0) continue;
      const double br = std::hypot(b.rect.width(), b.rect.height()) * 0.45;
      const double perp = std::abs(rel.x() * (-u.y()) + rel.y() * u.x());
      if (perp <= br) maxSlope = std::max(maxSlope, b.height / proj);
     }
     const double beta = std::atan(maxSlope);
     openSum += std::cos(beta) * std::cos(beta);
    }
    val = openSum / dirs;
   } else if (kind == "utci") {
    double temp = 34.5;
    int shadedCount = 0;
    for (const auto &sp : daySuns) {
     if (sp.altitudeDeg < 20.0) continue;
     const double tanAlt = std::tan(sp.altitudeDeg * M_PI / 180.0);
     const double hLen = std::hypot(double(sp.direction.x()), double(sp.direction.y()));
     const QPointF toSun = hLen > 1e-4 ? QPointF(sp.direction.x() / hLen, sp.direction.y() / hLen) : QPointF(0, 0);
     for (const auto &b : bldgs) {
      const QPointF rel = b.rect.center() - pt;
      const double proj = rel.x() * toSun.x() + rel.y() * toSun.y();
      const double br = std::hypot(b.rect.width(), b.rect.height()) * 0.45;
      if (proj > 0 && proj < b.height / tanAlt && std::abs(rel.x() * (-toSun.y()) + rel.y() * toSun.x()) <= br) { ++shadedCount; break; }
     }
    }
    const double shadeFrac = daySuns.isEmpty() ? 0.0 : double(shadedCount) / daySuns.size();
    temp -= shadeFrac * 4.2;
    for (const auto &gr : greenBoxes) if (gr.adjusted(-18, -18, 18, 18).contains(pt)) { temp -= 2.1; break; }
    for (const auto &wa : waterBoxes) if (wa.adjusted(-28, -28, 28, 28).contains(pt)) { temp -= 2.8; break; }
    val = std::clamp(temp, 24.0, 41.0);
   } else if (kind == "traffic-noise") {
    double maxDb = 44.0;
    for (const auto &[poly, baseDb] : roadLevels) {
     for (int i = 0; i + 1 < poly.size(); ++i) {
      const double dist = pointSegmentDist(pt, poly[i], poly[i + 1]);
      const double atten = baseDb - 10.0 * std::log10(std::max(1.0, dist / 6.0));
      maxDb = std::max(maxDb, atten);
     }
    }
    val = std::clamp(maxDb, 42.0, 78.0);
   } else if (kind == "city-block") {
    // Built density kernel within 65m radius (%)
    double built = 0.0;
    for (const auto &b : bldgs) {
     const double d = QLineF(pt, b.rect.center()).length();
     if (d < 65.0) built += b.feat->area() * (1.0 - d / 65.0);
    }
    val = std::clamp(built / (M_PI * 65.0 * 65.0) * 180.0, 0.0, 100.0);
   } else if (kind == "floor-area") {
    double a = 0.0;
    for (const auto &b : bldgs) {
     if (pointInsideBuilding(pt, b)) { a = b.feat->area(); break; }
    }
    if (a <= 0.0) continue;
    val = std::min(a, 5000.0);
   } else if (kind == "business") {
    double density = 0.0;
    for (const auto &box : bizBoxes) {
     const double d = QLineF(pt, box.center()).length();
     if (d < 90.0) density += std::exp(-(d * d) / (2.0 * 35.0 * 35.0));
    }
    val = density;
   }

   gridVals[gy * N + gx] = val;
   minV = std::min(minV, val);
   maxV = std::max(maxV, val);
   sumV += val;
   ++countV;
  }
 }

 if (kind == "sunlight-hours") {
  out.titleVi = "Bản đồ số giờ nắng trực tiếp";
  out.titleEn = "Direct Sunlight Hours";
  out.unit = "giờ / h";
  out.legendStops = {QColor(46, 68, 108, 195), QColor(122, 166, 178, 195), QColor(242, 214, 122, 200), QColor(232, 134, 64, 210), QColor(186, 62, 46, 215)};
 } else if (kind == "solar-radiation") {
  out.titleVi = "Bức xạ nhiệt mặt trời (ngày)";
  out.titleEn = "Daily Solar Radiation";
  out.unit = "kWh/m²";
  out.legendStops = {QColor(54, 62, 114, 195), QColor(136, 102, 152, 195), QColor(228, 146, 86, 205), QColor(246, 216, 112, 215)};
 } else if (kind == "svf") {
  out.titleVi = "Hệ số nhìn thấy bầu trời (SVF)";
  out.titleEn = "Sky View Factor (SVF)";
  out.unit = "0–1";
  out.legendStops = {QColor(38, 44, 64, 205), QColor(88, 118, 152, 195), QColor(168, 204, 216, 190), QColor(244, 246, 236, 190)};
 } else if (kind == "utci") {
  out.titleVi = "Tiện nghi nhiệt ngoài trời UTCI";
  out.titleEn = "Outdoor Thermal Comfort (UTCI)";
  out.unit = "°C";
  out.legendStops = {QColor(72, 136, 142, 195), QColor(162, 198, 154, 195), QColor(242, 208, 124, 205), QColor(218, 118, 72, 210), QColor(166, 56, 54, 215)};
 } else if (kind == "traffic-noise") {
  out.titleVi = "Ước tính tiếng ồn giao thông";
  out.titleEn = "Estimated Traffic Noise";
  out.unit = "dB(A)";
  out.legendStops = {QColor(102, 158, 136, 170), QColor(224, 206, 126, 190), QColor(224, 122, 68, 205), QColor(162, 48, 62, 215)};
 } else if (kind == "city-block") {
  out.titleVi = "Mật độ ô phố đô thị";
  out.titleEn = "Urban Block Density";
  out.unit = "%";
  out.legendStops = {QColor(245, 243, 238, 140), QColor(238, 186, 144, 190), QColor(218, 110, 74, 210), QColor(148, 42, 42, 225)};
 } else if (kind == "floor-area") {
  out.titleVi = "Diện tích đế công trình";
  out.titleEn = "Building Footprint Area";
  out.unit = "m²";
  out.legendStops = {QColor(232, 226, 214, 190), QColor(226, 168, 112, 205), QColor(208, 92, 62, 215), QColor(138, 36, 46, 230)};
 } else if (kind == "business") {
  out.titleVi = "Cụm kinh doanh & thương mại";
  out.titleEn = "Business & Commercial Clusters";
  out.unit = "idx";
  out.legendStops = {QColor(254, 243, 199, 120), QColor(251, 146, 60, 195), QColor(220, 38, 38, 215), QColor(127, 29, 29, 230)};
 }

 if (countV > 0) {
  out.minVal = minV;
  out.maxVal = maxV;
  out.meanVal = sumV / countV;
  const double span = std::max(1e-4, maxV - minV);
  for (int gy = 0; gy < N; ++gy) {
   for (int gx = 0; gx < N; ++gx) {
    const double v = gridVals[gy * N + gx];
    if (v < -1e8) continue;
    img.setPixelColor(gx, gy, sampleRamp((v - minV) / span, out.legendStops));
   }
  }
  out.heatmap = img;
  out.summaryVi = QString("TB %1 · Min %2 · Max %3 %4 (mô hình tham khảo theo hình học khu đất).")
                   .arg(out.meanVal, 0, 'f', 1).arg(minV, 0, 'f', 1).arg(maxV, 0, 'f', 1).arg(out.unit);
  out.summaryEn = QString("Mean %1 · Min %2 · Max %3 %4 (geometry-derived reference estimate).")
                   .arg(out.meanVal, 0, 'f', 1).arg(minV, 0, 'f', 1).arg(maxV, 0, 'f', 1).arg(out.unit);
 }
 return out;
}

} // namespace ga
