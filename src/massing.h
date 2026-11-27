#pragma once
#include "urbananalytics.h"
#include "scene.h"
#include <QColor>
#include <QJsonObject>
#include <QPolygonF>

namespace ga {

struct MassingConfig {
 bool enabled = false;
 double setbackM = 4.0;          // Inward setback from plot boundary (m)
 int podiumFloors = 3;           // Podium stories [0..15]
 double podiumFloorHeight = 4.2; // Metres per podium floor
 int towerFloors = 12;           // Tower stories [0..90]
 double towerFloorHeight = 3.4;  // Metres per tower floor
 double towerCoverage = 0.45;    // Tower footprint ratio relative to podium [0.15..1.0]
 double towerShiftX = 0.0;       // Normalized X shift inside podium [-0.35..0.35]
 double towerShiftY = 0.0;       // Normalized Y shift inside podium [-0.35..0.35]
 double towerRotationDeg = 0.0;  // Extra rotation for tower block (degrees)
 bool showFloorPlates = true;    // Architectural floor slab lines on facade
 QColor accentColor{"#d2643c"};  // Proposed massing color

 bool operator==(const MassingConfig &) const = default;
 QJsonObject toJson() const;
 static MassingConfig fromJson(const QJsonObject &v);
};

struct MassingMetrics {
 bool valid = false;
 double siteAreaM2 = 0.0;
 double buildableAreaM2 = 0.0;
 double podiumFootprintM2 = 0.0;
 double towerFootprintM2 = 0.0;
 double bcrPercent = 0.0;   // Building Coverage Ratio (%)
 double totalGfaM2 = 0.0;   // Gross Floor Area (m2)
 double far = 0.0;          // Floor Area Ratio (He so su dung dat)
 double podiumHeightM = 0.0;
 double towerHeightM = 0.0;
 double totalHeightM = 0.0;
 int totalFloors = 0;
 QPolygonF podiumRing;      // Local site metres
 QPolygonF towerRing;       // Local site metres
};

// Inward polygon offset with safe miter clamp and centroid-scale fallback
QPolygonF insetPolygon(const QPolygonF &ring, double setbackM);

// Compute real architectural massing footprints & statutory KPIs (BCR, FAR, GFA)
MassingMetrics evaluateMassing(const PlotDesign &plot, const MassingConfig &cfg);

// Append 3D architectural podium + tower geometry (casts real 4096px GPU shadows!)
void appendMassingToMesh(Mesh &mesh, const PlotDesign &plot, const MassingConfig &cfg, double baseZ = 0.0);

} // namespace ga
