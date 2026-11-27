#include "planpainter.h"
#include "contextcamera.h"
#include <QPainterPath>
#include <cmath>
#include <QtMath>

bool PlanImageCache::reusable(const QTransform &transform,QSize size,double ratio)const{
 if(image.isNull()||viewport!=size||image.devicePixelRatio()!=ratio||!mapToImage.isInvertible())return false;
 const auto relative=mapToImage.inverted()*transform;const auto scale=std::hypot(relative.m11(),relative.m12());if(scale<.65||scale>1.55)return false;
 return relative.mapRect(QRectF(QPointF(),image.deviceIndependentSize())).contains(QRectF(QPointF(),size));
}
QTransform PlanImageCache::begin(QSize size,double ratio,const QTransform &transform,QColor background){
 viewport=size;const QPoint margin(qCeil(size.width()*.3),qCeil(size.height()*.3));const auto padded=size+QSize(margin.x()*2,margin.y()*2);const QSize pixels(qCeil(padded.width()*ratio),qCeil(padded.height()*ratio));
 if(image.size()!=pixels||image.devicePixelRatio()!=ratio)image=QImage(pixels,QImage::Format_ARGB32_Premultiplied);
 image.setDevicePixelRatio(ratio);image.fill(background);mapToImage=transform*QTransform::fromTranslate(margin.x(),margin.y());return mapToImage;
}
void PlanImageCache::draw(QPainter &p,const QTransform &transform)const{p.save();p.setRenderHint(QPainter::SmoothPixmapTransform);p.setWorldTransform(mapToImage.inverted()*transform,true);p.drawImage(QPointF(),image);p.restore();}

PlanCache preparePlan(const ga::Project &project,const QVector<ga::Layer> &layers,const QString &diagram){PlanCache result(layers.size());ga::FeaturePaintCache classifier(layers,diagram);for(int i=0;i<project.features.size();++i)for(const auto &paint:classifier.paints(project.features[i]))result[paint.order].append({i,paint.color,paint.kind});return result;}
PlanGeometry preparePlanGeometry(const ga::Project &project){
 PlanGeometry result;result.paths.reserve(project.features.size());
 for(const auto &f:project.features){QPainterPath path;path.setFillRule(Qt::OddEvenFill);for(const auto &ring:f.rings){if(ring.isEmpty())continue;QPolygonF points;points.reserve(ring.size());for(const auto &v:ring)points.append(ga::mapPlanePoint(v,project.site));if(f.geometry==ga::Geometry::Polygon){path.addPolygon(points);path.closeSubpath();}else{path.moveTo(points.first());for(int i=1;i<points.size();++i)path.lineTo(points[i]);}}result.paths.append(std::move(path));}return result;
}
static void paintPlanImpl(QPainter &p,const ga::Project &project,const QVector<ga::Layer> &layers,const ga::Palette &palette,const QString &diagram,const std::function<QPointF(QPointF)> &point,double ppm,bool paper,int selected,bool grid,const PlanCache *cache,const PlanGeometry *geometry=nullptr){
 if(ppm<=0||!std::isfinite(ppm))return;
 const auto shape=[&](const ga::Feature &f,QPointF offset=QPointF()){QPainterPath path;path.setFillRule(Qt::OddEvenFill);for(const auto &r:f.rings){QPolygonF ring;for(const auto &v:r)ring.append(point(v+offset));path.addPolygon(ring);path.closeSubpath();}return path;};
 QPolygonF boundary;for(const auto &v:project.site.boundary)boundary.append(point(v));QPainterPath clip;clip.addPolygon(boundary);clip.closeSubpath();p.save();p.setRenderHint(QPainter::Antialiasing);p.setClipPath(clip,Qt::IntersectClip);if(paper)p.fillPath(clip,palette.paper);
 const auto pen=[&](QPen value){if(geometry)value.setCosmetic(true);return value;};
 if(grid){p.setPen(pen(QPen(palette.ink.lighter(230),.5)));const double r=project.site.radius;for(double v=-std::ceil(r/100)*100;v<=r;v+=100){p.drawLine(point({-r,v}),point({r,v}));p.drawLine(point({v,-r}),point({v,r}));}}
 const auto prepared=cache?PlanCache():preparePlan(project,layers,diagram);const auto &buckets=cache?*cache:prepared;
 const auto baseOpacity=p.opacity();
 for(int order=0;order<layers.size();++order){const auto &l=layers[order];if(buckets[order].isEmpty())continue;const auto &o=l.presentation;const auto shadow=o["shadow"].toObject();
  if(!shadow.isEmpty()){p.setPen(Qt::NoPen);p.setBrush(QColor(shadow["color"].toString()));p.setOpacity(baseOpacity*l.opacity*shadow["opacity"].toDouble());const QPointF offset(shadow["dx"].toDouble(),-shadow["dy"].toDouble());for(const auto &record:buckets[order])if(record.kind==ga::Geometry::Polygon)p.drawPath(shape(project.features[record.feature],offset));}
  p.setOpacity(baseOpacity*l.opacity);
  for(const auto &record:buckets[order]){const auto &f=project.features[record.feature];const auto color=record.feature==selected?QColor("#d59a59"):record.color;const auto outline=o["stroke"].toString();const auto width=o["strokeWidth"].toDouble(1)*project.site.radius/166*ppm;
   p.setPen(!outline.isEmpty()&&outline!="none"&&width>0?pen(QPen(QColor(outline),width,Qt::SolidLine,Qt::SquareCap,Qt::MiterJoin)):QPen(Qt::NoPen));p.setBrush(color);
   if(record.kind==ga::Geometry::Polygon)p.drawPath(geometry?geometry->paths[record.feature]:shape(f));
   else if(record.kind==ga::Geometry::Point){auto radius=ga::pointRadiusMetres(f,l,project.site.radius)*ppm;if(geometry)radius/=std::hypot(p.transform().m11(),p.transform().m12());p.drawEllipse(point(f.rings[0][0]),radius,radius);}
   else{p.setBrush(Qt::NoBrush);p.setPen(pen(QPen(color,ga::lineWidthMetres(f,l,project.site.radius)*ppm,Qt::SolidLine,Qt::FlatCap,o.contains("lineWidth")?Qt::MiterJoin:Qt::RoundJoin)));if(geometry&&f.geometry==ga::Geometry::Line)p.drawPath(geometry->paths[record.feature]);else{QPolygonF line;for(const auto &v:f.rings[0])line.append(point(v));p.drawPolyline(line);}}
  }
 }
 p.restore();p.save();p.setBrush(Qt::NoBrush);p.setPen(pen(QPen(palette.ink,0.7)));p.drawPolygon(boundary);p.restore();
}
void paintPlan(QPainter &p,const ga::Project &project,const QVector<ga::Layer> &layers,const ga::Palette &palette,const QString &diagram,const std::function<QPointF(QPointF)> &point,double ppm,bool paper,int selected,bool grid,const PlanCache *cache){paintPlanImpl(p,project,layers,palette,diagram,point,ppm,paper,selected,grid,cache);}
void paintPlanMercator(QPainter &p,const ga::Project &project,const QVector<ga::Layer> &layers,const ga::Palette &palette,const QString &diagram,const QTransform &transform,double ppm,int selected,bool grid,const PlanCache &cache,const PlanGeometry &geometry){
 p.save();p.setWorldTransform(transform,true);paintPlanImpl(p,project,layers,palette,diagram,[&](QPointF v){return ga::mapPlanePoint(v,project.site);},ppm,true,selected,grid,&cache,&geometry);p.restore();
}
