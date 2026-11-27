#include "mainwindow.h"
#include "viewport.h"
#include "appearance.h"
#include "windowchrome.h"
#include "geojsonoverlay.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <cmath>

int main(int argc,char **argv){
 QSurfaceFormat surface;surface.setVersion(3,3);surface.setProfile(QSurfaceFormat::CoreProfile);surface.setDepthBufferSize(24);surface.setSamples(4);QSurfaceFormat::setDefaultFormat(surface);
 QApplication app(argc,argv);app.setApplicationVersion("0.10.0");app.setStyle("Fusion");app.setStyleSheet(workspaceStyle());installApplicationChrome(app);if(argc!=4)return 2;
 const auto data=QString::fromLocal8Bit(argv[1]),fixture=QString::fromLocal8Bit(argv[2]),out=QString::fromLocal8Bit(argv[3]);QDir().mkpath(out);ga::loadPresentationCatalog(data+"/presentations.json");
 QFile input(fixture);if(!input.open(QIODevice::ReadOnly))return 3;auto imported=QJsonDocument::fromJson(input.readAll()).object()["input"].toObject();QJsonArray records;for(const auto &v:imported["features"].toArray())if(v.toObject()["id"]!="limit")records.append(v);imported["features"]=records;
 const auto importFile=out+"/controlled-import.geojson",projectFile=out+"/controlled-project.gatlas";{QFile f(importFile);if(!f.open(QIODevice::WriteOnly))return 3;f.write(QJsonDocument(imported).toJson());}
 auto seed=ga::Project::load(data+"/hoankiem.gatlas");seed.site.name="QA · controlled GeoJSON overlays";seed.notes="Validation only: imported shapes in this project are artificial test geometry, not surveyed buildings/planning.";seed.style="ga-style-porcelain";seed.save(projectFile);
 MainWindow window(out+"/session",projectFile);installWindowChrome(&window);window.resize(1480,940);window.show();window.viewport()->setGeographicView(21.0288,105.8525,18.5);window.findChild<QAction*>("view-2d")->trigger();auto scroll=window.findChild<QScrollArea*>("site-scroll");scroll->verticalScrollBar()->setValue(570);
 const auto roles=ga::geoJsonRoles();int stage=-1;bool pending=false,ok=true,sourceUnchanged=false,exportsOk=false,wheelZoomed=false,roundtripOk=false;QElapsedTimer phase,total;phase.start();total.start();QJsonArray frames;QTimer timer;const auto originalCount=seed.features.size();
 const auto writeReview=[&]{QFile file(out+"/review.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"passed",ok},{"native",true},{"hardware",true},{"webview",false},{"test_geometry","Artificial user-import fixture; primary source unchanged"},{"elapsed_ms",total.elapsed()},{"primary_source_unchanged",sourceUnchanged},{"exports_ok",exportsOk},{"wheel_zoomed",wheelZoomed},{"roundtrip_ok",roundtripOk},{"frames",frames}}).toJson(QJsonDocument::Indented));};
 QObject::connect(&timer,&QTimer::timeout,&app,[&]{
  if(total.elapsed()>180000){ok=false;writeReview();window.viewport()->shutdown();app.exit(1);return;}
  const auto diagnostics=window.diagnostics();if(diagnostics["source_busy"].toBool())return;
  if(pending){if(diagnostics["imports"].toArray().size()!=stage+1){ok=false;writeReview();app.exit(1);return;}pending=false;auto diagram=window.findChild<QComboBox*>("diagrams");const QString id=QStringList{"height","parcels","planning","trees","heritage","transit","green"}[stage];diagram->setCurrentIndex(diagram->findData(id));phase.restart();}
  const auto map=window.viewport()->basemapDiagnostics();window.viewport()->grabFramebuffer();if((!map["fully_rendered"].toBool()&&phase.elapsed()<25000)||phase.elapsed()<1800)return;
  const QString name=stage<0?"primary":stage<roles.size()?roles[stage]:stage==7?"perspective":stage==8?"wheel":stage==9?"removed-planning":"reopened";
  const auto image=window.viewport()->grabFramebuffer();QSet<QRgb> colors;for(int y=40;y<image.height()-60;y+=3)for(int x=25;x<image.width()-25;x+=3)colors.insert(image.pixel(x,y));
  auto frame=window.diagnostics();frame["image"]=name+".png";frame["saved"]=image.save(out+"/"+name+".png");frame["window_saved"]=window.grab().save(out+"/"+name+"-window.png");frame["colors"]=colors.size();frame["landmark_error_px"]=map["maximum_landmark_error_px"];frames.append(frame);
  ok&=frame["saved"].toBool()&&frame["window_saved"].toBool()&&colors.size()>35&&frame["graphics_error"].toString().isEmpty()&&map["gl_error"].toInt()==0&&map["fully_rendered"].toBool()&&map["maximum_landmark_error_px"].toDouble()<.2;
  if(stage>=0&&stage<7)ok&=frame["features"].toInt()>originalCount&&frame["imports"].toArray().size()==stage+1;
  ++stage;
  if(stage<7){window.findChild<QComboBox*>("language")->setCurrentIndex(stage%2);pending=window.importGeoJson(importFile,roles[stage]);ok&=pending;phase.restart();return;}
  if(stage==7){auto diagram=window.findChild<QComboBox*>("diagrams");diagram->setCurrentIndex(diagram->findData("context"));window.findChild<QAction*>("view-3d")->trigger();auto c=window.viewport()->camera();c.preset("perspective");window.viewport()->setCamera(c);phase.restart();return;}
  if(stage==8){const auto before=window.viewport()->geographicView()["zoom"].toDouble();const QPointF p(500,400);QWheelEvent wheel(p,p,{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(window.viewport(),&wheel);wheelZoomed=window.viewport()->geographicView()["zoom"].toDouble()>before;ok&=wheelZoomed;phase.restart();return;}
  if(stage==9){const auto imports=window.diagnostics()["imports"].toArray();QString id;for(const auto &v:imports)if(v.toObject()["role"]=="planning")id=v.toObject()["id"].toString();ok&=window.removeImportedGeoJson(id);ok&=window.diagnostics()["imports"].toArray().size()==6;phase.restart();return;}
  if(stage==10){window.findChild<QAction*>("save")->trigger();const auto saved=ga::Project::load(projectFile);sourceUnchanged=saved.source==seed.source;ok&=saved.imports.size()==6&&sourceUnchanged;exportsOk=window.viewport()->exportVector(out+"/overlays.svg",false)&&window.viewport()->exportVector(out+"/overlays.pdf",true)&&window.viewport()->exportObj(out+"/overlays.obj");QFile f(out+"/overlays.geojson");ok&=f.open(QIODevice::WriteOnly);if(f.isOpen()){const auto bytes=QJsonDocument(saved.geoJson()).toJson();exportsOk&=f.write(bytes)==bytes.size();}else exportsOk=false;ok&=exportsOk;saved.save(out+"/roundtrip.gatlas");roundtripOk=window.loadPath(out+"/roundtrip.gatlas")&&window.diagnostics()["imports"].toArray().size()==6;ok&=roundtripOk;phase.restart();return;}
  timer.stop();writeReview();window.findChild<QAction*>("save")->trigger();window.close();app.exit(ok?0:1);
 });timer.start(350);return app.exec();
}
