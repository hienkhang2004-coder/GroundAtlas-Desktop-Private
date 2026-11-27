#include "mainwindow.h"
#include "viewport.h"
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
#include <QPlainTextEdit>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>
#include <QMouseEvent>
#include <cmath>

int main(int argc,char **argv){
 QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);format.setSamples(4);QSurfaceFormat::setDefaultFormat(format);
 QApplication app(argc,argv);app.setStyle("Fusion");app.setApplicationVersion("0.11.0");app.setStyleSheet(workspaceStyle());installApplicationChrome(app);if(argc!=4)return 2;
 const auto project=QString::fromLocal8Bit(argv[1]),data=QString::fromLocal8Bit(argv[2]),out=QString::fromLocal8Bit(argv[3]);QDir().mkpath(out);ga::loadPresentationCatalog(data+"/presentations.json");QElapsedTimer opening;opening.start();MainWindow window(out+"/session",project);const auto openMs=opening.elapsed();installWindowChrome(&window);window.resize(1480,940);window.show();auto diagram=window.findChild<QComboBox*>("diagrams");diagram->setCurrentIndex(diagram->findData("parcels"));window.findChild<QPlainTextEdit*>("notes")->setPlainText("QA only: three full-size artificial GeoJSON inputs. Not surveyed parcels or statutory planning.");
 QStringList names={"full-3d","full-2d","wheel-2d","pan-3d"};int stage=0;bool passed=true;QJsonArray frames;QElapsedTimer phase,total;phase.start();total.start();QTimer timer;
 QObject::connect(&timer,&QTimer::timeout,&app,[&]{
  window.viewport()->grabFramebuffer();const auto state=window.diagnostics(),map=state["scene_basemap"].toObject();if(!map["fully_rendered"].toBool()&&phase.elapsed()<30000)return;if(phase.elapsed()<1500)return;
  QElapsedTimer capture;capture.start();const auto image=window.viewport()->grabFramebuffer();const auto captureMs=capture.elapsed();auto frame=state;frame["image"]=names[stage]+".png";frame["capture_ms"]=captureMs;frame["saved"]=image.save(out+"/"+names[stage]+".png");frame["window_saved"]=window.grab().save(out+"/"+names[stage]+"-window.png");frames.append(frame);passed&=frame["saved"].toBool()&&frame["window_saved"].toBool()&&state["features"].toInt()==749998&&state["imports"].toArray().size()==3&&state["graphics_error"].toString().isEmpty()&&map["gl_error"].toInt()==0&&map["fully_rendered"].toBool()&&map["maximum_landmark_error_px"].toDouble()<.2;
  ++stage;if(stage==1)window.findChild<QAction*>("view-2d")->trigger();else if(stage==2){const auto before=window.viewport()->geographicView()["zoom"].toDouble();const QPointF point(500,380);QWheelEvent wheel(point,point,{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(window.viewport(),&wheel);passed&=window.viewport()->geographicView()["zoom"].toDouble()>before;}else if(stage==3){window.findChild<QAction*>("view-3d")->trigger();auto c=window.viewport()->camera();c.preset("perspective");c.panX+=35;c.panY+=20;window.viewport()->setCamera(c);}else{
   timer.stop();const auto svg=window.viewport()->exportVector(out+"/large.svg",false);QFile file(out+"/review.json");passed&=svg;QJsonObject report{{"passed",passed},{"native",true},{"webview",false},{"hardware",true},{"features",749998},{"imports",3},{"open_ms",openMs},{"elapsed_ms",total.elapsed()},{"timing_scope","Constructor/capture probe timings are not a frame-rate benchmark"},{"svg_exported",svg},{"frames",frames}};if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));window.findChild<QAction*>("save")->trigger();window.close();app.exit(passed?0:1);return;
  }phase.restart();
 });timer.start(500);return app.exec();
}
