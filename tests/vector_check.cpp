#include "mapcanvas.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSurfaceFormat>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>

int main(int argc,char **argv){
 QSurfaceFormat surface;surface.setVersion(3,3);surface.setProfile(QSurfaceFormat::CoreProfile);surface.setDepthBufferSize(24);surface.setStencilBufferSize(8);surface.setSamples(4);QSurfaceFormat::setDefaultFormat(surface);QApplication app(argc,argv);app.setApplicationVersion("0.7.0");
 if(argc!=3)return 2;const auto data=QString::fromLocal8Bit(argv[1]),out=QString::fromLocal8Bit(argv[2]);QDir().mkpath(out);ga::loadPresentationCatalog(data+"/presentations.json");const auto project=ga::Project::load(data+"/hoankiem.gatlas");const auto original=project.toJson();
 MapCanvas map(out+"/cache");map.resize(1100,800);map.setProject(&project);map.setPresentation(ga::presentationLayers(ga::defaultLayers(),"ga-style-porcelain"),ga::paletteFor("ga-style-porcelain"),"context");map.setCandidate(project.site,true);map.centerOn(project.site.lat,project.site.lon,15.5);map.show();QJsonArray frames;int step=0;QElapsedTimer elapsed,phase;elapsed.start();QImage styled;QTimer timer;
 // Hidden test processes do not receive normal exposed-window paint events.
 // Drive the actual framebuffer so asynchronous tile/glyph updates can complete.
 QObject::connect(&timer,&QTimer::timeout,&app,[&]{map.grabFramebuffer();const auto state=map.basemapDiagnostics();if(!state["fully_rendered"].toBool()&&elapsed.elapsed()<40000)return;
  // A fully loaded map can still be in its default 300 ms paint transition.
  if(!phase.isValid()){phase.start();return;}if(phase.elapsed()<1000)return;
  if(step==0){const auto image=map.grabFramebuffer();styled=image;auto frame=state;frame["image"]="context-styled.png";frame["saved"]=image.save(out+"/context-styled.png");QSet<QRgb> colors;for(int y=100;y<image.height()-90;y+=3)for(int x=20;x<image.width()-20;x+=3)if(x<200||x>image.width()-200)colors.insert(image.pixel(x,y));frame["outside_colors"]=colors.size();frames.append(frame);map.setContextEnabled(false);phase.restart();++step;return;}
  const auto image=map.grabFramebuffer();auto frame=state;frame["image"]="context-original.png";frame["saved"]=image.save(out+"/context-original.png");int changed=0;for(int y=120;y<image.height()-100;++y)for(int x=20;x<image.width()-20;++x)if((x<190||x>image.width()-190)&&image.pixel(x,y)!=styled.pixel(x,y))++changed;frame["outside_pixels_changed"]=changed;frames.append(frame);bool ok=project.toJson()==original;for(const auto &v:frames){const auto f=v.toObject();ok&=f["fully_rendered"].toBool()&&f["style_loaded"].toBool()&&f["saved"].toBool()&&f["error"].toString().isEmpty()&&f["gl_error"].toInt()==0;}ok&=frames[0].toObject()["outside_colors"].toInt()>30&&changed>10000;QFile file(out+"/review.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"passed",ok},{"hardware",true},{"elapsed_ms",elapsed.elapsed()},{"site_features_before",project.features.size()},{"site_features_after",project.features.size()},{"analysis_input_unchanged",project.toJson()==original},{"frames",frames}}).toJson(QJsonDocument::Indented));timer.stop();map.shutdown();map.close();app.exit(ok?0:1);
 });timer.start(500);return app.exec();
}
