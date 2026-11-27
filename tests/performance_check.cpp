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
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

// Deliberately includes synchronous framebuffer readback: paired regression
// probe on the same machine, not an advertised interactive FPS measurement.
int main(int argc,char **argv){
 QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);format.setSamples(4);format.setSwapInterval(1);QSurfaceFormat::setDefaultFormat(format);
 QApplication app(argc,argv);app.setStyle("Fusion");app.setApplicationVersion("performance-probe");app.setStyleSheet(workspaceStyle());installApplicationChrome(app);if(argc!=4)return 2;
 const auto input=QString::fromLocal8Bit(argv[1]),data=QString::fromLocal8Bit(argv[2]),out=QString::fromLocal8Bit(argv[3]);QDir().mkpath(out);ga::loadPresentationCatalog(data+"/presentations.json");auto project=ga::Project::load(input);project.notes="Owned performance QA copy";ga::Camera camera;camera.preset("perspective");project.view=camera.toJson();project.view["navigation"]=QJsonObject{{"lat",project.site.lat},{"lon",project.site.lon},{"zoom",16.2}};project.save(out+"/scene.gatlas");
 MainWindow window(out+"/session",out+"/scene.gatlas");installWindowChrome(&window);window.resize(1480,940);window.show();auto view=window.viewport();auto map=window.findChild<MapCanvas*>();if(project.features.size()>100000){auto diagram=window.findChild<QComboBox*>("diagrams");diagram->setCurrentIndex(diagram->findData("parcels"));}
 QTimer::singleShot(4500,&window,[&]{
  QJsonArray results;bool passed=true;
  const auto run=[&](const QString &id,QOpenGLWidget *widget,const std::function<void(int)> &step){
   widget->grabFramebuffer();QVector<double> durations;QImage image;const QPointF start(widget->width()*.5,widget->height()*.5);QMouseEvent press(QEvent::MouseButtonPress,start,start,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);if(id.contains("pan"))QApplication::sendEvent(widget,&press);
   const auto before=view->basemapDiagnostics();
   for(int i=0;i<48;++i){QElapsedTimer clock;clock.start();step(i);image=widget->grabFramebuffer();durations.append(clock.nsecsElapsed()/1e6);QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);}
   if(id.contains("pan")){QMouseEvent release(QEvent::MouseButtonRelease,start+QPointF(48,0),start,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(widget,&release);}
   QEventLoop settle;QTimer::singleShot(180,&settle,&QEventLoop::quit);settle.exec();image=widget->grabFramebuffer();
   const auto after=view->basemapDiagnostics();auto sorted=durations;std::sort(sorted.begin(),sorted.end());double sum=0;for(auto t:durations)sum+=t;
   const auto state=widget==view?view->basemapDiagnostics():map->basemapDiagnostics();const bool ok=view->graphicsError().isEmpty()&&state["gl_error"].toInt()==0;passed&=ok;
   bool cached=true;if(id=="3d-pan"||id=="3d-wheel"){const auto a=before["performance"].toObject(),b=after["performance"].toObject();if(!a.isEmpty())cached=a["buffer_uploads"]==b["buffer_uploads"]&&a["mesh_builds"]==b["mesh_builds"];passed&=cached;}
   results.append(QJsonObject{{"case",id},{"frames",durations.size()},{"mean_ms",sum/durations.size()},{"median_ms",sorted[sorted.size()/2]},{"p95_ms",sorted[int(sorted.size()*.95)]},{"max_ms",sorted.last()},{"timings_ms",QJsonArray::fromVariantList([&]{QVariantList a;for(double t:durations)a.append(t);return a;}())},{"saved",image.save(out+"/"+id+".png")},{"before",before},{"after",after},{"gl_ok",ok},{"navigation_keeps_gpu_buffers",cached}});
  };
  run("3d-orbit",view,[&](int i){auto c=view->camera();c.yaw=18+i*.6;view->setCamera(c);});
  const auto move=[&](QOpenGLWidget *widget,int i){const QPointF p(widget->width()*.5+i+1,widget->height()*.5);QMouseEvent event(QEvent::MouseMove,p,p,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(widget,&event);};
  const auto wheel=[&](QOpenGLWidget *widget,int i){const QPointF p(widget->width()*.5,widget->height()*.5);QWheelEvent event(p,p,{},QPoint(0,(i<24?1:-1)*15),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);QApplication::sendEvent(widget,&event);};
  run("3d-pan",view,[&](int i){move(view,i);});run("3d-wheel",view,[&](int i){wheel(view,i);});
  window.findChild<QAction*>("view-2d")->trigger();run("2d-pan",view,[&](int i){move(view,i);});run("2d-wheel",view,[&](int i){wheel(view,i);});
  window.findChild<QAction*>("map")->trigger();run("map-pan",map,[&](int i){move(map,i);});run("map-wheel",map,[&](int i){wheel(map,i);});
  // Save only the owned copy, close normally, and retain recoverable session.
  window.findChild<QAction*>("save")->trigger();const bool closed=window.close();passed&=closed;QFile report(out+"/review.json");if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(QJsonObject{{"passed",passed},{"closed",closed},{"features",project.features.size()},{"native",true},{"scope","48 synchronous input+framebuffer readback probes per case; same-machine comparison, not live FPS"},{"cases",results}}).toJson());app.exit(passed?0:1);
 });return app.exec();
}
