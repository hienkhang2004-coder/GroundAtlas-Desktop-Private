#pragma once
#include "project.h"
#include <QJsonDocument>
#include <atomic>
#include <QPainterPath>
namespace ga {
// Build the boundary once for a batch; the original clipping topology is kept.
class SiteClipper {
public:
 explicit SiteClipper(const Site &site);
 QVector<Feature> clip(const Feature &feature)const;
private:
 QPolygonF boundary_;
 QPainterPath path_;
 QRectF bounds_;
 bool axisAlignedRectangle_=false;
 double innerCircleRadius_=0;
 bool definitelyInside(const QPointF &point)const;
};
// Public OSM records only. Relation rings are joined by their actual endpoints.
Project importOverpass(const QJsonObject &root,const Site &site,const std::atomic_bool *cancel=nullptr,bool allowEmpty=false);
QVector<Feature> clipToSite(const Feature &feature,const Site &site);
}
