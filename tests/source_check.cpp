#include "dataaccess.h"
#include "sitegeometry.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QElapsedTimer>

// Actual public service integration. Two bounded user-initiated network reads;
// the intermediate repeat and fresh-client read must use the persisted cache.
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);if(argc!=3)return 2;const QString data=app.arguments()[1],out=app.arguments()[2];QDir().mkpath(out);
 ga::Site selection;selection.name="Hoan Kiem / native source validation";selection.lat=21.0301;selection.lon=105.8515;selection.radius=100;selection.boundary=ga::primitiveBoundary(selection);auto base=ga::reclipProject(ga::Project::load(data+"/hoankiem.gatlas"),selection);if(base.stats().buildings<1||base.stats().knownHeights<1)return 3;base.notes="Native source verification";base.save(out+"/primary.gatlas");const auto before=base.toJson();
 DataAccess access(out+"/source-cache"),fresh(out+"/source-cache");int step=0;QJsonArray stages;bool passed=true,done=false;QElapsedTimer clock;clock.start();
 const auto finish=[&](QString error={}){if(done)return;done=true;QJsonObject review{{"passed",passed&&error.isEmpty()&&step==4},{"actual_public_network",true},{"primary_buildings",base.stats().buildings},{"source_geometry_unchanged",base.toJson()==before},{"elapsed_ms",clock.elapsed()},{"stages",stages},{"error",error},{"transport_diagnostics",step<2?access.property("osmDeliveryDiagnostics").toJsonObject():fresh.property("osmDeliveryDiagnostics").toJsonObject()}};QFile file(out+"/review.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(review).toJson());app.exit(review["passed"].toBool()?0:1);};
 const auto loaded=[&](ga::Project project){
  const auto history=project.source["enrichments"].toArray();const auto entry=history.isEmpty()?QJsonObject():history.last().toObject();const auto delivery=(step<2?entry["source"].toObject():project.source)["delivery"].toObject();
  QJsonObject record{{"step",step},{"features",project.features.size()},{"buildings",project.stats().buildings},{"delivery",delivery},{"added_geometry_parts",entry["added_geometry_parts"]}};
  if(step<2){const auto prior=before["features"].toArray(),after=project.toJson()["features"].toArray();bool same=after.size()>=prior.size();for(int i=0;i<prior.size();++i)same&=prior[i]==after[i];record["primary_geometry_unchanged"]=same;passed&=same&&project.stats().buildings==base.stats().buildings&&project.notes==base.notes;}
  passed&=delivery["cache"].toString()==(step==1||step==2?"hit":"miss");if(step==1)passed&=entry["added_geometry_parts"].toInt()==0;stages.append(record);project.save(out+"/stage-"+QString::number(step)+".gatlas");++step;
  if(step==1)access.enrich(project);else if(step==2)fresh.load(selection);else if(step==3)fresh.load(selection,"all",true);else finish();
 };
 QObject::connect(&access,&DataAccess::loaded,&app,loaded);QObject::connect(&fresh,&DataAccess::loaded,&app,loaded);QObject::connect(&access,&DataAccess::failed,&app,[&](QString error){finish(error);});QObject::connect(&fresh,&DataAccess::failed,&app,[&](QString error){finish(error);});QTimer::singleShot(125000,&app,[&]{access.cancel();fresh.cancel();finish("Source integration deadline exceeded");});access.enrich(base);return app.exec();
}
