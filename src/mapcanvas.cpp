#include "mapcanvas.h"
#include "planningtiles.h"
#include "sitegeometry.h"
#include "planpainter.h"
#include "urbananalytics.h"
#include "vectorstyle.h"
#include <QMapLibre/Map>
#include <QMapLibre/Settings>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocale>
#include <QLineF>
#include <cmath>

namespace {
QString satelliteStyleJson() {
 const QJsonObject style{
  {"version", 8},
  {"name", "Esri World Imagery"},
  {"sources", QJsonObject{
   {"esri-imagery", QJsonObject{
    {"type", "raster"},
    {"tiles", QJsonArray{"https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"}},
    {"tileSize", 256},
    {"maxzoom", 19},
    {"attribution", "Esri, Maxar, Earthstar Geographics"}
   }}
  }},
  {"layers", QJsonArray{
   QJsonObject{{"id", "bg"}, {"type", "background"}, {"paint", QJsonObject{{"background-color", "#14181e"}}}},
   QJsonObject{{"id", "satellite"}, {"type", "raster"}, {"source", "esri-imagery"}, {"minzoom", 0}, {"maxzoom", 22}}
  }}
 };
 return QString::fromUtf8(QJsonDocument(style).toJson(QJsonDocument::Compact));
}

QString formatAreaLabel(double areaM2, bool english) {
 const QLocale loc(english ? "en_US" : "vi_VN");
 if (areaM2 >= 1000000.0) return loc.toString(areaM2 / 1000000.0, 'f', 2) + " km²";
 if (areaM2 >= 10000.0) return loc.toString(areaM2 / 10000.0, 'f', 2) + " ha";
 return loc.toString(areaM2, 'f', 0) + " m²";
}

struct CityPreset {
 const char *id;
 const char *labelVi;
 const char *labelEn;
 const char *nameVi;
 const char *nameEn;
 double lat;
 double lon;
};

const QVector<CityPreset> &vietnamCities() {
 static const QVector<CityPreset> list = {
  {"city:hn", "Hà Nội", "Hanoi", "Hồ Hoàn Kiếm, Hà Nội", "Hoan Kiem, Hanoi", 21.0288, 105.8525},
  {"city:hl", "Hạ Long", "Ha Long", "Cảng tàu QT Hạ Long · CV Đại Dương", "Ha Long Int'l Cruise Port · Ocean Park", 20.95334, 107.055938},
  {"city:hcm", "TP.HCM", "HCMC", "Quận 1, TP. Hồ Chí Minh", "District 1, Ho Chi Minh City", 10.7769, 106.7009},
  {"city:dn", "Đà Nẵng", "Da Nang", "Cầu Rồng, Đà Nẵng", "Dragon Bridge, Da Nang", 16.0610, 108.2272},
  {"city:hue", "Huế", "Hue", "Kinh thành Huế", "Imperial City, Hue", 16.4698, 107.5784},
  {"city:dl", "Đà Lạt", "Da Lat", "Hồ Xuân Hương, Đà Lạt", "Xuan Huong Lake, Da Lat", 11.9404, 108.4421},
  {"city:pq", "Phú Quốc", "Phu Quoc", "Dương Đông, Phú Quốc", "Duong Dong, Phu Quoc", 10.2189, 103.9595},
 };
 return list;
}
} // namespace

MapCanvas::MapCanvas(const QString &directory,QWidget *parent):QOpenGLWidget(parent),cacheDirectory_(directory){
 interactionIdle_.setSingleShot(true);interactionIdle_.setInterval(120);connect(&interactionIdle_,&QTimer::timeout,this,[this]{interacting_=false;overlayDirty_=true;hudDirty_=true;update();if(deferredNavSync_){deferredNavSync_=false;emit navigationChanged();emit viewChanged();}});
 auto surface=QSurfaceFormat::defaultFormat();surface.setStencilBufferSize(8);surface.setVersion(3,3);surface.setProfile(QSurfaceFormat::CoreProfile);setFormat(surface);
 loadingTimer_=new QTimer(this);loadingTimer_->setSingleShot(true);loadingTimer_->setInterval(25000);connect(loadingTimer_,&QTimer::timeout,this,[this]{if(!fullyRendered_&&mapError_.isEmpty()){mapError_=english_?"Some basemap resources are still loading.":"Một số thành phần nền bản đồ vẫn đang tải.";hudDirty_=true;emit message(mapError_);update();}});
 setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);setMinimumSize(360,300);setAccessibleName("Native vector geographic map");
 QFile file(ga::appDataPath("basemap/positron.json"));if(file.open(QIODevice::ReadOnly))originalStyle_=QJsonDocument::fromJson(file.readAll()).object();
}
MapCanvas::~MapCanvas(){cleanupVector();if(context())disconnect(context(),nullptr,this,nullptr);}
void MapCanvas::shutdown(){cleanupVector();}
void MapCanvas::cleanupVector(){
 const bool trace=qEnvironmentVariableIsSet("GA_NATIVE_LIFECYCLE_TRACE");
 if(trace)qInfo()<<"vector cleanup: enter";
 if(vector_){if(context())makeCurrent();vector_->destroyRenderer();if(trace)qInfo()<<"vector cleanup: renderer destroyed";vector_.reset();if(trace)qInfo()<<"vector cleanup: map destroyed";if(vectorVao_.isCreated())vectorVao_.destroy();if(context())doneCurrent();}
 styleReady_=fullyRendered_=false;appliedHanoiLayer_.clear();lastVectorSize_={};lastVectorPixelSize_={};lastVectorFbo_=0;loadingTimer_->stop();if(trace)qInfo()<<"vector cleanup: complete";
}
void MapCanvas::loadStyleForMode(){
 if(!vector_)return;
 styleReady_=false;fullyRendered_=false;appliedHanoiLayer_.clear();mapError_.clear();hudDirty_=true;
 if(basemapMode_=="satellite"){
  vector_->setStyleJson(satelliteStyleJson());
 }else{
  if(originalStyle_.isEmpty())vector_->setStyleUrl("https://tiles.openfreemap.org/styles/positron");
  else vector_->setStyleJson(QString::fromUtf8(QJsonDocument(originalStyle_).toJson(QJsonDocument::Compact)));
 }
 loadingTimer_->start();
}
void MapCanvas::setBasemapMode(const QString &mode){
 const QString normalized=(mode=="satellite")?"satellite":"vector";
 if(basemapMode_==normalized)return;
 basemapMode_=normalized;
 hudDirty_=true;
 loadStyleForMode();
 update();
 emit basemapModeChanged(basemapMode_);
 emit viewChanged();
}
void MapCanvas::setHanoiPlanning(bool enabled,const QString &layerId,double opacity){
 const QString norm=ga::normalizePlanningLayerId(layerId);
 const double op=std::clamp(opacity,0.05,1.0);
 if(hanoiPlanEnabled_==enabled&&hanoiPlanLayer_==norm&&std::abs(hanoiPlanOpacity_-op)<1e-3&&norm!="custom")return;
 hanoiPlanEnabled_=enabled;
 hanoiPlanLayer_=norm;
 hanoiPlanOpacity_=op;
 hudDirty_=true;
 if(norm=="custom")appliedHanoiLayer_.clear();
 syncHanoiPlanningLayer();
 update();
 emit viewChanged();
}
void MapCanvas::syncHanoiPlanningLayer(){
 if(!vector_||!styleReady_)return;
 const QString srcId=QStringLiteral("ga-hanoi-qh");
 const QString lyrId=QStringLiteral("ga-hanoi-qh-layer");
 if(!hanoiPlanEnabled_){
  if(vector_->layerExists(lyrId))vector_->removeLayer(lyrId);
  if(vector_->sourceExists(srcId))vector_->removeSource(srcId);
  appliedHanoiLayer_.clear();
  return;
 }
 if(appliedHanoiLayer_!=hanoiPlanLayer_||!vector_->sourceExists(srcId)||!vector_->layerExists(lyrId)){
  if(vector_->layerExists(lyrId))vector_->removeLayer(lyrId);
  if(vector_->sourceExists(srcId))vector_->removeSource(srcId);
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
  vector_->addSource(srcId,src);
  vector_->addLayer(lyrId,QVariantMap{
   {"type","raster"},
   {"source",srcId}
  });
  vector_->setPaintProperty(lyrId,QStringLiteral("raster-fade-duration"),0);
  appliedHanoiLayer_=hanoiPlanLayer_;
 }
 vector_->setPaintProperty(lyrId,QStringLiteral("raster-opacity"),hanoiPlanOpacity_);
}
void MapCanvas::initializeGL(){
 connect(context(),&QOpenGLContext::aboutToBeDestroyed,this,&MapCanvas::cleanupVector,Qt::DirectConnection);
 vectorVao_.create();
 QMapLibre::Settings settings;settings.setContextMode(QMapLibre::Settings::SharedGLContext);settings.setMapMode(QMapLibre::Settings::Continuous);settings.setProviderTemplate(QMapLibre::Settings::NoProvider);QDir().mkpath(cacheDirectory_);settings.setCacheDatabasePath(cacheDirectory_+"/vector-cache.db");settings.setCacheDatabaseMaximumSize(256*1024*1024);settings.setClientName("GroundAtlasDesktop");settings.setClientVersion(QCoreApplication::applicationVersion());
#ifdef Q_OS_MACOS
 settings.setLocalFontFamily("SF Pro Text");
#else
 settings.setLocalFontFamily("Segoe UI");
#endif
 const auto gl=context()->functions();const auto name=gl->glGetString(GL_RENDERER);renderer_=name?QString::fromLatin1(reinterpret_cast<const char*>(name)):QString();
 vector_=std::make_unique<QMapLibre::Map>(nullptr,settings,size(),devicePixelRatioF());
 lastVectorSize_=size();
 connect(vector_.get(),&QMapLibre::Map::needsRendering,this,QOverload<>::of(&MapCanvas::update));
 connect(vector_.get(),&QMapLibre::Map::mapLoadingFailed,this,[this](QMapLibre::Map::MapLoadingFailure,const QString &reason){mapError_=reason;hudDirty_=true;emit message(english_?"Basemap temporarily unavailable; loaded site data is retained.":"Chưa nạp được nền bản đồ; dữ liệu khu đất vẫn được giữ.");update();});
 connect(vector_.get(),&QMapLibre::Map::mapChanged,this,[this](QMapLibre::Map::MapChange change){
  if(change==QMapLibre::Map::MapChangeDidFinishLoadingStyle){styleReady_=true;mapError_.clear();hudDirty_=true;if(basemapMode_=="vector")applyVectorStyle();syncHanoiPlanningLayer();}
  if(change==QMapLibre::Map::MapChangeDidFinishRenderingMapFullyRendered||change==QMapLibre::Map::MapChangeDidFinishRenderingFrameFullyRendered){const bool nextReady=vector_->isFullyLoaded();if(nextReady!=fullyRendered_){fullyRendered_=nextReady;hudDirty_=true;}if(fullyRendered_){if(!mapError_.isEmpty()){mapError_.clear();hudDirty_=true;}loadingTimer_->stop();}}
 });
 syncVectorView();
 loadStyleForMode();
}
void MapCanvas::resizeGL(int,int){overlayDirty_=true;hudDirty_=true;fullyRendered_=false;if(vector_)loadingTimer_->start();}
void MapCanvas::syncVectorView(){overlayDirty_=true;hudDirty_=true;fullyRendered_=false;if(vector_){mapError_.clear();if(!interacting_)loadingTimer_->start();vector_->setCoordinateZoom({lat_,lon_},zoom_-1);vector_->setBearing(0);vector_->setPitch(0);}update();}
void MapCanvas::setContextEnabled(bool value){if(contextEnabled_==value)return;contextEnabled_=value;if(basemapMode_=="vector")applyVectorStyle();update();emit viewChanged();}
void MapCanvas::applyVectorStyle(){if(!vector_||!styleReady_||basemapMode_!="vector")return;fullyRendered_=false;loadingTimer_->start();if(originalStyle_.isEmpty())originalStyle_=QJsonDocument::fromJson(vector_->styleJson().toUtf8()).object();const auto styled=ga::restyleContext(originalStyle_,layers_,palette_.paper.name(),contextEnabled_);const auto originalLayers=originalStyle_["layers"].toArray(),styledLayers=styled["layers"].toArray();for(int i=0;i<originalLayers.size()&&i<styledLayers.size();++i){const auto original=originalLayers[i].toObject(),changed=styledLayers[i].toObject();const auto id=original["id"].toString();for(const auto &key:ga::contextPaintProperties(original)){const auto value=changed["paint"].toObject()[key];if(!vector_->setPaintProperty(id,key,value.toVariant()))mapError_="A basemap paint property could not be applied.";}}}
QJsonObject MapCanvas::basemapDiagnostics()const{return{{"provider",basemapMode_=="satellite"?"Esri World Imagery":"OpenFreeMap / OpenMapTiles / OpenStreetMap"},{"basemap_mode",basemapMode_},{"renderer_engine","MapLibre Native for Qt 3.0.0"},{"renderer",renderer_},{"native",true},{"webview",false},{"gl_error",int(vectorGlError_)},{"gl_profile",context()?int(context()->format().profile()):-1},{"style_loaded",styleReady_},{"fully_rendered",fullyRendered_},{"context_styling",contextEnabled_},{"style_layers",originalStyle_["layers"].toArray().size()},{"error",mapError_},{"view",view()}};}
QPointF MapCanvas::world(double lon,double lat,double z)const{const double size=256*std::pow(2,z),s=std::sin(std::clamp(lat,-85.05112878,85.05112878)*M_PI/180);return{(lon+180)/360*size,(.5-std::log((1+s)/(1-s))/(4*M_PI))*size};}
QPointF MapCanvas::unworld(const QPointF &p,double z)const{const double size=256*std::pow(2,z);double lon=p.x()/size*360-180;lon=std::remainder(lon,360.);return{lon,std::clamp(std::atan(std::sinh(M_PI*(1-2*p.y()/size)))*180/M_PI,-85.,85.)};}
QPointF MapCanvas::screen(double lon,double lat)const{auto delta=world(lon,lat,zoom_)-world(lon_,lat_,zoom_);delta.setX(std::remainder(delta.x(),256*std::pow(2,zoom_)));return delta+QPointF(width()/2.,height()/2.);}
QPointF MapCanvas::location()const{return{lon_,lat_};}
void MapCanvas::centerOn(double lat,double lon,double zoom){lat_=std::clamp(lat,-85.,85.);lon_=std::remainder(lon,360.);zoom_=std::clamp(zoom,2.,20.);syncVectorView();emit navigationChanged();emit viewChanged();}
void MapCanvas::setProject(const ga::Project *p){project_=p;planGeometry_={};interactionImage_.clear();planCache_=p?preparePlan(*p,layers_,diagram_):PlanCache();if(p){const double z=std::log2(156543.03392*std::cos(p->site.lat*M_PI/180)*std::max(400,height())/(p->site.radius*2.5));centerOn(p->site.lat,p->site.lon,z);}overlayDirty_=true;hudDirty_=true;update();}
void MapCanvas::setPresentation(QVector<ga::Layer> layers,ga::Palette p,QString diagram){interactionImage_.clear();layers_=std::move(layers);palette_=std::move(p);diagram_=std::move(diagram);planCache_=project_?preparePlan(*project_,layers_,diagram_):PlanCache();overlayDirty_=true;hudDirty_=true;applyVectorStyle();update();}
void MapCanvas::beginInteraction(){interacting_=true;interactionIdle_.start();}
void MapCanvas::zoomBy(double amount){beginInteraction();zoom_=std::clamp(zoom_+amount,2.,20.);syncVectorView();emit navigationChanged();emit viewChanged();}
void MapCanvas::paintGL(){
 QPainter p(this);p.beginNativePainting();
 const double dpr=devicePixelRatioF();
 const QSize pixelSize(qRound(width()*dpr),qRound(height()*dpr));
 if(pixelSize.isEmpty()){p.endNativePainting();return;}
 if(vector_){
  auto gl=context()->functions();
  for(int i=0;i<8&&gl->glGetError()!=GL_NO_ERROR;++i){}
  if(vectorVao_.isCreated())vectorVao_.bind();
  if(size()!=lastVectorSize_){vector_->resize(size());lastVectorSize_=size();}
  const unsigned fbo=defaultFramebufferObject();
  if(fbo!=lastVectorFbo_||pixelSize!=lastVectorPixelSize_){
   vector_->setOpenGLFramebufferObject(fbo,pixelSize);
   lastVectorFbo_=fbo;lastVectorPixelSize_=pixelSize;
  }
  vector_->render();
  vectorGlError_=gl->glGetError();
  if(vectorVao_.isCreated())vectorVao_.release();
 }else{
  auto gl=context()->functions();
  gl->glClearColor(darkUi_?.09f:.95f,darkUi_?.10f:.95f,darkUi_?.12f:.93f,1);
  gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 }
 p.endNativePainting();

 // Skip off-screen or empty site overlay allocation & upload completely (huge zoom perf win)
 if(overlayDirty_||(hasSiteOverlay_&&siteOverlay_.size()!=pixelSize)){
  bool siteVisible=false;
  if(project_&&!project_->features.isEmpty()){
   const auto siteCenter=screen(project_->site.lon,project_->site.lat);
   const double pixelsPerM=256.0*std::pow(2.0,zoom_)/(40075016.686*std::cos(project_->site.lat*M_PI/180.0));
   const double extent=ga::envelopeRadius(project_->site)*pixelsPerM;
   siteVisible=(extent>=3.0&&QRectF(siteCenter-QPointF(extent*1.5,extent*1.5),QSizeF(extent*3.0,extent*3.0)).intersects(rect()));
  }
  hasSiteOverlay_=siteVisible;
  if(siteVisible){
   if(siteOverlay_.size()!=pixelSize){
    siteOverlay_=QImage(pixelSize,QImage::Format_ARGB32_Premultiplied);
    siteOverlay_.setDevicePixelRatio(dpr);
   }
   siteOverlay_.fill(Qt::transparent);
   QPainter site(&siteOverlay_);site.setRenderHint(QPainter::Antialiasing);
   drawGeometry(site);
   site.end();
  }
  overlayDirty_=false;
 }

 // Reuse persistent HUD QImage & skip repaint + GPU re-upload on async tile-arrival frames!
 if(hudDirty_||hudBuffer_.size()!=pixelSize){
  if(hudBuffer_.size()!=pixelSize){
   hudBuffer_=QImage(pixelSize,QImage::Format_ARGB32_Premultiplied);
   hudBuffer_.setDevicePixelRatio(dpr);
  }
  hudBuffer_.fill(Qt::transparent);
  QPainter ui(&hudBuffer_);
  ui.setRenderHint(QPainter::Antialiasing);
  ui.setRenderHint(QPainter::TextAntialiasing);
  drawSelection(ui);
  drawHud(ui);
  ui.end();
  hudDirty_=false;
 }

 if(hasSiteOverlay_)p.drawImage(QPointF(0,0),siteOverlay_);
 p.drawImage(QPointF(0,0),hudBuffer_);
}
void MapCanvas::drawGeometry(QPainter &p){if(!project_||project_->features.isEmpty())return;const auto siteCenter=screen(project_->site.lon,project_->site.lat);const double pixelsPerM=256*std::pow(2,zoom_)/(40075016.686*std::cos(project_->site.lat*M_PI/180)),extent=ga::envelopeRadius(project_->site)*pixelsPerM;if(extent<3||!QRectF(siteCenter-QPointF(extent*1.5,extent*1.5),QSizeF(extent*3,extent*3)).intersects(rect()))return;
 const auto point=[&](QPointF v){const auto ll=ga::geographicPoint(v,project_->site);return screen(ll.x(),ll.y());};const double period=256*std::pow(2,zoom_);if(std::abs(siteCenter.x()-width()/2.)+extent*2<period/2){if(planGeometry_.paths.size()!=project_->features.size())planGeometry_=preparePlanGeometry(*project_);const double scale=period/(360*111320*std::cos(project_->site.lat*M_PI/180));const QTransform transform(scale,0,0,-scale,siteCenter.x(),siteCenter.y());if(!interacting_||!interactionImage_.reusable(transform,size(),devicePixelRatioF())){const auto imageTransform=interactionImage_.begin(size(),devicePixelRatioF(),transform,Qt::transparent);QPainter retained(&interactionImage_.image);paintPlanMercator(retained,*project_,layers_,palette_,diagram_,imageTransform,pixelsPerM,-1,false,planCache_,planGeometry_);retained.end();}interactionImage_.draw(p,transform);}else paintPlan(p,*project_,layers_,palette_,diagram_,point,pixelsPerM,true,-1,false,&planCache_);}
QPointF MapCanvas::geographicAt(const QPointF &point)const{return unworld(world(lon_,lat_,zoom_)+point-QPointF(width()/2.,height()/2.),zoom_);}

void MapCanvas::drawSelection(QPainter &p){
 p.save();
 const bool sat=(basemapMode_=="satellite");
 const QColor strokeCol=sat?QColor("#60a5fa"):QColor("#3b63fb");
 const QColor fillCol=sat?QColor(96,165,250,36):QColor(59,99,251,22);
 const auto marker=screen(candidate_.lon,candidate_.lat);
 QPolygonF ring;
 const auto boundary=candidate_.boundary.isEmpty()?ga::primitiveBoundary(candidate_):candidate_.boundary;
 for(const auto &v:boundary){const auto ll=ga::geographicPoint(v,candidate_);ring.append(screen(ll.x(),ll.y()));}
 if(ring.size()>=3){
  // Subtle halo under boundary on satellite for crisp readability
  if(sat){p.setPen(QPen(QColor(0,0,0,120),3.4,Qt::SolidLine));p.setBrush(Qt::NoBrush);p.drawPolygon(ring);}
  p.setPen(QPen(strokeCol,1.8,Qt::DashLine));p.setBrush(fillCol);p.drawPolygon(ring);
 }
 if(candidate_.shape=="polygon"&&!candidateLocked_&&!drawing_){
  p.setBrush(Qt::white);p.setPen(QPen(strokeCol,1.6));
  for(const auto &v:ring)p.drawEllipse(v,4.5,4.5);
 }

 // Live area badge under candidate center when not drawing
 const double candArea=std::abs(ga::ringArea(boundary));
 if(!drawing_&&candArea>10.0&&ring.boundingRect().width()>55.0){
  const QString areaTxt=formatAreaLabel(candArea,english_);
  p.setFont(QFont("Segoe UI",8,QFont::DemiBold));
  const int tw=p.fontMetrics().horizontalAdvance(areaTxt)+16;
  const QRectF pill(marker.x()-tw/2.0,marker.y()+13.0,tw,20.0);
  p.setPen(QPen(darkUi_||sat?QColor(255,255,255,44):QColor(59,99,251,72),1.0));
  p.setBrush(darkUi_||sat?QColor(24,24,24,224):QColor(255,255,255,240));
  p.drawRoundedRect(pill,10.0,10.0);
  p.setPen(darkUi_||sat?QColor("#e0eaff"):QColor("#274dea"));
  p.drawText(pill,Qt::AlignCenter,areaTxt);
 }

 if(drawing_){
  QPolygonF draft;
  for(const auto &ll:draft_)draft.append(screen(ll.x(),ll.y()));
  p.setBrush(QColor(59,130,246,34));p.setPen(QPen(QColor("#2563eb"),2.2));
  if(draft.size()>=3)p.drawPolygon(draft);else p.drawPolyline(draft);
  if(!draft.isEmpty()&&hasHover_&&!freehand_){
   p.setPen(QPen(QColor("#3b82f6"),1.4,Qt::DashLine));
   p.drawLine(draft.last(),hoverScreen_);
   if(draft.size()>=2)p.drawLine(hoverScreen_,draft.first());
  }
  // Edge length labels for clarity while sketching boundary
  p.setFont(QFont("Segoe UI",7,QFont::DemiBold));
  for(int i=1;i<draft.size()&&draft.size()<=18;++i){
   const QPointF a=draft[i-1],b=draft[i];
   if(QLineF(a,b).length()<36.0)continue;
   const QPointF la=ga::localPoint(draft_[i-1].x(),draft_[i-1].y(),candidate_);
   const QPointF lb=ga::localPoint(draft_[i].x(),draft_[i].y(),candidate_);
   const double segM=QLineF(la,lb).length();
   const QString lbl=segM>=1000.0?QString::number(segM/1000.0,'f',2)+" km":QString::number(qRound(segM))+" m";
   const QPointF mid=(a+b)*0.5;
   const int lw=p.fontMetrics().horizontalAdvance(lbl)+10;
   const QRectF lr(mid.x()-lw/2.0,mid.y()-8.0,lw,16.0);
   p.setPen(Qt::NoPen);p.setBrush(QColor(15,23,42,205));p.drawRoundedRect(lr,6.0,6.0);
   p.setPen(QColor("#f8fafc"));p.drawText(lr,Qt::AlignCenter,lbl);
  }
  // Live polygon area pill at centroid while drawing (>=3 points)
  if(draft_.size()>=3){
   QPolygonF localDraft;
   for(const auto &ll:draft_)localDraft.append(ga::localPoint(ll.x(),ll.y(),candidate_));
   const double draftArea=std::abs(ga::ringArea(localDraft));
   const QPointF ctr=draft.boundingRect().center();
   const QString lbl=formatAreaLabel(draftArea,english_)+" · "+QString::number(draft_.size())+(english_?" pts":" đỉnh");
   p.setFont(QFont("Segoe UI",8,QFont::DemiBold));
   const int lw=p.fontMetrics().horizontalAdvance(lbl)+18;
   const QRectF lr(ctr.x()-lw/2.0,ctr.y()-11.0,lw,22.0);
   p.setPen(QPen(QColor("#60a5fa"),1.0));p.setBrush(QColor(15,23,42,224));p.drawRoundedRect(lr,11.0,11.0);
   p.setPen(Qt::white);p.drawText(lr,Qt::AlignCenter,lbl);
  }
  p.setBrush(Qt::white);p.setPen(QPen(QColor("#2563eb"),1.6));
  for(const auto &v:draft)p.drawEllipse(v,4.0,4.0);
 }

 // Center crosshair pin
 p.setPen(QPen(Qt::white,3.2));p.drawLine(marker-QPointF(8,0),marker+QPointF(8,0));p.drawLine(marker-QPointF(0,8),marker+QPointF(0,8));
 p.setPen(QPen(strokeCol,1.6));p.setBrush(Qt::white);p.drawEllipse(marker,4.2,4.2);
 p.restore();
}

void MapCanvas::drawHud(QPainter &ui){
 hudButtons_.clear();
 const bool dark=darkUi_||(basemapMode_=="satellite");
 const QColor glassBg=dark?QColor(24,24,24,238):QColor(255,255,255,244);
 const QColor glassBorder=dark?QColor(255,255,255,34):QColor(0,0,0,22);
 const QColor textPrimary=dark?QColor("#ebebeb"):QColor("#1f1f1f");
 const QColor textMuted=dark?QColor("#969696"):QColor("#6e6e6e");
 const QColor activeBg=dark?QColor("#f5f5f5"):QColor("#131313");
 const QColor activeFg=dark?QColor("#131313"):QColor("#ffffff");
 const QColor hoverBg=dark?QColor(255,255,255,18):QColor(0,0,0,12);

 // 1. Top-Left Glass Status & Site Card (14,14 -> stays far above y=180 test zone!)
 const int cardW=std::min(312,std::max(210,width()-28));
 const QRectF topCard(14.0,14.0,cardW,52.0);
 ui.setPen(QPen(glassBorder,1.0));
 ui.setBrush(glassBg);
 ui.drawRoundedRect(topCard,12.0,12.0);

 // Status indicator dot (emerald ready / amber loading)
 ui.setPen(Qt::NoPen);
 ui.setBrush(fullyRendered_?QColor("#16a34a"):QColor("#f59e0b"));
 ui.drawEllipse(QPointF(28.0,30.0),4.0,4.0);

 ui.setFont(QFont("Segoe UI",9,QFont::DemiBold));
 ui.setPen(textPrimary);
 const QString titleStr=ui.fontMetrics().elidedText(candidate_.name.isEmpty()?(project_?project_->site.name:QString("GroundAtlas")):candidate_.name,Qt::ElideRight,cardW-48);
 ui.drawText(QRectF(40.0,18.0,cardW-52.0,22.0),Qt::AlignLeft|Qt::AlignVCenter,titleStr);

 ui.setFont(QFont("Segoe UI",7.5,QFont::Medium));
 ui.setPen(textMuted);
 const QString modeTag=basemapMode_=="satellite"
  ?(english_?"ESRI SATELLITE IMAGERY":"ẢNH VỆ TINH TOÀN CẦU")
  :(fullyRendered_?(english_?"GLOBAL VECTOR BASEMAP":"NỀN VECTOR TOÀN CẦU"):(english_?"VECTOR BASEMAP · LOADING":"NỀN VECTOR · ĐANG TẢI"));
 ui.drawText(QRectF(26.0,40.0,cardW-38.0,18.0),Qt::AlignLeft|Qt::AlignVCenter,
  modeTag+QString(" · z%1").arg(zoom_,0,'f',1));

 // 2. Top-Right Quick Vietnamese City Jump Pills (shown when width >= 740, y=14..48)
 if(width()>=740){
  const auto &cities=vietnamCities();
  QFont cf("Segoe UI",8,QFont::DemiBold);
  ui.setFont(cf);
  double totalW=12.0;
  QVector<double> widths;
  for(const auto &c:cities){
   const QString lbl=QString::fromUtf8(english_?c.labelEn:c.labelVi);
   const double bw=ui.fontMetrics().horizontalAdvance(lbl)+16.0;
   widths.append(bw);
   totalW+=bw+4.0;
  }
  totalW+=4.0;
  const double startX=width()-14.0-totalW;
  if(startX>topCard.right()+12.0){
   const QRectF barRect(startX,14.0,totalW,34.0);
   ui.setPen(QPen(glassBorder,1.0));
   ui.setBrush(glassBg);
   ui.drawRoundedRect(barRect,17.0,17.0);
   double cx=startX+8.0;
   for(int i=0;i<cities.size();++i){
    const auto &c=cities[i];
    const QString lbl=QString::fromUtf8(english_?c.labelEn:c.labelVi);
    const QRectF r(cx,18.0,widths[i],26.0);
    const bool nearCity=std::hypot(lat_-c.lat,lon_-c.lon)<0.04;
    const bool hovered=(hoverHudId_==c.id);
    if(nearCity||hovered){
     ui.setPen(Qt::NoPen);
     ui.setBrush(nearCity?activeBg:hoverBg);
     ui.drawRoundedRect(r,13.0,13.0);
    }
    ui.setPen(nearCity?activeFg:textPrimary);
    ui.drawText(r,Qt::AlignCenter,lbl);
    hudButtons_.append({QString::fromLatin1(c.id),lbl,QString::fromUtf8(english_?c.nameEn:c.nameVi),r,nearCity,false,c.lat,c.lon});
    cx+=widths[i]+4.0;
   }
  }
 }

 // 3. Bottom-Center Floating Glass Dock (y = height()-52..height()-14)
 {
  struct Spec { QString id; QString label; double minW; bool active; bool accent; };
  const bool isHaLongCtx=(hanoiPlanEnabled_&&hanoiPlanLayer_=="halong_cvdd")||(!hanoiPlanEnabled_&&ga::isLikelyInHaLongArea(lat_,lon_));
  const QString planBtnLbl=isHaLongCtx?(english_?"Ha Long Plan":"QH Hạ Long"):(english_?"Hanoi Plan":"QH Hà Nội");
  const QVector<Spec> specs = {
   {"zoom-out", QString::fromUtf8("−"), 28.0, false, false},
   {"zoom-val", QString("z%1").arg(zoom_, 0, 'f', 1), 42.0, false, false},
   {"zoom-in", "+", 28.0, false, false},
   {"recenter", english_ ? "Center" : "Về tâm", 54.0, false, false},
   {"mode-vector", "Vector", 54.0, basemapMode_ == "vector", false},
   {"mode-sat", english_ ? "Satellite" : "Vệ tinh", 60.0, basemapMode_ == "satellite", false},
   {"toggle-hanoi-qh", planBtnLbl, 74.0, hanoiPlanEnabled_, false},
   {"draw", drawing_ ? (english_ ? "Finish" : "Khép ranh") : (english_ ? "Draw" : "Vẽ ranh"), 64.0, drawing_, false},
   {"load-site", english_ ? "Load OSM" : "Nạp OSM", 74.0, false, true},
  };
  QFont df("Segoe UI",8,QFont::DemiBold);
  ui.setFont(df);
  QVector<double> ws;
  double totalW=14.0;
  for(const auto &s:specs){
   const double bw=std::max(s.minW,double(ui.fontMetrics().horizontalAdvance(s.label)+16));
   ws.append(bw);
   totalW+=bw+4.0;
  }
  totalW+=6.0;
  const double dockX=std::max(14.0,(width()-totalW)*0.5);
  const double dockY=height()-52.0;
  if(dockY>90.0&&totalW<width()-24.0){
   const QRectF dockRect(dockX,dockY,totalW,38.0);
   ui.setPen(QPen(glassBorder,1.0));
   ui.setBrush(glassBg);
   ui.drawRoundedRect(dockRect,19.0,19.0);
   double cx=dockX+8.0;
   for(int i=0;i<specs.size();++i){
    const auto &s=specs[i];
    const QRectF r(cx,dockY+5.0,ws[i],28.0);
    const bool hovered=(hoverHudId_==s.id);
    if(s.accent){
     ui.setPen(Qt::NoPen);
     ui.setBrush(hovered?activeBg.lighter(110):activeBg);
     ui.drawRoundedRect(r,14.0,14.0);
     ui.setPen(activeFg);
    }else if(s.active){
     ui.setPen(Qt::NoPen);
     ui.setBrush(dark?QColor(255,255,255,34):QColor(30,29,27,20));
     ui.drawRoundedRect(r,14.0,14.0);
     ui.setPen(textPrimary);
    }else if(hovered&&s.id!="zoom-val"){
     ui.setPen(Qt::NoPen);
     ui.setBrush(hoverBg);
     ui.drawRoundedRect(r,14.0,14.0);
     ui.setPen(textPrimary);
    }else{
     ui.setPen(s.id=="zoom-val"?textMuted:textPrimary);
    }
    ui.drawText(r,Qt::AlignCenter,s.label);
    if(s.id!="zoom-val")hudButtons_.append({s.id,s.label,{},r,s.active,s.accent,0,0});
    cx+=ws[i]+4.0;
   }
  }
 }

 // 4. Subtle bottom-left hint & bottom-right attribution pill (stays below y = height()-22)
 ui.setFont(QFont("Segoe UI",7.5));
 if(width()>=920){
  const QString hint=drawing_
   ?(english_?"Click: add vertex · Shift+drag: freehand · Enter: finish · Esc: cancel":"Bấm: thêm đỉnh · Shift+kéo: vẽ tự do · Enter: khép · Esc: hủy")
   :(english_?"Scroll zoom · Drag pan · Click map to pick site":"Cuộn thu phóng · Kéo di chuyển · Bấm chọn khu đất");
  const int hw=ui.fontMetrics().horizontalAdvance(hint)+18;
  const QRectF hr(14.0,height()-28.0,hw,20.0);
  if(hr.right()<width()*0.5-240.0){
   ui.setPen(Qt::NoPen);ui.setBrush(glassBg);ui.drawRoundedRect(hr,10.0,10.0);
   ui.setPen(textMuted);ui.drawText(hr,Qt::AlignCenter,hint);
  }
 }
 const QString baseAttr=basemapMode_=="satellite"
  ?"Esri World Imagery · Maxar · Earthstar"
  :"OpenFreeMap · © OpenMapTiles · © OSM";
 const QString planAttr=hanoiPlanLayer_=="halong_cvdd"
  ?QStringLiteral(" · QH Hạ Long CVDD 2025")
  :QStringLiteral(" · quyhoach.hanoi.vn");
 const QString attribution=hanoiPlanEnabled_?(baseAttr+planAttr):baseAttr;
 const int creditW=std::min(width()-24,ui.fontMetrics().horizontalAdvance(attribution)+18);
 const QRectF creditR(width()-creditW-10.0,height()-24.0,creditW,18.0);
 if(width()>=820||creditR.left()>width()*0.5+200.0){
  ui.setPen(Qt::NoPen);ui.setBrush(glassBg);ui.drawRoundedRect(creditR,9.0,9.0);
  ui.setPen(textMuted);ui.drawText(creditR,Qt::AlignCenter,attribution);
 }

 // 5. Optional warning banner below top-left card
 if(!mapError_.isEmpty()){
  const QRectF errR(14.0,74.0,std::min(420,width()-28),34.0);
  ui.setPen(QPen(QColor("#d97706"),1.0));
  ui.setBrush(glassBg);
  ui.drawRoundedRect(errR,10.0,10.0);
  ui.setFont(QFont("Segoe UI",8));
  ui.setPen(dark?QColor("#fbbf24"):QColor("#9a3412"));
  ui.drawText(errR.adjusted(10,0,-10,0),Qt::AlignLeft|Qt::AlignVCenter,
   english_?"Basemap partially loaded · site geometry stays ready.":"Nền bản đồ chưa tải đủ · hình học khu đất vẫn sẵn sàng.");
 }
}

QString MapCanvas::hitTestHud(const QPointF &pos)const{
 for(const auto &b:hudButtons_)if(b.rect.contains(pos))return b.id;
 return{};
}

bool MapCanvas::handleHudClick(const QString &id){
 if(id.isEmpty())return false;
 if(id=="zoom-in"){zoomBy(1.0);return true;}
 if(id=="zoom-out"){zoomBy(-1.0);return true;}
 if(id=="recenter"){centerOn(candidate_.lat,candidate_.lon,std::max(15.2,zoom_));return true;}
 if(id=="mode-vector"){setBasemapMode("vector");return true;}
 if(id=="mode-sat"){setBasemapMode("satellite");return true;}
 if(id=="toggle-hanoi-qh"){
  const bool nextOn=!hanoiPlanEnabled_;
  QString nextLayer=hanoiPlanLayer_;
  if(nextOn){
   if(ga::isLikelyInHaLongArea(lat_,lon_))nextLayer=QStringLiteral("halong_cvdd");
   else if(nextLayer=="halong_cvdd"&&ga::isLikelyInHanoiArea(lat_,lon_))nextLayer=QStringLiteral("qhpk");
  }
  setHanoiPlanning(nextOn,nextLayer,hanoiPlanOpacity_);
  emit hanoiPlanningToggled(hanoiPlanEnabled_);
  return true;
 }
 if(id=="draw"){if(drawing_)finishBoundaryDrawing();else beginBoundaryDrawing();return true;}
 if(id=="load-site"){emit loadSiteRequested();return true;}
 if(id.startsWith("city:")){
  for(const auto &b:hudButtons_){
   if(b.id==id){
    centerOn(b.lat,b.lon,15.8);
    emit cityJumpRequested(b.lat,b.lon,b.tooltip);
    return true;
   }
  }
 }
 return false;
}

void MapCanvas::beginBoundaryDrawing(){draft_.clear();drawing_=true;freehand_=false;draggedVertex_=-1;hudDirty_=true;setFocus();setCursor(Qt::CrossCursor);emit drawingChanged(true,0);update();}
void MapCanvas::finishBoundaryDrawing(){if(!drawing_)return;try{QPolygonF verts=draft_;if(verts.size()>28)verts=ga::simplifyPolyline(verts,0.00004);auto site=ga::siteFromGeographicBoundary(verts);const auto issue=ga::selectionError(site);if(!issue.isEmpty())throw std::runtime_error(issue.toUtf8().constData());drawing_=false;freehand_=false;draft_.clear();hudDirty_=true;unsetCursor();emit boundaryDrawn(verts);emit drawingChanged(false,0);update();}catch(const std::exception &error){emit message((english_?"Invalid boundary: ":"Ranh chưa hợp lệ: ")+ga::localizedSiteError(ga::errorText(error),english_));}}
void MapCanvas::undoBoundaryVertex(){if(!drawing_||draft_.isEmpty())return;draft_.removeLast();hudDirty_=true;emit drawingChanged(true,draft_.size());update();}
void MapCanvas::cancelBoundaryDrawing(){drawing_=false;freehand_=false;draft_.clear();draggedVertex_=-1;hudDirty_=true;unsetCursor();emit drawingChanged(false,0);update();}
void MapCanvas::mousePressEvent(QMouseEvent *e){
 lastMouse_=pressMouse_=e->position();vertexMoved_=false;draggedVertex_=-1;
 if(e->button()!=Qt::LeftButton)return;
 const QString hudId=hitTestHud(e->position());
 if(!hudId.isEmpty()){
  handleHudClick(hudId);
  skipRelease_=true;
  e->accept();
  return;
 }
 if(drawing_&&(e->modifiers()&Qt::ShiftModifier)){freehand_=true;const auto ll=geographicAt(e->position());draft_.append(ll);hudDirty_=true;emit drawingChanged(true,draft_.size());update();return;}
 if(!drawing_&&!candidateLocked_&&candidate_.shape=="polygon"){for(int i=0;i<candidate_.boundary.size();++i){const auto ll=ga::geographicPoint(candidate_.boundary[i],candidate_);const auto delta=screen(ll.x(),ll.y())-e->position();if(std::hypot(delta.x(),delta.y())<8){draggedVertex_=i;setCursor(Qt::SizeAllCursor);return;}}}
 dragging_=true;setCursor(drawing_?Qt::CrossCursor:Qt::ClosedHandCursor);
}
void MapCanvas::mouseMoveEvent(QMouseEvent *e){
 hoverScreen_=e->position();hasHover_=true;
 if(!(e->buttons()&Qt::LeftButton)){
  const QString hudId=hitTestHud(e->position());
  if(hudId!=hoverHudId_){
   hoverHudId_=hudId;
   hudDirty_=true;
   if(!hudId.isEmpty())setCursor(Qt::PointingHandCursor);
   else if(drawing_)setCursor(Qt::CrossCursor);
   else unsetCursor();
   update();
  }
  if(drawing_){hudDirty_=true;update();}
  return;
 }
 if(drawing_&&freehand_&&(e->buttons()&Qt::LeftButton)){const auto ll=geographicAt(e->position());if(draft_.isEmpty()||QLineF(screen(draft_.last().x(),draft_.last().y()),e->position()).length()>=7.0){draft_.append(ll);hudDirty_=true;emit drawingChanged(true,draft_.size());update();}return;}
 if(draggedVertex_>=0&&e->buttons()&Qt::LeftButton){const auto ll=geographicAt(e->position());candidate_.boundary[draggedVertex_]=ga::localPoint(ll.x(),ll.y(),candidate_);vertexMoved_=true;hudDirty_=true;update();return;}
 if(dragging_&&e->buttons()&Qt::LeftButton){beginInteraction();const auto delta=e->position()-lastMouse_;const auto ll=unworld(world(lon_,lat_,zoom_)-delta,zoom_);lon_=ll.x();lat_=ll.y();lastMouse_=e->position();deferredNavSync_=true;syncVectorView();}
}
void MapCanvas::mouseReleaseEvent(QMouseEvent *e){dragging_=false;if(hoverHudId_.isEmpty())unsetCursor();if(skipRelease_){skipRelease_=false;return;}if(freehand_){freehand_=false;if(draft_.size()>=5)finishBoundaryDrawing();return;}if(draggedVertex_>=0){draggedVertex_=-1;if(vertexMoved_){QPolygonF vertices;for(const auto &p:candidate_.boundary)vertices.append(ga::geographicPoint(p,candidate_));emit boundaryEdited(vertices);}return;}if(e->button()!=Qt::LeftButton||(e->position()-pressMouse_).manhattanLength()>=4)return;const auto ll=geographicAt(e->position());if(drawing_){if(draft_.isEmpty()||std::hypot(draft_.last().x()-ll.x(),draft_.last().y()-ll.y())>1e-9){draft_.append(ll);hudDirty_=true;emit drawingChanged(true,draft_.size());update();}}else emit sitePicked(ll.y(),ll.x());}
void MapCanvas::mouseDoubleClickEvent(QMouseEvent *e){if(drawing_&&e->button()==Qt::LeftButton){finishBoundaryDrawing();skipRelease_=true;}e->accept();}
void MapCanvas::keyPressEvent(QKeyEvent *e){if(drawing_){if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter){finishBoundaryDrawing();e->accept();return;}if(e->key()==Qt::Key_Backspace||e->key()==Qt::Key_Delete){undoBoundaryVertex();e->accept();return;}if(e->key()==Qt::Key_Escape){cancelBoundaryDrawing();e->accept();return;}}QOpenGLWidget::keyPressEvent(e);}
void MapCanvas::leaveEvent(QEvent *e){if(!hoverHudId_.isEmpty()){hoverHudId_.clear();hudDirty_=true;update();}QOpenGLWidget::leaveEvent(e);}
void MapCanvas::wheelEvent(QWheelEvent *e){beginInteraction();const auto worldBefore=world(lon_,lat_,zoom_)+e->position()-QPointF(width()/2.,height()/2.);const auto ll=unworld(worldBefore,zoom_);const double delta=e->angleDelta().y()?e->angleDelta().y()/240.:e->pixelDelta().y()/160.;zoom_=std::clamp(zoom_+delta,2.,20.);const auto center=unworld(world(ll.x(),ll.y(),zoom_)-e->position()+QPointF(width()/2.,height()/2.),zoom_);lon_=center.x();lat_=center.y();deferredNavSync_=true;syncVectorView();e->accept();}

QJsonObject MapCanvas::draftState()const{if(!drawing_)return{};QJsonArray points;for(const auto &p:draft_)points.append(QJsonArray{p.x(),p.y()});return{{"active",true},{"points",points}};}
void MapCanvas::restoreDraft(const QJsonObject &state){if(!state["active"].toBool())return;const auto rows=state["points"].toArray();if(rows.size()>512)return;QPolygonF points;for(const auto &v:rows){const auto p=v.toArray();if(p.size()!=2||!p[0].isDouble()||!p[1].isDouble()||std::abs(p[0].toDouble())>180||std::abs(p[1].toDouble())>85)return;points.append({p[0].toDouble(),p[1].toDouble()});}draft_=points;drawing_=true;hudDirty_=true;setCursor(Qt::CrossCursor);emit drawingChanged(true,draft_.size());update();}
