#include "viewport.h"
#include "analysis.h"
#include "gifwriter.h"
#include "sitegeometry.h"
#include "planpainter.h"
#include "nativebasemap.h"
#include "contextcamera.h"
#include "earcut.hpp"
#include <array>
#include <vector>
#include <QStandardPaths>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QPdfWriter>
#include <QPageSize>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTextStream>
#include <QLocale>
#include <QSet>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <numeric>

Viewport::Viewport(QWidget *parent):Viewport(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/map-cache",parent){}
Viewport::Viewport(const QString &cache,QWidget *parent):QOpenGLWidget(parent){
 auto format=QSurfaceFormat::defaultFormat();
 format.setVersion(3,3);
 format.setProfile(QSurfaceFormat::CoreProfile);
 format.setStencilBufferSize(8);
 setFormat(format);
 setMinimumSize(360,300);
 setMouseTracking(true);
 setFocusPolicy(Qt::StrongFocus);
 setAccessibleName("GroundAtlas native OpenGL viewport");
 interactionIdle_.setSingleShot(true);
 interactionIdle_.setInterval(120);
 connect(&interactionIdle_,&QTimer::timeout,this,[this]{interacting_=false;planDirty_=true;update();});
 camAnimTimer_.setTimerType(Qt::PreciseTimer);
 camAnimTimer_.setInterval(5);
 connect(&camAnimTimer_,&QTimer::timeout,this,[this]{
  const double raw=double(camAnimClock_.elapsed())/std::max(1,camAnimDurationMs_);
  const double t=std::clamp(raw,0.0,1.0);
  const double ease=1.0-std::pow(1.0-t,3.0);
  const double dYaw=std::remainder(camAnimTarget_.yaw-camAnimStart_.yaw,360.0);
  const double prevSep=camera_.separation;
  camera_.yaw=camAnimStart_.yaw+dYaw*ease;
  camera_.elevation=camAnimStart_.elevation+(camAnimTarget_.elevation-camAnimStart_.elevation)*ease;
  const double logStart=std::log(std::max(1e-7,camAnimStart_.zoom));
  const double logEnd=std::log(std::max(1e-7,camAnimTarget_.zoom));
  camera_.zoom=std::clamp(std::exp(logStart+(logEnd-logStart)*ease),1e-7,1e5);
  camera_.panX=camAnimStart_.panX+(camAnimTarget_.panX-camAnimStart_.panX)*ease;
  camera_.panY=camAnimStart_.panY+(camAnimTarget_.panY-camAnimStart_.panY)*ease;
  camera_.separation=camAnimStart_.separation+(camAnimTarget_.separation-camAnimStart_.separation)*ease;
  if(std::abs(camera_.separation-prevSep)>1e-3)dirty_=true;
  if(project_)ga::normalizeCameraCenter(camera_,project_->site);
  sortDirty_=true;planDirty_=contextViewDirty_=true;
  if(t>=1.0){
   const bool prevExp=camera_.exploded;
   camera_=camAnimTarget_;
   if(camera_.exploded!=prevExp||std::abs(camera_.separation-prevSep)>1e-3)dirty_=true;
   if(project_)ga::normalizeCameraCenter(camera_,project_->site);
   camAnimTimer_.stop();
   interacting_=false;
   planDirty_=true;
  }
  update();
  emit cameraChanged();
 });
 // High-refresh-rate (up to 500 Hz) smooth wheel-zoom & orbit inertia timer
 dampTimer_.setTimerType(Qt::PreciseTimer);
 dampTimer_.setInterval(5);
 connect(&dampTimer_,&QTimer::timeout,this,[this]{
  const double dt=std::clamp(double(dampClock_.restart())/1000.0,0.001,0.05);
  bool active=false;
  if(dampZoomActive_){
   const double k=1.0-std::exp(-18.0*dt);
   const double logCur=std::log(std::max(1e-7,camera_.zoom));
   const double logTgt=std::log(std::max(1e-7,dampTargetZoom_));
   if(std::abs(logTgt-logCur)<0.0008){
    camera_.zoom=dampTargetZoom_;
    dampZoomActive_=false;
   }else{
    camera_.zoom=std::clamp(std::exp(logCur+(logTgt-logCur)*k),1e-7,1e5);
    active=true;
   }
   constrainZoom();
   const auto after=groundPoint(dampAnchorScreen_);
   panGround(dampAnchorLocal_,after);
   planDirty_=contextViewDirty_=true;
  }
  if((std::abs(dampYawVel_)>0.08||std::abs(dampElVel_)>0.08)&&camera_.mode!="2d"){
   camera_.yaw+=dampYawVel_*dt;
   camera_.elevation=std::clamp(camera_.elevation+dampElVel_*dt,8.0,89.5);
   const double decay=std::exp(-11.0*dt);
   dampYawVel_*=decay;
   dampElVel_*=decay;
   sortDirty_=true;planDirty_=contextViewDirty_=true;
   active=true;
  }else{
   dampYawVel_=dampElVel_=0.0;
  }
  if(!active){
   dampTimer_.stop();
   interacting_=false;
   planDirty_=true;
  }else beginInteraction();
  update();
  emit cameraChanged();
 });
 basemap_=std::make_unique<NativeBasemap>(cache);
 basemap_->setPresentation(layers_,palette_,contextStyled_);
 connect(basemap_.get(),&NativeBasemap::needsRendering,this,QOverload<>::of(&Viewport::update));
}
Viewport::~Viewport(){cleanupGL();if(context())disconnect(context(),nullptr,this,nullptr);}
void Viewport::shutdown(){cleanupGL();}
void Viewport::cleanupGL(){
 if(context()){
  makeCurrent();
  basemap_->release();
  if(shadowTex_){glDeleteTextures(1,&shadowTex_);shadowTex_=0;}
  if(shadowFbo_){glDeleteFramebuffers(1,&shadowFbo_);shadowFbo_=0;}
  if(hanoiPlanTex_){glDeleteTextures(1,&hanoiPlanTex_);hanoiPlanTex_=0;}
  destroySsaoBuffers();
  buffer_.destroy();
  vao_.destroy();
  sunBuffer_.destroy();
  sunVao_.destroy();
  planTexBuffer_.destroy();
  planTexVao_.destroy();
  quadVao_.destroy();
  shader_.removeAllShaders();
  shadowShader_.removeAllShaders();
  skyShader_.removeAllShaders();
  tintShader_.removeAllShaders();
  ssaoShader_.removeAllShaders();
  ssaoBlurShader_.removeAllShaders();
  planTexShader_.removeAllShaders();
  planTexVerts_=0;
  doneCurrent();
 }
 dirty_=uploadDirty_=sunDirty_=sunUploadDirty_=planDirty_=contextViewDirty_=hanoiPlanTexDirty_=true;
}
void Viewport::resizeGL(int,int){contextViewDirty_=planDirty_=true;}
void Viewport::resizeEvent(QResizeEvent *event){
 const auto previousSize=navigationSize_.isValid()?navigationSize_:event->oldSize();
 const bool preserve=project_&&previousSize.width()>0&&previousSize.height()>0;
 ga::ContextCamera previous;if(preserve)previous=ga::contextCamera(project_->site,sceneCamera(),previousSize);
 QOpenGLWidget::resizeEvent(event);
 if(preserve){const QSignalBlocker block(this);setGeographicView(previous.lat,previous.lon,previous.zoom+1);}
 if(project_)navigationSize_=size();
 if(project_)emit framingChanged();
}
void Viewport::setContextStyleEnabled(bool value){contextStyled_=value;basemap_->setPresentation(layers_,palette_,value);update();}
void Viewport::setContextVisible(bool value){if(contextVisible_==value)return;contextVisible_=value;interactionImage_.clear();contextViewDirty_=planDirty_=true;update();}
void Viewport::setProject(const ga::Project *p){project_=p;planGeometry_={};interactionImage_.clear();stats_=p?p->stats():ga::Stats();selected_=-1;hoveredFeature_=-1;hanoiComposite_={};hanoiPlanTexDirty_=true;if(p){const double r=std::max(80.0,p->site.radius*0.86);sectionA_={-r,0};sectionB_={r,0};}reloadTerrainDem();dirty_=sunDirty_=planDirty_=planCacheDirty_=contextViewDirty_=true;update();}
void Viewport::setLayers(const QVector<ga::Layer> &v){interactionImage_.clear();layers_=v;dirty_=planDirty_=planCacheDirty_=true;basemap_->setPresentation(layers_,palette_,contextStyled_);update();}
void Viewport::setPalette(const ga::Palette &p){interactionImage_.clear();palette_=p;dirty_=planDirty_=planCacheDirty_=true;basemap_->setPresentation(layers_,palette_,contextStyled_);update();}
void Viewport::setDiagram(const QString &s){interactionImage_.clear();diagram_=s;dirty_=planDirty_=planCacheDirty_=true;update();}
void Viewport::setPlot(const ga::PlotDesign &p){plot_=p;if(massing_.enabled)dirty_=true;update();}
void Viewport::setMassing(const ga::MassingConfig &m){if(massing_==m)return;massing_=m;dirty_=true;update();}
void Viewport::setModels3d(const QVector<ga::ImportedModel3D> &models){
 models3d_=models;
 if(selectedModel3d_>=models3d_.size())selectedModel3d_=models3d_.isEmpty()?-1:models3d_.size()-1;
 if(models3d_.isEmpty()&&placingModel3d_)setPlacingModel3d(false);
 dirty_=true;
 update();
}
void Viewport::setSelectedModel3d(int index){
 selectedModel3d_=(index>=0&&index<models3d_.size())?index:-1;
 update();
}
void Viewport::setPlacingModel3d(bool active){
 const bool next=active&&selectedModel3d_>=0&&selectedModel3d_<models3d_.size();
 if(placingModel3d_==next)return;
 placingModel3d_=next;
 draggingModel3d_=false;
 if(placingModel3d_){
  if(drawingPlot_)cancelPlotDrawing();
  if(measuring_)setMeasuring(false);
  setFocus();
  setCursor(Qt::SizeAllCursor);
 }else{
  unsetCursor();
 }
 emit placingModel3dChanged(placingModel3d_);
 update();
}
void Viewport::focusModel3d(int index){
 if(!project_||index<0||index>=models3d_.size())return;
 const auto &m=models3d_[index];
 auto target=camera_;
 target.panX=m.x;
 target.panY=m.y;
 const auto sz=m.effectiveSizeM();
 const double span=std::max({18.0,double(sz.x()),double(sz.y()),double(sz.z())});
 const double desiredZoom=std::clamp(project_->site.radius/(span*2.4),0.8,8.0);
 target.zoom=std::max(target.zoom,desiredZoom);
 animateCameraTo(target,340);
}
void Viewport::setAnalyticalOverlay(const ga::AnalyticalOverlay &o){overlay_=o;update();}
void Viewport::setFuturePlanningOverlay(const ga::FuturePlanningOverlay &o){
 const bool needMesh=(planningOverlay_.show3DEnvelopes||planningOverlay_.show3DCorridors||o.show3DEnvelopes||o.show3DCorridors);
 planningOverlay_=o;
 if(needMesh)dirty_=true;
 update();
}
void Viewport::setPlanningImageOverlay(const ga::PlanningImageOverlay &img){
 planningImage_=img;
 update();
}
void Viewport::setHanoiPlanning(bool enabled,const QString &layerId,double opacity){
 const QString norm=(layerId=="khsdd"||layerId=="qhct"||layerId=="custom")?layerId:QStringLiteral("qhpk");
 const double op=std::clamp(opacity,0.05,1.0);
 hanoiPlanEnabled_=enabled;
 hanoiPlanLayer_=norm;
 hanoiPlanOpacity_=op;
 if(basemap_)basemap_->setHanoiPlanning(enabled,norm,op);
 update();
}
void Viewport::setHanoiTileComposite(const ga::HanoiTileComposite &composite){
 hanoiComposite_=composite;
 hanoiPlanTexDirty_=true;
 update();
}
void Viewport::setLanguage(bool en){english_=en;setAccessibleName(en?"Site viewport":"Khung nhìn khu đất");update();}
void Viewport::setDarkUi(bool dark){if(darkUi_==dark)return;darkUi_=dark;update();}
void Viewport::setAutoDarkBasemap(bool enabled){if(autoDarkBasemap_==enabled)return;autoDarkBasemap_=enabled;update();}
void Viewport::setSmoothDamping(bool enabled){smoothDamping_=enabled;if(!enabled){dampTimer_.stop();dampZoomActive_=false;dampYawVel_=dampElVel_=0.0;}}
void Viewport::setGraphicsConfig(const ga::ResolvedGraphicsConfig &cfg){
 gfxConfig_=cfg;
 const int tickMs=std::clamp(int(std::floor(1000.0/std::clamp(cfg.targetFps,60,500))),1,16);
 camAnimTimer_.setInterval(tickMs);
 dampTimer_.setInterval(tickMs);
 if(cfg.shadowMapSize!=shadowSize_){
  shadowSize_=std::clamp(cfg.shadowMapSize,512,4096);
  shadowNeedsResize_=true;
 }
 update();
}
void Viewport::reloadTerrainDem(){
 terrainDem_={};
 if(!terrain3d_||!project_||envDirectory_.isEmpty())return;
 const auto fields=ga::environmentFields(envDirectory_,project_->site);
 for(const auto &f:fields){
  if(f["id"].toString()=="gedtm30"){
   terrainDem_=ga::TerrainGrid::fromEnvironmentField(f);
   break;
  }
 }
}
void Viewport::setTerrain3d(bool enabled,const QString &envDirectory){
 if(!envDirectory.isEmpty())envDirectory_=envDirectory;
 const bool changed=(terrain3d_!=enabled);
 terrain3d_=enabled;
 reloadTerrainDem();
 if(changed){dirty_=true;update();}
}
void Viewport::setHudDockVisible(bool visible){if(hudDockVisible_==visible)return;hudDockVisible_=visible;update();}
void Viewport::setMeasuring(bool active){
 if(measuring_==active)return;
 measuring_=active;
 if(measuring_){
  if(drawingPlot_)cancelPlotDrawing();
  if(placingModel3d_)setPlacingModel3d(false);
  measurePts_.clear();
  setFocus();
  setCursor(Qt::CrossCursor);
 }else{
  measurePts_.clear();
  unsetCursor();
 }
 emit measuringChanged(measuring_);
 update();
}
void Viewport::clearMeasurement(){measurePts_.clear();update();}
void Viewport::setSectionCutActive(bool active){
 if(sectionCutActive_==active)return;
 sectionCutActive_=active;
 dragSectionPart_=hoverSectionPart_=0;
 if(sectionCutActive_&&project_&&QLineF(sectionA_,sectionB_).length()<20.0){
  const double r=std::max(80.0,project_->site.radius*0.86);
  sectionA_={-r,0};sectionB_={r,0};
 }
 emit sectionCutChanged(sectionA_,sectionB_,sectionCutActive_);
 update();
}
void Viewport::setSectionCutLine(QPointF a,QPointF b){
 if(QLineF(a,b).length()<10.0)return;
 sectionA_=a;sectionB_=b;
 emit sectionCutChanged(sectionA_,sectionB_,sectionCutActive_);
 update();
}
void Viewport::stopCameraAnimation(){
 if(camAnimTimer_.isActive())camAnimTimer_.stop();
 if(dampTimer_.isActive())dampTimer_.stop();
 dampZoomActive_=false;dampYawVel_=dampElVel_=0.0;
}
void Viewport::setSolarLive(const ga::SolarState &solar){
 const bool treesChanged=(solar.trees3d!=camera_.solar.trees3d);
 camera_.solar=solar;
 if(treesChanged)dirty_=true;
 sunDirty_=true;
 update();
}
void Viewport::animateCameraTo(const ga::Camera &target,int durationMs){
 if(!isVisible()||durationMs<=16){setCamera(target);return;}
 camAnimStart_=camera_;
 camAnimTarget_=target;
 // If enabling Exploded Stack from flat 3D, start separation smoothly from 0
 if(target.exploded&&!camera_.exploded)camAnimStart_.separation=0.0;
 if(target.grid!=camera_.grid)interactionImage_.clear();
 if(target.exploded!=camera_.exploded||target.grid!=camera_.grid)dirty_=true;
 if(!(target.solar==camera_.solar))sunDirty_=true;
 camera_.mode=target.mode;
 camera_.projection=target.projection;
 camera_.exploded=target.exploded;
 camera_.grid=target.grid;
 camera_.solar=target.solar;
 camera_.separation=camAnimStart_.separation;
 if(target.exploded&&!camAnimStart_.exploded)dirty_=true;
 sortDirty_=true;planDirty_=contextViewDirty_=true;
 camAnimDurationMs_=std::max(60,durationMs);
 camAnimClock_.restart();
 beginInteraction();
 camAnimTimer_.start();
 update();
}
void Viewport::animateResetView(int durationMs){
 ga::Camera target=camera_;
 target.panX=0;target.panY=0;target.zoom=1.0;
 animateCameraTo(target,durationMs);
}
void Viewport::animateZoomBy(double factor,int durationMs){
 ga::Camera target=camAnimTimer_.isActive()?camAnimTarget_:camera_;
 target.zoom=std::clamp(target.zoom*factor,1e-7,1e5);
 animateCameraTo(target,durationMs);
}
void Viewport::animateAlignNorth(int durationMs){
 ga::Camera target=camAnimTimer_.isActive()?camAnimTarget_:camera_;
 target.yaw=0.0;
 animateCameraTo(target,durationMs);
}

void Viewport::beginPlotDrawing(){
 plotDraft_.clear();
 drawingPlot_=true;
 freehandPlot_=false;
 skipPlotRelease_=false;
 setFocus();
 setCursor(Qt::CrossCursor);
 emit plotDrawingChanged(true,0);
 update();
}
void Viewport::finishPlotDrawing(){
 if(!drawingPlot_)return;
 QPolygonF finalRing=plotDraft_;
 if(finalRing.size()>20){
  const double tol=std::max(1.2,project_?project_->site.radius*0.005:2.0);
  finalRing=ga::simplifyPolyline(finalRing,tol);
 }
 drawingPlot_=false;
 freehandPlot_=false;
 plotDraft_.clear();
 unsetCursor();
 emit plotDrawingChanged(false,0);
 if(finalRing.size()>=3){
  emit plotDrawn(finalRing);
 }
 update();
}
void Viewport::undoPlotVertex(){
 if(!drawingPlot_||plotDraft_.isEmpty())return;
 plotDraft_.removeLast();
 emit plotDrawingChanged(true,plotDraft_.size());
 update();
}
void Viewport::cancelPlotDrawing(){
 if(!drawingPlot_)return;
 drawingPlot_=false;
 freehandPlot_=false;
 plotDraft_.clear();
 unsetCursor();
 emit plotDrawingChanged(false,0);
 update();
}

void Viewport::setCamera(const ga::Camera &c){
 stopCameraAnimation();
 if(c.grid!=camera_.grid)interactionImage_.clear();
 if(c.exploded!=camera_.exploded||c.grid!=camera_.grid||c.separation!=camera_.separation||c.solar.trees3d!=camera_.solar.trees3d)dirty_=true;
 if(!(c.solar==camera_.solar))sunDirty_=true;
 if(c.yaw!=camera_.yaw||c.elevation!=camera_.elevation||c.projection!=camera_.projection||c.mode!=camera_.mode)sortDirty_=true;
 camera_=c;
 if(project_)ga::normalizeCameraCenter(camera_,project_->site);
 planDirty_=contextViewDirty_=true;
 update();
 emit cameraChanged();
}
void Viewport::setSelected(int i){interactionImage_.clear();selected_=i;dirty_=planDirty_=planCacheDirty_=true;update();emit featureSelected(i);}
void Viewport::beginInteraction(){interacting_=true;interactionIdle_.start();}
void Viewport::resetView(){camera_.panX=camera_.panY=0;camera_.zoom=1;planDirty_=contextViewDirty_=true;update();emit cameraChanged();}
void Viewport::zoomBy(double factor){beginInteraction();camera_.zoom=std::clamp(camera_.zoom*factor,1e-7,1e5);constrainZoom();planDirty_=contextViewDirty_=true;update();emit cameraChanged();}
QJsonObject Viewport::geographicView()const{if(!project_)return{};const auto c=ga::contextCamera(project_->site,sceneCamera(),size());return{{"lat",c.lat},{"lon",c.lon},{"zoom",c.zoom+1}};}
void Viewport::setGeographicView(double lat,double lon,double zoom){if(!project_)return;if(dirty_&&camera_.mode!="2d")rebuild();auto c=ga::cameraAtGeographicView(project_->site,sceneCamera(),size(),lat,lon,zoom);setCamera(c);navigationSize_=size();}
void Viewport::constrainZoom(){if(!project_)return;const auto view=geographicView();const auto c=ga::cameraAtGeographicView(project_->site,sceneCamera(),size(),view["lat"].toDouble(),view["lon"].toDouble(),view["zoom"].toDouble());camera_.zoom=c.zoom;}
ga::Camera Viewport::sceneCamera()const{auto c=camera_;c.stackHeight=stackHeight_;return c;}
QMatrix4x4 Viewport::matrix() const{return project_?ga::sceneCameraMatrix(project_->site,sceneCamera(),double(width())/std::max(1,height())):camera_.matrix(500,double(width())/std::max(1,height()));}

QMatrix4x4 Viewport::sunLightMatrix(const QVector3D &sunDir,const QPointF &offset) const {
 const float siteR=project_?float(std::max(120.0,ga::envelopeRadius(project_->site)*1.20)):600.0f;
 const float half=siteR*1.12f;
 const float dist=siteR*2.4f+320.0f;
 const QVector3D target(float(offset.x()),float(offset.y()),18.0f);
 const QVector3D eye=target+sunDir.normalized()*dist;
 const QVector3D up=(std::abs(sunDir.z())>0.96f)?QVector3D(0,1,0):QVector3D(0,0,1);
 QMatrix4x4 view,proj;
 view.lookAt(eye,target,up);
 proj.ortho(-half,half,-half,half,10.0f,dist+siteR*2.2f+420.0f);
 return proj*view;
}

void Viewport::initShadowMap(int size){
 if(shadowTex_){glDeleteTextures(1,&shadowTex_);shadowTex_=0;}
 if(shadowFbo_){glDeleteFramebuffers(1,&shadowFbo_);shadowFbo_=0;}
 GLint maxTex=2048;
 glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTex);
 shadowSize_=std::clamp(size,512,std::min(4096,int(maxTex)));
 glGenFramebuffers(1,&shadowFbo_);
 glGenTextures(1,&shadowTex_);
 glBindTexture(GL_TEXTURE_2D,shadowTex_);
 glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,shadowSize_,shadowSize_,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
 const float borderColor[4]={1.0f,1.0f,1.0f,1.0f};
 glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,borderColor);
 glBindFramebuffer(GL_FRAMEBUFFER,shadowFbo_);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,shadowTex_,0);
 glDrawBuffer(GL_NONE);
 glReadBuffer(GL_NONE);
 const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
 glBindFramebuffer(GL_FRAMEBUFFER,defaultFramebufferObject());
 glBindTexture(GL_TEXTURE_2D,0);
 if(status!=GL_FRAMEBUFFER_COMPLETE){
  glDeleteTextures(1,&shadowTex_);shadowTex_=0;
  glDeleteFramebuffers(1,&shadowFbo_);shadowFbo_=0;
 }
 shadowNeedsResize_=false;
}

void Viewport::destroySsaoBuffers(){
 if(ssaoDepthTex_){glDeleteTextures(1,&ssaoDepthTex_);ssaoDepthTex_=0;}
 if(ssaoDepthFbo_){glDeleteFramebuffers(1,&ssaoDepthFbo_);ssaoDepthFbo_=0;}
 if(ssaoTex_){glDeleteTextures(1,&ssaoTex_);ssaoTex_=0;}
 if(ssaoFbo_){glDeleteFramebuffers(1,&ssaoFbo_);ssaoFbo_=0;}
 if(ssaoBlurTex_){glDeleteTextures(1,&ssaoBlurTex_);ssaoBlurTex_=0;}
 if(ssaoBlurFbo_){glDeleteFramebuffers(1,&ssaoBlurFbo_);ssaoBlurFbo_=0;}
 ssaoW_=ssaoH_=0;
 ssaoReady_=false;
}

void Viewport::initSsaoBuffers(int halfW,int halfH){
 const int w=std::clamp(halfW,128,1024);
 const int h=std::clamp(halfH,128,1024);
 if(ssaoReady_&&ssaoW_==w&&ssaoH_==h)return;
 destroySsaoBuffers();
 ssaoW_=w;ssaoH_=h;

 glGenFramebuffers(1,&ssaoDepthFbo_);
 glGenTextures(1,&ssaoDepthTex_);
 glBindTexture(GL_TEXTURE_2D,ssaoDepthTex_);
 glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,w,h,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glBindFramebuffer(GL_FRAMEBUFFER,ssaoDepthFbo_);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,ssaoDepthTex_,0);
 glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
 const GLenum st0=glCheckFramebufferStatus(GL_FRAMEBUFFER);

 auto makeR8=[&](GLuint &fbo,GLuint &tex)->GLenum{
  glGenFramebuffers(1,&fbo);
  glGenTextures(1,&tex);
  glBindTexture(GL_TEXTURE_2D,tex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_R8,w,h,0,GL_RED,GL_UNSIGNED_BYTE,nullptr);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  return glCheckFramebufferStatus(GL_FRAMEBUFFER);
 };
 const GLenum st1=makeR8(ssaoFbo_,ssaoTex_);
 const GLenum st2=makeR8(ssaoBlurFbo_,ssaoBlurTex_);

 glBindFramebuffer(GL_FRAMEBUFFER,defaultFramebufferObject());
 glBindTexture(GL_TEXTURE_2D,0);
 if(st0==GL_FRAMEBUFFER_COMPLETE&&st1==GL_FRAMEBUFFER_COMPLETE&&st2==GL_FRAMEBUFFER_COMPLETE){
  ssaoReady_=true;
 }else{
  destroySsaoBuffers();
 }
}

void Viewport::initializeGL(){
 connect(context(),&QOpenGLContext::aboutToBeDestroyed,this,&Viewport::cleanupGL,Qt::DirectConnection);
 initializeOpenGLFunctions();
 const auto text=[&](GLenum name){const auto s=glGetString(name);return s?QString::fromLatin1(reinterpret_cast<const char*>(s)):QString();};
 renderer_=text(GL_RENDERER)+" · "+text(GL_VERSION);

 // Pass 1: Hardware 2048x2048 Shadow Map Depth Shader (excludes emissive dome/sun triangles where length(normal) < 0.1)
 const char *shVert=R"(#version 150
 in vec3 position;in vec3 normal;
 uniform mat4 lightMatrix;uniform vec2 renderOffset;uniform float worldWidth;
 void main(){
  if(length(normal)<0.1){gl_Position=vec4(2.0,2.0,2.0,1.0);return;}
  vec3 scene=position;scene.xy+=renderOffset;
  if(scene.x>worldWidth*0.5)scene.x-=worldWidth;
  else if(scene.x<-worldWidth*0.5)scene.x+=worldWidth;
  gl_Position=lightMatrix*vec4(scene,1.0);
 }
 )";
 const char *shFrag=R"(#version 150
 void main(){}
 )";
 if(shadowShader_.addShaderFromSourceCode(QOpenGLShader::Vertex,shVert)&&shadowShader_.addShaderFromSourceCode(QOpenGLShader::Fragment,shFrag)){
  shadowShader_.bindAttributeLocation("position",0);
  shadowShader_.bindAttributeLocation("normal",1);
  shadowShader_.link();
 }

 // Fullscreen triangle vertex shader shared by Sky Dome, SSAO, and Bilateral Blur
 const char *quadVert=R"(#version 150
 out vec2 uv;
 void main(){
  vec2 pos=vec2((gl_VertexID==1)?3.0:-1.0,(gl_VertexID==2)?3.0:-1.0);
  uv=pos*0.5+0.5;
  gl_Position=vec4(pos,0.0,1.0);
 }
 )";

 // Atmospheric Sky Gradient Shader (Rayleigh + Golden Hour Mie glow by solar altitude)
 const char *skyFrag=R"(#version 150
 in vec2 uv;
 uniform float sunAlt;uniform int goldenHour;uniform int darkUi;uniform float horizonY;
 out vec4 fragColor;
 void main(){
  float t=clamp((uv.y-horizonY)/max(0.15,1.0-horizonY),0.0,1.0);
  float goldT=(goldenHour==1)?clamp((36.0-sunAlt)/28.0,0.0,1.0):0.0;
  vec3 zenDay=darkUi==1?vec3(0.08,0.12,0.19):vec3(0.46,0.63,0.82);
  vec3 horDay=darkUi==1?vec3(0.16,0.19,0.24):vec3(0.86,0.90,0.93);
  vec3 zenGold=darkUi==1?vec3(0.12,0.11,0.18):vec3(0.32,0.42,0.64);
  vec3 horGold=darkUi==1?vec3(0.38,0.24,0.17):vec3(0.96,0.77,0.58);
  vec3 zen=mix(zenDay,zenGold,goldT);
  vec3 hor=mix(horDay,horGold,goldT);
  vec3 sky=mix(hor,zen,pow(t,0.68));
  // Soft warm horizon band near sunrise/sunset
  float band=exp(-abs(uv.y-horizonY)*8.0)*goldT*(darkUi==1?0.24:0.18);
  sky+=vec3(1.0,0.62,0.28)*band;
  fragColor=vec4(clamp(sky,0.0,1.0),1.0);
 }
 )";
 skyShader_.addShaderFromSourceCode(QOpenGLShader::Vertex,quadVert);
 skyShader_.addShaderFromSourceCode(QOpenGLShader::Fragment,skyFrag);
 skyShader_.link();

 // Core Profile fullscreen solid tint shader (replaces legacy fixed-function quad for Dark Mode basemap harmonization on macOS + Windows)
 const char *tintFrag=R"(#version 150
 uniform vec4 tintColor;
 out vec4 fragColor;
 void main(){
  fragColor=tintColor;
 }
 )";
 tintShader_.addShaderFromSourceCode(QOpenGLShader::Vertex,quadVert);
 tintShader_.addShaderFromSourceCode(QOpenGLShader::Fragment,tintFrag);
 tintShader_.link();

 // World-Space Metric GPU SSAO Shader (works identically on Ortho Isometric + Perspective + 2-Point!)
 const char *ssaoFrag=R"(#version 150
 in vec2 uv;
 uniform sampler2D depthTex;
 uniform mat4 invCamMat;
 uniform vec3 viewDir;
 uniform vec2 texSize;
 uniform int sampleCount;
 out vec4 fragColor;
 const vec2 kKernel16[16]=vec2[16](
  vec2(-0.707, 0.707),vec2( 0.707,-0.707),vec2(-0.35, -0.82),vec2( 0.85,  0.32),
  vec2(-0.92, -0.18),vec2( 0.24,  0.94),vec2(-0.14,  0.48),vec2( 0.48, -0.22),
  vec2(-0.55, -0.45),vec2( 0.52,  0.60),vec2( 0.12, -0.62),vec2(-0.62,  0.22),
  vec2( 0.32,  0.28),vec2(-0.28, -0.26),vec2( 0.72, -0.15),vec2(-0.18, -0.95)
 );
 vec3 worldPosAt(vec2 p,float d){
  vec4 ndc=vec4(p*2.0-1.0,d*2.0-1.0,1.0);
  vec4 wp=invCamMat*ndc;
  float w=(abs(wp.w)<1e-5)?1e-5:wp.w;
  return wp.xyz/w;
 }
 void main(){
  float d0=texture(depthTex,uv).r;
  if(d0>=0.9995){fragColor=vec4(1.0);return;}
  vec3 P=worldPosAt(uv,d0);
  vec3 dPx=dFdx(P),dPy=dFdy(P);
  vec3 N=cross(dPx,dPy);
  float nLen=length(N);
  if(nLen<1e-6){fragColor=vec4(1.0);return;}
  N/=nLen;
  if(dot(N,viewDir)<0.0)N=-N;
  float ign=fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(0.06711056,0.00583715))));
  float ang=ign*6.2831853;
  mat2 rot=mat2(cos(ang),-sin(ang),sin(ang),cos(ang));
  vec2 texel=vec2(1.0)/max(texSize,vec2(1.0));
  float pxRadius=clamp(min(texSize.x,texSize.y)*0.022,6.0,18.0);
  float radiusM=15.0;
  float occ=0.0;
  int taps=clamp(sampleCount,4,16);
  for(int i=0;i<taps;++i){
   vec2 off=(rot*kKernel16[i])*pxRadius*texel;
   vec2 suv=clamp(uv+off,vec2(0.002),vec2(0.998));
   float sd=texture(depthTex,suv).r;
   if(sd>=0.9995)continue;
   vec3 Q=worldPosAt(suv,sd);
   vec3 diff=Q-P;
   float dist=length(diff);
   if(dist>0.35&&dist<radiusM){
    float rangeW=smoothstep(radiusM,radiusM*0.20,dist);
    float cosA=dot(N,diff/dist);
    occ+=max(0.0,cosA-0.14)*rangeW;
   }
  }
  float ao=clamp(1.0-(occ/float(taps))*1.75,0.38,1.0);
  fragColor=vec4(ao,ao,ao,1.0);
 }
 )";
 ssaoShader_.addShaderFromSourceCode(QOpenGLShader::Vertex,quadVert);
 ssaoShader_.addShaderFromSourceCode(QOpenGLShader::Fragment,ssaoFrag);
 ssaoShader_.link();

 // Bilateral Depth-Aware SSAO Blur (preserves crisp building silhouettes)
 const char *blurFrag=R"(#version 150
 in vec2 uv;
 uniform sampler2D aoTex;
 uniform sampler2D depthTex;
 uniform vec2 texSize;
 out vec4 fragColor;
 void main(){
  float dc=texture(depthTex,uv).r;
  vec2 texel=vec2(1.0)/max(texSize,vec2(1.0));
  float sum=0.0,wsum=0.0;
  for(int x=-2;x<=2;++x){
   for(int y=-2;y<=2;++y){
    vec2 suv=clamp(uv+vec2(x,y)*texel,vec2(0.001),vec2(0.999));
    float d=texture(depthTex,suv).r;
    float spatial=exp(-float(x*x+y*y)*0.22);
    float depthW=exp(-abs(d-dc)*550.0);
    float w=spatial*depthW;
    sum+=texture(aoTex,suv).r*w;
    wsum+=w;
   }
  }
  float v=wsum>1e-5?sum/wsum:1.0;
  fragColor=vec4(v,v,v,1.0);
 }
 )";
 ssaoBlurShader_.addShaderFromSourceCode(QOpenGLShader::Vertex,quadVert);
 ssaoBlurShader_.addShaderFromSourceCode(QOpenGLShader::Fragment,blurFrag);
 ssaoBlurShader_.link();

 // Pass 2: Main Architectural OpenGL 3.3 Sun-Sky + Poisson PCF + Fresnel Water + 3D Foliage + GPU SSAO
 const char *vertex=R"(#version 150
 in vec3 position;in vec3 normal;in vec4 color;in float matType;
 uniform mat4 matrix;uniform mat4 lightMatrix;uniform vec2 renderOffset;uniform float worldWidth;
 out vec4 paint;out vec3 n;out vec4 lightSpacePos;out vec3 worldPos;out float vMat;
 void main(){
  vec3 scene=position;scene.xy+=renderOffset;
  if(scene.x>worldWidth*0.5)scene.x-=worldWidth;
  else if(scene.x<-worldWidth*0.5)scene.x+=worldWidth;
  gl_Position=matrix*vec4(scene,1.0);
  lightSpacePos=lightMatrix*vec4(scene,1.0);
  worldPos=position;
  paint=color;n=normal;vMat=matType;
 }
 )";
 const char *fragment=R"(#version 150
 in vec4 paint;in vec3 n;in vec4 lightSpacePos;in vec3 worldPos;in float vMat;
 uniform vec3 sunDir;uniform vec3 viewDir;
 uniform int sunMode;uniform int hasShadowMap;uniform int goldenHour;
 uniform int pcfQuality;uniform int creaseMode;uniform int useSsao;uniform int skyDomeOn;uniform int darkUi;
 uniform float sunAlt;uniform float shadowStrength;
 uniform vec2 viewportSize;
 uniform sampler2D shadowMap;
 uniform sampler2D ssaoMap;
 out vec4 fragmentColor;

 const vec2 kPoisson16[16]=vec2[16](
  vec2(-0.94201624,-0.39906216),vec2(0.94558609,-0.76890725),
  vec2(-0.09418410,-0.92938870),vec2(0.34495938,0.29387760),
  vec2(-0.91588581,0.45771432),vec2(-0.81544232,-0.87912464),
  vec2(-0.38277543,0.27676845),vec2(0.97484398,0.75648379),
  vec2(0.44323325,-0.97511554),vec2(0.53742981,-0.47373420),
  vec2(-0.26496911,-0.41893023),vec2(0.79197514,0.19090188),
  vec2(-0.24188840,0.99706507),vec2(-0.81409955,0.91437590),
  vec2(0.19984126,0.78641367),vec2(0.14383161,-0.14100790)
 );

 float calcShadow(vec4 lsPos,float ndotl){
  if(hasShadowMap==0)return 0.0;
  vec3 proj=lsPos.xyz/lsPos.w;
  proj=proj*0.5+0.5;
  if(proj.z>0.999||proj.x<0.002||proj.x>0.998||proj.y<0.002||proj.y>0.998)return 0.0;
  float bias=max(0.00045,0.0024*(1.0-ndotl));
  vec2 texel=vec2(1.0)/vec2(textureSize(shadowMap,0));
  if(pcfQuality<=0){
   float sh=0.0;
   vec2 o=texel*1.2;
   sh+=(proj.z-bias>texture(shadowMap,proj.xy+vec2(-o.x,-o.y)).r)?1.0:0.0;
   sh+=(proj.z-bias>texture(shadowMap,proj.xy+vec2( o.x,-o.y)).r)?1.0:0.0;
   sh+=(proj.z-bias>texture(shadowMap,proj.xy+vec2(-o.x, o.y)).r)?1.0:0.0;
   sh+=(proj.z-bias>texture(shadowMap,proj.xy+vec2( o.x, o.y)).r)?1.0:0.0;
   return sh*0.25;
  }
  if(pcfQuality==1){
   float sh=0.0;
   for(int x=-1;x<=1;++x){
    for(int y=-1;y<=1;++y){
     float d=texture(shadowMap,proj.xy+vec2(x,y)*texel*1.45).r;
     sh+=(proj.z-bias>d)?1.0:0.0;
    }
   }
   return sh/9.0;
  }
  float ign=fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(0.06711056,0.00583715))));
  float ang=ign*6.2831853;
  float ca=cos(ang),sa=sin(ang);
  mat2 rot=mat2(ca,-sa,sa,ca);
  float radius=2.15;
  float sh=0.0;
  for(int i=0;i<16;++i){
   vec2 off=(rot*kPoisson16[i])*texel*radius;
   float d=texture(shadowMap,proj.xy+off).r;
   sh+=(proj.z-bias>d)?1.0:0.0;
  }
  return sh/16.0;
 }

 void main(){
  if(length(n)<0.1){fragmentColor=paint;return;}
  vec3 N=normalize(n);
  vec3 L=normalize(sunDir);
  if(sunMode==0){
   float light=0.72+0.28*max(0.0,dot(N,L));
   fragmentColor=vec4(paint.rgb*light,paint.a);
   return;
  }
  float ndotl=max(0.0,dot(N,L));
  float shadow=calcShadow(lightSpacePos,ndotl)*shadowStrength;
  float lit=max(0.0,1.0-shadow);

  float goldT=(goldenHour==1)?clamp((36.0-sunAlt)/28.0,0.0,1.0):0.0;
  vec3 sunColor=mix(vec3(1.10,1.05,0.97),vec3(1.32,0.82,0.47),goldT);
  vec3 skyColor=mix(vec3(0.58,0.68,0.82),vec3(0.42,0.52,0.75),goldT);
  vec3 groundBounce=mix(vec3(0.48,0.45,0.41),vec3(0.50,0.39,0.34),goldT);
  vec3 V=normalize(viewDir);
  vec3 H=normalize(L+V);

  float ssao=1.0;
  if(useSsao==1&&viewportSize.x>1.0&&viewportSize.y>1.0){
   ssao=mix(1.0,texture(ssaoMap,clamp(gl_FragCoord.xy/viewportSize,vec2(0.002),vec2(0.998))).r,0.72);
  }

  // Material 1: Architectural Reflective Water (Fresnel sky reflection + subtle ripple + solar glint + real shadows!)
  if(vMat>0.5&&vMat<1.5){
   float r1=sin(worldPos.x*0.24+worldPos.y*0.17)*0.022+cos(worldPos.x*0.49-worldPos.y*0.33)*0.014;
   float r2=cos(worldPos.x*0.31-worldPos.y*0.27)*0.022+sin(worldPos.x*0.19+worldPos.y*0.44)*0.014;
   vec3 Nw=normalize(vec3(r1,r2,1.0));
   float fresnel=mix(0.16,0.72,pow(1.0-max(0.0,dot(Nw,V)),3.8));
   vec3 skyReflect=mix(mix(vec3(0.56,0.72,0.88),vec3(0.94,0.72,0.52),goldT),vec3(0.18,0.24,0.34),float(darkUi)*0.45);
   vec3 deepTint=paint.rgb*(0.76+0.24*lit*ndotl)*mix(0.84,1.0,ssao);
   float sunGlint=pow(max(0.0,dot(Nw,H)),110.0)*lit*(0.55+0.45*goldT);
   vec3 wcol=mix(deepTint,skyReflect*(0.65+0.35*lit),fresnel)+sunColor*sunGlint;
   wcol=mix(wcol,wcol*(2.1*wcol+0.18)/(wcol*(2.0*wcol+0.55)+0.06),0.36);
   fragmentColor=vec4(clamp(wcol,0.0,1.0),paint.a);
   return;
  }

  float hemi=clamp(N.z*0.5+0.5,0.0,1.0);
  vec3 ambient=mix(groundBounce,skyColor,hemi)*(0.60+0.14*(1.0-goldT))*ssao;
  float wallFactor=1.0-abs(N.z);
  float contactAO=mix(1.0,mix(0.78,1.0,smoothstep(0.0,11.0,max(0.0,worldPos.z))),wallFactor);
  float crease=1.0;
  if(creaseMode==1){
   float edgeN=length(dFdx(N))+length(dFdy(N));
   crease=mix(1.0,0.88,clamp(edgeN*0.60,0.0,1.0));
  }

  // Material 2: Sculpted 3D Architectural Foliage (soft wrap-diffuse + sunlit subsurface translucency)
  if(vMat>1.5&&vMat<2.5){
   float wrapDiff=clamp((dot(N,L)+0.32)/1.32,0.0,1.0);
   float sss=pow(max(0.0,dot(-L,V)),2.6)*0.22*lit;
   vec3 directFoliage=sunColor*(wrapDiff*lit*0.92+sss);
   vec3 fcol=paint.rgb*(ambient+directFoliage)*mix(1.0,crease,0.45);
   fcol=mix(fcol,fcol*(2.1*fcol+0.18)/(fcol*(2.0*fcol+0.55)+0.06),0.38);
   fragmentColor=vec4(clamp(fcol,0.0,1.0),paint.a);
   return;
  }

  vec3 direct=sunColor*(ndotl*lit*(0.90+0.12*(1.0-goldT)));
  float specPow=(vMat>2.5)?44.0:32.0;
  float specAmp=(vMat>2.5)?0.24:0.15*step(0.7,N.z);
  float spec=pow(max(0.0,dot(N,H)),specPow)*lit*specAmp;
  float rim=(vMat>2.5)?pow(1.0-max(0.0,dot(N,V)),3.0)*0.16*lit:0.0;

  vec3 col=paint.rgb*(ambient*contactAO+direct)*crease+sunColor*(spec+rim);
  if(skyDomeOn==1){
   float distHaze=clamp(length(worldPos.xy)*0.00018,0.0,0.12);
   vec3 hazeCol=mix(vec3(0.84,0.88,0.92),vec3(0.95,0.78,0.62),goldT);
   if(darkUi==1)hazeCol=mix(vec3(0.14,0.16,0.20),vec3(0.28,0.20,0.16),goldT);
   col=mix(col,hazeCol,distHaze);
  }
  vec3 mapped=col*(2.1*col+0.18)/(col*(2.0*col+0.55)+0.06);
  col=mix(col,mapped,0.42);
  fragmentColor=vec4(clamp(col,0.0,1.0),paint.a);
 }
 )";
 if(!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex)||!shader_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment)){graphicsError_=shader_.log();return;}
 shader_.bindAttributeLocation("position",0);
 shader_.bindAttributeLocation("normal",1);
 shader_.bindAttributeLocation("color",2);
 shader_.bindAttributeLocation("matType",3);
 if(!shader_.link()){graphicsError_=shader_.log();return;}
 // Textured Ground Overlay Shader (for Hanoi Land-Use Planning tiles draped under 3D buildings with real PCF shadows!)
 const char *ptVert=R"(#version 150
 in vec3 position;in vec2 uv;
 uniform mat4 matrix;uniform mat4 lightMatrix;uniform vec2 renderOffset;uniform float worldWidth;
 out vec2 vUv;out vec4 lightPos;
 void main(){
  vec3 scene=position;scene.xy+=renderOffset;
  if(scene.x>worldWidth*0.5)scene.x-=worldWidth;
  else if(scene.x<-worldWidth*0.5)scene.x+=worldWidth;
  vUv=uv;
  lightPos=lightMatrix*vec4(scene,1.0);
  gl_Position=matrix*vec4(scene,1.0);
 }
 )";
 const char *ptFrag=R"(#version 150
 in vec2 vUv;in vec4 lightPos;
 uniform sampler2D planTex;uniform sampler2D shadowMap;
 uniform int hasShadowMap;uniform float opacity;uniform float shadowStrength;
 out vec4 fragmentColor;
 void main(){
  vec4 c=texture(planTex,vUv);
  if(c.a<0.01)discard;
  float shade=1.0;
  if(hasShadowMap==1){
   vec3 sc=lightPos.xyz/max(lightPos.w,0.0001)*0.5+0.5;
   if(sc.x>0.01&&sc.x<0.99&&sc.y>0.01&&sc.y<0.99&&sc.z<0.999){
    vec2 ts=1.0/vec2(textureSize(shadowMap,0));
    float bias=0.0014;
    float occ=0.0;
    occ+=step(texture(shadowMap,sc.xy).r+bias,sc.z);
    occ+=step(texture(shadowMap,sc.xy+vec2(ts.x,0.0)).r+bias,sc.z);
    occ+=step(texture(shadowMap,sc.xy-vec2(ts.x,0.0)).r+bias,sc.z);
    occ+=step(texture(shadowMap,sc.xy+vec2(0.0,ts.y)).r+bias,sc.z);
    occ+=step(texture(shadowMap,sc.xy-vec2(0.0,ts.y)).r+bias,sc.z);
    shade=1.0-(occ/5.0)*shadowStrength*0.55;
   }
  }
  fragmentColor=vec4(c.rgb*shade,c.a*opacity);
 }
 )";
 if(planTexShader_.addShaderFromSourceCode(QOpenGLShader::Vertex,ptVert)&&planTexShader_.addShaderFromSourceCode(QOpenGLShader::Fragment,ptFrag)){
  planTexShader_.bindAttributeLocation("position",0);
  planTexShader_.bindAttributeLocation("uv",1);
  planTexShader_.link();
 }
 buffer_.create();
 vao_.create();
 sunBuffer_.create();
 sunVao_.create();
 planTexBuffer_.create();
 planTexVao_.create();
 quadVao_.create();
 initShadowMap(shadowSize_);
 dirty_=sunDirty_=planDirty_=planCacheDirty_=hanoiPlanTexDirty_=true;
 emit graphicsReady(renderer_);
}

void Viewport::uploadHanoiPlanTexture(){
 hanoiPlanTexDirty_=false;
 planTexVerts_=0;
 if(!project_||!hanoiComposite_.valid()||!planTexShader_.isLinked())return;
 const QImage rgba=hanoiComposite_.image.convertToFormat(QImage::Format_RGBA8888);
 if(rgba.isNull())return;
 if(!hanoiPlanTex_)glGenTextures(1,&hanoiPlanTex_);
 glActiveTexture(GL_TEXTURE0);
 glBindTexture(GL_TEXTURE_2D,hanoiPlanTex_);
 glPixelStorei(GL_UNPACK_ALIGNMENT,4);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,rgba.width(),rgba.height(),0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.constBits());
 glGenerateMipmap(GL_TEXTURE_2D);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glBindTexture(GL_TEXTURE_2D,0);

 const auto &boundary=project_->site.boundary.isEmpty()?ga::primitiveBoundary(project_->site):project_->site.boundary;
 if(boundary.size()<3)return;
 std::vector<std::array<double,2>> ring;
 QVector<QPointF> pts;
 for(int i=0;i<boundary.size();++i){
  if(i==boundary.size()-1&&boundary[i]==boundary[0])continue;
  ring.push_back({boundary[i].x(),boundary[i].y()});
  pts.append(boundary[i]);
 }
 if(ring.size()<3)return;
 const auto indices=mapbox::earcut<uint32_t>(std::vector<std::vector<std::array<double,2>>>{ring});
 if(indices.size()<3)return;
 const double spanX=std::max(1e-4,hanoiComposite_.topRight.x()-hanoiComposite_.topLeft.x());
 const double spanY=std::max(1e-4,hanoiComposite_.topLeft.y()-hanoiComposite_.bottomLeft.y());
 struct TexVert{float x,y,z,u,v;};
 QVector<TexVert> verts;
 verts.reserve(int(indices.size()));
 for(uint32_t idx:indices){
  const QPointF &local=pts[int(idx)];
  const QPointF mp=ga::mapPlanePoint(local,project_->site);
  const float u=float((local.x()-hanoiComposite_.topLeft.x())/spanX);
  const float v=float((hanoiComposite_.topLeft.y()-local.y())/spanY);
  verts.append({float(mp.x()),float(mp.y()),0.92f,u,v});
 }
 planTexVerts_=verts.size();
 planTexVao_.bind();
 planTexBuffer_.bind();
 planTexBuffer_.allocate(verts.constData(),int(verts.size()*sizeof(TexVert)));
 planTexShader_.bind();
 planTexShader_.enableAttributeArray(0);
 planTexShader_.enableAttributeArray(1);
 planTexShader_.setAttributeBuffer(0,GL_FLOAT,0,3,sizeof(TexVert));
 planTexShader_.setAttributeBuffer(1,GL_FLOAT,3*int(sizeof(float)),2,sizeof(TexVert));
 planTexShader_.release();
 planTexBuffer_.release();
 planTexVao_.release();
}

void Viewport::rebuild(){
 if(!project_)return;
 auto modelCamera=camera_;modelCamera.mode="3d";
 const ga::TerrainGrid *dem=(terrain3d_&&terrainDem_.valid)?&terrainDem_:nullptr;
 const ga::PlotDesign *activePlot=(plot_.active&&massing_.enabled)?&plot_:nullptr;
 const ga::MassingConfig *activeMassing=(plot_.active&&massing_.enabled)?&massing_:nullptr;
 const QVector<ga::ImportedModel3D> *activeModels3d=models3d_.isEmpty()?nullptr:&models3d_;
 const ga::FuturePlanningOverlay *activePlanning=(planningOverlay_.valid()&&(planningOverlay_.show3DEnvelopes||planningOverlay_.show3DCorridors))?&planningOverlay_:nullptr;
 mesh_=ga::buildMesh(*project_,layers_,palette_,modelCamera,diagram_,selected_,false,dem,activePlot,activeMassing,activeModels3d,activePlanning);++meshBuilds_;
 stackHeight_=0;
 for(auto *vertices:{&mesh_.triangles,&mesh_.lines,&mesh_.transparent})
  for(auto &v:*vertices){
   stackHeight_=std::max(stackHeight_,double(v.z));
   const auto point=ga::mapPlanePoint({v.x,v.y},project_->site);
   v.x=point.x();v.y=point.y();
  }
 transparentCenters_.clear();transparentCenters_.reserve(mesh_.transparent.size()/3);
 for(int i=0;i+2<mesh_.transparent.size();i+=3){
  QVector3D center;
  for(int k=0;k<3;++k){const auto &v=mesh_.transparent[i+k];center+=QVector3D(v.x,v.y,v.z)/3;}
  transparentCenters_.append(center);
 }
 dirty_=false;uploadDirty_=sortDirty_=planDirty_=contextViewDirty_=true;
}
void Viewport::rebuildSunDome(){
 sunDirty_=false;
 sunMesh_={};
 if(!project_||camera_.mode=="2d"||camera_.exploded||!camera_.solar.enabled||!camera_.solar.showPath){
  sunUploadDirty_=true;
  return;
 }
 sunMesh_=ga::buildSunDomeMesh(*project_,camera_.solar);
 for(auto *vertices:{&sunMesh_.triangles,&sunMesh_.lines,&sunMesh_.transparent})
  for(auto &v:*vertices){
   const auto point=ga::mapPlanePoint({v.x,v.y},project_->site);
   v.x=point.x();v.y=point.y();
  }
 sunUploadDirty_=true;
}
void Viewport::uploadSunDome(){
 sunTriVerts_=sunMesh_.triangles.size();sunLineVerts_=sunMesh_.lines.size();sunTransVerts_=sunMesh_.transparent.size();
 QVector<ga::Vertex> all;all.reserve(sunTriVerts_+sunLineVerts_+sunTransVerts_);
 all.append(sunMesh_.triangles);all.append(sunMesh_.lines);all.append(sunMesh_.transparent);
 sunVao_.bind();sunBuffer_.bind();
 if(!all.isEmpty())sunBuffer_.allocate(all.constData(),int(all.size()*sizeof(ga::Vertex)));
 else sunBuffer_.allocate(nullptr,0);
 shader_.bind();shader_.enableAttributeArray(0);shader_.enableAttributeArray(1);shader_.enableAttributeArray(2);shader_.enableAttributeArray(3);
 shader_.setAttributeBuffer(0,GL_FLOAT,offsetof(ga::Vertex,x),3,sizeof(ga::Vertex));
 shader_.setAttributeBuffer(1,GL_FLOAT,offsetof(ga::Vertex,nx),3,sizeof(ga::Vertex));
 shader_.setAttributeBuffer(2,GL_FLOAT,offsetof(ga::Vertex,r),4,sizeof(ga::Vertex));
 shader_.setAttributeBuffer(3,GL_FLOAT,offsetof(ga::Vertex,mat),1,sizeof(ga::Vertex));
 shader_.release();sunBuffer_.release();sunVao_.release();sunUploadDirty_=false;
}
void Viewport::upload(){
 triangleVertices_=mesh_.triangles.size();lineVertices_=mesh_.lines.size();transparentVertices_=mesh_.transparent.size();
 QVector<ga::Vertex> all;all.reserve(triangleVertices_+lineVertices_+transparentVertices_);
 all.append(mesh_.triangles);all.append(mesh_.lines);all.append(mesh_.transparent);
 vao_.bind();buffer_.bind();buffer_.allocate(all.constData(),int(all.size()*sizeof(ga::Vertex)));
 ++bufferUploads_;uploadedBytes_+=all.size()*sizeof(ga::Vertex);
 shader_.bind();shader_.enableAttributeArray(0);shader_.enableAttributeArray(1);shader_.enableAttributeArray(2);shader_.enableAttributeArray(3);
 shader_.setAttributeBuffer(0,GL_FLOAT,offsetof(ga::Vertex,x),3,sizeof(ga::Vertex));
 shader_.setAttributeBuffer(1,GL_FLOAT,offsetof(ga::Vertex,nx),3,sizeof(ga::Vertex));
 shader_.setAttributeBuffer(2,GL_FLOAT,offsetof(ga::Vertex,r),4,sizeof(ga::Vertex));
 shader_.setAttributeBuffer(3,GL_FLOAT,offsetof(ga::Vertex,mat),1,sizeof(ga::Vertex));
 shader_.release();buffer_.release();vao_.release();uploadDirty_=false;
}
void Viewport::sortTransparent(){
 sortDirty_=false;if(mesh_.transparent.isEmpty())return;
 const auto m=matrix();struct Face{int index;float depth;};QVector<Face> faces;faces.reserve(transparentCenters_.size());
 for(int i=0;i<transparentCenters_.size();++i){const auto &c=transparentCenters_[i];faces.append({i*3,m(2,0)*c.x()+m(2,1)*c.y()+m(2,2)*c.z()});}
 std::stable_sort(faces.begin(),faces.end(),[](const Face &a,const Face &b){return a.depth>b.depth;});
 QVector<ga::Vertex> sorted;sorted.reserve(mesh_.transparent.size());
 for(const auto &face:faces)for(int k=0;k<3;++k)sorted.append(mesh_.transparent[face.index+k]);
 buffer_.bind();const auto bytes=int(sorted.size()*sizeof(ga::Vertex));
 buffer_.write((triangleVertices_+lineVertices_)*int(sizeof(ga::Vertex)),sorted.constData(),bytes);
 buffer_.release();++bufferUploads_;uploadedBytes_+=bytes;
}

void Viewport::renderScene3DToTarget(GLuint targetFbo,int pixelW,int pixelH,const QSize &logicalSize,bool drawBasemap,bool transparentBg){
 if(shadowNeedsResize_)initShadowMap(shadowSize_);
 if(project_&&camera_.mode!="2d"&&dirty_)rebuild();
 if(project_&&camera_.mode!="2d"&&sunDirty_)rebuildSunDome();
 glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);
 glViewport(0,0,pixelW,pixelH);
 if(transparentBg)glClearColor(0,0,0,0);
 else glClearColor(float(palette_.paper.redF()),float(palette_.paper.greenF()),float(palette_.paper.blueF()),1);
 glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
 // Atmospheric Sky Dome Backdrop when Sun 3D + Sky Dome enabled
 if(project_&&camera_.mode!="2d"&&!camera_.exploded&&camera_.solar.enabled&&camera_.solar.skyDome&&!transparentBg&&skyShader_.isLinked()){
  const auto sp=ga::solarPosition(project_->site.lat,camera_.solar.dayOfYear,camera_.solar.hour);
  const float horizonY=std::clamp(float(0.62-(camera_.elevation-18.0)*0.0075),0.15f,0.68f);
  glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDepthMask(GL_FALSE);
  skyShader_.bind();
  skyShader_.setUniformValue("sunAlt",float(std::max(1.0,sp.altitudeDeg)));
  skyShader_.setUniformValue("goldenHour",camera_.solar.goldenHour?1:0);
  skyShader_.setUniformValue("darkUi",darkUi_?1:0);
  skyShader_.setUniformValue("horizonY",horizonY);
  quadVao_.bind();
  glDrawArrays(GL_TRIANGLES,0,3);
  quadVao_.release();
  skyShader_.release();
  glDepthMask(GL_TRUE);
 }
 if(project_&&drawBasemap&&contextVisible_&&graphicsError_.isEmpty()){
  if(contextViewDirty_){basemap_->setCamera(ga::contextCamera(project_->site,sceneCamera(),logicalSize));contextViewDirty_=false;}
  basemap_->initialize(logicalSize,double(pixelW)/std::max(1,logicalSize.width()));
  basemap_->render(logicalSize,double(pixelW)/std::max(1,logicalSize.width()),targetFbo);
  glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);
  glViewport(0,0,pixelW,pixelH);
  glUseProgram(0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
  glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
  glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
  glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glStencilMask(0xFF);glClearStencil(0);
  glDepthRange(0.0,1.0);glClear(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
  // Harmonize light vector basemap softly in Dark Mode when Auto-Dark Basemap is enabled (pure OpenGL 3.3 Core Profile)
  if(darkUi_&&autoDarkBasemap_&&palette_.paper.lightnessF()>=0.4&&tintShader_.isLinked()){
   glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
   glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
   glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
   tintShader_.bind();
   tintShader_.setUniformValue("tintColor",0.075f,0.075f,0.078f,0.52f);
   quadVao_.bind();
   glDrawArrays(GL_TRIANGLES,0,3);
   quadVao_.release();
   tintShader_.release();
   glDisable(GL_BLEND);
   glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
   glDepthMask(GL_TRUE);
  }
 }
 if(camera_.mode!="2d"&&graphicsError_.isEmpty()&&project_){
  if(uploadDirty_)upload();
  if(sortDirty_)sortTransparent();
  if(sunUploadDirty_)uploadSunDome();

  const auto offset=ga::scenePlaneFromMapPoint({},project_->site,camera_);
  const float worldW=float(360*111320*std::cos(project_->site.lat*M_PI/180));
  QVector3D lightDir(-0.4f,-0.65f,1.0f);
  bool sunActive=false;
  float sunAlt=45.0f;
  QMatrix4x4 lightMat;
  bool useShadowMap=false;

  if(camera_.solar.enabled&&!camera_.exploded){
   const auto sp=ga::solarPosition(project_->site.lat,camera_.solar.dayOfYear,camera_.solar.hour);
   if(sp.aboveHorizon){
    sunActive=true;
    lightDir=sp.direction;
    sunAlt=float(sp.altitudeDeg);
    lightMat=sunLightMatrix(lightDir,offset);
    if(shadowFbo_&&shadowTex_&&shadowShader_.isLinked()&&triangleVertices_>0){
     glBindFramebuffer(GL_FRAMEBUFFER,shadowFbo_);
     glViewport(0,0,shadowSize_,shadowSize_);
     glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);
     glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
     glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1.6f,2.8f);
     glClear(GL_DEPTH_BUFFER_BIT);
     shadowShader_.bind();
     shadowShader_.setUniformValue("lightMatrix",lightMat);
     shadowShader_.setUniformValue("renderOffset",QVector2D(offset));
     shadowShader_.setUniformValue("worldWidth",worldW);
     vao_.bind();
     glDrawArrays(GL_TRIANGLES,0,triangleVertices_);
     vao_.release();
     shadowShader_.release();
     glDisable(GL_POLYGON_OFFSET_FILL);
     glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);
     glViewport(0,0,pixelW,pixelH);
     useShadowMap=true;
    }
   }
  }

  const double aspect=double(pixelW)/std::max(1,pixelH);
  const QMatrix4x4 camMat=project_?ga::sceneCameraMatrix(project_->site,sceneCamera(),aspect):camera_.matrix(500,aspect);
  const double el=camera_.elevation*M_PI/180.0,yw=camera_.yaw*M_PI/180.0;
  const QVector3D viewDir(float(std::sin(yw)*std::cos(el)),float(-std::cos(yw)*std::cos(el)),float(std::sin(el)));

  // Optional Half-Resolution Multi-Pass GPU SSAO + Bilateral Depth-Aware Blur (Medium/Ultra tiers)
  bool useSsao=false;
  if(sunActive&&gfxConfig_.ssaoEnabled&&gfxConfig_.ssaoSamples>0&&!camera_.exploded&&
     shadowShader_.isLinked()&&ssaoShader_.isLinked()&&ssaoBlurShader_.isLinked()&&triangleVertices_>0){
   initSsaoBuffers(pixelW/2,pixelH/2);
   bool invOk=false;
   const QMatrix4x4 invCam=camMat.inverted(&invOk);
   if(ssaoReady_&&invOk){
    // Pass A: Half-res city depth pre-pass
    glBindFramebuffer(GL_FRAMEBUFFER,ssaoDepthFbo_);
    glViewport(0,0,ssaoW_,ssaoH_);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    glClear(GL_DEPTH_BUFFER_BIT);
    shadowShader_.bind();
    shadowShader_.setUniformValue("lightMatrix",camMat);
    shadowShader_.setUniformValue("renderOffset",QVector2D(offset));
    shadowShader_.setUniformValue("worldWidth",worldW);
    vao_.bind();
    glDrawArrays(GL_TRIANGLES,0,triangleVertices_);
    vao_.release();
    shadowShader_.release();

    // Pass B: World-space metric hemisphere occlusion
    glBindFramebuffer(GL_FRAMEBUFFER,ssaoFbo_);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,ssaoDepthTex_);
    ssaoShader_.bind();
    ssaoShader_.setUniformValue("depthTex",0);
    ssaoShader_.setUniformValue("invCamMat",invCam);
    ssaoShader_.setUniformValue("viewDir",viewDir);
    ssaoShader_.setUniformValue("texSize",QVector2D(float(ssaoW_),float(ssaoH_)));
    ssaoShader_.setUniformValue("sampleCount",gfxConfig_.ssaoSamples);
    quadVao_.bind();
    glDrawArrays(GL_TRIANGLES,0,3);
    quadVao_.release();
    ssaoShader_.release();

    // Pass C: Bilateral depth-aware blur
    glBindFramebuffer(GL_FRAMEBUFFER,ssaoBlurFbo_);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,ssaoTex_);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,ssaoDepthTex_);
    ssaoBlurShader_.bind();
    ssaoBlurShader_.setUniformValue("aoTex",0);
    ssaoBlurShader_.setUniformValue("depthTex",1);
    ssaoBlurShader_.setUniformValue("texSize",QVector2D(float(ssaoW_),float(ssaoH_)));
    quadVao_.bind();
    glDrawArrays(GL_TRIANGLES,0,3);
    quadVao_.release();
    ssaoBlurShader_.release();

    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
    glBindFramebuffer(GL_FRAMEBUFFER,targetFbo);
    glViewport(0,0,pixelW,pixelH);
    glDepthMask(GL_TRUE);
    useSsao=true;
   }
  }

  glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,transparentBg?GL_TRUE:GL_FALSE);
  shader_.bind();
  shader_.setUniformValue("matrix",camMat);
  shader_.setUniformValue("lightMatrix",lightMat);
  shader_.setUniformValue("sunDir",lightDir);
  shader_.setUniformValue("viewDir",viewDir);
  shader_.setUniformValue("sunMode",sunActive?1:0);
  shader_.setUniformValue("hasShadowMap",useShadowMap?1:0);
  shader_.setUniformValue("goldenHour",camera_.solar.goldenHour?1:0);
  shader_.setUniformValue("pcfQuality",gfxConfig_.pcfQuality);
  shader_.setUniformValue("creaseMode",gfxConfig_.architecturalCrease?1:0);
  shader_.setUniformValue("useSsao",useSsao?1:0);
  shader_.setUniformValue("skyDomeOn",(camera_.solar.skyDome&&!transparentBg)?1:0);
  shader_.setUniformValue("darkUi",darkUi_?1:0);
  shader_.setUniformValue("sunAlt",sunAlt);
  shader_.setUniformValue("shadowStrength",float(camera_.solar.shadowStrength));
  shader_.setUniformValue("viewportSize",QVector2D(float(pixelW),float(pixelH)));
  shader_.setUniformValue("renderOffset",QVector2D(offset));
  shader_.setUniformValue("worldWidth",worldW);
  if(useShadowMap){
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D,shadowTex_);
   shader_.setUniformValue("shadowMap",1);
  }
  if(useSsao){
   glActiveTexture(GL_TEXTURE2);
   glBindTexture(GL_TEXTURE_2D,ssaoBlurTex_);
   shader_.setUniformValue("ssaoMap",2);
  }
  // Draw static city geometry
  vao_.bind();
  glDrawArrays(GL_TRIANGLES,0,triangleVertices_);
  glEnable(GL_BLEND);
  if(transparentBg)glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
  else glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
  glDrawArrays(GL_LINES,triangleVertices_,lineVertices_);
  vao_.release();

  // Drape Hanoi Land-Use Planning Tiles (quyhoach.hanoi.vn) directly on 3D ground plane under 3D buildings with live shadows!
  if(hanoiPlanEnabled_&&hanoiComposite_.valid()&&!camera_.exploded&&planTexShader_.isLinked()){
   if(hanoiPlanTexDirty_)uploadHanoiPlanTexture();
   if(hanoiPlanTex_&&planTexVerts_>0){
    shader_.release();
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    if(transparentBg)glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    else glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-2.0f,-2.0f);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,hanoiPlanTex_);
    if(useShadowMap){glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,shadowTex_);}
    planTexShader_.bind();
    planTexShader_.setUniformValue("matrix",camMat);
    planTexShader_.setUniformValue("lightMatrix",lightMat);
    planTexShader_.setUniformValue("renderOffset",QVector2D(offset));
    planTexShader_.setUniformValue("worldWidth",worldW);
    planTexShader_.setUniformValue("planTex",0);
    planTexShader_.setUniformValue("shadowMap",1);
    planTexShader_.setUniformValue("hasShadowMap",useShadowMap?1:0);
    planTexShader_.setUniformValue("opacity",float(hanoiPlanOpacity_));
    planTexShader_.setUniformValue("shadowStrength",float(camera_.solar.shadowStrength));
    planTexVao_.bind();
    glDrawArrays(GL_TRIANGLES,0,planTexVerts_);
    planTexVao_.release();
    planTexShader_.release();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
    shader_.bind();
   }
  }

  if(transparentVertices_){
   vao_.bind();
   glEnable(GL_BLEND);
   if(transparentBg)glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
   else glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
   glDepthMask(GL_FALSE);
   glDrawArrays(GL_TRIANGLES,triangleVertices_+lineVertices_,transparentVertices_);
   glDepthMask(GL_TRUE);
   vao_.release();
  }

  // Draw dynamic lightweight Sun-Path Dome (rebuilt in <0.1ms when scrubbing Sun slider!)
  if(sunActive&&camera_.solar.showPath&&(sunTriVerts_+sunLineVerts_+sunTransVerts_)>0){
   sunVao_.bind();
   glDisable(GL_BLEND);
   if(sunTriVerts_>0)glDrawArrays(GL_TRIANGLES,0,sunTriVerts_);
   glEnable(GL_BLEND);
   if(transparentBg)glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
   else glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
   if(sunLineVerts_>0)glDrawArrays(GL_LINES,sunTriVerts_,sunLineVerts_);
   if(sunTransVerts_>0){
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES,sunTriVerts_+sunLineVerts_,sunTransVerts_);
    glDepthMask(GL_TRUE);
   }
   sunVao_.release();
  }

  glDisable(GL_BLEND);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  shader_.release();
  if(useSsao){glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,0);}
  if(useShadowMap){glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);}
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_DEPTH_TEST);
 }
 glUseProgram(0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
 glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
 glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);
 glDisable(GL_BLEND);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glStencilMask(0xFF);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
}

void Viewport::paintGL(){
 QPainter painter(this);
 painter.beginNativePainting();
 const int pw=qRound(width()*devicePixelRatioF()),ph=qRound(height()*devicePixelRatioF());
 renderScene3DToTarget(defaultFramebufferObject(),pw,ph,size(),true,false);
 painter.endNativePainting();
 painter.setRenderHint(QPainter::Antialiasing);

 if(camera_.mode=="2d"&&project_){
  const auto pixelSize=QSize(qRound(width()*devicePixelRatioF()),qRound(height()*devicePixelRatioF()));
  if(planDirty_||planImage_.size()!=pixelSize){
   if(planCacheDirty_){planCache_=preparePlan(*project_,layers_,diagram_);planCacheDirty_=false;}
   planImage_=QImage(pixelSize,QImage::Format_ARGB32_Premultiplied);
   planImage_.setDevicePixelRatio(devicePixelRatioF());
   planImage_.fill(contextVisible_?QColor(Qt::transparent):palette_.paper);
   QPainter plan(&planImage_);++planPaints_;
   const auto m=matrix();
   const auto point=[&](QPointF v){const auto plane=ga::scenePlanePoint(v,project_->site,camera_);const auto clip=m*QVector4D(float(plane.x()),float(plane.y()),0,1);return QPointF((clip.x()/clip.w()+1)*width()/2.,(1-clip.y()/clip.w())*height()/2.);};
   const double ppm=QLineF(point({0,0}),point({1,0})).length();
   const auto offset=ga::scenePlaneFromMapPoint({},project_->site,camera_);
   const double period=360*111320*std::cos(project_->site.lat*M_PI/180);
   if(std::abs(offset.x())+ga::envelopeRadius(project_->site)*2<period/2){
    if(planGeometry_.paths.size()!=project_->features.size())planGeometry_=preparePlanGeometry(*project_);
    const double w=m(3,3);
    const QTransform transform(m(0,0)*width()/(2*w),-m(1,0)*height()/(2*w),m(0,1)*width()/(2*w),-m(1,1)*height()/(2*w),width()/2.+(m(0,3)+m(0,0)*offset.x()+m(0,1)*offset.y())*width()/(2*w),height()/2.-(m(1,3)+m(1,0)*offset.x()+m(1,1)*offset.y())*height()/(2*w));
    if(!interacting_||!interactionImage_.reusable(transform,size(),devicePixelRatioF())){
     const auto imageTransform=interactionImage_.begin(size(),devicePixelRatioF(),transform,contextVisible_?QColor(Qt::transparent):palette_.paper);
     QPainter retained(&interactionImage_.image);
     paintPlanMercator(retained,*project_,layers_,palette_,diagram_,imageTransform,ppm,selected_,camera_.grid,planCache_,planGeometry_);
     retained.end();
    }
    interactionImage_.draw(plan,transform);
   }else paintPlan(plan,*project_,layers_,palette_,diagram_,point,ppm,true,selected_,camera_.grid,&planCache_);
   plan.end();planDirty_=false;
  }
  painter.drawImage(QPointF(0,0),planImage_);
 }

 const auto pixelSize=QSize(qRound(width()*devicePixelRatioF()),qRound(height()*devicePixelRatioF()));
 QImage hud(pixelSize,QImage::Format_ARGB32_Premultiplied);
 hud.setDevicePixelRatio(devicePixelRatioF());
 hud.fill(Qt::transparent);
 QPainter ui(&hud);
 ui.setRenderHint(QPainter::Antialiasing);
 drawOverlay(ui);
 ui.end();
 painter.drawImage(QPointF(0,0),hud);
}

void Viewport::drawOverlay(QPainter &p){
 const auto tr=[&](const char *vi,const char *en){return QString::fromUtf8(english_?en:vi);};
 const bool dark=darkUi_||palette_.paper.lightnessF()<.4;
 const QColor ink=dark?QColor("#ebebeb"):QColor("#1f1f1f"),muted=dark?QColor("#969696"):QColor("#6e6e6e");
 const QColor glassFill=dark?QColor(26,26,26,238):QColor(255,255,255,244);
 const QColor glassBorder=dark?QColor(62,62,62,225):QColor(220,220,220,242);
 const QColor accentCol=dark?QColor("#f5f5f5"):QColor("#131313");

 const auto m=matrix();
 auto worldToScreen=[&](const QPointF &local,double z=0.4,bool *ok=nullptr){
  const auto plane=project_?ga::scenePlanePoint(local,project_->site,camera_):local;
  const auto clip=m*QVector4D(float(plane.x()),float(plane.y()),float(z),1.0f);
  if(ok)*ok=(clip.w()>0.001f);
  const float inv=clip.w()>0.001f?1.0f/clip.w():1.0f;
  return QPointF((clip.x()*inv+1.0)*width()*0.5,(1.0-clip.y()*inv)*height()*0.5);
 };

 // 00. Live Hanoi Land-Use Planning Tiles (quyhoach.hanoi.vn) in 2D Plan mode (draped cleanly over clipped site)
 if(camera_.mode=="2d"&&project_&&hanoiPlanEnabled_&&hanoiComposite_.valid()){
  bool o0=false,o1=false,o2=false,o3=false;
  const QPolygonF src{{0,0},{double(hanoiComposite_.image.width()),0},{double(hanoiComposite_.image.width()),double(hanoiComposite_.image.height())},{0,double(hanoiComposite_.image.height())}};
  const QPolygonF dst{
   worldToScreen(hanoiComposite_.topLeft,0.2,&o0),
   worldToScreen(hanoiComposite_.topRight,0.2,&o1),
   worldToScreen(hanoiComposite_.bottomRight,0.2,&o2),
   worldToScreen(hanoiComposite_.bottomLeft,0.2,&o3)
  };
  QTransform q2q;
  if(o0&&o1&&o2&&o3&&QTransform::quadToQuad(src,dst,q2q)){
   p.save();
   QPainterPath clipSite;
   QPolygonF sp;for(const auto &v:project_->site.boundary)sp.append(worldToScreen(v));
   clipSite.addPolygon(sp);p.setClipPath(clipSite);
   p.setOpacity(hanoiPlanOpacity_);
   p.setRenderHint(QPainter::SmoothPixmapTransform,true);
   p.setTransform(q2q,true);
   p.drawImage(0,0,hanoiComposite_.image);
   p.restore();
  }
 }

 // 0. Georeferenced Planning Map Image Overlay (PNG/JPG draped onto 2D & 3D ground plane!)
 if(project_&&planningImage_.valid()){
  const double wM=planningImage_.widthM;
  const double aspect=double(planningImage_.image.height())/std::max(1,planningImage_.image.width());
  const double hM=wM*aspect;
  const double rad=planningImage_.rotationDeg*M_PI/180.0;
  const double cs=std::cos(rad),sn=std::sin(rad);
  auto imgPt=[&](double lx,double ly){
   return QPointF(planningImage_.centerX+lx*cs-ly*sn,planningImage_.centerY+lx*sn+ly*cs);
  };
  bool o0=false,o1=false,o2=false,o3=false;
  QPolygonF src{{0,0},{double(planningImage_.image.width()),0},{double(planningImage_.image.width()),double(planningImage_.image.height())},{0,double(planningImage_.image.height())}};
  QPolygonF dst{
   worldToScreen(imgPt(-wM*0.5,hM*0.5),0.22,&o0),
   worldToScreen(imgPt(wM*0.5,hM*0.5),0.22,&o1),
   worldToScreen(imgPt(wM*0.5,-hM*0.5),0.22,&o2),
   worldToScreen(imgPt(-wM*0.5,-hM*0.5),0.22,&o3)
  };
  QTransform q2q;
  if(o0&&o1&&o2&&o3&&QTransform::quadToQuad(src,dst,q2q)){
   p.save();
   QPainterPath clipSite;
   QPolygonF sp;for(const auto &v:project_->site.boundary)sp.append(worldToScreen(v));
   clipSite.addPolygon(sp);p.setClipPath(clipSite);
   p.setOpacity(planningImage_.opacity);
   p.setRenderHint(QPainter::SmoothPixmapTransform,true);
   p.setTransform(q2q,true);
   p.drawImage(0,0,planningImage_.image);
   p.restore();
  }
 }

 // 0B. Future Planning Synchronization Overlay (Hanoi QHC 2045 / QHPK 1/2000 TT 04/2022 / QHCT 1/500 / Compare Diff)
 if(project_&&planningOverlay_.valid()){
  p.save();
  QPainterPath clipSite;
  QPolygonF sp;for(const auto &v:project_->site.boundary)sp.append(worldToScreen(v));
  clipSite.addPolygon(sp);
  p.setClipPath(clipSite);

  const double op=std::clamp(planningOverlay_.opacity,0.15,1.0);
  // Zoning Polygons (TT 04/2022/TT-BXD)
  if(planningOverlay_.levelId=="qhpk-2000"||planningOverlay_.levelId=="qhct-500"){
   for(const auto &z:planningOverlay_.zones){
    if(z.ring.size()<3)continue;
    QPolygonF polyS;
    for(const auto &v:z.ring)polyS.append(worldToScreen(v,0.35));
    QColor fill=z.fill;
    fill.setAlphaF(float(op*(planningOverlay_.levelId=="qhct-500"?0.22:0.46)));
    QColor stroke=z.stroke;
    stroke.setAlphaF(float(std::min(1.0,op+0.12)));
    p.setPen(QPen(stroke,z.source=="imported_dxf"||z.source=="imported_geojson"?2.2:1.5,z.source=="hanoi_masterplan"?Qt::DashLine:Qt::SolidLine));
    p.setBrush(fill);
    p.drawPolygon(polyS);
    if(z.source!="osm_inferred"&&polyS.boundingRect().width()>44.0){
     const QPointF cS=polyS.boundingRect().center();
     const QString lbl=z.maxHeightM>0.5?QString("%1 · ≤%2m").arg(z.code).arg(qRound(z.maxHeightM)):z.code;
     p.setFont(QFont("Segoe UI",7.4,QFont::Bold));
     const int lw=p.fontMetrics().horizontalAdvance(lbl)+10;
     const QRectF lb(cS.x()-lw*0.5,cS.y()-8.5,lw,17);
     p.setPen(QPen(stroke,0.9));p.setBrush(dark?QColor(22,20,18,215):QColor(255,253,249,225));
     p.drawRoundedRect(lb,5,5);
     p.setPen(dark?QColor("#faf8f5"):stroke.darker(130));
     p.drawText(lb,Qt::AlignCenter,lbl);
    }
   }
  }

  // Existing vs Future Diff Buildings
  if(!planningOverlay_.diffBuildings.isEmpty()){
   for(const auto &[ring,col]:planningOverlay_.diffBuildings){
    if(ring.size()<3)continue;
    QPolygonF polyS;
    for(const auto &v:ring)polyS.append(worldToScreen(v,0.45));
    QColor f=col;f.setAlphaF(float(f.alphaF()*op));
    p.setPen(QPen(col.darker(120),1.1));
    p.setBrush(f);
    p.drawPolygon(polyS);
   }
  }

  // Future Corridors (Metro Lines, Ring Roads, Future Bridges, Red River Axis, OSM Proposed)
  for(const auto &cor:planningOverlay_.corridors){
   if(cor.polyline.size()<2)continue;
   QPolygonF lineS;
   for(const auto &v:cor.polyline)lineS.append(worldToScreen(v,cor.underground?1.2:2.0));
   QColor halo=cor.color;halo.setAlphaF(float(0.28*op));
   p.setBrush(Qt::NoBrush);
   p.setPen(QPen(halo,cor.kind=="river_axis"?12.0:9.0,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
   p.drawPolyline(lineS);
   QPen corePen(cor.color,cor.kind=="river_axis"?4.0:3.2,cor.underground?Qt::DashLine:Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
   if(cor.underground)corePen.setDashPattern({5.0,3.5});
   p.setPen(corePen);
   p.drawPolyline(lineS);
   // White inner rail ticks for Metro corridors
   if(cor.kind=="metro"){
    QPen inner(Qt::white,1.2,Qt::DashLine);
    inner.setDashPattern({3.0,5.0});
    p.setPen(inner);
    p.drawPolyline(lineS);
   }
  }
  p.restore();

  // Metro Station Rings & Callout Pills (unclipped so pills stay readable near site border)
  p.save();
  for(const auto &st:planningOverlay_.stations){
   bool ok=false;
   const QPointF ptS=worldToScreen(st.pos,camera_.mode=="2d"?1.5:22.0,&ok);
   if(!ok||!rect().adjusted(-40,-40,40,40).contains(ptS.toPoint()))continue;
   if(camera_.mode!="2d"){
    const QPointF groundS=worldToScreen(st.pos,1.2);
    p.setPen(QPen(st.color,1.4,Qt::DashLine));
    p.drawLine(groundS,ptS);
   }
   QColor glow=st.color;glow.setAlpha(70);
   p.setPen(Qt::NoPen);p.setBrush(glow);p.drawEllipse(ptS,11.5,11.5);
   p.setPen(QPen(Qt::white,2.0));p.setBrush(st.color);p.drawEllipse(ptS,6.2,6.2);
   p.setFont(QFont("Segoe UI",7.6,QFont::Bold));
   const QString txt=QString("%1 · %2").arg(st.code,st.name);
   const int tw=p.fontMetrics().horizontalAdvance(txt)+14;
   const QRectF pill(std::clamp(ptS.x()+10.0,12.0,width()-tw-12.0),ptS.y()-10.0,tw,20.0);
   p.setPen(QPen(st.color,1.1));p.setBrush(QColor(22,20,26,232));
   p.drawRoundedRect(pill,10,10);
   p.setPen(QColor("#faf8f5"));
   p.drawText(pill,Qt::AlignCenter,txt);
  }
  p.restore();

  // Bottom-center glassmorphic Planning Legend & Legal Subdivision Card
  const double legendBottom=hudDockVisible_&&width()>=520?136.0:106.0;
  const double cardW=std::min(460.0,std::max(320.0,width()-48.0));
  const QRectF card(std::max(16.0,(width()-cardW)*0.5),height()-legendBottom,cardW,68.0);
  p.save();
  p.setPen(Qt::NoPen);p.setBrush(dark?QColor(0,0,0,90):QColor(26,25,24,26));p.drawRoundedRect(card.translated(0,2.5),12,12);
  p.setPen(QPen(glassBorder,1.0));p.setBrush(glassFill);p.drawRoundedRect(card,12,12);
  p.setPen(ink);p.setFont(QFont("Segoe UI",8.3,QFont::DemiBold));
  p.drawText(card.adjusted(12,6,-12,-42),Qt::AlignLeft|Qt::AlignVCenter,english_?planningOverlay_.titleEn:planningOverlay_.titleVi);
  // Multi-swatch legend strip
  double lx=card.left()+12.0;
  const double ly=card.top()+28.0;
  p.setFont(QFont("Segoe UI",7.1,QFont::Medium));
  for(const auto &[swCol,swLbl]:planningOverlay_.legendItems){
   const int lw=p.fontMetrics().horizontalAdvance(swLbl);
   if(lx+13+lw>card.right()-10.0)break;
   p.setPen(Qt::NoPen);p.setBrush(swCol);p.drawRoundedRect(QRectF(lx,ly+2.5,9,9),2.5,2.5);
   p.setPen(ink);p.drawText(QRectF(lx+12,ly,lw+4,14),Qt::AlignLeft|Qt::AlignVCenter,swLbl);
   lx+=12+lw+11;
  }
  p.setPen(muted);p.setFont(QFont("Segoe UI",7.2));
  p.drawText(card.adjusted(12,45,-12,-4),Qt::AlignLeft|Qt::AlignVCenter,
             p.fontMetrics().elidedText(english_?planningOverlay_.summaryEn:planningOverlay_.summaryVi,Qt::ElideRight,int(card.width()-24)));
  p.restore();
 }

 // 1. Active Analytical & Environmental Overlay
 if(project_&&overlay_.valid()){
  if(!overlay_.heatmap.isNull()&&overlay_.worldBounds.isValid()){
   const QRectF wb=overlay_.worldBounds;
   if(camera_.mode=="2d"){
    const QPointF tl=worldToScreen({wb.left(),wb.bottom()});
    const QPointF br=worldToScreen({wb.right(),wb.top()});
    p.save();
    QPainterPath clipSite;
    QPolygonF sp;for(const auto &v:project_->site.boundary)sp.append(worldToScreen(v));
    clipSite.addPolygon(sp);p.setClipPath(clipSite);
    p.setOpacity(0.76);p.setRenderHint(QPainter::SmoothPixmapTransform,true);
    p.drawImage(QRectF(tl,br).normalized(),overlay_.heatmap);
    p.restore();
   }else{
    QPolygonF src{{0,0},{double(overlay_.heatmap.width()),0},{double(overlay_.heatmap.width()),double(overlay_.heatmap.height())},{0,double(overlay_.heatmap.height())}};
    bool o0=false,o1=false,o2=false,o3=false;
    QPolygonF dst{worldToScreen({wb.left(),wb.bottom()},0.35,&o0),worldToScreen({wb.right(),wb.bottom()},0.35,&o1),worldToScreen({wb.right(),wb.top()},0.35,&o2),worldToScreen({wb.left(),wb.top()},0.35,&o3)};
    QTransform q2q;
    if(o0&&o1&&o2&&o3&&QTransform::quadToQuad(src,dst,q2q)){
     p.save();p.setOpacity(0.70);p.setTransform(q2q,true);p.setRenderHint(QPainter::SmoothPixmapTransform,true);p.drawImage(0,0,overlay_.heatmap);p.restore();
    }
   }
  }
  if(!overlay_.contours.isEmpty()){
   p.save();p.setPen(QPen(QColor(82,58,36,165),1.1));
   for(const auto &seg:overlay_.contours){
    if(seg.size()>=2)p.drawLine(worldToScreen(seg[0],0.5),worldToScreen(seg[1],0.5));
   }
   p.restore();
  }
  if(!overlay_.shadowPolygons.isEmpty()){
   p.save();
   QPainterPath clipSite;QPolygonF sp;for(const auto &v:project_->site.boundary)sp.append(worldToScreen(v));
   clipSite.addPolygon(sp);p.setClipPath(clipSite);
   p.setPen(Qt::NoPen);p.setBrush(QColor(26,38,56,110));
   for(const auto &hull:overlay_.shadowPolygons){
    QPolygonF poly;for(const auto &pt:hull)poly.append(worldToScreen(pt,0.45));
    p.drawPolygon(poly);
   }
   p.restore();
  }
  if(overlay_.showSunpath2d){
   p.save();
   const QPointF sc=worldToScreen({0,0},0.6);
   const double rPx=QLineF(sc,worldToScreen({project_->site.radius*1.04,0},0.6)).length();
   // Concentric altitude rings (15..75 deg)
   p.setBrush(Qt::NoBrush);
   for(int alt=15;alt<=75;alt+=15){
    const double rr=rPx*std::cos(alt*M_PI/180.0);
    p.setPen(QPen(QColor(115,118,125,alt%30==0?140:80),alt%30==0?1.1:0.7,alt%30==0?Qt::SolidLine:Qt::DashLine));
    p.drawEllipse(sc,rr,rr);
   }
   p.setPen(QPen(QColor("#27272a"),1.5));p.drawEllipse(sc,rPx,rPx);
   // Azimuth spokes
   for(int az=0;az<360;az+=30){
    const double a=az*M_PI/180.0;
    p.setPen(QPen(QColor(130,132,138,75),0.8));
    p.drawLine(sc,sc+QPointF(std::sin(a)*rPx,-std::cos(a)*rPx));
   }
   // Summer / Equinox / Winter / Active polar arcs
   const QVector<QPair<int,QColor>> arcs={{172,QColor("#e59824")},{80,QColor("#a84438")},{355,QColor("#3b82f6")},{overlay_.sunState.dayOfYear,QColor("#ea580c")}};
   for(int ai=0;ai<arcs.size();++ai){
    QPolygonF poly;
    for(int s=0;s<=48;++s){
     const auto sp=ga::solarPosition(project_->site.lat,arcs[ai].first,4.5+15.0*s/48.0);
     if(sp.altitudeDeg<0)continue;
     const double rr=rPx*std::cos(sp.altitudeDeg*M_PI/180.0),az=sp.azimuthDeg*M_PI/180.0;
     poly.append(sc+QPointF(std::sin(az)*rr,-std::cos(az)*rr));
    }
    p.setPen(QPen(arcs[ai].second,ai==3?2.8:1.8));
    p.drawPolyline(poly);
   }
   const auto curSun=ga::solarPosition(project_->site.lat,overlay_.sunState.dayOfYear,overlay_.sunState.hour);
   if(curSun.aboveHorizon){
    const double rr=rPx*std::cos(curSun.altitudeDeg*M_PI/180.0),az=curSun.azimuthDeg*M_PI/180.0;
    const QPointF sunPt=sc+QPointF(std::sin(az)*rr,-std::cos(az)*rr);
    p.setPen(QPen(QColor("#ea580c"),1.6,Qt::DashLine));p.drawLine(sc,sunPt);
    p.setPen(QPen(Qt::white,2));p.setBrush(QColor("#f59e0b"));p.drawEllipse(sunPt,7.5,7.5);
   }
   p.setFont(QFont("Segoe UI",8.5,QFont::Bold));p.setPen(QColor("#18181b"));
   const QVector<QPair<QString,double>> dirs={{"0° N",0},{"90° E",90},{"180° S",180},{"270° W",270}};
   for(const auto &[lbl,az]:dirs){
    const double a=az*M_PI/180.0;
    const QPointF lp=sc+QPointF(std::sin(a)*(rPx+18),-std::cos(a)*(rPx+18));
    p.drawText(QRectF(lp.x()-24,lp.y()-9,48,18),Qt::AlignCenter,lbl);
   }
   p.restore();
  }
  if(!overlay_.windRose16.isEmpty()){
   p.save();
   const QPointF sc=worldToScreen({0,0},0.6);
   const double rPx=std::min(220.0,std::max(110.0,QLineF(sc,worldToScreen({project_->site.radius*0.85,0},0.6)).length()));
   for(double f:{0.25,0.5,0.75,1.0}){
    p.setPen(QPen(QColor(90,115,140,95),0.9,Qt::DashLine));p.setBrush(Qt::NoBrush);
    p.drawEllipse(sc,rPx*f,rPx*f);
   }
   for(int i=0;i<16;++i){
    const double v=std::clamp(overlay_.windRose16[i],0.05,1.0);
    const double a0=(i*22.5-9.5)*M_PI/180.0,a1=(i*22.5+9.5)*M_PI/180.0;
    const double rr=rPx*v;
    QPolygonF wedge{sc,sc+QPointF(std::sin(a0)*rr,-std::cos(a0)*rr),sc+QPointF(std::sin(a1)*rr,-std::cos(a1)*rr)};
    QColor col=QColor::fromRgbF(0.08+0.12*(1-v),0.45+0.25*(1-v),0.72+0.15*v,0.62+0.25*v);
    p.setPen(QPen(QColor("#0369a1"),1.0));p.setBrush(col);p.drawPolygon(wedge);
   }
   p.setFont(QFont("Segoe UI",8.5,QFont::Bold));p.setPen(QColor("#0c4a6e"));
   const QVector<QPair<QString,double>> dirs={{"N",0},{"NE",45},{"E",90},{"SE",135},{"S",180},{"SW",225},{"W",270},{"NW",315}};
   for(const auto &[lbl,az]:dirs){
    const double a=az*M_PI/180.0;
    const QPointF lp=sc+QPointF(std::sin(a)*(rPx+15),-std::cos(a)*(rPx+15));
    p.drawText(QRectF(lp.x()-16,lp.y()-9,32,18),Qt::AlignCenter,lbl);
   }
   p.restore();
  }
  if(!overlay_.isovistPolygon.isEmpty()){
   QPolygonF poly;for(const auto &v:overlay_.isovistPolygon)poly.append(worldToScreen(v,0.8));
   p.save();p.setPen(QPen(QColor("#c45632"),1.8));p.setBrush(QColor(224,132,78,68));p.drawPolygon(poly);
   const QPointF vp=worldToScreen(overlay_.viewpoint,1.0);
   p.setPen(QPen(Qt::white,2));p.setBrush(QColor("#c45632"));p.drawEllipse(vp,5.5,5.5);p.restore();
  }
  if(!overlay_.walkSegments.isEmpty()){
   p.save();
   for(const auto &seg:overlay_.walkSegments){
    const QColor col=seg.customColor.isValid()?seg.customColor:(seg.minutes<=5.0?QColor("#1e8e5a"):(seg.minutes<=10.0?QColor("#d99026"):QColor("#c74c38")));
    p.setPen(QPen(col,seg.customColor.isValid()?2.8:(seg.minutes<=5.0?3.0:2.2),Qt::SolidLine,Qt::RoundCap));
    p.drawLine(worldToScreen(seg.a,0.7),worldToScreen(seg.b,0.7));
   }
   if(overlay_.id=="walkshed"){
    const QPointF vp=worldToScreen(overlay_.viewpoint,1.2);
    p.setPen(QPen(Qt::white,2));p.setBrush(QColor("#1a1918"));p.drawEllipse(vp,5.5,5.5);
   }
   p.restore();
  }
  if(!overlay_.streamlines.isEmpty()){
   p.save();p.setPen(QPen(QColor(18,128,158,195),1.7,Qt::SolidLine,Qt::RoundCap));p.setBrush(Qt::NoBrush);
   for(const auto &line:overlay_.streamlines){
    QPolygonF sPoly;for(const auto &pt:line)sPoly.append(worldToScreen(pt,4.0));
    p.drawPolyline(sPoly);
    if(sPoly.size()>=4){
     const QPointF a=sPoly[sPoly.size()/2-1],b=sPoly[sPoly.size()/2];
     const QPointF d=b-a;const double len=std::hypot(d.x(),d.y());
     if(len>2.0){const QPointF u=d/len,n(-u.y(),u.x());p.drawLine(b,b-u*6+n*3.5);p.drawLine(b,b-u*6-n*3.5);}
    }
   }
   p.restore();
  }
  const double legendBottom=hudDockVisible_&&width()>=520?134.0:104.0;
  const QRectF card(std::max(16.0,width()*0.5-190.0),height()-legendBottom,380.0,64.0);
  p.save();
  p.setPen(Qt::NoPen);p.setBrush(dark?QColor(0,0,0,85):QColor(26,25,24,24));p.drawRoundedRect(card.translated(0,2.5),12,12);
  p.setPen(QPen(glassBorder,1));p.setBrush(glassFill);p.drawRoundedRect(card,12,12);
  p.setPen(ink);p.setFont(QFont("Segoe UI",8.5,QFont::DemiBold));p.drawText(card.adjusted(12,6,-12,-36),Qt::AlignLeft,english_?overlay_.titleEn:overlay_.titleVi);
  if(!overlay_.legendStops.isEmpty()){
   QLinearGradient grad(card.left()+12,0,card.right()-12,0);
   for(int i=0;i<overlay_.legendStops.size();++i)grad.setColorAt(double(i)/std::max(1,int(overlay_.legendStops.size())-1),overlay_.legendStops[i]);
   p.setPen(Qt::NoPen);p.setBrush(grad);p.drawRoundedRect(QRectF(card.left()+12,card.top()+27,card.width()-24,8),4,4);
  }
  p.setPen(muted);p.setFont(QFont("Segoe UI",7.5));p.drawText(card.adjusted(12,39,-12,-4),Qt::AlignLeft,english_?overlay_.summaryEn:overlay_.summaryVi);
  p.restore();
 }

 // 2. 3D Solar Dome Atmospheric Halo + Cardinal & Hourly Floating Labels
 if(project_&&camera_.mode!="2d"&&!camera_.exploded&&camera_.solar.enabled&&camera_.solar.showPath){
  const double domeR=std::max(120.,project_->site.radius*1.08);
  const auto sun=ga::solarPosition(project_->site.lat,camera_.solar.dayOfYear,camera_.solar.hour);
  p.save();
  // Horizon cardinal labels (N / E / S / W)
  p.setFont(QFont("Segoe UI",8,QFont::Bold));
  const QVector<QPair<QString,QPointF>> cardinals={
   {tr("BẮC","N"),{0,domeR*1.07}},{tr("ĐÔNG","E"),{domeR*1.07,0}},
   {tr("NAM","S"),{0,-domeR*1.07}},{tr("TÂY","W"),{-domeR*1.07,0}},
  };
  for(const auto &[lbl,pos]:cardinals){
   bool ok=false;const QPointF sPt=worldToScreen(pos,2.0,&ok);
   if(!ok)continue;
   const QRectF b(sPt.x()-18,sPt.y()-9,36,18);
   p.setPen(QPen(lbl==tr("BẮC","N")?QColor("#d96b38"):QColor("#78716c"),1));
   p.setBrush(dark?QColor(29,28,26,225):QColor(255,255,255,228));
   p.drawRoundedRect(b,5,5);
   p.setPen(lbl==tr("BẮC","N")?QColor("#d96b38"):ink);
   p.drawText(b,Qt::AlignCenter,lbl);
  }
  // Hourly tick badges along active day arc
  p.setFont(QFont("Segoe UI",7.2,QFont::DemiBold));
  for(int h:{6,9,12,15,18}){
   const auto hp=ga::solarPosition(project_->site.lat,camera_.solar.dayOfYear,double(h));
   if(!hp.aboveHorizon)continue;
   const QVector3D v3=hp.direction*float(domeR*1.045);
   bool ok=false;const QPointF sPt=worldToScreen({v3.x(),v3.y()},v3.z(),&ok);
   if(!ok)continue;
   const QRectF b(sPt.x()-16,sPt.y()-8,32,16);
   p.setPen(QPen(QColor(232,126,56,190),1));
   p.setBrush(dark?QColor(28,25,22,220):QColor(255,250,242,228));
   p.drawRoundedRect(b,4,4);
   p.setPen(QColor("#b84b1f"));
   p.drawText(b,Qt::AlignCenter,QString("%1h").arg(h,2,10,QChar('0')));
  }
  // Soft radial golden sun glow + live readout pill near the Sun sphere
  if(sun.aboveHorizon){
   const QVector3D sp3=sun.direction*float(domeR);
   bool ok=false;const QPointF sPt=worldToScreen({sp3.x(),sp3.y()},sp3.z(),&ok);
   if(ok){
    QRadialGradient glow(sPt,42);
    glow.setColorAt(0.0,QColor(255,246,196,175));
    glow.setColorAt(0.35,QColor(255,186,74,105));
    glow.setColorAt(1.0,QColor(255,140,40,0));
    p.setPen(Qt::NoPen);p.setBrush(glow);p.drawEllipse(sPt,42,42);
    const int hh=int(std::floor(camera_.solar.hour)),mm=int(qRound((camera_.solar.hour-hh)*60.0));
    const QString tag=QString("%1:%2 · %3°").arg(hh,2,10,QChar('0')).arg(mm,2,10,QChar('0')).arg(sun.altitudeDeg,0,'f',1);
    p.setFont(QFont("Segoe UI",8,QFont::Bold));
    const int tw=p.fontMetrics().horizontalAdvance(tag)+14;
    const QRectF pill(sPt.x()+16,sPt.y()-11,tw,21);
    p.setPen(QPen(QColor("#e07c2e"),1.2));
    p.setBrush(QColor(29,24,19,225));
    p.drawRoundedRect(pill,10,10);
    p.setPen(QColor("#ffd88a"));
    p.drawText(pill,Qt::AlignCenter,tag);
   }
  }
  p.restore();
 }

 // 3. Design Plot Overlay (Rectangle OR Freehand/Polygon) + Live Freehand Drawing Draft
 if(project_&&plot_.active&&!drawingPlot_){
  const QPolygonF polyW=plot_.polygon();
  QPolygonF polyS;
  for(const auto &v:polyW)polyS.append(worldToScreen(v,0.8));
  p.save();
  p.setPen(QPen(plot_.strokeColor,2.3,Qt::DashLine));
  QColor fill=plot_.strokeColor;fill.setAlpha(46);p.setBrush(fill);
  p.drawPolygon(polyS);
  // Vertex corner dots (interactive draggable handles!)
  for(int vi=0;vi<polyS.size();++vi){
   const bool hi=(vi==hoveredPlotVertex_||vi==dragPlotVertex_);
   if(hi){
    p.setPen(QPen(plot_.strokeColor,2.0));p.setBrush(Qt::white);
    p.drawEllipse(polyS[vi],6.2,6.2);
   }else{
    p.setPen(QPen(Qt::white,1.5));p.setBrush(plot_.strokeColor);
    p.drawEllipse(polyS[vi],3.8,3.8);
   }
  }

  if(plot_.showDimensions&&polyS.size()>=3){
   p.setFont(QFont("Segoe UI",7.8,QFont::DemiBold));
   const int maxLabels=std::min(int(polyW.size()),14);
   for(int i=0;i<maxLabels;++i){
    const QPointF aW=polyW[i],bW=polyW[(i+1)%polyW.size()];
    const double lenM=QLineF(aW,bW).length();
    if(lenM<2.0)continue;
    const QPointF mid=(polyS[i]+polyS[(i+1)%polyS.size()])*0.5;
    const QString txt=(lenM>=100?QString::number(qRound(lenM)):QString::number(lenM,'f',1))+" m";
    const int tw=p.fontMetrics().horizontalAdvance(txt)+10;
    const QRectF pill(mid.x()-tw*0.5,mid.y()-9,tw,18);
    p.setPen(QPen(plot_.strokeColor,1));p.setBrush(dark?QColor("#1d1c1a"):Qt::white);p.drawRoundedRect(pill,4,4);
    p.setPen(plot_.strokeColor);p.drawText(pill,Qt::AlignCenter,txt);
   }
   // Area badge at polygon centroid
   QPointF cen(0,0);for(const auto &pt:polyS)cen+=pt;cen/=polyS.size();
   const double area=plot_.areaM2();
   const QLocale loc(english_?"en_US":"vi_VN");
   const QString aTxt=(area>=10000?loc.toString(area/10000.0,'f',2)+" ha":loc.toString(area,'f',0)+" m²");
   const int aw=p.fontMetrics().horizontalAdvance(aTxt)+14;
   const QRectF aPill(cen.x()-aw*0.5,cen.y()-10,aw,20);
   p.setPen(Qt::NoPen);p.setBrush(plot_.strokeColor);p.drawRoundedRect(aPill,10,10);
   p.setPen(Qt::white);p.drawText(aPill,Qt::AlignCenter,aTxt);
  }
  // 2D Massing footprint overlay (podium + tower setbacks)
  if(camera_.mode=="2d"&&massing_.enabled){
   const auto mm=ga::evaluateMassing(plot_,massing_);
   if(mm.valid){
    const QColor mc=massing_.accentColor.isValid()?massing_.accentColor:QColor("#d2643c");
    if(massing_.podiumFloors>0&&mm.podiumRing.size()>=3){
     QPolygonF podS;for(const auto &v:mm.podiumRing)podS.append(worldToScreen(v,0.9));
     QColor pf=mc;pf.setAlpha(95);
     p.setPen(QPen(mc.darker(130),1.6));p.setBrush(pf);p.drawPolygon(podS);
    }
    if(massing_.towerFloors>0&&mm.towerRing.size()>=3){
     QPolygonF towS;for(const auto &v:mm.towerRing)towS.append(worldToScreen(v,1.0));
     QColor tf=mc.darker(115);tf.setAlpha(165);
     p.setPen(QPen(Qt::white,1.5));p.setBrush(tf);p.drawPolygon(towS);
    }
   }
  }
  p.restore();
 }
 if(project_&&!models3d_.isEmpty()){
  p.save();
  for(int mi=0;mi<models3d_.size();++mi){
   const auto &mdl=models3d_[mi];
   if(!mdl.visible||!mdl.valid())continue;
   const bool isSel=(mi==selectedModel3d_);
   if(camera_.mode!="2d"&&!isSel&&!placingModel3d_)continue;
   const QPolygonF fp=mdl.worldFootprint2D();
   QPolygonF fpS;
   for(const auto &v:fp)fpS.append(worldToScreen(v,0.85));
   const QColor col=mdl.overrideColor?mdl.tintColor:QColor("#d97706");
   QColor fill=col;fill.setAlpha(camera_.mode=="2d"?(isSel?115:82):36);
   p.setPen(QPen(isSel?QColor("#f59e0b"):col.darker(120),isSel?2.2:1.5,isSel?Qt::SolidLine:Qt::DashLine));
   p.setBrush(fill);
   if(fpS.size()>=3)p.drawPolygon(fpS);
   const QPointF cenS=worldToScreen({mdl.x,mdl.y},camera_.mode=="2d"?1.0:double(mdl.z+mdl.effectiveSizeM().z()+2.0));
   if(isSel||camera_.mode=="2d"){
    const QVector3D sz=mdl.effectiveSizeM();
    const QString badge=QString("%1 · %2×%3×%4 m").arg(mdl.name.left(18)).arg(sz.x(),0,'f',0).arg(sz.y(),0,'f',0).arg(sz.z(),0,'f',0);
    p.setFont(QFont("Segoe UI",7.8,QFont::DemiBold));
    const int bw=p.fontMetrics().horizontalAdvance(badge)+16;
    const QRectF bRect(cenS.x()-bw*0.5,cenS.y()-10.5,bw,21);
    p.setPen(QPen(isSel?QColor("#f59e0b"):col,1.1));
    p.setBrush(QColor(22,20,18,220));
    p.drawRoundedRect(bRect,10,10);
    p.setPen(QColor("#faf8f5"));
    p.drawText(bRect,Qt::AlignCenter,badge);
   }
  }
  if(placingModel3d_&&selectedModel3d_>=0&&selectedModel3d_<models3d_.size()){
   const QString tip=tr("Đặt vị trí mô hình 3D: Nhấn hoặc kéo trên khung nhìn để di chuyển · Esc xong",
                        "Place 3D Model: Click or drag on viewport to move · Esc done");
   p.setFont(QFont("Segoe UI",8.5,QFont::DemiBold));
   const int tw=p.fontMetrics().horizontalAdvance(tip)+24;
   const QRectF banner(std::max(14.0,(width()-tw)*0.5),14,tw,28);
   p.setPen(QPen(QColor("#f59e0b"),1.2));p.setBrush(QColor(24,20,16,236));p.drawRoundedRect(banner,14,14);
   p.setPen(Qt::white);p.drawText(banner,Qt::AlignCenter,tip);
  }
  p.restore();
 }
 if(project_&&drawingPlot_){
  p.save();
  QPolygonF draftS;
  for(const auto &pt:plotDraft_)draftS.append(worldToScreen(pt,0.9));
  if(draftS.size()>=2){
   p.setPen(QPen(QColor("#e11d48"),2.2));p.setBrush(QColor(225,29,72,38));
   if(draftS.size()>=3)p.drawPolygon(draftS);
   else p.drawPolyline(draftS);
  }
  if(!draftS.isEmpty()&&hasHoverLocal_){
   const QPointF hS=worldToScreen(hoverLocal_,0.9);
   p.setPen(QPen(QColor("#e11d48"),1.5,Qt::DashLine));
   p.drawLine(draftS.last(),hS);
   if(draftS.size()>=2)p.drawLine(hS,draftS.first());
   const double segLen=QLineF(plotDraft_.last(),hoverLocal_).length();
   const QString txt=QString::number(segLen,'f',1)+" m";
   const QPointF mid=(draftS.last()+hS)*0.5;
   p.setFont(QFont("Segoe UI",8,QFont::DemiBold));
   const QRectF pill(mid.x()-24,mid.y()-9,48,18);
   p.setPen(QPen(QColor("#e11d48"),1));p.setBrush(Qt::white);p.drawRoundedRect(pill,4,4);
   p.setPen(QColor("#e11d48"));p.drawText(pill,Qt::AlignCenter,txt);
  }
  p.setPen(QPen(Qt::white,1.5));p.setBrush(QColor("#e11d48"));
  for(const auto &pt:draftS)p.drawEllipse(pt,4.0,4.0);
  // Glowing snap halo around first vertex when ready to close polygon
  if(draftS.size()>=3&&hasHoverLocal_){
   const QPointF hS=worldToScreen(hoverLocal_,0.9);
   if(QLineF(hS,draftS.first()).length()<16.0){
    p.setPen(QPen(QColor("#10b981"),2.0));p.setBrush(QColor(16,185,129,55));
    p.drawEllipse(draftS.first(),9.5,9.5);
   }
  }
  // Top-center drawing instruction pill
  const QString tip=tr("Vẽ ranh tự do: Bấm thêm đỉnh · Giữ Shift + kéo để vẽ nét liền · Nhấp đúp/Enter để khép · Esc hủy",
                       "Draw Plot: Click vertices · Hold Shift + drag freehand · Double-click/Enter to close · Esc cancel");
  p.setFont(QFont("Segoe UI",8.5,QFont::DemiBold));
  const int tw=p.fontMetrics().horizontalAdvance(tip)+24;
  const QRectF banner(std::max(14.0,(width()-tw)*0.5),14,tw,28);
  p.setPen(QPen(QColor("#e11d48"),1.2));p.setBrush(QColor(26,20,22,232));p.drawRoundedRect(banner,14,14);
  p.setPen(Qt::white);p.drawText(banner,Qt::AlignCenter,tip);
  p.restore();
 }

 // 3A-bis. Quick Distance & Area Ruler Tool (M)
 if(project_&&measuring_){
  p.save();
  QPolygonF mS;
  for(const auto &pt:measurePts_)mS.append(worldToScreen(pt,1.0));
  if(mS.size()>=2){
   p.setPen(QPen(QColor("#0ea5e9"),2.2));
   p.setBrush(mS.size()>=3?QColor(14,165,233,36):Qt::NoBrush);
   if(mS.size()>=3)p.drawPolygon(mS);
   else p.drawPolyline(mS);
  }
  const QLocale loc(english_?"en_US":"vi_VN");
  double totalDist=0.0;
  p.setFont(QFont("Segoe UI",7.8,QFont::DemiBold));
  for(int i=0;i+1<measurePts_.size();++i){
   const double seg=QLineF(measurePts_[i],measurePts_[i+1]).length();
   totalDist+=seg;
   const QPointF mid=(mS[i]+mS[i+1])*0.5;
   const QString txt=(seg>=1000?loc.toString(seg/1000.0,'f',2)+" km":loc.toString(seg,'f',1)+" m");
   const int tw=p.fontMetrics().horizontalAdvance(txt)+12;
   const QRectF pill(mid.x()-tw*0.5,mid.y()-9,tw,18);
   p.setPen(QPen(QColor("#0284c7"),1));p.setBrush(dark?QColor("#1d1c1a"):Qt::white);p.drawRoundedRect(pill,5,5);
   p.setPen(QColor("#0ea5e9"));p.drawText(pill,Qt::AlignCenter,txt);
  }
  if(!mS.isEmpty()&&hasHoverLocal_){
   const QPointF hS=worldToScreen(hoverLocal_,1.0);
   p.setPen(QPen(QColor("#0ea5e9"),1.6,Qt::DashLine));
   p.drawLine(mS.last(),hS);
   const double liveSeg=QLineF(measurePts_.last(),hoverLocal_).length();
   const QPointF mid=(mS.last()+hS)*0.5;
   const QString txt=loc.toString(liveSeg,'f',1)+" m";
   const int tw=p.fontMetrics().horizontalAdvance(txt)+12;
   const QRectF pill(mid.x()-tw*0.5,mid.y()-9,tw,18);
   p.setPen(QPen(QColor("#0ea5e9"),1));p.setBrush(QColor(14,24,34,230));p.drawRoundedRect(pill,5,5);
   p.setPen(QColor("#7dd3fc"));p.drawText(pill,Qt::AlignCenter,txt);
  }
  p.setPen(QPen(Qt::white,1.5));p.setBrush(QColor("#0ea5e9"));
  for(const auto &pt:mS)p.drawEllipse(pt,4.2,4.2);

  QString summary=tr("Thước đo (M): Bấm các điểm để đo khoảng cách & diện tích · Backspace lùi · Esc thoát",
                     "Ruler (M): Click points to measure distance & area · Backspace undo · Esc exit");
  if(measurePts_.size()>=2){
   summary=tr("Tổng chiều dài: ","Total length: ")+(totalDist>=1000?loc.toString(totalDist/1000.0,'f',2)+" km":loc.toString(totalDist,'f',1)+" m");
   if(measurePts_.size()>=3){
    const double ar=std::abs(ga::ringArea(measurePts_));
    summary+=tr("  ·  Diện tích: ","  ·  Area: ")+(ar>=10000?loc.toString(ar/10000.0,'f',2)+" ha":loc.toString(ar,'f',0)+" m²");
   }
   summary+=tr("  ·  (Esc thoát)","  ·  (Esc exit)");
  }
  p.setFont(QFont("Segoe UI",8.4,QFont::DemiBold));
  const int tw=p.fontMetrics().horizontalAdvance(summary)+26;
  const QRectF banner(std::max(14.0,(width()-tw)*0.5),14,tw,28);
  p.setPen(QPen(QColor("#0ea5e9"),1.2));p.setBrush(QColor(15,23,32,235));p.drawRoundedRect(banner,14,14);
  p.setPen(QColor("#e0f2fe"));p.drawText(banner,Qt::AlignCenter,summary);
  p.restore();
 }

 // 3A-ter. Interactive Live Section Cut A–A' Line directly on Main 2D/3D Viewport!
 if(project_&&sectionCutActive_&&!drawingPlot_){
  const QPointF sA=worldToScreen(sectionA_,1.2);
  const QPointF sB=worldToScreen(sectionB_,1.2);
  const QPointF dir=sB-sA;
  const double lenPx=std::hypot(dir.x(),dir.y());
  p.save();
  if(lenPx>8.0){
   // Project a real world point on the +normal (view-direction) side so 3D shading stays perspective-accurate
   const QPointF wd=sectionB_-sectionA_;
   const double wLen=std::max(1.0,std::hypot(wd.x(),wd.y()));
   const QPointF wn(-wd.y()/wLen,wd.x()/wLen);
   const double bandM=std::clamp(wLen*0.08,16.0,55.0);
   const QPointF sA2=worldToScreen(sectionA_+wn*bandM,1.2);
   const QPointF sB2=worldToScreen(sectionB_+wn*bandM,1.2);
   p.setPen(Qt::NoPen);
   p.setBrush(dark?QColor(245,245,245,32):QColor(19,19,19,24));
   p.drawPolygon(QPolygonF{sA,sB,sB2,sA2});
   // Look-direction arrows near A and A'
   const QPointF nS(sA2-sA);
   const double nLen=std::hypot(nS.x(),nS.y());
   if(nLen>4.0){
    const QPointF nu=nS/nLen*14.0;
    const QPointF tu=dir/lenPx*4.5;
    p.setPen(QPen(accentCol,1.8,Qt::SolidLine,Qt::RoundCap));
    for(const QPointF &base:{sA+dir*0.08,sB-dir*0.08}){
     const QPointF tip=base+nu;
     p.drawLine(base,tip);
     p.drawLine(tip,tip-nu*0.35+tu);
     p.drawLine(tip,tip-nu*0.35-tu);
    }
   }
  }
  // Cut line halo + dashed accent
  p.setPen(QPen(dark?QColor(0,0,0,150):QColor(255,255,255,190),4.4));
  p.drawLine(sA,sB);
  const bool lineHi=(dragSectionPart_==3||hoverSectionPart_==3);
  p.setPen(QPen(lineHi?(dark?QColor("#ffffff"):QColor("#3b63fb")):accentCol,lineHi?3.0:2.2,Qt::DashDotLine));
  p.drawLine(sA,sB);

  // Midpoint length badge
  if(lenPx>80.0){
   const double cutLenM=QLineF(sectionA_,sectionB_).length();
   const QString lTxt=QString("A–A' · %1 m").arg(qRound(cutLenM));
   p.setFont(QFont("Segoe UI",7.8,QFont::Bold));
   const int tw=p.fontMetrics().horizontalAdvance(lTxt)+14;
   const QPointF mid=(sA+sB)*0.5;
   const QRectF lp(mid.x()-tw*0.5,mid.y()-10,tw,20);
   p.setPen(QPen(glassBorder,1.0));p.setBrush(dark?QColor(22,22,22,240):QColor(255,255,255,244));
   p.drawRoundedRect(lp,10,10);
   p.setPen(ink);p.drawText(lp,Qt::AlignCenter,lTxt);
  }

  // Interactive draggable A and A' circular handles
  auto drawSecHandle=[&](const QPointF &pt,const QString &tag,bool hi){
   const double r=hi?12.5:10.5;
   p.setPen(QPen(dark?QColor("#161616"):Qt::white,2.2));
   p.setBrush(hi?QColor("#3b63fb"):accentCol);
   p.drawEllipse(pt,r,r);
   p.setFont(QFont("Segoe UI",8.2,QFont::Bold));
   p.setPen(hi?Qt::white:(dark?QColor("#131313"):Qt::white));
   p.drawText(QRectF(pt.x()-12,pt.y()-10,24,20),Qt::AlignCenter,tag);
  };
  drawSecHandle(sA,"A",dragSectionPart_==1||hoverSectionPart_==1);
  drawSecHandle(sB,"A'",dragSectionPart_==2||hoverSectionPart_==2);
  p.restore();
 }

 // 3B. Live Hover Building Highlight & Floating Architectural Inspector Card
 if(project_&&!dragging_&&!drawingPlot_&&hoveredFeature_>=0&&hoveredFeature_<project_->features.size()&&hoveredDockId_.isEmpty()&&!compassHovered_){
  const auto &f=project_->features[hoveredFeature_];
  if(!f.rings.isEmpty()){
   const double hz=(camera_.mode!="2d"&&!camera_.exploded&&f.knownHeight())?f.height+0.6:0.7;
   QPolygonF hPoly;for(const auto &v:f.rings[0])hPoly.append(worldToScreen(v,hz));
   p.save();
   p.setPen(QPen(accentCol,2.0));
   QColor hf=accentCol;hf.setAlpha(44);p.setBrush(hf);
   p.drawPolygon(hPoly);

   const QLocale loc(english_?"en_US":"vi_VN");
   QString title=f.tags["name"].toString();
   if(title.isEmpty()){
    const QString bUse=f.tags["building"].toString();
    title=(bUse.isEmpty()||bUse=="yes")?tr("Công trình","Building"):bUse.left(1).toUpper()+bUse.mid(1);
   }
   const QString sub=(f.knownHeight()
    ?tr("Cao ","H ")+loc.toString(f.height,'f',1)+" m · ~"+QString::number(std::max(1,int(qRound(f.height/3.3))))+tr(" tầng · "," fl · ")
    :tr("Chưa có chiều cao · ","No height · "))+loc.toString(f.area(),'f',0)+" m²";
   QFont tf("Segoe UI",8.5,QFont::DemiBold),sf("Segoe UI",7.6);
   const int cardW=std::clamp(std::max(QFontMetrics(tf).horizontalAdvance(title)+28,QFontMetrics(sf).horizontalAdvance(sub)+24),136,260);
   const double cardH=44.0;
   const double cx=std::clamp(hoverScreen_.x()+16.0,14.0,std::max(14.0,width()-cardW-14.0));
   const double cy=std::clamp(hoverScreen_.y()-cardH-12.0,52.0,std::max(52.0,height()-cardH-70.0));
   const QRectF hCard(cx,cy,cardW,cardH);
   p.setPen(Qt::NoPen);p.setBrush(dark?QColor(0,0,0,90):QColor(26,25,24,26));p.drawRoundedRect(hCard.translated(0,2.5),10,10);
   p.setPen(QPen(glassBorder,1.0));p.setBrush(glassFill);p.drawRoundedRect(hCard,10,10);
   p.setPen(Qt::NoPen);p.setBrush(accentCol);p.drawEllipse(QPointF(hCard.left()+12,hCard.top()+14.5),3.5,3.5);
   p.setPen(ink);p.setFont(tf);p.drawText(hCard.adjusted(21,5,-10,-22),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(title,Qt::ElideRight,cardW-30));
   p.setPen(muted);p.setFont(sf);p.drawText(hCard.adjusted(12,23,-10,-5),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(sub,Qt::ElideRight,cardW-22));
   p.restore();
  }
 }

 // 4. GroundAtlas 1.2 Floating Glass HUD (Top-Left Telemetry Pill + Clickable North Compass + Bottom Floating Dock + Scale/Credits)
 const QString modeLabel=(camera_.mode=="2d"?tr("2D · Mặt bằng","2D · Plan"):camera_.projection=="axonometric"?tr("3D · Trục đo","3D · Axonometric"):camera_.projection=="isometric"?tr("3D · Đẳng cự","3D · Isometric"):camera_.projection=="one-point"?tr("3D · 1 điểm tụ","3D · 1-Point"):camera_.projection=="two-point"?tr("3D · 2 điểm tụ","3D · 2-Point"):tr("3D · Phối cảnh","3D · Perspective"));
 if(!drawingPlot_&&!measuring_){
  QString topText=modeLabel;
  if(project_&&camera_.mode!="2d"&&!camera_.exploded&&camera_.solar.enabled){
   const auto sp=ga::solarPosition(project_->site.lat,camera_.solar.dayOfYear,camera_.solar.hour);
   const int hh=int(std::floor(camera_.solar.hour)),mm=int(qRound((camera_.solar.hour-hh)*60.0));
   topText+=QString::fromUtf8("  ·  ☀ %1:%2  (%3° · PCF %4px)").arg(hh,2,10,QChar('0')).arg(mm,2,10,QChar('0')).arg(sp.altitudeDeg,0,'f',1).arg(shadowSize_);
  }
  p.save();
  p.setFont(QFont("Segoe UI",8.2,QFont::DemiBold));
  const int pillW=p.fontMetrics().horizontalAdvance(topText)+32;
  const QRectF topPill(16,14,pillW,28);
  p.setPen(Qt::NoPen);p.setBrush(dark?QColor(0,0,0,75):QColor(26,25,24,20));p.drawRoundedRect(topPill.translated(0,2),14,14);
  p.setPen(QPen(glassBorder,1.0));p.setBrush(glassFill);p.drawRoundedRect(topPill,14,14);
  // Live indicator dot
  const QColor dotCol=camera_.solar.enabled?QColor("#f59e0b"):(camera_.mode=="2d"?QColor("#16a34a"):accentCol);
  p.setPen(Qt::NoPen);p.setBrush(dotCol);p.drawEllipse(QPointF(topPill.left()+13,topPill.center().y()),3.5,3.5);
  p.setPen(ink);p.drawText(topPill.adjusted(22,0,-10,0),Qt::AlignVCenter|Qt::AlignLeft,topText);
  p.restore();
 }

 // Clickable North Compass (top-right)
 {
  const QRectF compPill=compassRect();
  p.save();
  p.setPen(Qt::NoPen);p.setBrush(dark?QColor(0,0,0,80):QColor(26,25,24,22));p.drawEllipse(compPill.translated(0,2));
  p.setPen(QPen(compassHovered_?accentCol:glassBorder,compassHovered_?1.5:1.0));
  p.setBrush(glassFill);
  p.drawEllipse(compPill);
  const QPointF c=compPill.center();
  // Subtle cardinal ticks
  p.setPen(QPen(muted,1.0));
  for(int deg=0;deg<360;deg+=90){
   const double rad=deg*M_PI/180.0;
   p.drawLine(c+QPointF(std::sin(rad)*14.5,-std::cos(rad)*14.5),c+QPointF(std::sin(rad)*17.0,-std::cos(rad)*17.0));
  }
  const double yaw=camera_.mode=="2d"?0:camera_.yaw*M_PI/180.0;
  const QPointF tip=c+QPointF(std::sin(yaw)*11.5,-std::cos(yaw)*11.5),side=c+QPointF(std::cos(yaw)*3.6,std::sin(yaw)*3.6);
  p.setPen(Qt::NoPen);p.setBrush(QColor("#e34850"));p.drawPolygon(QPolygonF{tip,side,c-(side-c)});
  p.setBrush(dark?QColor("#6e6e6e"):QColor("#a8a8a8"));p.drawPolygon(QPolygonF{c-(tip-c),side,c-(side-c)});
  p.setBrush(glassFill);p.drawEllipse(c,2.1,2.1);
  if(compassHovered_){
   const QString cTip=tr("Xoay về hướng Bắc (0°)","Align North (0°)");
   p.setFont(QFont("Segoe UI",7.6,QFont::DemiBold));
   const int cw=p.fontMetrics().horizontalAdvance(cTip)+16;
   const QRectF tipRect(compPill.right()-cw,compPill.bottom()+6,cw,22);
   p.setPen(Qt::NoPen);p.setBrush(QColor(24,24,24,238));p.drawRoundedRect(tipRect,6,6);
   p.setPen(QColor("#ffffff"));p.drawText(tipRect,Qt::AlignCenter,cTip);
  }
  p.restore();
 }

 // Interactive Bottom-Center Floating Spectrum 2 Viewport Dock
 if(hudDockVisible_&&project_&&!drawingPlot_&&width()>=540){
  const QRectF dock=dockPillRect();
  const auto items=dockItems();
  p.save();
  // Ambient layered shadow
  p.setPen(Qt::NoPen);
  p.setBrush(dark?QColor(0,0,0,108):QColor(0,0,0,22));
  p.drawRoundedRect(dock.translated(0,3.0),18,18);
  // Frosted Spectrum neutral pill container
  p.setPen(QPen(glassBorder,1.0));
  p.setBrush(glassFill);
  p.drawRoundedRect(dock,18,18);
  // Delicate top rim highlight
  p.setPen(QPen(dark?QColor(255,255,255,20):QColor(255,255,255,190),1.0));
  p.drawLine(QPointF(dock.left()+18,dock.top()+1),QPointF(dock.right()-18,dock.top()+1));

  // Subtle vertical separators between groups (after index 2 and index 5)
  p.setPen(QPen(dark?QColor(255,255,255,24):QColor(0,0,0,20),1.0));
  for(int sepIdx:{2,5}){
   if(sepIdx+1<items.size()){
    const double sx=(items[sepIdx].rect.right()+items[sepIdx+1].rect.left())*0.5;
    p.drawLine(QPointF(sx,dock.top()+8),QPointF(sx,dock.bottom()-8));
   }
  }

  for(const auto &it:items){
   const bool hov=(it.id==hoveredDockId_);
   if(it.active){
    p.setPen(Qt::NoPen);
    p.setBrush(dark?QColor("#f5f5f5"):QColor("#131313"));
    p.drawRoundedRect(it.rect,13,13);
   }else if(hov){
    p.setPen(Qt::NoPen);
    p.setBrush(dark?QColor(255,255,255,20):QColor(0,0,0,12));
    p.drawRoundedRect(it.rect,13,13);
   }
   p.setFont(QFont("Segoe UI",it.id=="zoom-in"||it.id=="zoom-out"?10.5:8.2,it.active?QFont::Bold:QFont::DemiBold));
   const QColor txtCol=it.active?(dark?QColor("#131313"):QColor("#ffffff")):(hov?ink:muted);
   p.setPen(txtCol);
   p.drawText(it.rect,Qt::AlignCenter,it.label);

   // Micro floating tooltip above hovered dock item
   if(hov&&!it.tip.isEmpty()){
    p.setFont(QFont("Segoe UI",7.6,QFont::DemiBold));
    const int tw=p.fontMetrics().horizontalAdvance(it.tip)+16;
    const QRectF tipBox(std::clamp(it.rect.center().x()-tw*0.5,12.0,width()-tw-12.0),dock.top()-28,tw,22);
    p.setPen(Qt::NoPen);p.setBrush(QColor(24,24,24,238));p.drawRoundedRect(tipBox,6,6);
    p.setPen(QColor("#ffffff"));p.drawText(tipBox,Qt::AlignCenter,it.tip);
   }
  }
  p.restore();
 }

 if(!graphicsError_.isEmpty()){p.setPen(QColor("#a95e46"));p.drawText(rect().adjusted(35,80,-35,-80),Qt::AlignCenter|Qt::TextWordWrap,tr("Không khởi tạo được OpenGL. Kiểm tra driver đồ họa.\n","OpenGL initialization failed. Check the graphics driver.\n")+graphicsError_.left(300));}
 else if(project_){
  const auto &s=stats_;const auto locale=QLocale(english_?"en_US":"vi_VN");
  const double unitsPerPixel=ga::groundMetresPerPixel(project_->site,sceneCamera(),size(),{double(width()-76),double(height()-38)}),raw=unitsPerPixel*86,base=std::pow(10,std::floor(std::log10(std::max(.001,raw)))),length=(raw/base>5?5:raw/base>2?2:1)*base,pixels=std::clamp(length/unitsPerPixel,36.0,130.0);
  const QString scaleLbl=(length>=1000?locale.toString(length/1000,'g',3)+" km":locale.toString(length,'g',3)+" m");
  // Bottom-right sleek frosted scale pill
  p.save();
  const double sbW=pixels+24.0;
  const QRectF sbPill(width()-sbW-16.0,height()-44.0,sbW,28.0);
  p.setPen(QPen(glassBorder,1.0));p.setBrush(glassFill);p.drawRoundedRect(sbPill,10,10);
  const double sx=sbPill.left()+12.0,sy=sbPill.bottom()-7.0;
  p.setPen(QPen(ink,1.5));p.drawLine(QPointF(sx,sy),QPointF(sx+pixels,sy));
  p.drawLine(QPointF(sx,sy-3),QPointF(sx,sy+1));p.drawLine(QPointF(sx+pixels,sy-3),QPointF(sx+pixels,sy+1));
  p.setFont(QFont("Segoe UI",7.6,QFont::DemiBold));p.setPen(ink);
  p.drawText(QRectF(sbPill.left(),sbPill.top()+2,sbPill.width(),15),Qt::AlignCenter,scaleLbl);
  p.restore();

  // Bottom-left compact site telemetry pill (clamped so it never touches the center dock)
  const QString footFull=locale.toString(s.buildings)+tr(" công trình · "," buildings · ")+locale.toString(s.knownHeights)+tr(" có chiều cao"," with heights");
  const QString footShort=locale.toString(s.buildings)+tr(" công trình"," buildings");
  p.save();
  p.setFont(QFont("Segoe UI",7.5,QFont::Medium));
  const double maxFootW=hudDockVisible_&&width()>=540?std::max(104.0,dockPillRect().left()-26.0):double(width()-200);
  const double fullNeed=double(p.fontMetrics().horizontalAdvance(footFull)+20);
  const QString &foot=(fullNeed<=maxFootW)?footFull:footShort;
  const double fw=std::min(maxFootW,double(p.fontMetrics().horizontalAdvance(foot)+20));
  if(fw>=96.0){
   const QRectF footPill(16.0,height()-42.0,fw,24.0);
   p.setPen(QPen(glassBorder,1.0));p.setBrush(glassFill);p.drawRoundedRect(footPill,10,10);
   p.setPen(muted);p.drawText(footPill.adjusted(9,0,-9,0),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(foot,Qt::ElideRight,int(fw-18)));
  }
  p.restore();
 }
}

QRectF Viewport::compassRect() const {
 return QRectF(width()-54.0,14.0,40.0,40.0);
}

QRectF Viewport::dockPillRect() const {
 const double dockW=576.0,dockH=36.0;
 return QRectF((width()-dockW)*0.5,height()-dockH-14.0,dockW,dockH);
}

QVector<Viewport::DockItem> Viewport::dockItems() const {
 const auto tr=[&](const char *vi,const char *en){return QString::fromUtf8(english_?en:vi);};
 const QRectF dock=dockPillRect();
 const double y=dock.top()+5.0,h=dock.height()-10.0;
 const int zPct=std::clamp(int(qRound(camera_.zoom*100.0)),1,9999);
 const QString zoomStr=zPct>=1000?QString::number(camera_.zoom,'f',0)+"×":QString::number(zPct)+"%";
 const bool is2d=(camera_.mode=="2d");
 const bool isIso=(!is2d&&(camera_.projection=="isometric"||camera_.projection=="axonometric"));
 const bool isPersp=(!is2d&&!isIso);

 struct Def{QString id;double w;QString lbl;QString tip;bool active;bool accent;double gapAfter;};
 const QVector<Def> defs={
  {"zoom-out",28,QString::fromUtf8("−"),tr("Thu nhỏ mượt","Smooth zoom out"),false,false,2},
  {"reset",46,zoomStr,tr("Về tâm & 100% (F)","Recenter & 100% (F)"),false,false,2},
  {"zoom-in",28,"+",tr("Phóng to mượt","Smooth zoom in"),false,false,10},
  {"2d",36,"2D",tr("Mặt bằng 2D","2D Plan View"),is2d,false,2},
  {"iso",38,"Iso",tr("Trục đo Đẳng cự 3D","3D Isometric View"),isIso,false,2},
  {"3d",36,"3D",tr("Phối cảnh 3D","3D Perspective View"),isPersp,false,10},
  {"explode",48,tr("Tách","Explode"),tr("Tách tầng 3D mượt","Smooth 3D exploded stack"),camera_.exploded,true,2},
  {"grid",42,tr("Lưới","Grid"),tr("Lưới tọa độ 100 m","100 m coordinate grid"),camera_.grid,false,2},
  {"measure",52,tr("Thước","Ruler"),tr("Đo khoảng cách & diện tích (M)","Measure distance & area (M)"),measuring_,true,2},
  {"section",56,tr("Cắt A-A'","Cut A-A'"),tr("Kéo mặt cắt trực tiếp A–A'","Interactive Live Section A–A'"),sectionCutActive_,true,2},
  {"hanoi-qh",52,tr("QH HN","HN Plan"),tr("Bật/tắt Quy hoạch sử dụng đất Hà Nội (quyhoach.hanoi.vn)","Toggle Hanoi Land-Use Planning Layer (quyhoach.hanoi.vn)"),hanoiPlanEnabled_,true,2},
  {"sun",64,tr("☀ Nắng","☀ Sun"),QString::fromUtf8(english_?"GPU PCF Soft Shadows (%1px)":"Bóng đổ GPU PCF (%1px)").arg(shadowSize_),camera_.solar.enabled,true,0},
 };
 QVector<DockItem> out;
 double totalW=0;for(int i=0;i<defs.size();++i)totalW+=defs[i].w+(i+1<defs.size()?defs[i].gapAfter:0);
 double x=dock.center().x()-totalW*0.5;
 for(const auto &d:defs){
  out.append({d.id,QRectF(x,y,d.w,h),d.lbl,d.tip,d.active,d.accent});
  x+=d.w+d.gapAfter;
 }
 return out;
}

int Viewport::hitTestBuilding(const QPointF &point) const {
 if(!project_)return -1;
 for(int i=project_->features.size()-1;i>=0;--i){
  const auto &f=project_->features[i];
  if(f.geometry!=ga::Geometry::Polygon||!f.building())continue;
  if(!f.bounds().contains(point))continue;
  const bool visible=!ga::featurePaints(f,layers_,diagram_).isEmpty();
  if(!visible||!f.rings[0].containsPoint(point,Qt::OddEvenFill))continue;
  bool hole=false;for(int j=1;j<f.rings.size();++j)hole|=f.rings[j].containsPoint(point,Qt::OddEvenFill);
  if(!hole)return i;
 }
 return -1;
}

void Viewport::triggerDockAction(const QString &id){
 if(id=="zoom-in")animateZoomBy(1.32,180);
 else if(id=="zoom-out")animateZoomBy(1.0/1.32,180);
 else if(id=="reset")animateResetView(320);
 else if(id=="north")animateAlignNorth(280);
 else if(id=="2d"){auto c=camera_;c.mode="2d";animateCameraTo(c,260);}
 else if(id=="iso"){auto c=camera_;c.mode="3d";c.preset("isometric");animateCameraTo(c,320);}
 else if(id=="3d"){auto c=camera_;c.mode="3d";c.preset("perspective");animateCameraTo(c,320);}
 else if(id=="explode"){
  auto c=camera_;c.exploded=!c.exploded;
  if(c.exploded){c.mode="3d";c.preset("isometric");if(c.separation<15)c.separation=60;}
  animateCameraTo(c,340);
 }else if(id=="grid"){auto c=camera_;c.grid=!c.grid;setCamera(c);}
 else if(id=="measure"){setMeasuring(!measuring_);}
 else if(id=="section"){setSectionCutActive(!sectionCutActive_);}
 else if(id=="hanoi-qh"){emit hanoiPlanningToggled(!hanoiPlanEnabled_);}
 else if(id=="sun"){
  auto c=camera_;c.solar.enabled=!c.solar.enabled;
  if(c.solar.enabled){
   c.exploded=false;
   if(c.mode=="2d"){c.mode="3d";c.preset("isometric");animateCameraTo(c,340);}
   else setCamera(c);
  }else setCamera(c);
 }
 emit dockActionTriggered(id);
}

QPointF Viewport::groundPoint(const QPointF &s) const {
 const auto point=ga::groundOnMapPlane(matrix(),size(),s);
 return project_?ga::localFromScenePlane(point,project_->site,camera_):point;
}
void Viewport::mousePressEvent(QMouseEvent *e){
 lastMouse_=pressMouse_=e->position();
 if(drawingPlot_&&e->button()==Qt::LeftButton){
  if(e->modifiers()&Qt::ShiftModifier){
   freehandPlot_=true;
   const QPointF pt=groundPoint(e->position());
   plotDraft_.append(pt);
   emit plotDrawingChanged(true,plotDraft_.size());
   update();
  }
  e->accept();
  return;
 }
 // Clickable North Compass & Floating Glass Dock hit-test
 if(e->button()==Qt::LeftButton&&!drawingPlot_){
  if(compassRect().contains(e->position())){
   triggerDockAction("north");
   e->accept();
   return;
  }
  if(hudDockVisible_&&project_&&width()>=540&&dockPillRect().contains(e->position())){
   for(const auto &it:dockItems()){
    if(it.rect.contains(e->position())){
     triggerDockAction(it.id);
     e->accept();
     return;
    }
   }
   e->accept();
   return;
  }
 }
 // Ruler tool vertex click
 if(measuring_&&project_){
  if(e->button()==Qt::RightButton){
   if(!measurePts_.isEmpty()){measurePts_.removeLast();update();}
   else setMeasuring(false);
   e->accept();
   return;
  }
  if(e->button()==Qt::LeftButton){
   measurePts_.append(groundPoint(e->position()));
   update();
   e->accept();
   return;
  }
 }
 // Interactive Live Section A–A' Handle / Cut-Line Drag
 if(e->button()==Qt::LeftButton&&project_&&sectionCutActive_&&!drawingPlot_&&!measuring_){
  const auto m=matrix();
  auto toScreen=[&](const QPointF &local){
   const auto pl=ga::scenePlanePoint(local,project_->site,camera_);
   const auto cl=m*QVector4D(float(pl.x()),float(pl.y()),1.2f,1.0f);
   const float inv=cl.w()>0.001f?1.0f/cl.w():1.0f;
   return QPointF((cl.x()*inv+1.0)*width()*0.5,(1.0-cl.y()*inv)*height()*0.5);
  };
  const QPointF sA=toScreen(sectionA_),sB=toScreen(sectionB_);
  int part=0;
  if(QLineF(e->position(),sA).length()<=15.0)part=1;
  else if(QLineF(e->position(),sB).length()<=15.0)part=2;
  else{
   const QPointF ab=sB-sA;
   const double l2=ab.x()*ab.x()+ab.y()*ab.y();
   if(l2>16.0){
    const double t=std::clamp(((e->position().x()-sA.x())*ab.x()+(e->position().y()-sA.y())*ab.y())/l2,0.0,1.0);
    if(QLineF(e->position(),sA+ab*t).length()<=10.0)part=3;
   }
  }
  if(part>0){
   stopCameraAnimation();
   dragSectionPart_=part;
   dragSectionStartLocal_=groundPoint(e->position());
   dragSectionStartA_=sectionA_;
   dragSectionStartB_=sectionB_;
   setCursor(Qt::SizeAllCursor);
   e->accept();
   return;
  }
 }
 // Direct 3D Imported Model Placement / Alt+Drag Move
 if(e->button()==Qt::LeftButton&&project_&&!models3d_.isEmpty()&&!drawingPlot_&&!measuring_){
  const QPointF pt=groundPoint(e->position());
  if(placingModel3d_&&selectedModel3d_>=0&&selectedModel3d_<models3d_.size()){
   stopCameraAnimation();
   auto &mdl=models3d_[selectedModel3d_];
   mdl.x=pt.x();mdl.y=pt.y();
   draggingModel3d_=true;
   dragModelStartLocal_=pt;
   dragModelStartXY_={mdl.x,mdl.y};
   dirty_=true;
   emit model3dMoved(selectedModel3d_,mdl.x,mdl.y);
   update();
   e->accept();
   return;
  }
  if(e->modifiers()&Qt::AltModifier){
   for(int mi=models3d_.size()-1;mi>=0;--mi){
    if(!models3d_[mi].visible||!models3d_[mi].valid())continue;
    if(models3d_[mi].worldFootprint2D().containsPoint(pt,Qt::OddEvenFill)){
     stopCameraAnimation();
     selectedModel3d_=mi;
     draggingModel3d_=true;
     dragModelStartLocal_=pt;
     dragModelStartXY_={models3d_[mi].x,models3d_[mi].y};
     setCursor(Qt::SizeAllCursor);
     e->accept();
     return;
    }
   }
  }
 }
 // Direct Plot Vertex Drag OR Whole-Plot Move (grab any corner dot or center badge!)
 if(e->button()==Qt::LeftButton&&project_&&plot_.active&&!drawingPlot_&&!measuring_){
  const QPolygonF polyW=plot_.polygon();
  const auto m=matrix();
  auto toScreen=[&](const QPointF &local){
   const auto pl=ga::scenePlanePoint(local,project_->site,camera_);
   const auto cl=m*QVector4D(float(pl.x()),float(pl.y()),0.8f,1.0f);
   const float inv=cl.w()>0.001f?1.0f/cl.w():1.0f;
   return QPointF((cl.x()*inv+1.0)*width()*0.5,(1.0-cl.y()*inv)*height()*0.5);
  };
  for(int vi=0;vi<polyW.size();++vi){
   if(QLineF(e->position(),toScreen(polyW[vi])).length()<=11.0){
    stopCameraAnimation();
    dragPlotVertex_=vi;
    dragPlotStartRing_=polyW;
    plot_.isPolygon=true;
    plot_.customRing=polyW;
    setCursor(Qt::SizeAllCursor);
    e->accept();
    return;
   }
  }
  if(polyW.size()>=3){
   QPointF cenS(0,0);for(const auto &v:polyW)cenS+=toScreen(v);cenS/=polyW.size();
   if(QLineF(e->position(),cenS).length()<=24.0||((e->modifiers()&Qt::AltModifier)&&polyW.containsPoint(groundPoint(e->position()),Qt::OddEvenFill))){
    stopCameraAnimation();
    dragPlotWhole_=true;
    dragPlotStartLocal_=groundPoint(e->position());
    dragPlotStartRing_=polyW;
    setCursor(Qt::SizeAllCursor);
    e->accept();
    return;
   }
  }
 }
 stopCameraAnimation();
 dragging_=true;
 hoveredFeature_=-1;
 setCursor(e->button()==Qt::RightButton?Qt::SizeAllCursor:Qt::ClosedHandCursor);
 e->accept();
}
void Viewport::panGround(QPointF before,QPointF after){
 if(project_){
  const auto target=ga::mapPlanePoint({camera_.panX,camera_.panY},project_->site)+ga::mapPlanePoint(before,project_->site)-ga::mapPlanePoint(after,project_->site);
  const auto local=ga::sitePlanePoint(target,project_->site);
  camera_.panX=local.x();camera_.panY=local.y();
  ga::normalizeCameraCenter(camera_,project_->site);
 }else{camera_.panX+=before.x()-after.x();camera_.panY+=before.y()-after.y();}
}
void Viewport::mouseMoveEvent(QMouseEvent *e){
 hoverScreen_=e->position();
 const auto point=groundPoint(e->position());
 emit cursorPosition(point);
 if(drawingPlot_){
  hoverLocal_=point;
  hasHoverLocal_=true;
  if(freehandPlot_&&(e->buttons()&Qt::LeftButton)){
   if(plotDraft_.isEmpty()||QLineF(plotDraft_.last(),point).length()>=3.5){
    plotDraft_.append(point);
    emit plotDrawingChanged(true,plotDraft_.size());
   }
  }
  update();
  e->accept();
  return;
 }
 if(measuring_){
  hoverLocal_=point;
  hasHoverLocal_=true;
  update();
  e->accept();
  return;
 }
 if(dragSectionPart_>0&&(e->buttons()&Qt::LeftButton)&&project_){
  const double lim=std::max(120.0,project_->site.radius*1.15);
  auto clampPt=[&](QPointF q){const double r=std::hypot(q.x(),q.y());return r>lim?q*(lim/r):q;};
  if(dragSectionPart_==1){
   const QPointF next=clampPt(point);
   if(QLineF(next,sectionB_).length()>=24.0)sectionA_=next;
  }else if(dragSectionPart_==2){
   const QPointF next=clampPt(point);
   if(QLineF(sectionA_,next).length()>=24.0)sectionB_=next;
  }else if(dragSectionPart_==3){
   const QPointF delta=point-dragSectionStartLocal_;
   sectionA_=clampPt(dragSectionStartA_+delta);
   sectionB_=clampPt(dragSectionStartB_+delta);
  }
  emit sectionCutChanged(sectionA_,sectionB_,true);
  update();
  e->accept();
  return;
 }
 if(draggingModel3d_&&(e->buttons()&Qt::LeftButton)&&selectedModel3d_>=0&&selectedModel3d_<models3d_.size()){
  const QPointF delta=point-dragModelStartLocal_;
  auto &mdl=models3d_[selectedModel3d_];
  mdl.x=dragModelStartXY_.x()+delta.x();
  mdl.y=dragModelStartXY_.y()+delta.y();
  dirty_=true;
  emit model3dMoved(selectedModel3d_,mdl.x,mdl.y);
  update();
  e->accept();
  return;
 }
 if(dragPlotVertex_>=0&&(e->buttons()&Qt::LeftButton)){
  if(dragPlotVertex_<plot_.customRing.size()){
   plot_.customRing[dragPlotVertex_]=point;
   if(massing_.enabled)dirty_=true;
   update();
  }
  e->accept();
  return;
 }
 if(dragPlotWhole_&&(e->buttons()&Qt::LeftButton)){
  const QPointF delta=point-dragPlotStartLocal_;
  if(plot_.isPolygon){
   QPolygonF moved=dragPlotStartRing_;
   for(auto &pt:moved)pt+=delta;
   plot_.customRing=moved;
  }else{
   QPointF origCen(0,0);for(const auto &v:dragPlotStartRing_)origCen+=v;origCen/=std::max<qsizetype>(1,dragPlotStartRing_.size());
   const QPointF nc=origCen+delta;
   plot_.centerX=nc.x();plot_.centerY=nc.y();
  }
  if(massing_.enabled)dirty_=true;
  update();
  e->accept();
  return;
 }
 if(dragging_&&e->buttons()!=Qt::NoButton){
  beginInteraction();
  const auto delta=e->position()-lastMouse_;
  if(e->buttons()&Qt::RightButton&&camera_.mode!="2d"){
   camera_.yaw+=delta.x()*.35;
   camera_.elevation=std::clamp(camera_.elevation-delta.y()*.25,8.,89.5);
   if(camera_.projection=="isometric")camera_.projection="axonometric";
   if(smoothDamping_){
    dampYawVel_=delta.x()*18.0;
    dampElVel_=-delta.y()*12.0;
   }
  }else{
   const auto before=groundPoint(lastMouse_),after=point;
   panGround(before,after);
  }
  lastMouse_=e->position();
  if(e->buttons()&Qt::RightButton)sortDirty_=true;
  planDirty_=contextViewDirty_=true;
  update();
  emit cameraChanged();
  e->accept();
  return;
 }
 // Interactive HUD hover, Plot vertex handle hover & live building inspector
 const bool prevComp=compassHovered_;
 const QString prevDock=hoveredDockId_;
 const int prevHover=hoveredFeature_;
 const int prevPv=hoveredPlotVertex_;
 compassHovered_=compassRect().contains(e->position());
 hoveredDockId_.clear();
 hoveredPlotVertex_=-1;
 if(!compassHovered_&&hudDockVisible_&&project_&&width()>=540&&dockPillRect().contains(e->position())){
  for(const auto &it:dockItems())if(it.rect.contains(e->position())){hoveredDockId_=it.id;break;}
 }
 const bool overHud=compassHovered_||!hoveredDockId_.isEmpty()||(hudDockVisible_&&project_&&width()>=540&&dockPillRect().contains(e->position()));
 if(overHud){
  hoveredFeature_=-1;
  setCursor(Qt::PointingHandCursor);
 }else{
  if(project_&&plot_.active&&isVisible()){
   const QPolygonF polyW=plot_.polygon();
   const auto m=matrix();
   for(int vi=0;vi<polyW.size();++vi){
    const auto pl=ga::scenePlanePoint(polyW[vi],project_->site,camera_);
    const auto cl=m*QVector4D(float(pl.x()),float(pl.y()),0.8f,1.0f);
    if(cl.w()>0.001f){
     const QPointF s((cl.x()/cl.w()+1.0)*width()*0.5,(1.0-cl.y()/cl.w())*height()*0.5);
     if(QLineF(e->position(),s).length()<=11.0){hoveredPlotVertex_=vi;break;}
    }
   }
  }
  if(hoveredPlotVertex_>=0){
   hoveredFeature_=-1;
   hoverSectionPart_=0;
   setCursor(Qt::SizeAllCursor);
  }else{
   const int prevSec=hoverSectionPart_;
   hoverSectionPart_=0;
   if(project_&&sectionCutActive_&&isVisible()){
    const auto m=matrix();
    auto toScreen=[&](const QPointF &local){
     const auto pl=ga::scenePlanePoint(local,project_->site,camera_);
     const auto cl=m*QVector4D(float(pl.x()),float(pl.y()),1.2f,1.0f);
     const float inv=cl.w()>0.001f?1.0f/cl.w():1.0f;
     return QPointF((cl.x()*inv+1.0)*width()*0.5,(1.0-cl.y()*inv)*height()*0.5);
    };
    const QPointF sA=toScreen(sectionA_),sB=toScreen(sectionB_);
    if(QLineF(e->position(),sA).length()<=15.0)hoverSectionPart_=1;
    else if(QLineF(e->position(),sB).length()<=15.0)hoverSectionPart_=2;
    else{
     const QPointF ab=sB-sA;const double l2=ab.x()*ab.x()+ab.y()*ab.y();
     if(l2>16.0){
      const double t=std::clamp(((e->position().x()-sA.x())*ab.x()+(e->position().y()-sA.y())*ab.y())/l2,0.0,1.0);
      if(QLineF(e->position(),sA+ab*t).length()<=10.0)hoverSectionPart_=3;
     }
    }
   }
   if(hoverSectionPart_>0){
    hoveredFeature_=-1;
    setCursor(Qt::SizeAllCursor);
    if(prevSec!=hoverSectionPart_)update();
   }else{
    hoveredFeature_=isVisible()?hitTestBuilding(point):-1;
    if(hoveredFeature_>=0)setCursor(Qt::PointingHandCursor);
    else unsetCursor();
    if(prevSec!=hoverSectionPart_)update();
   }
  }
 }
 if(prevComp!=compassHovered_||prevDock!=hoveredDockId_||prevHover!=hoveredFeature_||prevPv!=hoveredPlotVertex_||hoveredFeature_>=0)update();
 e->accept();
}
void Viewport::leaveEvent(QEvent *e){
 if(compassHovered_||!hoveredDockId_.isEmpty()||hoveredFeature_>=0||hoveredPlotVertex_>=0){
  compassHovered_=false;hoveredDockId_.clear();hoveredFeature_=-1;hoveredPlotVertex_=-1;
  if(!drawingPlot_&&!dragging_&&!measuring_)unsetCursor();
  update();
 }
 QOpenGLWidget::leaveEvent(e);
}
void Viewport::mouseReleaseEvent(QMouseEvent *e){
 if(drawingPlot_&&e->button()==Qt::LeftButton){
  if(skipPlotRelease_){skipPlotRelease_=false;e->accept();return;}
  if(freehandPlot_){
   freehandPlot_=false;
   if(plotDraft_.size()>=5)finishPlotDrawing();
   e->accept();
   return;
  }
  if((e->position()-pressMouse_).manhattanLength()<6){
   const QPointF pt=groundPoint(e->position());
   if(plotDraft_.size()>=3){
    const auto m=matrix();
    const auto pl=project_?ga::scenePlanePoint(plotDraft_.first(),project_->site,camera_):plotDraft_.first();
    const auto cl=m*QVector4D(float(pl.x()),float(pl.y()),0.9f,1.0f);
    if(cl.w()>0.001f){
     const QPointF firstS((cl.x()/cl.w()+1.0)*width()*0.5,(1.0-cl.y()/cl.w())*height()*0.5);
     if(QLineF(e->position(),firstS).length()<14.0){finishPlotDrawing();e->accept();return;}
    }
   }
   if(plotDraft_.isEmpty()||QLineF(plotDraft_.last(),pt).length()>=0.5){
    plotDraft_.append(pt);
    emit plotDrawingChanged(true,plotDraft_.size());
    update();
   }
  }
  e->accept();
  return;
 }
 if(dragSectionPart_>0){
  dragSectionPart_=0;unsetCursor();
  emit sectionCutChanged(sectionA_,sectionB_,true);
  e->accept();
  return;
 }
 if(draggingModel3d_){
  draggingModel3d_=false;
  if(!placingModel3d_)unsetCursor();
  if(selectedModel3d_>=0&&selectedModel3d_<models3d_.size()){
   emit model3dMoved(selectedModel3d_,models3d_[selectedModel3d_].x,models3d_[selectedModel3d_].y);
  }
  e->accept();
  return;
 }
 if(dragPlotVertex_>=0||dragPlotWhole_){
  dragPlotVertex_=-1;dragPlotWhole_=false;unsetCursor();
  emit plotEdited(plot_);
  e->accept();
  return;
 }
 const bool wasRightOrbit=(dragging_&&e->button()==Qt::RightButton&&camera_.mode!="2d");
 dragging_=false;unsetCursor();
 if(wasRightOrbit&&smoothDamping_&&(std::abs(dampYawVel_)>4.0||std::abs(dampElVel_)>4.0)){
  dampClock_.restart();
  if(!dampTimer_.isActive())dampTimer_.start();
 }else{
  dampYawVel_=dampElVel_=0.0;
 }
 if(e->button()==Qt::LeftButton&&(e->position()-pressMouse_).manhattanLength()<4&&project_&&!measuring_){
  setSelected(hitTestBuilding(groundPoint(e->position())));
 }
 e->accept();
}
void Viewport::mouseDoubleClickEvent(QMouseEvent *e){
 if(drawingPlot_&&e->button()==Qt::LeftButton){
  skipPlotRelease_=true;
  finishPlotDrawing();
  e->accept();
  return;
 }
 if(placingModel3d_){setPlacingModel3d(false);e->accept();return;}
 if(measuring_){setMeasuring(false);e->accept();return;}
 if(compassRect().contains(e->position())||(hudDockVisible_&&dockPillRect().contains(e->position()))){e->accept();return;}
 animateResetView(320);
 e->accept();
}
void Viewport::keyPressEvent(QKeyEvent *e){
 if(drawingPlot_){
  if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter){finishPlotDrawing();e->accept();return;}
  if(e->key()==Qt::Key_Backspace||e->key()==Qt::Key_Delete){undoPlotVertex();e->accept();return;}
  if(e->key()==Qt::Key_Escape){cancelPlotDrawing();e->accept();return;}
 }
 if(placingModel3d_){
  if(e->key()==Qt::Key_Escape||e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter){setPlacingModel3d(false);e->accept();return;}
 }
 if(measuring_){
  if(e->key()==Qt::Key_Backspace||e->key()==Qt::Key_Delete){if(!measurePts_.isEmpty()){measurePts_.removeLast();update();}e->accept();return;}
  if(e->key()==Qt::Key_Escape||e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter){setMeasuring(false);e->accept();return;}
 }
 if(e->key()==Qt::Key_M&&e->modifiers()==Qt::NoModifier&&!drawingPlot_){
  setMeasuring(!measuring_);
  emit dockActionTriggered("measure");
  e->accept();
  return;
 }
 QOpenGLWidget::keyPressEvent(e);
}
void Viewport::wheelEvent(QWheelEvent *e){
 if(camAnimTimer_.isActive())camAnimTimer_.stop();
 beginInteraction();
 const auto before=groundPoint(e->position());
 const double delta=e->angleDelta().y()?e->angleDelta().y()/120.:e->pixelDelta().y()/100.;
 if(smoothDamping_&&isVisible()){
  const double baseZoom=dampZoomActive_?dampTargetZoom_:camera_.zoom;
  dampTargetZoom_=std::clamp(baseZoom*std::exp(delta*.14),1e-7,1e5);
  dampAnchorLocal_=before;
  dampAnchorScreen_=e->position();
  dampZoomActive_=true;
  dampClock_.restart();
  if(!dampTimer_.isActive())dampTimer_.start();
  e->accept();
  return;
 }
 camera_.zoom=std::clamp(camera_.zoom*std::exp(delta*.13),1e-7,1e5);
 constrainZoom();
 const auto after=groundPoint(e->position());
 panGround(before,after);
 planDirty_=contextViewDirty_=true;
 update();
 emit cameraChanged();
 e->accept();
}
void Viewport::drawPlan(QPainter &p,const QRectF &rect){
 if(!project_)return;
 const auto bounds=project_->site.boundary.boundingRect();const double scale=std::min(rect.width()/std::max(1.,bounds.width()),rect.height()/std::max(1.,bounds.height()));
 const auto point=[&](QPointF v){return rect.center()+QPointF((v.x()-bounds.center().x())*scale,-(v.y()-bounds.center().y())*scale);};
 p.save();p.setClipRect(rect);p.fillRect(rect,palette_.paper);paintPlan(p,*project_,layers_,palette_,diagram_,point,scale,true,-1,camera_.grid);p.restore();
}
bool Viewport::exportVector(const QString &file,bool pdf){
 if(!project_)return false;
 QSaveFile output(file);if(!output.open(QIODevice::WriteOnly))return false;bool painted=false;
 const auto sheet=[&](QPainter &p,const QRectF &page){p.fillRect(page,Qt::white);p.setPen(QColor("#333d36"));p.setFont(QFont("Segoe UI",18,QFont::DemiBold));p.drawText(page.adjusted(38,25,-38,-25),Qt::AlignTop|Qt::AlignLeft,project_->site.name);p.setFont(QFont("Segoe UI",9));p.drawText(38,76,english_?"GROUNDATLAS DESKTOP / 2D SITE DRAWING":"GROUNDATLAS DESKTOP / BẢN VẼ KHU ĐẤT 2D");drawPlan(p,page.adjusted(38,100,-38,-100));p.setPen(QColor("#777e74"));p.setFont(QFont("Segoe UI",8));p.drawText(page.adjusted(38,page.height()-79,-38,-20),Qt::AlignTop|Qt::TextWordWrap,project_->source["attribution"].toString()+"\n"+(english_?"Saved data. AI heights are estimates; this drawing is not a legal or topographic survey.":"Dữ liệu đã lưu. Chiều cao AI là ước tính; bản vẽ không thay hồ sơ pháp lý hoặc khảo sát địa hình."));};
 if(pdf){QPdfWriter writer(&output);writer.setPageSize(QPageSize(QPageSize::A3));writer.setResolution(100);writer.setTitle(project_->site.name);writer.setCreator("GroundAtlas Desktop 2.2.0 · C++/Qt");QPainter p(&writer);painted=p.isActive();if(painted)sheet(p,{0,0,double(writer.width()),double(writer.height())});p.end();}
 else{QSvgGenerator svg;svg.setOutputDevice(&output);svg.setSize({1200,1350});svg.setViewBox(QRect(0,0,1200,1350));svg.setTitle(project_->site.name);svg.setDescription(project_->source["attribution"].toString());QPainter p(&svg);painted=p.isActive();if(painted)sheet(p,{0,0,1200,1350});p.end();}
 return painted&&output.commit();
}
bool Viewport::exportHighResPng(const QString &file,int targetWidth,int targetHeight,bool transparentBg){
 if(!project_||targetWidth<=0||targetHeight<=0)return false;
 if(camera_.mode=="2d"){
  QImage img(targetWidth,targetHeight,QImage::Format_ARGB32_Premultiplied);
  img.fill(transparentBg?QColor(Qt::transparent):palette_.paper);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing,true);
  const auto bounds=project_->site.boundary.boundingRect();
  const double pad=0.92;
  const double scale=std::min(targetWidth*pad/std::max(1.0,bounds.width()),targetHeight*pad/std::max(1.0,bounds.height()));
  const auto point=[&](QPointF v){return QPointF(targetWidth*0.5+(v.x()-bounds.center().x())*scale,targetHeight*0.5-(v.y()-bounds.center().y())*scale);};
  paintPlan(p,*project_,layers_,palette_,diagram_,point,scale,true,-1,camera_.grid);
  p.end();
  return img.save(file,"PNG");
 }
 if(!context())return false;
 makeCurrent();
 GLint maxRb=4096;
 glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&maxRb);
 const int w=std::min(targetWidth,int(maxRb));
 const int h=std::min(targetHeight,int(maxRb));
 QOpenGLFramebufferObjectFormat fmt;
 fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
 fmt.setSamples(w<=4096?4:0);
 QOpenGLFramebufferObject fbo(w,h,fmt);
 if(!fbo.isValid()){doneCurrent();return false;}
 fbo.bind();
 renderScene3DToTarget(fbo.handle(),w,h,QSize(w,h),false,transparentBg);
 fbo.release();
 QImage img=fbo.toImage();
 doneCurrent();
 if(img.isNull())return false;
 if(img.width()!=targetWidth||img.height()!=targetHeight){
  img=img.scaled(targetWidth,targetHeight,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
 }
 return img.save(file,"PNG");
}
bool Viewport::exportPlanPng(const QString &file,int targetWidth,int targetHeight,bool transparentBg){
 if(!project_||targetWidth<=0||targetHeight<=0)return false;
 const auto savedCam=camera_;
 camera_.mode="2d";
 const bool ok=exportHighResPng(file,targetWidth,targetHeight,transparentBg);
 camera_=savedCam;
 return ok;
}
bool Viewport::exportSun3dPng(const QString &file,int targetWidth,int targetHeight,bool transparentBg){
 if(!project_||targetWidth<=0||targetHeight<=0||!context())return false;
 const auto savedCam=camera_;
 camera_.mode="3d";
 camera_.exploded=false;
 if(camera_.projection!="isometric"&&camera_.projection!="perspective")camera_.preset("isometric");
 camera_.solar.enabled=true;
 camera_.solar.goldenHour=true;
 if(camera_.solar.hour<6.5||camera_.solar.hour>17.5)camera_.solar.hour=16.25;
 sunDirty_=true;
 const bool ok=exportHighResPng(file,targetWidth,targetHeight,transparentBg);
 camera_=savedCam;
 sunDirty_=true;
 update();
 return ok;
}
bool Viewport::exportSunTimelapseGif(const QString &file,int targetWidth,int targetHeight,int frames,int delayCs){
 if(!project_||!context()||targetWidth<=0||targetHeight<=0||frames<2)return false;
 const auto savedCam=camera_;
 makeCurrent();
 GLint maxRb=4096;glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&maxRb);
 const int w=std::clamp(targetWidth,240,std::min(1600,int(maxRb)));
 const int h=std::clamp(targetHeight,160,std::min(1200,int(maxRb)));
 QOpenGLFramebufferObjectFormat fmt;
 fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
 fmt.setSamples(4);
 QOpenGLFramebufferObject fbo(w,h,fmt);
 if(!fbo.isValid()){doneCurrent();return false;}

 camera_.mode="3d";
 camera_.exploded=false;
 camera_.solar.enabled=true;
 camera_.solar.goldenHour=true;
 camera_.solar.skyDome=true;
 if(dirty_)rebuild();

 QVector<QImage> seq;
 seq.reserve(frames);
 for(int i=0;i<frames;++i){
  const double t=double(i)/std::max(1,frames-1);
  camera_.solar.hour=6.5+t*11.0; // 06:30 -> 17:30 full day sun study
  sunDirty_=true;
  fbo.bind();
  renderScene3DToTarget(fbo.handle(),w,h,QSize(w,h),false,false);
  fbo.release();
  QImage img=fbo.toImage().convertToFormat(QImage::Format_RGB32);
  // Crisp minimalist architectural solar timestamp badge
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing,true);
  const int hh=int(std::floor(camera_.solar.hour)),mm=int(qRound((camera_.solar.hour-hh)*60.0));
  const auto sp=ga::solarPosition(project_->site.lat,camera_.solar.dayOfYear,camera_.solar.hour);
  const QString badge=QString::fromUtf8("☀ %1:%2  ·  Alt %3°  ·  %4")
   .arg(hh,2,10,QChar('0')).arg(mm,2,10,QChar('0')).arg(sp.altitudeDeg,0,'f',1).arg(project_->site.name);
  p.setFont(QFont("Segoe UI",11,QFont::DemiBold));
  const int bw=std::min(w-32,p.fontMetrics().horizontalAdvance(badge)+28);
  const QRectF br(16,16,bw,32);
  p.setPen(QPen(QColor(255,255,255,48),1.0));
  p.setBrush(QColor(20,19,18,215));
  p.drawRoundedRect(br,16,16);
  p.setPen(QColor("#ffd88a"));
  p.drawText(br,Qt::AlignCenter,badge);
  p.end();
  seq.append(std::move(img));
 }
 camera_=savedCam;
 sunDirty_=true;
 doneCurrent();
 update();
 return ga::writeAnimatedGif(file,seq,delayCs);
}
bool Viewport::exportTurntableGif(const QString &file,int targetWidth,int targetHeight,int frames,int delayCs){
 if(!project_||!context()||targetWidth<=0||targetHeight<=0||frames<4)return false;
 const auto savedCam=camera_;
 makeCurrent();
 GLint maxRb=4096;glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&maxRb);
 const int w=std::clamp(targetWidth,240,std::min(1600,int(maxRb)));
 const int h=std::clamp(targetHeight,160,std::min(1200,int(maxRb)));
 QOpenGLFramebufferObjectFormat fmt;
 fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
 fmt.setSamples(4);
 QOpenGLFramebufferObject fbo(w,h,fmt);
 if(!fbo.isValid()){doneCurrent();return false;}

 camera_.mode="3d";
 camera_.exploded=false;
 if(camera_.elevation>65.0||camera_.elevation<15.0)camera_.elevation=35.264;
 camera_.solar.enabled=true;
 camera_.solar.goldenHour=true;
 camera_.solar.skyDome=true;
 if(dirty_)rebuild();
 sunDirty_=true;

 QVector<QImage> seq;
 seq.reserve(frames);
 const double startYaw=camera_.yaw;
 for(int i=0;i<frames;++i){
  camera_.yaw=startYaw+(360.0*double(i)/frames);
  sortDirty_=true;
  fbo.bind();
  renderScene3DToTarget(fbo.handle(),w,h,QSize(w,h),false,false);
  fbo.release();
  QImage img=fbo.toImage().convertToFormat(QImage::Format_RGB32);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing,true);
  const QString badge=QString::fromUtf8("360° · %1°  ·  %2").arg(qRound(std::fmod(std::fmod(camera_.yaw,360.0)+360.0,360.0))).arg(project_->site.name);
  p.setFont(QFont("Segoe UI",11,QFont::DemiBold));
  const int bw=std::min(w-32,p.fontMetrics().horizontalAdvance(badge)+28);
  const QRectF br(16,16,bw,32);
  p.setPen(QPen(QColor(255,255,255,48),1.0));
  p.setBrush(QColor(20,19,18,215));
  p.drawRoundedRect(br,16,16);
  p.setPen(QColor("#f5f3ef"));
  p.drawText(br,Qt::AlignCenter,badge);
  p.end();
  seq.append(std::move(img));
 }
 camera_=savedCam;
 sortDirty_=sunDirty_=true;
 doneCurrent();
 update();
 return ga::writeAnimatedGif(file,seq,delayCs);
}
bool Viewport::exportObj(const QString &file){
 if(!project_)return false;
 auto c=camera_;c.mode="3d";c.exploded=false;c.solar.enabled=false;
 auto exportLayers=layers_;for(auto &layer:exportLayers)layer.presentation.remove("shadow");
 const ga::PlotDesign *activePlot=(plot_.active&&massing_.enabled)?&plot_:nullptr;
 const ga::MassingConfig *activeMassing=(plot_.active&&massing_.enabled)?&massing_:nullptr;
 const QVector<ga::ImportedModel3D> *activeModels3d=models3d_.isEmpty()?nullptr:&models3d_;
 auto mesh=ga::buildMesh(*project_,exportLayers,palette_,c,diagram_,-1,false,nullptr,activePlot,activeMassing,activeModels3d);mesh.triangles.append(mesh.transparent);
 QSaveFile out(file);if(!out.open(QIODevice::WriteOnly))return false;
 QTextStream s(&out);s.setRealNumberPrecision(9);
 s<<"# GroundAtlas Desktop · native geometry export\n# Metres; X east, Y north, Z up.\n# "<<project_->source["attribution"].toString()<<"\n# AI heights are estimates; unknown heights remain flat.\n";
 for(const auto &v:mesh.triangles)s<<"v "<<v.x<<' '<<v.y<<' '<<v.z<<' '<<v.r<<' '<<v.g<<' '<<v.b<<'\n';
 for(int i=0;i<mesh.triangles.size();i+=3)s<<"f "<<i+1<<' '<<i+2<<' '<<i+3<<'\n';
 s.flush();return out.commit();
}
QJsonObject Viewport::basemapDiagnostics()const{
 auto state=basemap_->diagnostics();state["visible"]=contextVisible_;state["projection"]=camera_.projection;
 state["performance"]=QJsonObject{{"mesh_builds",double(meshBuilds_)},{"buffer_uploads",double(bufferUploads_)},{"uploaded_bytes",double(uploadedBytes_)},{"plan_paints",double(planPaints_)}};
 state["hardware_shadow_map"]=bool(shadowFbo_&&shadowTex_);
 state["shadow_map_size"]=shadowSize_;
 state["ssao_enabled"]=bool(gfxConfig_.ssaoEnabled&&ssaoReady_);
 state["archviz_v2"]=true;
 state["target_fps"]=gfxConfig_.targetFps;
 if(!project_||!state["initialized"].toBool())return state;
 QJsonArray errors;double maximum=0;QVector<QPointF> points;const auto r=std::min(500.,ga::envelopeRadius(project_->site));
 for(const auto &p:QVector<QPointF>{{0,0},{r,0},{-r,0},{0,r},{0,-r},{r*.5,r*.5}})points.append(ga::localFromScenePlane(p,project_->site,camera_));
 state["landmark_scope"]="view-centred Mercator plane";
 const auto m=matrix();
 for(const auto &p:points){const auto world=ga::scenePlanePoint(p,project_->site,camera_),ll=ga::geographicPoint(p,project_->site);auto clip=m*QVector4D(world.x(),world.y(),0,1);const auto expected=QPointF((clip.x()/clip.w()+1)*width()/2.,(1-clip.y()/clip.w())*height()/2.);const auto actual=basemap_->pixelForCoordinate(ll.y(),ll.x());const double error=QLineF(expected,actual).length();maximum=std::max(maximum,error);errors.append(error);}
 QRectF bounds;bool first=true;
 for(const auto *vertices:{&mesh_.triangles,&mesh_.lines,&mesh_.transparent})
  for(const auto &v:*vertices){const auto point=ga::scenePlaneFromMapPoint({v.x,v.y},project_->site,camera_);const auto q=m*QVector4D(point.x(),point.y(),v.z,1);if(q.w()<=0)continue;const QPointF pixel((q.x()/q.w()+1)*width()/2.,(1-q.y()/q.w())*height()/2.);if(first){bounds=QRectF(pixel,QSizeF(.0001,.0001));first=false;}else bounds=bounds.united(QRectF(pixel,QSizeF(.0001,.0001)));}
 state["projected_geometry_bounds"]=QJsonArray{bounds.left(),bounds.top(),bounds.right(),bounds.bottom()};
 state["stack_height_metres"]=stackHeight_;state["landmark_pixel_errors"]=errors;state["maximum_landmark_error_px"]=maximum;return state;
}
