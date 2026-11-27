#pragma once
#include "scene.h"
#include <QSize>

namespace ga {
// Scene coordinates are a local Mercator plane; analytical/source coordinates
// remain the original metre-based site frame. No basemap feature enters Project.
QPointF mapPlanePoint(QPointF local,const Site &site);
QPointF sitePlanePoint(QPointF mercator,const Site &site);
// View-centred render coordinates keep GPU floats small while exploring the
// world. Source/analysis coordinates and saved geometry remain site-local.
QPointF scenePlanePoint(QPointF local,const Site &site,const Camera &camera);
QPointF scenePlaneFromMapPoint(QPointF point,const Site &site,const Camera &camera);
QPointF localFromScenePlane(QPointF point,const Site &site,const Camera &camera);
void normalizeCameraCenter(Camera &camera,const Site &site);
QMatrix4x4 sceneCameraMatrix(const Site &site,const Camera &camera,double aspect);
QPointF groundOnMapPlane(const QMatrix4x4 &matrix,QSize viewport,QPointF screen);
struct ContextCamera {
 QVector<double> projection;
 double lat=0,lon=0,zoom=0,pitch=0,bearing=0;
};
ContextCamera contextCamera(const Site &site,const Camera &camera,QSize viewport);
Camera cameraAtGeographicView(const Site &site,Camera camera,QSize viewport,double latitude,double longitude,double logicalZoom);
double groundMetresPerPixel(const Site &site,const Camera &camera,QSize viewport,QPointF screen);
}
