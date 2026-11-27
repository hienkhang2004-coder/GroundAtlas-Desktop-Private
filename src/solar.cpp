#include "solar.h"
#include <algorithm>
#include <cmath>

namespace ga {
namespace {
constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;

double cross2(const QPointF &o, const QPointF &a, const QPointF &b) {
 return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

QPolygonF convexHull(QVector<QPointF> pts) {
 if (pts.size() <= 3) return QPolygonF(pts);
 std::sort(pts.begin(), pts.end(), [](const QPointF &a, const QPointF &b) {
  return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
 });
 pts.erase(std::unique(pts.begin(), pts.end(), [](const QPointF &a, const QPointF &b) {
  return std::abs(a.x() - b.x()) < 1e-4 && std::abs(a.y() - b.y()) < 1e-4;
 }), pts.end());
 if (pts.size() <= 2) return QPolygonF(pts);
 QVector<QPointF> hull;
 hull.reserve(pts.size() * 2);
 for (const auto &p : pts) {
  while (hull.size() >= 2 && cross2(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
  hull.push_back(p);
 }
 const int lower = hull.size() + 1;
 for (int i = pts.size() - 2; i >= 0; --i) {
  const auto &p = pts[i];
  while (hull.size() >= lower && cross2(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
  hull.push_back(p);
 }
 if (!hull.isEmpty()) hull.pop_back();
 return QPolygonF(hull);
}
} // namespace

QJsonObject SolarState::toJson() const {
 return {
  {"enabled", enabled},
  {"show_path", showPath},
  {"day_of_year", dayOfYear},
  {"hour", hour},
  {"shadow_strength", shadowStrength},
  {"golden_hour", goldenHour},
  {"sky_dome", skyDome},
  {"trees_3d", trees3d},
 };
}

SolarState SolarState::fromJson(const QJsonObject &v) {
 SolarState s;
 s.enabled = v["enabled"].toBool(false);
 s.showPath = v["show_path"].toBool(true);
 s.dayOfYear = std::clamp(v["day_of_year"].toInt(172), 1, 366);
 s.hour = std::clamp(v["hour"].toDouble(14.0), 5.0, 19.0);
 s.shadowStrength = std::clamp(v["shadow_strength"].toDouble(0.72), 0.15, 1.0);
 s.goldenHour = v["golden_hour"].toBool(true);
 s.skyDome = v["sky_dome"].toBool(true);
 s.trees3d = v["trees_3d"].toBool(true);
 return s;
}

SolarPosition solarPosition(double latitudeDeg, int dayOfYear, double localSolarHour) {
 SolarPosition out;
 const double lat = std::clamp(latitudeDeg, -89.0, 89.0) * kDegToRad;
 // Cooper's astronomical declination approximation (degrees)
 const double decl = 23.44 * std::sin(2.0 * M_PI * (284.0 + std::clamp(dayOfYear, 1, 366)) / 365.0) * kDegToRad;
 // Hour angle: 0 at solar noon (12:00), negative morning (East), positive afternoon (West)
 const double ha = (localSolarHour - 12.0) * 15.0 * kDegToRad;

 const double sinAlt = std::clamp(std::sin(lat) * std::sin(decl) + std::cos(lat) * std::cos(decl) * std::cos(ha), -1.0, 1.0);
 const double alt = std::asin(sinAlt);
 const double cosAlt = std::max(1e-6, std::cos(alt));

 // Sun unit vector in local ENU (X = East, Y = North, Z = Up)
 // East component = -cos(decl)*sin(ha), North component = sin(decl)*cos(lat) - cos(decl)*sin(lat)*cos(ha)
 const double east = -std::cos(decl) * std::sin(ha);
 const double north = std::sin(decl) * std::cos(lat) - std::cos(decl) * std::sin(lat) * std::cos(ha);
 const double up = sinAlt;

 double az = std::atan2(east, north) * kRadToDeg;
 if (az < 0) az += 360.0;

 QVector3D dir{float(east), float(north), float(up)};
 if (dir.lengthSquared() > 1e-6f) dir.normalize();

 out.altitudeDeg = alt * kRadToDeg;
 out.azimuthDeg = az;
 out.direction = dir;
 out.aboveHorizon = out.altitudeDeg > 1.5;
 Q_UNUSED(cosAlt);
 return out;
}

QVector<QVector3D> solarArcPoints(double latitudeDeg, int dayOfYear, double domeRadius, int steps) {
 QVector<QVector3D> pts;
 const int n = std::max(8, steps);
 pts.reserve(n + 1);
 for (int i = 0; i <= n; ++i) {
  const double hour = 4.5 + 15.0 * double(i) / n;
  const auto sp = solarPosition(latitudeDeg, dayOfYear, hour);
  if (sp.altitudeDeg >= 0.0) {
   pts.append(sp.direction * float(domeRadius));
  }
 }
 return pts;
}

QPolygonF buildingShadowHull(const QPolygonF &ring, double height, const SolarPosition &sun) {
 if (ring.size() < 3 || height <= 0.2 || !sun.aboveHorizon) return {};
 const double clampedAlt = std::max(sun.altitudeDeg, 6.0) * kDegToRad;
 const double shadowLen = std::min(height / std::tan(clampedAlt), height * 8.0);
 const double horizLen = std::hypot(double(sun.direction.x()), double(sun.direction.y()));
 if (horizLen < 1e-5) return {};
 const QPointF shift(-double(sun.direction.x()) / horizLen * shadowLen,
                     -double(sun.direction.y()) / horizLen * shadowLen);
 QVector<QPointF> combined;
 combined.reserve(ring.size() * 2);
 for (const auto &pt : ring) {
  combined.append(pt);
  combined.append(pt + shift);
 }
 return convexHull(std::move(combined));
}

} // namespace ga
