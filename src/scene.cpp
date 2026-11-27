#include "scene.h"
#include "section.h"
#include "massing.h"
#include "model3d.h"
#include "planning.h"
#include "geoimport.h"
#include "earcut.hpp"
#include <QJsonObject>
#include <QHash>
#include <QLineF>
#include <QSet>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ga {
namespace {
using Point=std::array<double,2>;
struct Triangulation {std::vector<std::vector<Point>> polygon;QVector<QPointF> points;std::vector<uint32_t> indices;};
Triangulation triangulate(const Feature &f) {
 Triangulation t;for(const auto &r:f.rings){std::vector<Point> ring;for(int i=0;i<r.size();++i){if(i==r.size()-1&&r[i]==r[0])continue;ring.push_back({r[i].x(),r[i].y()});t.points.append(r[i]);}if(ring.size()>=3)t.polygon.push_back(ring);}
 if(!t.polygon.empty())t.indices=mapbox::earcut<uint32_t>(t.polygon);
 return t;
}
Vertex vertex(const QPointF &p,double z,const QVector3D &normal,const QColor &color,float mat=0.0f) {return {float(p.x()),float(p.y()),float(z),normal.x(),normal.y(),normal.z(),float(color.redF()),float(color.greenF()),float(color.blueF()),float(color.alphaF()),mat};}
void tri(QVector<Vertex> &out,const QPointF &a,double az,const QPointF &b,double bz,const QPointF &c,double cz,const QColor &color,QVector3D normal={0,0,1},float mat=0.0f) {out.append(vertex(a,az,normal,color,mat));out.append(vertex(b,bz,normal,color,mat));out.append(vertex(c,cz,normal,color,mat));}
void line(QVector<Vertex> &out,const QPointF &a,double az,const QPointF &b,double bz,const QColor &color) {out.append(vertex(a,az,{0,0,0},color,0.0f));out.append(vertex(b,bz,{0,0,0},color,0.0f));}
void polygon(QVector<Vertex> &out,const Feature &f,double z,const QColor &color,float mat=0.0f) {const auto t=triangulate(f);for(size_t i=0;i+2<t.indices.size();i+=3)tri(out,t.points[t.indices[i]],z,t.points[t.indices[i+1]],z,t.points[t.indices[i+2]],z,color,{0,0,1},mat);}
void walls(QVector<Vertex> &out,const Feature &f,double base,double top,const QColor &color,float mat=0.0f) {
 for(int ringIndex=0;ringIndex<f.rings.size();++ringIndex){const auto &r=f.rings[ringIndex];const bool ccw=ringArea(r)>0;for(int i=0;i<r.size();++i){const auto a=r[i],b=r[(i+1)%r.size()];if(QLineF(a,b).length()<.0001)continue;QVector3D n(float(b.y()-a.y()),float(a.x()-b.x()),0);if(ccw!=(ringIndex==0))n=-n;n.normalize();tri(out,a,base,b,base,b,top,color,n,mat);tri(out,a,base,b,top,a,top,color,n,mat);}}
}
void ribbon(QVector<Vertex> &out,const QPolygonF &r,double width,double z,const QColor &color,bool miter=true){
 if(r.size()<2||width<=0)return;
 if(r.size()==2){const auto d=r[1]-r[0];const double length=std::hypot(d.x(),d.y());if(length<1e-10)return;const QPointF n(-d.y()*width/(2*length),d.x()*width/(2*length));tri(out,r[0]+n,z,r[1]+n,z,r[1]-n,z,color);tri(out,r[0]+n,z,r[1]-n,z,r[0]-n,z,color);return;}
 QPainterPath center;center.moveTo(r[0]);for(int i=1;i<r.size();++i)center.lineTo(r[i]);if(r.first()==r.last())center.closeSubpath();
 QPainterPathStroker stroker;stroker.setWidth(width);stroker.setCapStyle(Qt::FlatCap);stroker.setJoinStyle(miter?Qt::MiterJoin:Qt::RoundJoin);stroker.setMiterLimit(4);const auto rings=stroker.createStroke(center).simplified().toSubpathPolygons();
 QVector<int> depth(rings.size());for(int i=0;i<rings.size();++i)for(int j=0;j<rings.size();++j)if(i!=j&&std::abs(ringArea(rings[j]))>std::abs(ringArea(rings[i]))&&rings[j].containsPoint(rings[i][0],Qt::OddEvenFill))++depth[i];
 for(int i=0;i<rings.size();++i)if(depth[i]%2==0){Feature stroke;stroke.rings={rings[i]};for(int j=0;j<rings.size();++j)if(depth[j]==depth[i]+1&&rings[i].containsPoint(rings[j][0],Qt::OddEvenFill))stroke.rings.append(rings[j]);polygon(out,stroke,z,color);}
}
void pointSymbol(QVector<Vertex> &out,const QPointF &p,double radius,double z,const QColor &color){for(int i=0;i<12;++i){const double a=i*M_PI/6,b=(i+1)*M_PI/6;tri(out,p,z,p+QPointF(std::cos(a),std::sin(a))*radius,z,p+QPointF(std::cos(b),std::sin(b))*radius,z,color);}}
void addTree3D(QVector<Vertex> &out,const QPointF &p,double radius,double baseZ,const QColor &color,uint seed){
 static constexpr double kCos6[6]={1.0,0.5,-0.5,-1.0,-0.5,0.5};
 static constexpr double kSin6[6]={0.0,0.866025404,0.866025404,0.0,-0.866025404,-0.866025404};
 const double jitter=double(seed%1000)/1000.0;
 const double cr=std::clamp(radius*1.08,1.8,7.2)*(0.86+0.28*jitter);
 const double treeH=std::clamp(cr*2.35+1.8,5.2,13.5);
 const double trunkH=treeH*0.34;
 const double rBot=std::max(0.28,cr*0.14),rTop=std::max(0.18,cr*0.09);
 QColor bark(112,92,74,color.alpha());
 QColor canopyLo=color.darker(114);
 QColor canopyMid=color;
 QColor canopyHi=color.lighter(112);

 // 6-sided tapered wood trunk
 for(int i=0;i<6;++i){
  const int j=(i+1)%6;
  const QPointF b0(p.x()+kCos6[i]*rBot,p.y()+kSin6[i]*rBot);
  const QPointF b1(p.x()+kCos6[j]*rBot,p.y()+kSin6[j]*rBot);
  const QPointF t0(p.x()+kCos6[i]*rTop,p.y()+kSin6[i]*rTop);
  const QPointF t1(p.x()+kCos6[j]*rTop,p.y()+kSin6[j]*rTop);
  const double aMid=(i+0.5)*M_PI/3.0;
  const QVector3D n(float(std::cos(aMid)),float(std::sin(aMid)),0.08f);
  tri(out,b0,baseZ,b1,baseZ,t1,baseZ+trunkH,bark,n.normalized(),0.0f);
  tri(out,b0,baseZ,t1,baseZ+trunkH,t0,baseZ+trunkH,bark,n.normalized(),0.0f);
 }

 // Sculpted 3-belt low-poly architectural maquette canopy (casts real shadows!)
 const double z0=baseZ+trunkH*0.86;
 const double z1=baseZ+trunkH+(treeH-trunkH)*0.34;
 const double z2=baseZ+trunkH+(treeH-trunkH)*0.76;
 const double z3=baseZ+treeH;
 const double r1=cr*0.98,r2=cr*0.72;
 const double rot=(seed%36)*0.1745329;
 auto ringPt=[&](int idx,double rad,double angOff){
  const double a=idx*M_PI/3.0+rot+angOff;
  return QPointF(p.x()+std::cos(a)*rad,p.y()+std::sin(a)*rad);
 };
 auto faceNormal=[](const QPointF &a,double az,const QPointF &b,double bz,const QPointF &c,double cz){
  QVector3D n=QVector3D::crossProduct(QVector3D(float(b.x()-a.x()),float(b.y()-a.y()),float(bz-az)),
                                      QVector3D(float(c.x()-a.x()),float(c.y()-a.y()),float(cz-az)));
  return n.lengthSquared()>1e-6f?n.normalized():QVector3D(0,0,1);
 };
 for(int i=0;i<6;++i){
  const int j=(i+1)%6;
  const QPointF l0=ringPt(i,r1,0.0),l1=ringPt(j,r1,0.0);
  const QPointF u0=ringPt(i,r2,0.26),u1=ringPt(j,r2,0.26);
  // Underside skirt
  const QVector3D nBot=faceNormal(p,z0,l1,z1,l0,z1);
  tri(out,p,z0,l1,z1,l0,z1,canopyLo,nBot,2.0f);
  // Mid faceted canopy band
  const QVector3D nM1=faceNormal(l0,z1,l1,z1,u1,z2);
  const QVector3D nM2=faceNormal(l0,z1,u1,z2,u0,z2);
  tri(out,l0,z1,l1,z1,u1,z2,canopyMid,nM1,2.0f);
  tri(out,l0,z1,u1,z2,u0,z2,canopyMid,nM2,2.0f);
  // Upper crown pyramid
  const QVector3D nTop=faceNormal(u0,z2,u1,z2,p,z3);
  tri(out,u0,z2,u1,z2,p,z3,canopyHi,nTop,2.0f);
 }
}
}
QJsonObject Camera::toJson() const{QJsonObject j{{"mode",mode},{"projection",projection},{"yaw",yaw},{"elevation",elevation},{"zoom",zoom},{"pan_x",panX},{"pan_y",panY},{"exploded",exploded},{"grid",grid},{"separation",separation}};if(solar.enabled)j["solar"]=solar.toJson();return j;}
Camera Camera::fromJson(const QJsonObject &v){Camera c;const auto safe=[&](const char *key,double fallback,double lo,double hi){const auto x=v.value(QLatin1String(key));return x.isDouble()&&std::isfinite(x.toDouble())?std::clamp(x.toDouble(),lo,hi):fallback;};c.mode=v["mode"].toString()=="2d"?"2d":"3d";const QString p=v["projection"].toString();if(QSet<QString>{"axonometric","isometric","perspective","one-point","two-point"}.contains(p))c.projection=p;c.yaw=safe("yaw",-45,-36000,36000);c.elevation=safe("elevation",35.264,8,89.9);c.zoom=safe("zoom",1,1e-7,1e5);c.panX=safe("pan_x",0,-41000000,41000000);c.panY=safe("pan_y",0,-20000000,20000000);c.separation=safe("separation",60,0,300);c.exploded=v["exploded"].toBool();c.grid=v["grid"].toBool();if(v["solar"].isObject())c.solar=SolarState::fromJson(v["solar"].toObject());if(c.projection=="isometric"&&(std::abs(std::fmod(std::abs(c.yaw),90.)-45.)>.01||std::abs(c.elevation-35.26438968)>.01))c.projection="axonometric";return c;}
void Camera::preset(const QString &id){projection=id;mode="3d";if(id=="isometric"){yaw=-45;elevation=35.26438968;}else if(id=="axonometric"){yaw=-30;elevation=30;}else if(id=="one-point"){yaw=0;elevation=35;}else if(id=="two-point"){yaw=-45;elevation=35;}else{yaw=18;elevation=35;}}

QMatrix4x4 Camera::matrix(double radius,double aspect) const {
 const bool top=mode=="2d";const double height=exploded&&!top?(stackHeight>0?stackHeight:separation*3.6):0,r=std::hypot(std::max(50.,radius),height/2),distance=r*3.39/zoom,nearClip=top||projection=="axonometric"||projection=="isometric"?.1:std::max(.1,distance*.1);const double el=(top?89.999:elevation)*M_PI/180,y=(top?0:yaw)*M_PI/180;const QVector3D target(float(panX),float(panY),float(height/2));
 const QVector3D eye=target+QVector3D(float(std::sin(y)*std::cos(el)*distance),float(-std::cos(y)*std::cos(el)*distance),float(std::sin(el)*distance));
 QMatrix4x4 view,projectionMatrix;view.lookAt(eye,target,top?QVector3D(0,1,0):QVector3D(0,0,1));
 if(top||projection=="axonometric"||projection=="isometric"){const float half=float(r*1.13/zoom);projectionMatrix.ortho(-half*float(aspect),half*float(aspect),-half,half,0.1f,float(distance+r/zoom*30+2000));}
 else{projectionMatrix.perspective(float(2*std::atan(1./3)*180/M_PI),float(aspect),float(nearClip),float(distance+r/zoom*30+2000));}
 auto result=projectionMatrix*view;
 if(!top&&(projection=="one-point"||projection=="two-point")){
  // Same image rectification as reference-camera-projections.mjs: send the
  // vertical vanishing point to infinity while preserving the ground camera.
  const double shear=-double(result(3,2))/result(1,2),near=nearClip,far=distance+r/zoom*30+2000,a=(far+near)/(far-near),b=-2*far*near/(far-near);
  for(int c=0;c<4;++c){result(3,c)=float(double(result(3,c))+shear*result(1,c));result(2,c)=float(a*result(3,c)+(c==3?b:0));}
 }

 return result;
}
Mesh buildSunDomeMesh(const Project &project,const SolarState &solar){
 Mesh mesh;
 if(!solar.enabled||!solar.showPath)return mesh;
 const auto sun=solarPosition(project.site.lat,solar.dayOfYear,solar.hour);
 const auto addQuad3=[&](QVector<Vertex> &out,const QVector3D &a,const QVector3D &b,const QVector3D &c,const QVector3D &d,const QColor &col){
  const QVector3D zero(0,0,0);
  out.append({a.x(),a.y(),a.z(),zero.x(),zero.y(),zero.z(),float(col.redF()),float(col.greenF()),float(col.blueF()),float(col.alphaF())});
  out.append({b.x(),b.y(),b.z(),zero.x(),zero.y(),zero.z(),float(col.redF()),float(col.greenF()),float(col.blueF()),float(col.alphaF())});
  out.append({c.x(),c.y(),c.z(),zero.x(),zero.y(),zero.z(),float(col.redF()),float(col.greenF()),float(col.blueF()),float(col.alphaF())});
  out.append({a.x(),a.y(),a.z(),zero.x(),zero.y(),zero.z(),float(col.redF()),float(col.greenF()),float(col.blueF()),float(col.alphaF())});
  out.append({c.x(),c.y(),c.z(),zero.x(),zero.y(),zero.z(),float(col.redF()),float(col.greenF()),float(col.blueF()),float(col.alphaF())});
  out.append({d.x(),d.y(),d.z(),zero.x(),zero.y(),zero.z(),float(col.redF()),float(col.greenF()),float(col.blueF()),float(col.alphaF())});
 };
 const auto addSphere3=[&](QVector<Vertex> &out,const QVector3D &center,float radius,const QColor &col,int rings,int slices,bool emissive){
  for(int r=0;r<rings;++r){
   const float phi0=float(M_PI)*float(r)/rings-float(M_PI)*0.5f;
   const float phi1=float(M_PI)*float(r+1)/rings-float(M_PI)*0.5f;
   for(int s=0;s<slices;++s){
    const float th0=2.0f*float(M_PI)*float(s)/slices;
    const float th1=2.0f*float(M_PI)*float(s+1)/slices;
    auto pt=[&](float ph,float th){return QVector3D(std::cos(ph)*std::cos(th),std::cos(ph)*std::sin(th),std::sin(ph));};
    const QVector3D n00=pt(phi0,th0),n10=pt(phi0,th1),n11=pt(phi1,th1),n01=pt(phi1,th0);
    const QVector3D p00=center+n00*radius,p10=center+n10*radius,p11=center+n11*radius,p01=center+n01*radius;
    auto push=[&](const QVector3D &p,const QVector3D &n){
     const QVector3D nv=emissive?QVector3D(0,0,0):n;
     out.append({p.x(),p.y(),p.z(),nv.x(),nv.y(),nv.z(),float(col.redF()),float(col.greenF()),float(col.blueF()),float(col.alphaF())});
    };
    push(p00,n00);push(p10,n10);push(p11,n11);
    push(p00,n00);push(p11,n11);push(p01,n01);
   }
  }
 };
 const auto addArcRibbon=[&](const QVector<QVector3D> &pts,float halfW,const QColor &col){
  for(int i=0;i+1<pts.size();++i){
   const QVector3D &a=pts[i],&b=pts[i+1];
   QVector3D tang=(b-a);
   if(tang.lengthSquared()<1e-5f)continue;
   tang.normalize();
   QVector3D radial=a.normalized();
   QVector3D side=QVector3D::crossProduct(tang,radial);
   if(side.lengthSquared()<1e-5f)side=QVector3D(0,0,1);
   side.normalize();
   addQuad3(mesh.triangles,a-side*halfW,b-side*halfW,b+side*halfW,a+side*halfW,col);
   addQuad3(mesh.triangles,a-radial*halfW,b-radial*halfW,b+radial*halfW,a+radial*halfW,col);
   line(mesh.lines,{a.x(),a.y()},a.z(),{b.x(),b.y()},b.z(),col);
  }
 };

 const double domeR=std::max(120.,project.site.radius*1.08);
 const float rw=float(std::max(1.8,domeR*0.0042));
 for(int i=0;i<96;++i){
  const double a0=i*2*M_PI/96.,a1=(i+1)*2*M_PI/96.;
  const QVector3D p0(float(std::cos(a0)*domeR),float(std::sin(a0)*domeR),0.55f);
  const QVector3D p1(float(std::cos(a1)*domeR),float(std::sin(a1)*domeR),0.55f);
  const QVector3D q0(float(std::cos(a0)*(domeR+rw*2.2)),float(std::sin(a0)*(domeR+rw*2.2)),0.55f);
  const QVector3D q1(float(std::cos(a1)*(domeR+rw*2.2)),float(std::sin(a1)*(domeR+rw*2.2)),0.55f);
  addQuad3(mesh.triangles,p0,p1,q1,q0,QColor(196,164,120,175));
  line(mesh.lines,{p0.x(),p0.y()},p0.z(),{p1.x(),p1.y()},p1.z(),QColor(186,154,112,210));
 }
 for(int deg=0;deg<360;deg+=15){
  const double rad=deg*M_PI/180.;
  const bool major=(deg%90==0);
  const double rIn=domeR-(major?domeR*0.055:domeR*0.024);
  const double rOut=domeR+(major?domeR*0.03:0.0);
  line(mesh.lines,{std::sin(rad)*rIn,std::cos(rad)*rIn},0.6,{std::sin(rad)*rOut,std::cos(rad)*rOut},0.6,major?QColor(220,118,62,220):QColor(176,156,128,140));
 }
 addArcRibbon(solarArcPoints(project.site.lat,172,domeR,56),rw*0.95f,QColor(232,148,58,195));
 addArcRibbon(solarArcPoints(project.site.lat,80,domeR,56),rw*0.80f,QColor(206,184,118,170));
 addArcRibbon(solarArcPoints(project.site.lat,355,domeR,56),rw*0.95f,QColor(92,156,214,195));
 addArcRibbon(solarArcPoints(project.site.lat,solar.dayOfYear,domeR,64),rw*1.55f,QColor(246,104,44,240));
 for(int h=6;h<=18;++h){
  const auto hp=solarPosition(project.site.lat,solar.dayOfYear,double(h));
  if(!hp.aboveHorizon)continue;
  const QVector3D pos=hp.direction*float(domeR);
  addSphere3(mesh.triangles,pos,float(std::max(3.5,domeR*0.0095)),(h%3==0)?QColor(255,196,88,235):QColor(238,142,74,195),6,8,true);
 }
 if(sun.aboveHorizon){
  const QVector3D sp=sun.direction*float(domeR);
  QVector3D upRef=std::abs(sun.direction.z())<0.95f?QVector3D(0,0,1):QVector3D(1,0,0);
  QVector3D u=QVector3D::crossProduct(sun.direction,upRef).normalized();
  QVector3D v=QVector3D::crossProduct(sun.direction,u).normalized();
  const float beamR=float(std::max(6.0,domeR*0.016));
  addQuad3(mesh.triangles,-u*beamR*1.6f,u*beamR*1.6f,sp+u*beamR*0.5f,sp-u*beamR*0.5f,QColor(255,186,82,58));
  addQuad3(mesh.triangles,-v*beamR*1.6f,v*beamR*1.6f,sp+v*beamR*0.5f,sp-v*beamR*0.5f,QColor(255,186,82,58));
  line(mesh.lines,{0,0},0.6,{sp.x(),sp.y()},sp.z(),QColor(255,168,58,225));
  line(mesh.lines,{sp.x(),sp.y()},0.6,{sp.x(),sp.y()},sp.z(),QColor(240,148,68,145));
  line(mesh.lines,{0,0},0.6,{sp.x(),sp.y()},0.6,QColor(240,148,68,155));
  const float sr=float(std::max(14.,domeR*0.032));
  addSphere3(mesh.triangles,sp,sr,QColor(255,232,148,255),10,14,true);
  addSphere3(mesh.triangles,sp,sr*1.55f,QColor(255,176,58,92),8,12,true);
  addSphere3(mesh.triangles,sp,sr*2.25f,QColor(255,142,42,42),8,12,true);
 }
 QVector<Vertex> opaque;opaque.reserve(mesh.triangles.size());
 for(int i=0;i+2<mesh.triangles.size();i+=3){
  auto &out=mesh.triangles[i].a<.999f?mesh.transparent:opaque;
  out.append(mesh.triangles[i]);out.append(mesh.triangles[i+1]);out.append(mesh.triangles[i+2]);
 }
 mesh.triangles=std::move(opaque);
 return mesh;
}

Mesh buildMesh(const Project &project,const QVector<Layer> &layers,const Palette &p,const Camera &camera,const QString &diagram,int selected,bool includeSunDome,const TerrainGrid *dem,const PlotDesign *plot,const MassingConfig *massing,const QVector<ImportedModel3D> *models3d,const FuturePlanningOverlay *planning){
 Mesh mesh;QVector<QVector<Paint>> classified;classified.reserve(project.features.size());QSet<int> activeOrders;FeaturePaintCache classifier(layers,diagram);for(const auto &feature:project.features){auto paints=classifier.paints(feature);for(const auto &paint:paints)activeOrders.insert(paint.order);classified.append(std::move(paints));}
 QHash<int,double> elevations;int visibleIndex=0;for(int i=0;i<layers.size();++i)if(activeOrders.contains(i))elevations[i]=camera.exploded?visibleIndex++*camera.separation:0;
 const bool useDem=(dem&&dem->valid&&!camera.exploded&&camera.mode!="2d");
 double demBase=0.0;
 if(useDem){
  demBase=1e9;
  for(const auto &v:dem->values)if(std::isfinite(v))demBase=std::min(demBase,double(v));
  if(!std::isfinite(demBase)||demBase>1e8)demBase=0.0;
 }
 auto terrainZ=[&](const QPointF &pt)->double{
  if(!useDem)return 0.0;
  const double v=dem->sample(pt.x(),pt.y());
  return std::isfinite(v)?std::max(0.0,(v-demBase)*1.35):0.0;
 };
 if(useDem){
  // Build sculpted 3D terrain mesh clipped to site boundary
  const QRectF b=project.site.boundary.boundingRect();
  const int N=42;
  const double dx=b.width()/N,dy=b.height()/N;
  for(int gy=0;gy<N;++gy){
   for(int gx=0;gx<N;++gx){
    const QPointF p00(b.left()+gx*dx,b.top()+gy*dy);
    const QPointF p10(b.left()+(gx+1)*dx,b.top()+gy*dy);
    const QPointF p11(b.left()+(gx+1)*dx,b.top()+(gy+1)*dy);
    const QPointF p01(b.left()+gx*dx,b.top()+(gy+1)*dy);
    const QPointF mid=(p00+p11)*0.5;
    if(!project.site.boundary.containsPoint(mid,Qt::OddEvenFill))continue;
    const double z00=terrainZ(p00)-0.15,z10=terrainZ(p10)-0.15,z11=terrainZ(p11)-0.15,z01=terrainZ(p01)-0.15;
    auto normalOf=[](const QPointF &a,double az,const QPointF &b,double bz,const QPointF &c,double cz){
     QVector3D n=QVector3D::crossProduct(QVector3D(float(b.x()-a.x()),float(b.y()-a.y()),float(bz-az)),QVector3D(float(c.x()-a.x()),float(c.y()-a.y()),float(cz-az)));
     if(n.z()<0)n=-n;
     return n.lengthSquared()>1e-6f?n.normalized():QVector3D(0,0,1);
    };
    tri(mesh.triangles,p00,z00,p10,z10,p11,z11,p.paper,normalOf(p00,z00,p10,z10,p11,z11));
    tri(mesh.triangles,p00,z00,p11,z11,p01,z01,p.paper,normalOf(p00,z00,p11,z11,p01,z01));
   }
  }
 }else{
  Feature base;base.rings={project.site.boundary};polygon(mesh.triangles,base,-.1,p.paper);
 }
 for(int index=0;index<project.features.size();++index){const auto &f=project.features[index];const auto &paints=classified[index];if(paints.isEmpty())continue;
  const QPointF fCen=f.bounds().center();
  const double tz=terrainZ(fCen);
  const auto render=[&](const QVector<Paint> &stack){const auto &top=stack.last();const auto &layer=top.layer;auto color=compositePaints(stack);if(index==selected)color=QColor("#d59a59");const auto kind=top.kind;
   const double baseZ=tz+elevations.value(top.order)+.012*top.order,h=matchesLayer(f,"buildings")&&kind==Geometry::Polygon&&camera.mode!="2d"&&f.knownHeight()?f.height:0;
   const float polyMat=matchesLayer(f,"water")?1.0f:0.0f;
   if(kind==Geometry::Polygon){polygon(mesh.triangles,f,baseZ+h,color,polyMat);if(matchesLayer(f,"buildings")){if(h>0)walls(mesh.triangles,f,useDem?std::max(-0.2,baseZ-2.0):baseZ,baseZ+h,color.darker(108));}
    const Layer *outline=nullptr;for(const auto &paint:stack)if(paint.layer.presentation.contains("stroke"))outline=&paint.layer;
    if(outline){const auto &o=outline->presentation;const auto stroke=o["stroke"].toString();const auto width=o["strokeWidth"].toDouble(1)*project.site.radius/166.;if(stroke!="none"&&!stroke.isEmpty()&&width>0){auto ink=QColor(stroke);ink.setAlphaF(outline->opacity);for(auto ring:f.rings){if(ring.first()!=ring.last())ring.append(ring.first());ribbon(mesh.triangles,ring,width,baseZ+h+.025,ink);}}}
   }else if(kind==Geometry::Line){for(const auto &paint:stack){auto ink=paint.color;ink.setAlphaF(paint.opacity);ribbon(mesh.triangles,f.rings[0],lineWidthMetres(f,paint.layer,project.site.radius),baseZ+.012*paint.order+.12,ink,paint.layer.presentation.contains("lineWidth"));}}
   else if(camera.mode!="2d"&&!camera.exploded&&camera.solar.trees3d&&(matchesLayer(f,"trees")||f.tags["natural"].toString()=="tree")){
    const QPointF pt=f.rings[0][0];
    const uint seed=uint(std::abs(int(qRound(pt.x()*17.0)))*73856093u^uint(std::abs(int(qRound(pt.y()*19.0)))*19349663u));
    addTree3D(mesh.triangles,pt,pointRadiusMetres(f,layer,project.site.radius),baseZ,color,seed);
   }else pointSymbol(mesh.triangles,f.rings[0][0],pointRadiusMetres(f,layer,project.site.radius),baseZ+.2,color);
  };
  for(const auto &paint:paints)if(paint.kind==Geometry::Polygon&&paint.layer.presentation.contains("shadow")){const auto sh=paint.layer.presentation["shadow"].toObject();Feature offset=f;for(auto &ring:offset.rings)for(auto &point:ring)point+=QPointF(sh["dx"].toDouble(),-sh["dy"].toDouble());auto color=QColor(sh["color"].toString());color.setAlphaF(paint.opacity*sh["opacity"].toDouble());for(const auto &part:clipToSite(offset,project.site))polygon(mesh.triangles,part,tz+elevations.value(paint.order)+.012*paint.order+.001,color);}
  if(camera.exploded){for(const auto &paint:paints)render({paint});}else render(paints);
  if(matchesLayer(f,"buildings")&&f.geometry==Geometry::Polygon){++mesh.roofs;if(!f.knownHeight())++mesh.unknownFlat;}
 }
 if(plot&&massing&&plot->active&&massing->enabled&&camera.mode!="2d"&&!camera.exploded){
  const QPointF pCen=plot->polygon().boundingRect().center();
  appendMassingToMesh(mesh,*plot,*massing,terrainZ(pCen));
 }
 if(models3d&&!models3d->isEmpty()&&camera.mode!="2d"&&!camera.exploded){
  appendImportedModelsToMesh(mesh,*models3d,terrainZ);
 }
 if(planning&&planning->valid()&&camera.mode!="2d"&&!camera.exploded){
  appendPlanning3DToMesh(mesh,*planning,terrainZ);
 }
 const auto &boundary=project.site.boundary;for(int i=0;i<boundary.size();++i)line(mesh.lines,boundary[i],.35,boundary[(i+1)%boundary.size()],.35,p.ink.lighter(130));
 if(camera.grid){const double r=project.site.radius;for(double i=-std::ceil(r/100)*100;i<=r;i+=100){line(mesh.lines,{-r,i},-.05,{r,i},-.05,p.ink.lighter(260));line(mesh.lines,{i,-r},-.05,{i,r},-.05,p.ink.lighter(260));}}
 if(camera.exploded)for(int order=0;order<layers.size();++order)if(elevations.contains(order)){const double z=elevations[order];for(int i=0;i<boundary.size();++i)line(mesh.lines,boundary[i],z,boundary[(i+1)%boundary.size()],z,p.ink.lighter(160));}
 QVector<Vertex> opaque;opaque.reserve(mesh.triangles.size());for(int i=0;i+2<mesh.triangles.size();i+=3){auto &out=mesh.triangles[i].a<.999f?mesh.transparent:opaque;out.append(mesh.triangles[i]);out.append(mesh.triangles[i+1]);out.append(mesh.triangles[i+2]);}mesh.triangles=std::move(opaque);
 if(includeSunDome&&camera.solar.enabled&&camera.mode!="2d"&&!camera.exploded){
  const auto dome=buildSunDomeMesh(project,camera.solar);
  mesh.triangles.append(dome.triangles);
  mesh.lines.append(dome.lines);
  mesh.transparent.append(dome.transparent);
 }
 return mesh;
}
double meshTopArea(const Feature &f){const auto t=triangulate(f);double area=0;for(size_t i=0;i+2<t.indices.size();i+=3){const auto a=t.points[t.indices[i]],b=t.points[t.indices[i+1]],c=t.points[t.indices[i+2]];area+=std::abs((b.x()-a.x())*(c.y()-a.y())-(b.y()-a.y())*(c.x()-a.x()))/2;}return area;}
}
