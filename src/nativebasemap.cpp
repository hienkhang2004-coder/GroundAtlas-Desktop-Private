#include "nativebasemap.h"
#include "planningtiles.h"
#include "project.h"
#include "vectorstyle.h"
#include <QMapLibre/Map>
#include <QMapLibre/Settings>
#include <QCoreApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>

NativeBasemap::NativeBasemap(QString directory,QObject *parent):QObject(parent),cacheDirectory_(std::move(directory)){
 QFile f(ga::appDataPath("basemap/positron.json"));if(f.open(QIODevice::ReadOnly))originalStyle_=QJsonDocument::fromJson(f.readAll()).object();
 loading_.setSingleShot(true);loading_.setInterval(25000);connect(&loading_,&QTimer::timeout,this,[this]{if(!ready_&&error_.isEmpty()){error_="resources-incomplete";emit needsRendering();}});
}
NativeBasemap::~NativeBasemap(){release();}
void NativeBasemap::initialize(QSize size,double ratio){
 if(map_)return;
 vao_.create();QMapLibre::Settings settings;settings.setContextMode(QMapLibre::Settings::SharedGLContext);settings.setMapMode(QMapLibre::Settings::Continuous);settings.setProviderTemplate(QMapLibre::Settings::NoProvider);QDir().mkpath(cacheDirectory_);settings.setCacheDatabasePath(cacheDirectory_+"/vector-cache.db");settings.setCacheDatabaseMaximumSize(256*1024*1024);settings.setClientName("GroundAtlasDesktop");settings.setClientVersion(QCoreApplication::applicationVersion());
#ifdef Q_OS_MACOS
 settings.setLocalFontFamily("SF Pro Text");
#else
 settings.setLocalFontFamily("Segoe UI");
#endif
 auto gl=QOpenGLContext::currentContext()->functions();const auto name=gl->glGetString(GL_RENDERER);renderer_=name?QString::fromLatin1(reinterpret_cast<const char*>(name)):QString();map_=std::make_unique<QMapLibre::Map>(nullptr,settings,size,ratio);
 connect(map_.get(),&QMapLibre::Map::needsRendering,this,&NativeBasemap::needsRendering);
 connect(map_.get(),&QMapLibre::Map::mapLoadingFailed,this,[this](auto,const QString &reason){error_=reason;ready_=false;emit needsRendering();});
 connect(map_.get(),&QMapLibre::Map::mapChanged,this,[this](auto event){if(event==QMapLibre::Map::MapChangeDidFinishLoadingStyle){styleReady_=true;error_.clear();if(basemapMode_=="vector")applyStyle();syncHanoiPlanningLayer();}if(event==QMapLibre::Map::MapChangeDidFinishRenderingMapFullyRendered||event==QMapLibre::Map::MapChangeDidFinishRenderingFrameFullyRendered){ready_=map_->isFullyLoaded();if(ready_){error_.clear();loading_.stop();}}});
 applyCamera();loadStyleForMode();
}
void NativeBasemap::loadStyleForMode(){
 if(!map_)return;
 styleReady_=false;ready_=false;appliedHanoiLayer_.clear();error_.clear();
 if(basemapMode_=="satellite"){
  map_->setStyleJson(ga::satelliteBasemapStyleJson());
 }else{
  if(originalStyle_.isEmpty())map_->setStyleUrl("https://tiles.openfreemap.org/styles/positron");
  else map_->setStyleJson(QString::fromUtf8(QJsonDocument(originalStyle_).toJson(QJsonDocument::Compact)));
 }
 loading_.start();
}
void NativeBasemap::setBasemapMode(const QString &mode){
 const QString norm=(mode=="satellite")?QStringLiteral("satellite"):QStringLiteral("vector");
 if(basemapMode_==norm)return;
 basemapMode_=norm;
 loadStyleForMode();
 emit needsRendering();
}
void NativeBasemap::release(){loading_.stop();if(map_){map_->destroyRenderer();map_.reset();vao_.destroy();}ready_=styleReady_=false;appliedHanoiLayer_.clear();}
void NativeBasemap::setCamera(const ga::ContextCamera &camera){camera_=camera;applyCamera();}
void NativeBasemap::applyCamera(){if(!map_)return;ready_=false;error_.clear();loading_.start();if(!map_->setCustomProjection(camera_.projection))error_="invalid-projection";map_->setCoordinateZoom({camera_.lat,camera_.lon},camera_.zoom);map_->setBearing(camera_.bearing);map_->setPitch(camera_.pitch);}
void NativeBasemap::setPresentation(QVector<ga::Layer> layers,ga::Palette palette,bool enabled){layers_=std::move(layers);palette_=std::move(palette);styled_=enabled;if(basemapMode_=="vector")applyStyle();}
void NativeBasemap::setHanoiPlanning(bool enabled,const QString &layerId,double opacity){
 const QString norm=ga::normalizePlanningLayerId(layerId);
 const double op=std::clamp(opacity,0.05,1.0);
 if(hanoiPlanEnabled_==enabled&&hanoiPlanLayer_==norm&&std::abs(hanoiPlanOpacity_-op)<1e-3&&norm!="custom")return;
 hanoiPlanEnabled_=enabled;hanoiPlanLayer_=norm;hanoiPlanOpacity_=op;
 if(norm=="custom")appliedHanoiLayer_.clear();
 syncHanoiPlanningLayer();
 emit needsRendering();
}
void NativeBasemap::syncHanoiPlanningLayer(){
 if(!map_||!styleReady_)return;
 const QString srcId=QStringLiteral("ga-hanoi-qh"),lyrId=QStringLiteral("ga-hanoi-qh-layer");
 if(!hanoiPlanEnabled_){
  if(map_->layerExists(lyrId))map_->removeLayer(lyrId);
  if(map_->sourceExists(srcId))map_->removeSource(srcId);
  appliedHanoiLayer_.clear();
  return;
 }
 if(appliedHanoiLayer_!=hanoiPlanLayer_||!map_->sourceExists(srcId)||!map_->layerExists(lyrId)){
  if(map_->layerExists(lyrId))map_->removeLayer(lyrId);
  if(map_->sourceExists(srcId))map_->removeSource(srcId);
  const auto info=ga::hanoiPlanningLayerInfo(hanoiPlanLayer_);
  QVariantMap src{
   {"type","raster"},
   {"tiles",QStringList{ga::hanoiPlanningTileTemplate(hanoiPlanLayer_)}},
   {"tileSize",256},
   {"minzoom",info.minZoom},
   {"maxzoom",info.maxZoom}
  };
  if(info.hasBounds){
   src.insert(QStringLiteral("bounds"),QVariantList{info.west,info.south,info.east,info.north});
  }
  map_->addSource(srcId,src);
  map_->addLayer(lyrId,QVariantMap{{"type","raster"},{"source",srcId}});
  map_->setPaintProperty(lyrId,QStringLiteral("raster-fade-duration"),0);
  appliedHanoiLayer_=hanoiPlanLayer_;
 }
 map_->setPaintProperty(lyrId,QStringLiteral("raster-opacity"),hanoiPlanOpacity_);
}
void NativeBasemap::applyStyle(){if(!map_||!styleReady_||basemapMode_!="vector")return;ready_=false;loading_.start();if(originalStyle_.isEmpty())originalStyle_=QJsonDocument::fromJson(map_->styleJson().toUtf8()).object();const auto changed=ga::restyleContext(originalStyle_,layers_,palette_.paper.name(),styled_)["layers"].toArray(),original=originalStyle_["layers"].toArray();for(int i=0;i<original.size()&&i<changed.size();++i){const auto layer=original[i].toObject(),paint=changed[i].toObject()["paint"].toObject();for(const auto &key:ga::contextPaintProperties(layer))if(!map_->setPaintProperty(layer["id"].toString(),key,paint[key].toVariant()))error_="paint-rejected";}}
void NativeBasemap::render(QSize size,double ratio,unsigned framebuffer){if(!map_)return;auto gl=QOpenGLContext::currentContext()->functions();for(int i=0;i<8&&gl->glGetError()!=GL_NO_ERROR;++i){}vao_.bind();map_->resize(size);map_->setOpenGLFramebufferObject(framebuffer,size*ratio);map_->render();glError_=gl->glGetError();vao_.release();}
QPointF NativeBasemap::pixelForCoordinate(double lat,double lon)const{return map_?map_->pixelForCoordinate({lat,lon}):QPointF();}
QJsonObject NativeBasemap::diagnostics()const{return{{"native",true},{"webview",false},{"basemap_mode",basemapMode_},{"provider",basemapMode_=="satellite"?"Esri World Imagery":"OpenFreeMap / OpenMapTiles / OpenStreetMap"},{"renderer_engine","MapLibre Native for Qt 3.0.0 + GroundAtlas shared camera"},{"renderer",renderer_},{"initialized",bool(map_)},{"fully_rendered",ready_},{"style_loaded",styleReady_},{"gl_error",int(glError_)},{"error",error_},{"style_enabled",styled_},{"native_zoom",camera_.zoom},{"native_pitch",camera_.pitch},{"actual_native_pitch",map_?map_->pitch():0},{"native_bearing",camera_.bearing},{"projection_coefficients",camera_.projection.size()},{"analysis_data_added",0}};}
