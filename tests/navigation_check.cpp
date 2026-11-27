#include "mainwindow.h"
#include "viewport.h"
#include "mapcanvas.h"
#include "appearance.h"
#include "windowchrome.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QTimer>
#include <QSurfaceFormat>
#include <cmath>

// Real OpenGL and network integration, separate from the offscreen UI suite.
// Navigation never imports the displayed vector tiles into the site project.
int main(int argc,char **argv){
 QSurfaceFormat surface;surface.setVersion(3,3);surface.setProfile(QSurfaceFormat::CoreProfile);surface.setDepthBufferSize(24);surface.setSamples(4);QSurfaceFormat::setDefaultFormat(surface);
 QApplication app(argc,argv);app.setApplicationVersion("0.9.0");app.setStyle("Fusion");app.setStyleSheet(workspaceStyle());installApplicationChrome(app);if(argc!=3)return 2;
 const auto data=QString::fromLocal8Bit(argv[1]),out=QString::fromLocal8Bit(argv[2]);QDir().mkpath(out);ga::loadPresentationCatalog(data+"/presentations.json");
 auto initial=ga::Project::load(data+"/hoankiem.gatlas");ga::Camera initialCamera;initialCamera.preset("perspective");initial.view=initialCamera.toJson();initial.view["navigation"]=QJsonObject{{"lat",21.0288},{"lon",105.8525},{"zoom",16.2}};initial.save(out+"/initial.gatlas");MainWindow window(out+"/session",out+"/initial.gatlas");installWindowChrome(&window);window.resize(1480,940);auto map=window.findChild<MapCanvas*>();auto view=window.viewport();auto preset=window.findChild<QComboBox*>("camera-presets");auto style=window.findChild<QComboBox*>("styles");Q_UNUSED(style);
 const auto command=[&](const char *name){window.findChild<QAction*>(name)->trigger();};
 struct Case{QString id;double lat,lon,zoom;QString mode,preset;};const QVector<Case> cases={
  {"saved-startup",21.0288,105.8525,16.2,"3d",""},
  {"new-york-map",40.7128,-74.006,16,"map",""},
  {"new-york-perspective",40.7128,-74.006,16,"3d","perspective"},
  {"new-york-plan",40.7128,-74.006,16,"2d",""},
  {"sydney-two-point",-33.8688,151.2093,15,"3d","two-point"},
  {"world-perspective",21,105,3,"3d","perspective"},
  {"dateline-plan",-16.8,179.99,9,"2d",""},
  {"dateline-map",-16.8,179.99,9,"map",""},
  {"hoankiem-return",21.0288,105.8525,16,"3d","perspective"},
  {"hoankiem-resize",21.0288,105.8525,16,"3d","perspective"}
 };
 int step=0;bool passed=true;QJsonArray frames;QElapsedTimer phase,settle,total;QTimer timer;
 const auto next=[&]{const auto &c=cases[step];if(QStringList{"new-york-map","sydney-two-point","world-perspective","dateline-plan","hoankiem-return"}.contains(c.id)){command("map");map->centerOn(c.lat,c.lon,c.zoom);}if(c.id=="saved-startup"){phase.restart();settle.invalidate();return;}if(c.mode=="map")command("map");else{command(c.mode=="2d"?"view-2d":"view-3d");if(!c.preset.isEmpty())preset->setCurrentIndex(preset->findData(c.preset));}if(c.id=="hoankiem-resize")window.resize(1240,820);phase.restart();settle.invalidate();};
 window.show();next();total.start();
 QObject::connect(&timer,&QTimer::timeout,&app,[&]{
  const auto &c=cases[step];const bool geographic=c.mode=="map";const auto image=geographic?map->grabFramebuffer():view->grabFramebuffer();const auto state=geographic?map->basemapDiagnostics():view->basemapDiagnostics();
  if(!state["fully_rendered"].toBool()&&phase.elapsed()<35000)return;
  if(!settle.isValid()){settle.start();return;}if(settle.elapsed()<1100)return;
  const auto a=view->geographicView(),b=map->view();const double centerError=std::max(std::abs(a["lat"].toDouble()-c.lat),std::abs(std::remainder(a["lon"].toDouble()-c.lon,360.)));const double zoomError=std::max(std::abs(a["zoom"].toDouble()-c.zoom),std::abs(b["zoom"].toDouble()-c.zoom));QSet<QRgb> colors;for(int y=110;y<image.height()-110;y+=3)for(int x=20;x<image.width()-20;x+=3)colors.insert(image.pixel(x,y));
  auto frame=state;frame["case"]=c.id;frame["image"]=c.id+".png";frame["saved"]=image.save(out+"/"+c.id+".png");frame["scene_view"]=a;frame["map_view"]=b;frame["center_error_degrees"]=centerError;frame["zoom_error"]=zoomError;frame["map_colors"]=colors.size();frame["document_dirty"]=window.windowTitle().startsWith("●");frames.append(frame);
  passed&=frame["saved"].toBool()&&state["fully_rendered"].toBool()&&state["error"].toString().isEmpty()&&state["gl_error"].toInt()==0&&centerError<1e-7&&zoomError<.0001&&colors.size()>10&&view->graphicsError().isEmpty()&&(geographic||state["maximum_landmark_error_px"].toDouble()<.2);
  if(c.id=="saved-startup")passed&=!window.windowTitle().startsWith("●");
  if(++step<cases.size()){next();return;}
  timer.stop();const auto diagnostic=window.diagnostics();passed&=diagnostic["buildings"].toInt()==2262;QFile review(out+"/review.json");if(review.open(QIODevice::WriteOnly))review.write(QJsonDocument(QJsonObject{{"passed",passed},{"native",true},{"webview",false},{"hardware",true},{"elapsed_ms",total.elapsed()},{"saved_buildings",diagnostic["buildings"]},{"frames",frames}}).toJson());
  map->shutdown();view->shutdown();window.hide();app.exit(passed?0:1);
 });timer.start(450);return app.exec();
}
