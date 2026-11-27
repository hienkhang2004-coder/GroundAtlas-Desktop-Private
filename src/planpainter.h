#pragma once
#include "scene.h"
#include <QPainter>
#include <QPainterPath>
#include <functional>

struct PlanRecord{int feature=0;QColor color;ga::Geometry kind=ga::Geometry::Polygon;};
using PlanCache=QVector<QVector<PlanRecord>>;
PlanCache preparePlan(const ga::Project &project,const QVector<ga::Layer> &layers,const QString &diagram);
// Retained Mercator paths, independent of navigation and presentation. Source
// geometry is untouched; export continues to use the source-coordinate painter.
struct PlanGeometry {QVector<QPainterPath> paths;};
// A padded presentation snapshot can follow a gesture without re-rasterizing
// thousands of paths per input event. A precise redraw follows after 120 ms.
struct PlanImageCache {
 QImage image;QTransform mapToImage;QSize viewport;
 void clear(){image={};}
 bool reusable(const QTransform &mapToScreen,QSize size,double ratio)const;
 QTransform begin(QSize size,double ratio,const QTransform &mapToScreen,QColor background);
 void draw(QPainter &painter,const QTransform &mapToScreen)const;
};
PlanGeometry preparePlanGeometry(const ga::Project &project);
void paintPlanMercator(QPainter &painter,const ga::Project &project,const QVector<ga::Layer> &layers,
 const ga::Palette &palette,const QString &diagram,const QTransform &mapToScreen,double pixelsPerMetre,
 int selected,bool grid,const PlanCache &cache,const PlanGeometry &geometry);

// Shared native painter for the geographic overlay, 2D viewport, SVG and PDF.
// Coordinates stay in local metres; only this projection maps them to pixels.
void paintPlan(QPainter &painter,const ga::Project &project,const QVector<ga::Layer> &layers,
 const ga::Palette &palette,const QString &diagram,const std::function<QPointF(QPointF)> &projectPoint,
 double pixelsPerMetre,bool paper=true,int selected=-1,bool grid=false,const PlanCache *cache=nullptr);
