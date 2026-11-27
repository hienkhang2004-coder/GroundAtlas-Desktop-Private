#pragma once
#include "project.h"
#include <QVector3D>
#include <QVector>

namespace ga {

struct SolarState {
 bool enabled = false;
 bool showPath = true;
 int dayOfYear = 172; // Summer solstice default (June 21)
 double hour = 14.0;  // Local solar hour [5.0 .. 19.0]
 double shadowStrength = 0.72; // GPU PCF shadow contrast [0.2 .. 1.0]
 bool goldenHour = true;       // Physically-inspired warm sunrise/sunset color shift
 bool skyDome = true;          // Dynamic atmospheric sky gradient by solar altitude
 bool trees3d = true;          // Sculpted low-poly 3D architectural trees casting shadows
 bool operator==(const SolarState &) const = default;
 QJsonObject toJson() const;
 static SolarState fromJson(const QJsonObject &v);
};

struct SolarPosition {
 double altitudeDeg = 0;
 double azimuthDeg = 180; // 0=North, 90=East, 180=South, 270=West
 QVector3D direction{0, -0.5f, 0.866f}; // Unit vector toward sun (X=East, Y=North, Z=Up)
 bool aboveHorizon = false;
};

SolarPosition solarPosition(double latitudeDeg, int dayOfYear, double localSolarHour);
QVector<QVector3D> solarArcPoints(double latitudeDeg, int dayOfYear, double domeRadius, int steps = 48);
QPolygonF buildingShadowHull(const QPolygonF &ring, double height, const SolarPosition &sun);

} // namespace ga
