#include "geojsonoverlay.h"
#include "sitegeometry.h"
#include "scene.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QThread>
#include <thread>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
QJsonObject read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read fixture");return QJsonDocument::fromJson(f.readAll()).object();}
QByteArray bytes(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read input");return f.readAll();}
qint64 peakBytes(){
#ifdef Q_OS_WIN
 PROCESS_MEMORY_COUNTERS c{};c.cb=sizeof(c);if(GetProcessMemoryInfo(GetCurrentProcess(),&c,sizeof(c)))return c.PeakWorkingSetSize;
#endif
 return 0;
}
QByteArray hashGeometry(const ga::Project &p){QCryptographicHash hash(QCryptographicHash::Sha256);for(const auto &f:p.features){QByteArray line;for(const auto &point:f.rings[0]){if(!line.isEmpty())line+=';';line+=QByteArray::number(qint64(std::floor(point.x()*1000+.5)))+','+QByteArray::number(qint64(std::floor(point.y()*1000+.5)));}line+='\n';hash.addData(line);}return hash.result().toHex();}
}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);if(argc!=4)return 2;const QString input=QString::fromLocal8Bit(argv[1]),data=QString::fromLocal8Bit(argv[2]),out=QString::fromLocal8Bit(argv[3]);QDir().mkpath(out);ga::loadPresentationCatalog(data+"/presentations.json");QJsonArray cases;bool passed=true,finished=false;QJsonObject report{{"native",true},{"webview",false},{"test_geometry","Artificial full-size GeoJSON fixtures; not measured site data"}};
 const auto write=[&]{report["passed"]=finished&&passed;report["in_progress"]=!finished;report["cases"]=cases;report["peak_working_set_bytes"]=peakBytes();QFile f(out+"/review.json");if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));};
 try{for(const auto &value:read(input+"/web-review.json")["cases"].toArray()){
  const auto row=value.toObject(),site=row["site"].toObject();ga::Project base;base.site.name=site["name"].toString();base.site.lat=site["lat"].toDouble();base.site.lon=site["lon"].toDouble();base.site.radius=site["radius"].toDouble();base.site.shape=site["shape"].toString();base.site.boundary=ga::primitiveBoundary(base.site);base.source={{"provider","Controlled test geometry"}};
  QJsonObject result{{"file",row["file"]},{"web_geometry_expected_parts",row["clipped_parts"]},{"web_handler_accepted_in_vm",row["handler_accepted"]},{"phase","import"}};cases.append(result);write();QElapsedTimer timer;timer.start();const auto source=bytes(input+"/"+row["file"].toString());const auto id=row["kind"]=="line"?QString("00000000-0000-4000-8000-000000000001"):QString("00000000-0000-4000-8000-000000000002");auto p=ga::addGeoJsonOverlay(base,source,row["file"].toString(),"parcels",id);result["import_ms"]=timer.elapsed();result["parts"]=p.features.size();result["geometry_sha256_mm"]=QString::fromLatin1(hashGeometry(p));result["geometry_matches_web"]=result["geometry_sha256_mm"]==row["geometry_sha256_mm"];passed&=p.features.size()==row["clipped_parts"].toInt()&&result["geometry_matches_web"].toBool()&&p.source==base.source;result["phase"]="save";cases[cases.size()-1]=result;write();
  timer.restart();const auto path=out+"/"+row["kind"].toString()+".gatlas";p.save(path);result["save_ms"]=timer.elapsed();result["file_bytes"]=QFileInfo(path).size();result["phase"]="reopen";cases[cases.size()-1]=result;write();
  timer.restart();{const auto reopened=ga::Project::load(path);result["reopen_ms"]=timer.elapsed();result["roundtrip_ok"]=reopened.imports==p.imports&&reopened.features.size()==p.features.size()&&hashGeometry(reopened)==hashGeometry(p)&&reopened.source==base.source;passed&=result["roundtrip_ok"].toBool();}
  timer.restart();const auto counts=ga::layerCounts(p.features);result["classify_ms"]=timer.elapsed();result["parcels_count"]=counts["parcels"];passed&=counts["parcels"]==p.features.size();
  auto layers=ga::defaultLayers();for(auto &l:layers){l.visible=l.id=="parcels";l.opacity=1;l.presentation={{"lineWidth",.3}};}
  timer.restart();{const auto mesh=ga::buildMesh(p,layers,ga::paletteFor("ga-style-porcelain"),ga::Camera());result["mesh_ms"]=timer.elapsed();result["triangles"]=(mesh.triangles.size()+mesh.transparent.size())/3;passed&=result["triangles"].toInt()>=2*p.features.size();double area=0;for(const auto *vertices:{&mesh.triangles,&mesh.transparent})for(int i=0;i+2<vertices->size();i+=3){const auto a=(*vertices)[i],b=(*vertices)[i+1],c=(*vertices)[i+2];area+=std::abs((double(b.x)-a.x)*(double(c.y)-a.y)-(double(b.y)-a.y)*(double(c.x)-a.x))/2;}const double expected=std::abs(ga::ringArea(p.site.boundary))+row["total_length_metres"].toDouble()*.3;result["mesh_area"]=area;result["expected_mesh_area"]=expected;passed&=std::abs(area-expected)/expected<.001;}
  result["phase"]="done";result["peak_working_set_bytes"]=peakBytes();cases[cases.size()-1]=result;write();
 }
 // Combine three full-size inputs, preserving their independent identities.
 {QJsonObject combined{{"phase","load-first"}};report["combined"]=combined;write();auto p=ga::Project::load(out+"/line.gatlas");const auto primary=p.source;QElapsedTimer timer;timer.start();combined["phase"]="second-input";report["combined"]=combined;write();p=ga::addGeoJsonOverlay(std::move(p),bytes(input+"/multipart.geojson"),"multipart.geojson","parcels","00000000-0000-4000-8000-000000000002");combined["second_import_ms"]=timer.elapsed();timer.restart();combined["phase"]="third-input";report["combined"]=combined;write();p=ga::addGeoJsonOverlay(std::move(p),bytes(input+"/line.geojson"),"line-again.geojson","parcels","00000000-0000-4000-8000-000000000003");combined["third_import_ms"]=timer.elapsed();combined["parts"]=p.features.size();combined["imports"]=p.imports.size();passed&=p.features.size()==749998&&p.imports.size()==3&&p.source==primary;const auto geometry=hashGeometry(p);timer.restart();combined["phase"]="save";report["combined"]=combined;write();p.save(out+"/combined.gatlas");combined["save_ms"]=timer.elapsed();combined["file_bytes"]=QFileInfo(out+"/combined.gatlas").size();p=ga::Project();timer.restart();combined["phase"]="reopen";report["combined"]=combined;write();p=ga::Project::load(out+"/combined.gatlas");combined["reopen_ms"]=timer.elapsed();combined["roundtrip_ok"]=p.features.size()==749998&&p.imports.size()==3&&hashGeometry(p)==geometry&&p.source==primary;passed&=combined["roundtrip_ok"].toBool();p=ga::removeGeoJsonOverlay(std::move(p),"00000000-0000-4000-8000-000000000002");combined["remove_one_ok"]=p.imports.size()==2&&p.features.size()==599998&&p.source==primary;passed&=combined["remove_one_ok"].toBool();combined["phase"]="done";report["combined"]=combined;write();}
 // Cancellation is exercised while processing the full 300,000-coordinate file.
 const auto row=read(input+"/web-review.json")["cases"].toArray()[0].toObject();ga::Project base;base.site.shape="square";base.site.boundary=ga::primitiveBoundary(base.site);std::atomic_bool canceled=false;bool stopped=false;QElapsedTimer elapsed;elapsed.start();std::thread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));canceled=true;});try{ga::addGeoJsonOverlay(base,bytes(input+"/"+row["file"].toString()),"cancel.geojson","parcels","cancel",&canceled);}catch(const std::exception &e){stopped=ga::errorText(e)=="Canceled";}cancel.join();report["cancelled_during_full_input"]=stopped;report["cancel_elapsed_ms"]=elapsed.elapsed();passed&=stopped&&base.features.isEmpty()&&base.imports.isEmpty();
 }catch(const std::exception &e){passed=false;report["error"]=ga::errorText(e);}finished=true;write();return passed?0:1;
}
