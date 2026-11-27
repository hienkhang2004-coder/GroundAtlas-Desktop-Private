#pragma once
#include "project.h"
#include <atomic>
namespace ga {
QPolygonF primitiveBoundary(const Site &site);
QString boundaryError(const QPolygonF &boundary);
QString selectionError(const Site &site,double maximumHalfExtent=1500);
 QString localizedSiteError(const QString &error,bool english);
double envelopeRadius(const Site &site);
bool boundaryContains(const QPolygonF &boundary,const QPointF &point);
Site siteFromGeographicBoundary(const QPolygonF &lonLat,const QString &name={});
Project reclipProject(const Project &source,const Site &site,const std::atomic_bool *cancel=nullptr);
}
