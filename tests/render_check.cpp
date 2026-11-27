#include "viewport.h"
#include "planpainter.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSurfaceFormat>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonArray>
#include <cmath>
#include <algorithm>

// An actual GPU integration check, separate from offscreen UI tests. It renders
// the shipped native widget and compares its 2D pixels with an independent
// metre-to-pixel transform; no browser or external UI automation is involved.
int main(int argc,char **argv){
 QSurfaceFormat surface;surface.setVersion(3,3);surface.setProfile(QSurfaceFormat::CoreProfile);surface.setDepthBufferSize(24);surface.setSamples(4);QSurfaceFormat::setDefaultFormat(surface);QApplication app(argc,argv);
 if(argc!=3)return 2;const auto data=QString::fromLocal8Bit(argv[1]),out=QString::fromLocal8Bit(argv[2]);QDir().mkpath(out);ga::loadPresentationCatalog(data+"/presentations.json");const auto project=ga::Project::load(data+"/hoankiem.gatlas");Viewport view;view.setContextVisible(false);view.resize(960,720);view.setProject(&project);view.show();QJsonArray frames;int step=0;QElapsedTimer elapsed;elapsed.start();
 const auto record=[&]{const QStringList ids={"ga-style-graphite","ga-diagram-streets","ga-style-porcelain"};const auto id=ids[step/2];const bool plan=step%2;auto layers=ga::presentationLayers(ga::defaultLayers(),id,id.startsWith("ga-diagram-"));const auto palette=ga::paletteFor(id);auto camera=ga::Camera();camera.mode=plan?"2d":"3d";view.setLayers(layers);view.setPalette(palette);view.setDiagram(id.startsWith("ga-diagram-")?id:"context");view.setCamera(camera);view.repaint();const auto image=view.grabFramebuffer();const auto name=id+(plan?"-2d":"-3d")+".png";const bool saved=image.save(out+"/"+name);QJsonObject frame{{"id",id},{"mode",camera.mode},{"image",name},{"saved",saved},{"width",image.width()},{"height",image.height()},{"triangles",view.triangleCount()},{"renderer",view.renderer()},{"graphics_error",view.graphicsError()}};
  if(plan){QImage expected(image.size(),QImage::Format_ARGB32_Premultiplied);expected.fill(palette.paper);QPainter painter(&expected);const double scale=image.height()/(project.site.radius*2.26);paintPlan(painter,project,layers,palette,id.startsWith("ga-diagram-")?id:"context",[&](QPointF p){return QPointF(image.width()/2.+p.x()*scale,image.height()/2.-p.y()*scale);},scale);painter.end();expected.save(out+"/"+id+"-expected.png");int compared=0,errors=0;double total=0;for(int y=110;y<image.height()-120;++y)for(int x=90;x<image.width()-90;++x){const auto a=image.pixelColor(x,y),b=expected.pixelColor(x,y);const int error=std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});total+=error;++compared;if(error>16)++errors;}frame["mean_channel_error"]=total/std::max(1,compared);frame["pixels_over_16_fraction"]=double(errors)/std::max(1,compared);}
  frames.append(frame);++step;
 };
 QTimer timer;QObject::connect(&timer,&QTimer::timeout,&app,[&]{record();if(step==6){timer.stop();bool ok=true;for(const auto &v:frames){const auto f=v.toObject();ok&=f["saved"].toBool()&&f["graphics_error"].toString().isEmpty()&&!f["renderer"].toString().isEmpty();if(f["mode"]=="2d")ok&=f["mean_channel_error"].toDouble()<2&&f["pixels_over_16_fraction"].toDouble()<.02;}QFile file(out+"/review.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"hardware",true},{"passed",ok},{"elapsed_ms",elapsed.elapsed()},{"frames",frames}}).toJson(QJsonDocument::Indented));view.close();app.exit(ok?0:1);}});timer.start(650);return app.exec();
}
