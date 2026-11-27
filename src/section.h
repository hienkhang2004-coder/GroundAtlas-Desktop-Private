#pragma once
#include "project.h"
#include "scene.h"
#include <QColor>
#include <QJsonObject>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>

namespace ga {

enum class SectionMode {
 FlatUrban = 0,
 TerrainDem = 1,
};

struct TerrainGrid {
 bool valid = false;
 double minLon = 0, minLat = 0, maxLon = 0, maxLat = 0;
 int width = 0, height = 0;
 double minVal = 0, maxVal = 0;
 QVector<double> values; // row-major [y * width + x], NaN for nodata
 QString source;

 static TerrainGrid fromEnvironmentField(const QJsonObject &field);
 double sample(double lon, double lat, double fallback = 0.0) const;
};

struct PlotDesign;
struct MassingConfig;
struct ImportedModel3D;

struct SectionSpan {
 double s0 = 0;
 double s1 = 0;
 double groundElev = 0;
 double height = 0;
 double baseOffset = 0; // Optional height above ground (e.g., tower atop podium)
 bool knownHeight = false;
 QString kind; // building, proposed, road, water, green
 QString subkind;
 QString label;
 QColor color;
};

struct SectionBackground {
 double s0 = 0;
 double s1 = 0;
 double groundElev = 0;
 double height = 0;
 double depth = 0; // perpendicular distance behind section plane (m)
};

struct SectionCallout {
 double s = 0;
 double elev = 0;
 QString label;
 QString kind; // road, water, building
};

struct SectionData {
 QPointF a;
 QPointF b;
 double length = 0;
 SectionMode mode = SectionMode::FlatUrban;
 bool hasDem = false;
 double minGround = 0;
 double maxGround = 0;
 double maxTop = 20;
 QVector<QPointF> groundProfile; // (distance_m, elev_m)
 QVector<SectionSpan> buildings;
 QVector<SectionSpan> roads;
 QVector<SectionSpan> waters;
 QVector<SectionSpan> greens;
 QVector<SectionBackground> skyline;
 QVector<SectionCallout> callouts;
 QString demNote;
};

SectionData computeSection(const Project &project,
                           QPointF a,
                           QPointF b,
                           SectionMode mode = SectionMode::FlatUrban,
                           const TerrainGrid *dem = nullptr,
                           double contextDepth = 120.0,
                           const PlotDesign *plot = nullptr,
                           const MassingConfig *massing = nullptr,
                           const QVector<ImportedModel3D> *models3d = nullptr);

} // namespace ga
