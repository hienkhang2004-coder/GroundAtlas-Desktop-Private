#include "contextcamera.h"
#include "sitegeometry.h"
#include <QLineF>
#include <cmath>
#include <algorithm>

namespace ga {
namespace {
double northing(double latitude){const double radians=std::clamp(latitude,-85.05112878,85.05112878)*M_PI/180;return std::log(std::tan(M_PI/4+radians/2));}
double factor(const Site &site){return 111320*std::cos(site.lat*M_PI/180)*180/M_PI;}
QPointF clipPoint(const QMatrix4x4 &m,QPointF p,QSize size){auto v=m*QVector4D(p.x(),p.y(),0,1);return{v.x()/v.w()*size.width()/2.,v.y()/v.w()*size.height()/2.};}
double circumference(const Site &site){return 360*111320*std::cos(site.lat*M_PI/180);}
double wrapLongitude(double longitude){return std::remainder(longitude,360.);}
double rawNativeZoom(const Site &site,const Camera &camera,QSize size){
 const auto m=sceneCameraMatrix(site,camera,double(size.width())/size.height());const double yaw=camera.mode=="2d"?0:camera.yaw*M_PI/180;
 const auto side=QPointF(std::cos(yaw),std::sin(yaw));const double ppm=std::max(1e-14,QLineF(clipPoint(m,{},size),clipPoint(m,side,size)).length());
 return std::log2(circumference(site)*ppm/512);
}
}
QPointF mapPlanePoint(QPointF local,const Site &site){return{local.x(),(northing(site.lat+local.y()/111320)-northing(site.lat))*factor(site)};}
QPointF sitePlanePoint(QPointF p,const Site &site){const auto lat=(2*std::atan(std::exp(northing(site.lat)+p.y()/factor(site)))-M_PI/2)*180/M_PI;return{p.x(),(lat-site.lat)*111320};}
QPointF scenePlaneFromMapPoint(QPointF point,const Site &site,const Camera &camera){const auto target=mapPlanePoint({camera.panX,camera.panY},site);return{std::remainder(point.x()-target.x(),circumference(site)),point.y()-target.y()};}
QPointF scenePlanePoint(QPointF local,const Site &site,const Camera &camera){return scenePlaneFromMapPoint(mapPlanePoint(local,site),site,camera);}
QPointF localFromScenePlane(QPointF point,const Site &site,const Camera &camera){return sitePlanePoint(point+mapPlanePoint({camera.panX,camera.panY},site),site);}
void normalizeCameraCenter(Camera &camera,const Site &site){auto ll=geographicPoint({camera.panX,camera.panY},site);ll={wrapLongitude(ll.x()),std::clamp(ll.y(),-85.,85.)};const auto local=localPoint(ll.x(),ll.y(),site);camera.panX=local.x();camera.panY=local.y();}
QMatrix4x4 sceneCameraMatrix(const Site &site,const Camera &camera,double aspect){auto c=camera;c.panX=c.panY=0;return c.matrix(envelopeRadius(site),aspect);}
QPointF groundOnMapPlane(const QMatrix4x4 &matrix,QSize viewport,QPointF screen){
 // Inverting to float and then intersecting a near/far ray loses the ground
 // anchor at architectural scene distances. Invert the rendered coefficients
 // in double precision; GPU and picking still use the same camera.
 double rows[4][8]={};for(int r=0;r<4;++r)for(int c=0;c<8;++c)rows[r][c]=c<4?matrix(r,c):double(c-4==r);
 for(int c=0;c<4;++c){int pivot=c;for(int r=c+1;r<4;++r)if(std::abs(rows[r][c])>std::abs(rows[pivot][c]))pivot=r;if(std::abs(rows[pivot][c])<1e-15)return{};if(pivot!=c)for(int k=0;k<8;++k)std::swap(rows[c][k],rows[pivot][k]);const double divisor=rows[c][c];for(int k=0;k<8;++k)rows[c][k]/=divisor;for(int r=0;r<4;++r)if(r!=c){const double scale=rows[r][c];for(int k=0;k<8;++k)rows[r][k]-=scale*rows[c][k];}}
 const double x=screen.x()/std::max(1,viewport.width())*2-1,y=1-screen.y()/std::max(1,viewport.height())*2;double a[4]={},b[4]={};for(int r=0;r<4;++r){a[r]=rows[r][4]*x+rows[r][5]*y-rows[r][6]+rows[r][7];b[r]=rows[r][4]*x+rows[r][5]*y+rows[r][6]+rows[r][7];}if(std::abs(a[3])<1e-15||std::abs(b[3])<1e-15)return{};for(int i=0;i<3;++i){a[i]/=a[3];b[i]/=b[3];}const double dz=b[2]-a[2];if(std::abs(dz)<1e-12)return{};const double t=-a[2]/dz;return{a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t};
}
ContextCamera contextCamera(const Site &site,const Camera &camera,QSize viewport){
 ContextCamera result;const auto size=viewport.expandedTo({1,1});const auto m=sceneCameraMatrix(site,camera,double(size.width())/size.height());const auto coordinate=geographicPoint({camera.panX,camera.panY},site);
 result.zoom=std::clamp(rawNativeZoom(site,camera,size),1.,19.);const double world=512*std::pow(2,result.zoom),scale=circumference(site)/world,x=(coordinate.x()+180)/360*world,y=(.5-northing(coordinate.y())/(2*M_PI))*world;
 // Double precision is required here: world-pixel coordinates can be very large.
 // Multiplication happens in MapLibre in double precision before per-tile float uniforms.
 const double basis[16]={scale,0,0,0,0,-scale,0,0,0,0,1,0,-x*scale,y*scale,0,1};result.projection.fill(0,16);
 const auto centerClip=m*QVector4D(0,0,0,1);const double homogeneousScale=1.5*size.height()/centerClip.w();
 for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)result.projection[c*4+r]+=double(m(r,k))*basis[c*4+k]*homogeneousScale;
 result.lat=coordinate.y();result.lon=coordinate.x();result.bearing=camera.mode=="2d"?0:-camera.yaw;result.pitch=camera.mode=="2d"?0:90-camera.elevation;return result;
}
Camera cameraAtGeographicView(const Site &site,Camera camera,QSize viewport,double latitude,double longitude,double logicalZoom){
 const auto point=localPoint(std::remainder(longitude,360.),std::clamp(latitude,-85.,85.),site);camera.panX=point.x();camera.panY=point.y();
 // Shared 256-pixel logical zoom follows the web Map <-> 3D adapter. Solve in
 // log space because an exploded stack's elevated target changes ground scale.
 const double wanted=std::clamp(logicalZoom,2.,20.)-1;const auto size=viewport.expandedTo({1,1});double low=std::log(1e-7),high=std::log(1e5);
 for(int i=0;i<45;++i){const double middle=(low+high)/2;camera.zoom=std::exp(middle);if(rawNativeZoom(site,camera,size)<wanted)low=middle;else high=middle;}
 camera.zoom=std::exp((low+high)/2);return camera;
}
double groundMetresPerPixel(const Site &site,const Camera &camera,QSize viewport,QPointF screen){
 const auto m=sceneCameraMatrix(site,camera,double(std::max(1,viewport.width()))/std::max(1,viewport.height()));
 const auto a=geographicPoint(localFromScenePlane(groundOnMapPlane(m,viewport,screen-QPointF(.5,0)),site,camera),site),b=geographicPoint(localFromScenePlane(groundOnMapPlane(m,viewport,screen+QPointF(.5,0)),site,camera),site);
 const double latitude=(a.y()+b.y())*.5*M_PI/180,dx=std::remainder(b.x()-a.x(),360.)*111320*std::cos(latitude),dy=(b.y()-a.y())*111320;
 return std::max(1e-8,std::hypot(dx,dy));
}
}
