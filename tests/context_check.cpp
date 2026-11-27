#include "viewport.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QSet>
#include <QLineF>
#include <QSurfaceFormat>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

int main(int argc,char **argv){
 QSurfaceFormat surface;surface.setVersion(3,3);surface.setProfile(QSurfaceFormat::CoreProfile);surface.setDepthBufferSize(24);surface.setSamples(4);QSurfaceFormat::setDefaultFormat(surface);QApplication app(argc,argv);app.setApplicationVersion("0.9.0");if(argc!=3)return 2;
 const auto data=QString::fromLocal8Bit(argv[1]),out=QString::fromLocal8Bit(argv[2]);QDir().mkpath(out);ga::loadPresentationCatalog(data+"/presentations.json");const auto project=ga::Project::load(data+"/hoankiem.gatlas");const auto original=project.toJson();Viewport view(out+"/cache");view.resize(1100,800);view.setProject(&project);view.setLayers(ga::presentationLayers(ga::defaultLayers(),"ga-style-porcelain"));view.setPalette(ga::paletteFor("ga-style-porcelain"));
 const QStringList modes={"axonometric","isometric","perspective","one-point","two-point","2d","exploded","wheel-orbit"};int step=0;double wheelError=0;bool ok=true;QPointF cursor;QObject::connect(&view,&Viewport::cursorPosition,&app,[&](QPointF p){cursor=p;});
 const auto next=[&]{ga::Camera c;c.preset(step<5?modes[step]:step==5?"isometric":"perspective");if(step==5)c.mode="2d";if(step==6){c.exploded=true;c.separation=60;}view.setCamera(c);if(step==7){const QPointF point(690,430);QMouseEvent before(QEvent::MouseMove,point,point,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&before);const auto fixed=cursor;QWheelEvent wheel(point,point,{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&view,&wheel);QMouseEvent after(QEvent::MouseMove,point,point,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&after);wheelError=QLineF(fixed,cursor).length();auto turned=view.camera();turned.yaw+=28;turned.elevation=49;view.setCamera(turned);}};
 next();view.show();QTimer timer;QElapsedTimer phase,settle,total;phase.start();total.start();QJsonArray frames;
 QObject::connect(&timer,&QTimer::timeout,&app,[&]{view.grabFramebuffer();const auto state=view.basemapDiagnostics();if(!state["fully_rendered"].toBool()&&phase.elapsed()<30000)return;if(!settle.isValid()){settle.start();return;}if(settle.elapsed()<1000)return;
  const auto image=view.grabFramebuffer();QSet<QRgb> colors;for(int y=110;y<image.height()-110;y+=2)for(int x=20;x<image.width()-20;x+=2)if(x<175||x>image.width()-175)colors.insert(image.pixel(x,y));auto frame=state;const auto name=modes[step]+".png";frame["image"]=name;frame["saved"]=image.save(out+"/"+name);frame["camera"]=view.camera().toJson();frame["outside_colors"]=colors.size();frame["graphics_error"]=view.graphicsError();frame["site_triangles"]=view.triangleCount();frames.append(frame);ok&=frame["saved"].toBool()&&state["fully_rendered"].toBool()&&state["gl_error"].toInt()==0&&state["error"].toString().isEmpty()&&view.graphicsError().isEmpty()&&state["maximum_landmark_error_px"].toDouble()<.2&&std::abs(state["native_pitch"].toDouble()-state["actual_native_pitch"].toDouble())<.001&&colors.size()>40;
  if(step==6){const auto bounds=state["projected_geometry_bounds"].toArray();ok&=bounds.size()==4&&bounds[0].toDouble()>=0&&bounds[1].toDouble()>=0&&bounds[2].toDouble()<=view.width()&&bounds[3].toDouble()<=view.height();}
  ++step;if(step<modes.size()){next();phase.restart();settle.invalidate();return;}timer.stop();ok&=wheelError<.2&&project.toJson()==original;QFile file(out+"/review.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"passed",ok},{"hardware",true},{"native",true},{"webview",false},{"elapsed_ms",total.elapsed()},{"wheel_anchor_error_metres",wheelError},{"analysis_input_unchanged",project.toJson()==original},{"frames",frames}}).toJson(QJsonDocument::Indented));view.shutdown();view.close();app.exit(ok?0:1);
 });timer.start(450);return app.exec();
}
