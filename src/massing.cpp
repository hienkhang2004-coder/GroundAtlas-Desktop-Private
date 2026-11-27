#include "massing.h"
#include "earcut.hpp"
#include <QLineF>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ga {

QJsonObject MassingConfig::toJson() const {
 return {
  {"enabled", enabled},
  {"setback_m", setbackM},
  {"podium_floors", podiumFloors},
  {"podium_floor_height", podiumFloorHeight},
  {"tower_floors", towerFloors},
  {"tower_floor_height", towerFloorHeight},
  {"tower_coverage", towerCoverage},
  {"tower_shift_x", towerShiftX},
  {"tower_shift_y", towerShiftY},
  {"tower_rotation_deg", towerRotationDeg},
  {"show_floor_plates", showFloorPlates},
  {"accent_color", accentColor.name()},
 };
}

MassingConfig MassingConfig::fromJson(const QJsonObject &v) {
 MassingConfig c;
 c.enabled = v["enabled"].toBool(false);
 c.setbackM = std::clamp(v["setback_m"].toDouble(4.0), 0.0, 50.0);
 c.podiumFloors = std::clamp(v["podium_floors"].toInt(3), 0, 15);
 c.podiumFloorHeight = std::clamp(v["podium_floor_height"].toDouble(4.2), 2.5, 8.0);
 c.towerFloors = std::clamp(v["tower_floors"].toInt(12), 0, 90);
 c.towerFloorHeight = std::clamp(v["tower_floor_height"].toDouble(3.4), 2.5, 8.0);
 c.towerCoverage = std::clamp(v["tower_coverage"].toDouble(0.45), 0.12, 1.0);
 c.towerShiftX = std::clamp(v["tower_shift_x"].toDouble(0.0), -0.35, 0.35);
 c.towerShiftY = std::clamp(v["tower_shift_y"].toDouble(0.0), -0.35, 0.35);
 c.towerRotationDeg = std::clamp(v["tower_rotation_deg"].toDouble(0.0), -90.0, 90.0);
 c.showFloorPlates = v["show_floor_plates"].toBool(true);
 const QColor col(v["accent_color"].toString("#d2643c"));
 if (col.isValid()) c.accentColor = col;
 return c;
}

namespace {
QPointF polygonCentroid(const QPolygonF &ring) {
 if (ring.isEmpty()) return {};
 double area2 = 0.0, cx = 0.0, cy = 0.0;
 const int n = ring.size();
 for (int i = 0; i < n; ++i) {
  const auto &a = ring[i];
  const auto &b = ring[(i + 1) % n];
  const double cr = a.x() * b.y() - b.x() * a.y();
  area2 += cr;
  cx += (a.x() + b.x()) * cr;
  cy += (a.y() + b.y()) * cr;
 }
 if (std::abs(area2) > 1e-6) return {cx / (3.0 * area2), cy / (3.0 * area2)};
 QPointF avg(0, 0);
 for (const auto &p : ring) avg += p;
 return avg / double(n);
}

QPolygonF scaleRingAbout(const QPolygonF &ring, const QPointF &center, double scale, double rotDeg = 0.0, const QPointF &shift = {}) {
 const double rad = rotDeg * M_PI / 180.0;
 const double cs = std::cos(rad), sn = std::sin(rad);
 QPolygonF out;
 out.reserve(ring.size());
 for (const auto &p : ring) {
  const QPointF d = (p - center) * scale;
  const QPointF r(d.x() * cs - d.y() * sn, d.x() * sn + d.y() * cs);
  out.append(center + r + shift);
 }
 return out;
}

void addPrism(Mesh &mesh, const QPolygonF &ring, double z0, double z1, const QColor &roofColor, const QColor &wallColor,
              const QColor &lineColor, int floors, bool floorLines, float matId) {
 if (ring.size() < 3 || z1 <= z0 + 0.05) return;
 // Triangulate roof via earcut
 using Pt = std::array<double, 2>;
 std::vector<std::vector<Pt>> poly(1);
 QVector<QPointF> pts;
 pts.reserve(ring.size());
 for (int i = 0; i < ring.size(); ++i) {
  if (i == ring.size() - 1 && QLineF(ring[i], ring[0]).length() < 1e-4) continue;
  poly[0].push_back({ring[i].x(), ring[i].y()});
  pts.append(ring[i]);
 }
 if (pts.size() < 3) return;
 const auto indices = mapbox::earcut<uint32_t>(poly);

 auto pushV = [&](const QPointF &p, double z, const QVector3D &n, const QColor &c, QVector<Vertex> &out) {
  out.append({float(p.x()), float(p.y()), float(z), n.x(), n.y(), n.z(),
              float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF()), matId});
 };
 auto pushTri = [&](const QPointF &a, double az, const QPointF &b, double bz, const QPointF &c, double cz,
                    const QVector3D &n, const QColor &col) {
  pushV(a, az, n, col, mesh.triangles);
  pushV(b, bz, n, col, mesh.triangles);
  pushV(c, cz, n, col, mesh.triangles);
 };
 auto pushLine = [&](const QPointF &a, double az, const QPointF &b, double bz, const QColor &col) {
  pushV(a, az, {0, 0, 0}, col, mesh.lines);
  pushV(b, bz, {0, 0, 0}, col, mesh.lines);
 };

 // Top roof cap
 for (size_t i = 0; i + 2 < indices.size(); i += 3) {
  pushTri(pts[indices[i]], z1, pts[indices[i + 1]], z1, pts[indices[i + 2]], z1, {0, 0, 1}, roofColor);
 }

 // Extruded vertical facade walls with exact outward normals
 const bool ccw = ringArea(pts) > 0.0;
 const int n = pts.size();
 for (int i = 0; i < n; ++i) {
  const auto &a = pts[i];
  const auto &b = pts[(i + 1) % n];
  if (QLineF(a, b).length() < 1e-4) continue;
  QVector3D norm(float(b.y() - a.y()), float(a.x() - b.x()), 0.0f);
  if (!ccw) norm = -norm;
  norm.normalize();
  pushTri(a, z0, b, z0, b, z1, norm, wallColor);
  pushTri(a, z0, b, z1, a, z1, norm, wallColor);
  // Crisp architectural silhouette edges (roofline + corners)
  pushLine(a, z1 + 0.06, b, z1 + 0.06, lineColor);
  pushLine(a, z0 + 0.06, a, z1 + 0.06, lineColor);
 }

 // Architectural horizontal floor slab lines
 if (floorLines && floors > 1) {
  const double stepH = (z1 - z0) / double(floors);
  QColor slabCol = lineColor;
  slabCol.setAlpha(155);
  for (int fl = 1; fl < floors; ++fl) {
   const double zf = z0 + stepH * fl;
   for (int i = 0; i < n; ++i) {
    const auto &a = pts[i];
    const auto &b = pts[(i + 1) % n];
    pushLine(a, zf, b, zf, slabCol);
   }
  }
 }
}
} // namespace

QPolygonF insetPolygon(const QPolygonF &ring, double setbackM) {
 if (ring.size() < 3) return ring;
 QPolygonF clean = ring;
 if (clean.size() > 3 && QLineF(clean.first(), clean.last()).length() < 1e-4) clean.removeLast();
 if (setbackM <= 0.05) return clean;

 const double origArea = std::abs(ringArea(clean));
 if (origArea < 4.0) return clean;
 const bool ccw = ringArea(clean) > 0.0;
 const int n = clean.size();
 QPolygonF offset;
 offset.reserve(n);

 bool ok = true;
 for (int i = 0; i < n; ++i) {
  const QPointF prev = clean[(i + n - 1) % n];
  const QPointF curr = clean[i];
  const QPointF next = clean[(i + 1) % n];
  const QPointF e1 = curr - prev;
  const QPointF e2 = next - curr;
  const double l1 = std::hypot(e1.x(), e1.y());
  const double l2 = std::hypot(e2.x(), e2.y());
  if (l1 < 1e-4 || l2 < 1e-4) { ok = false; break; }
  // Inward unit normal for each edge
  const double sgn = ccw ? 1.0 : -1.0;
  const QPointF n1(-e1.y() / l1 * sgn, e1.x() / l1 * sgn);
  const QPointF n2(-e2.y() / l2 * sgn, e2.x() / l2 * sgn);
  QPointF bis = n1 + n2;
  const double bLen = std::hypot(bis.x(), bis.y());
  if (bLen < 1e-4) {
   offset.append(curr + n1 * setbackM);
  } else {
   bis /= bLen;
   const double denom = std::max(0.35, bis.x() * n1.x() + bis.y() * n1.y());
   const double miter = std::min(setbackM / denom, setbackM * 2.4);
   offset.append(curr + bis * miter);
  }
 }

 const double newArea = ok ? std::abs(ringArea(offset)) : 0.0;
 const bool sameSign = ok && ((ringArea(offset) > 0.0) == ccw);
 // Validate that inset didn't invert or blow up
 if (ok && sameSign && newArea > 9.0 && newArea < origArea * 0.995) {
  return offset;
 }

 // Safe centroid scale fallback (guaranteed valid inset for complex/narrow plots)
 const double perim = [&] {
  double p = 0.0;
  for (int i = 0; i < n; ++i) p += QLineF(clean[i], clean[(i + 1) % n]).length();
  return std::max(1.0, p);
 }();
 const double approxR = 2.0 * origArea / perim;
 const double scale = std::clamp(1.0 - setbackM / std::max(1.0, approxR), 0.20, 0.98);
 return scaleRingAbout(clean, polygonCentroid(clean), scale);
}

MassingMetrics evaluateMassing(const PlotDesign &plot, const MassingConfig &cfg) {
 MassingMetrics m;
 if (!plot.active) return m;
 const QPolygonF plotRing = plot.polygon();
 if (plotRing.size() < 3) return m;
 m.siteAreaM2 = std::abs(ringArea(plotRing));
 if (m.siteAreaM2 < 4.0) return m;

 m.podiumRing = insetPolygon(plotRing, cfg.setbackM);
 m.buildableAreaM2 = std::abs(ringArea(m.podiumRing));
 const QPointF cen = polygonCentroid(m.podiumRing);
 const QRectF pBounds = m.podiumRing.boundingRect();
 const double towerScale = std::sqrt(std::clamp(cfg.towerCoverage, 0.12, 1.0));
 const QPointF shift(cfg.towerShiftX * pBounds.width() * 0.5, cfg.towerShiftY * pBounds.height() * 0.5);
 m.towerRing = scaleRingAbout(m.podiumRing, cen, towerScale, cfg.towerRotationDeg, shift);

 m.podiumFootprintM2 = (cfg.podiumFloors > 0) ? m.buildableAreaM2 : 0.0;
 m.towerFootprintM2 = (cfg.towerFloors > 0) ? std::abs(ringArea(m.towerRing)) : 0.0;
 const double occupied = std::max(m.podiumFootprintM2, m.towerFootprintM2);
 m.bcrPercent = std::clamp((occupied / m.siteAreaM2) * 100.0, 0.0, 100.0);
 m.totalGfaM2 = m.podiumFootprintM2 * cfg.podiumFloors + m.towerFootprintM2 * cfg.towerFloors;
 m.far = m.totalGfaM2 / m.siteAreaM2;
 m.podiumHeightM = cfg.podiumFloors * cfg.podiumFloorHeight;
 m.towerHeightM = cfg.towerFloors * cfg.towerFloorHeight;
 m.totalHeightM = m.podiumHeightM + m.towerHeightM;
 m.totalFloors = cfg.podiumFloors + cfg.towerFloors;
 m.valid = (m.totalFloors > 0 && occupied > 1.0);
 return m;
}

void appendMassingToMesh(Mesh &mesh, const PlotDesign &plot, const MassingConfig &cfg, double baseZ) {
 if (!cfg.enabled || !plot.active) return;
 const auto m = evaluateMassing(plot, cfg);
 if (!m.valid) return;

 const QColor baseCol = cfg.accentColor.isValid() ? cfg.accentColor : QColor("#d2643c");
 const QColor podiumRoof = baseCol.lighter(110);
 const QColor podiumWall = baseCol.darker(108);
 const QColor towerRoof = baseCol.lighter(122);
 const QColor towerWall = baseCol.lighter(103);
 const QColor edgeInk = baseCol.darker(175);

 double curZ = baseZ;
 if (cfg.podiumFloors > 0 && m.podiumHeightM > 0.2) {
  addPrism(mesh, m.podiumRing, curZ, curZ + m.podiumHeightM, podiumRoof, podiumWall, edgeInk,
           cfg.podiumFloors, cfg.showFloorPlates, 3.0f);
  curZ += m.podiumHeightM;
 }
 if (cfg.towerFloors > 0 && m.towerHeightM > 0.2) {
  addPrism(mesh, m.towerRing, curZ, curZ + m.towerHeightM, towerRoof, towerWall, edgeInk,
           cfg.towerFloors, cfg.showFloorPlates, 3.0f);
  // Subtle rooftop mechanical penthouse core (20% of tower ring, +3.2m) for architectural realism
  const QPolygonF coreRing = scaleRingAbout(m.towerRing, polygonCentroid(m.towerRing), 0.36);
  if (std::abs(ringArea(coreRing)) >= 12.0) {
   addPrism(mesh, coreRing, curZ + m.towerHeightM, curZ + m.towerHeightM + 3.2,
            towerRoof.darker(106), towerWall.darker(114), edgeInk, 1, false, 3.0f);
  }
 }
}

} // namespace ga
