#pragma once
#include "project.h"
#include "scene.h"
#include "urbananalytics.h"
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPair>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

namespace ga {

enum class PlanningCrs {
 Auto = 0,
 Vn2000Hanoi3 = 1,    // VN-2000 Hanoi Zone 3 deg (CM 105 deg E, k0 = 0.9999, EPSG:9210)
 Vn2000National6 = 2, // VN-2000 National Zone 6 deg (CM 105 deg E, k0 = 0.9996)
 Wgs84 = 3,
 SiteLocal = 4,
};

struct PlanningZoneDef {
 QString code; // DI_SAN, ODT, HH, CC, GD, YT, CX, MN, GT, CN, HTKT
 QString vi;
 QString en;
 QColor fill;
 QColor stroke;
 double defaultMaxHeightM = 18.0;
 int defaultMaxBcrPct = 60;
};

struct HanoiSubdivisionInfo {
 bool matched = false;
 QString code;
 QString nameVi;
 QString nameEn;
 QString decision;
 QString maxHeightNote;
 QString densityNote;
 QString orientationVi;
 QString orientationEn;
 double defaultMaxHeightM = 24.0;
};

struct PlanningVectorZone {
 QString code;
 QString label;
 QPolygonF ring; // Local site metres
 QColor fill;
 QColor stroke;
 double maxHeightM = 0.0;
 int maxBcrPct = 0;
 QString source; // "hanoi_masterplan", "imported_dxf", "imported_geojson", "osm_inferred"
};

struct PlanningCorridor {
 QString id;
 QString name;
 QString kind; // "metro", "ring_road", "bridge", "river_axis", "road_widening", "osm_proposed"
 QPolygonF polyline; // Local site metres
 QColor color;
 double widthM = 12.0;
 bool underground = false;
 QString status;
};

struct PlanningStation {
 QString code;
 QString name;
 QString line;
 QPointF pos; // Local site metres
 QColor color;
 bool underground = true;
};

struct PlanningImageOverlay {
 bool enabled = false;
 QString imagePath;
 QImage image;
 double opacity = 0.56;
 double centerX = 0.0;
 double centerY = 0.0;
 double widthM = 1000.0;
 double rotationDeg = 0.0;

 bool valid() const { return enabled && !image.isNull() && widthM > 5.0; }
 QJsonObject toJson() const;
 static PlanningImageOverlay fromJson(const QJsonObject &v);
};

struct PlanningCustomData {
 QString sourceName;
 QString sourcePath;
 PlanningCrs crs = PlanningCrs::Auto;
 QVector<PlanningVectorZone> zones;
 QVector<PlanningCorridor> corridors;

 bool empty() const { return zones.isEmpty() && corridors.isEmpty(); }
};

struct FuturePlanningOverlay {
 QString levelId; // "qhc-2045", "qhpk-2000", "qhct-500", "compare"
 QString titleVi;
 QString titleEn;
 QString summaryVi;
 QString summaryEn;
 HanoiSubdivisionInfo subdivision;
 QVector<PlanningVectorZone> zones;
 QVector<PlanningCorridor> corridors;
 QVector<PlanningStation> stations;
 QVector<QPair<QPolygonF, QColor>> diffBuildings; // Existing vs Future diff highlight
 QVector<QPair<QColor, QString>> legendItems;
 double opacity = 0.80;
 bool show3DEnvelopes = false;
 bool show3DCorridors = false;

 bool valid() const { return !levelId.isEmpty(); }
};

// Official Vietnam TT 04/2022/TT-BXD & QCVN 01:2021/BXD zone color table
QVector<PlanningZoneDef> planningZoneCatalog();
PlanningZoneDef classifyPlanningCode(const QString &rawText);

// Exact ellipsoidal VN-2000 (Hanoi 3-deg CM 105E or National 6-deg) -> WGS84 (lon, lat)
QPointF vn2000ToWgs84(double easting, double northing, double centralMeridianDeg = 105.0, double scaleFactor = 0.9999);
QPointF wgs84ToVn2000(double lon, double lat, double centralMeridianDeg = 105.0, double scaleFactor = 0.9999);

// Detect active Hanoi QHC 2045 subdivision for any (lat, lon)
HanoiSubdivisionInfo detectHanoiSubdivision(double lat, double lon, const QString &planningDir);

// Import AutoCAD DXF or GeoJSON planning map (auto-detecting VN-2000 Hanoi / WGS84 / Local metres)
PlanningCustomData importPlanningVectorFile(const QString &filePath,
                                            const Site &site,
                                            PlanningCrs crs = PlanningCrs::Auto,
                                            QString *errorOut = nullptr);

// Build the full Future Planning overlay for one of the 4 planning levels:
// "qhc-2045" (1/25.000), "qhpk-2000" (1/2.000), "qhct-500" (1/500 3D Envelope), "compare" (Existing <-> Future)
FuturePlanningOverlay computeFuturePlanningOverlay(const Project &project,
                                                   const QString &levelId,
                                                   const QString &planningDir,
                                                   const PlanningCustomData *customData = nullptr,
                                                   const PlotDesign *plot = nullptr,
                                                   double opacity = 0.80);

// Append translucent 3D statutory height envelopes & glowing Metro corridors into GPU scene mesh
void appendPlanning3DToMesh(Mesh &mesh,
                            const FuturePlanningOverlay &planning,
                            const std::function<double(const QPointF &)> &terrainZ = {});

} // namespace ga
