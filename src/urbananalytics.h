#pragma once
#include "project.h"
#include "solar.h"
#include <QColor>
#include <QImage>
#include <QPair>
#include <QPolygonF>
#include <QString>
#include <QVector>

namespace ga {

struct PlotDesign {
 bool active = false;
 bool isPolygon = false; // false = rect (width x length x rotation), true = freehand/polygon customRing
 double centerX = 0;
 double centerY = 0;
 double width = 50;
 double length = 80;
 double rotationDeg = 0;
 QPolygonF customRing; // Local site metres (open ring, >= 3 points)
 bool showDimensions = true;
 QColor strokeColor{"#c4422a"};
 bool operator==(const PlotDesign &) const = default;
 QPolygonF polygon() const;
 double areaM2() const;
 double perimeterM() const;
 QJsonObject toJson() const;
 static PlotDesign fromJson(const QJsonObject &v);
};

// Simplify dense freehand stroke points (local metres or WGS84) with Ramer-Douglas-Peucker
QPolygonF simplifyPolyline(const QPolygonF &pts, double tolerance);

struct AnalyticalOverlay {
 QString id;
 QString titleVi, titleEn, unit, summaryVi, summaryEn;
 QImage heatmap; // Grid mapped onto worldBounds (left->right, top=+y -> bottom=-y)
 QRectF worldBounds; // Local metres rect
 double minVal = 0, maxVal = 1, meanVal = 0;
 QVector<QColor> legendStops;
 QPolygonF isovistPolygon;
 QPointF viewpoint{0, 0};

 // Network walkshed & traffic congestion colored segments
 struct WalkSegment { QPointF a, b; double minutes = 15; QColor customColor; };
 QVector<WalkSegment> walkSegments;

 // Wind streamlines & contour polylines
 QVector<QPolygonF> streamlines;
 QVector<QPolygonF> contours;
 QVector<QPolygonF> shadowPolygons;
 double windDirDeg = 135.0; // Wind blowing FROM (SE default)

 // Special 2D overlays (Polar sunpath & TMY Wind Rose)
 bool showSunpath2d = false;
 SolarState sunState;
 QVector<double> windRose16; // 16-sector normalized frequencies (0=N, 22.5° steps)
 QString windStationLabel;

 bool valid() const { return !id.isEmpty(); }
};

AnalyticalOverlay computeAnalyticalOverlay(const Project &project,
                                           const QString &kind,
                                           const SolarState &solar,
                                           double windFromDeg = 135.0,
                                           QPointF viewpoint = {0, 0},
                                           const QString &envDir = {},
                                           const QString &climateFile = {});

} // namespace ga
