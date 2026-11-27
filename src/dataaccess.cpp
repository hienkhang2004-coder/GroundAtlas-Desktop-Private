#include "dataaccess.h"
#include "geoimport.h"
#include "sitegeometry.h"
#include "osmworkflow.h"
#include "geojsonoverlay.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QTimer>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUuid>
#include <QSet>
#include <algorithm>
#include <QtConcurrent>
namespace {
QNetworkRequest request(const QUrl &url,int timeout){QNetworkRequest r(url);r.setTransferTimeout(timeout);r.setRawHeader("User-Agent","GroundAtlasDesktop/2.2 (+https://groundatlas.groundatlas.workers.dev/)");r.setRawHeader("Accept","application/json");return r;}
struct Converted {ga::Project project;QString error;};
bool permittedEndpoint(const QString &value){const QUrl url(value);return url.isValid()&&url.userInfo().isEmpty()&&url.fragment().isEmpty()&&(url.scheme()=="https"||(url.scheme()=="http"&&(url.host()=="127.0.0.1"||url.host()=="localhost")));}
bool isOsmApiV06(const QString &endpoint){return endpoint.contains("/api/0.6/map.json");}
struct GeoBbox{double west=180,south=85,east=-180,north=-85;};
GeoBbox siteBbox(const ga::Site &site){
 GeoBbox b;
 const auto ring=site.boundary.isEmpty()?ga::primitiveBoundary(site):site.boundary;
 for(const auto &pt:ring){const auto ll=ga::geographicPoint(pt,site);b.west=std::min(b.west,ll.x());b.east=std::max(b.east,ll.x());b.south=std::min(b.south,ll.y());b.north=std::max(b.north,ll.y());}
 return b;
}
QString formatBbox(const GeoBbox &b){
 const auto num=[](double v){return QString::number(v,'f',7);};
 return num(b.west)+","+num(b.south)+","+num(b.east)+","+num(b.north);
}
QByteArray digest(const QByteArray &bytes){return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();}
bool atomicWrite(const QString &path,const QByteArray &bytes){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
}
struct DataAccess::Request {
 ga::Site site;QString profile,query,key,endpoint,id=QUuid::createUuid().toString(QUuid::WithoutBraces);
 quint64 generation=0;int nextEndpoint=0,cacheAge=0;bool hit=false,tooLarge=false,timedOut=false;
 QVector<GeoBbox> subBoxes;int subBoxIndex=0;QJsonArray mergedElements;QSet<QString> mergedSeen;
 QByteArray bytes;QJsonArray trace;QDateTime received;QElapsedTimer timer;
 std::shared_ptr<std::atomic_bool> token;std::shared_ptr<ga::Project> base;
};
DataAccess::DataAccess(QObject *parent):DataAccess(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/source-cache",parent){}
DataAccess::DataAccess(QString directory,QObject *parent):DataAccess(Options{std::move(directory)},parent){}
DataAccess::DataAccess(Options options,QObject *parent):QObject(parent),options_(std::move(options)){
 options_.timeoutMs=std::clamp(options_.timeoutMs,50,60000);options_.attemptTimeoutMs=std::clamp(options_.attemptTimeoutMs,25,options_.timeoutMs);options_.maximumBytes=std::clamp<qint64>(options_.maximumBytes,256,35000000);
 options_.minNetworkIntervalMs=std::clamp(options_.minNetworkIntervalMs,0,2000);
 options_.endpoints.removeIf([](const QString &url){return !permittedEndpoint(url);});if(options_.endpoints.isEmpty())options_.endpoints=Options{}.endpoints;
}
DataAccess::~DataAccess(){
 if(cancelToken_)cancelToken_->store(true);
 ++loadGeneration_;++searchGeneration_;
 // A QObject child may be destroyed after its owner's UI. Do not emit busy or
 // invoke replies into the half-destroyed window during teardown.
 for(auto reply:{load_,search_})if(reply){disconnect(reply,nullptr,this,nullptr);reply->abort();}
}
void DataAccess::setOverpassEndpoint(const QString &endpoint){if(permittedEndpoint(endpoint))options_.endpoints={endpoint};}
void DataAccess::search(const QString &query,double lat,double lon,bool english){const auto generation=++searchGeneration_;if(search_)search_->abort();QUrl url("https://photon.komoot.io/api/");QUrlQuery parameters;parameters.addQueryItem("q",query.trimmed().left(200));parameters.addQueryItem("limit","5");parameters.addQueryItem("lat",QString::number(lat));parameters.addQueryItem("lon",QString::number(lon));if(english)parameters.addQueryItem("lang","en");url.setQuery(parameters);auto reply=network_.get(request(url,18000));search_=reply;connect(reply,&QNetworkReply::downloadProgress,this,[reply](qint64 bytes,qint64){if(bytes>2*1024*1024)reply->abort();});connect(reply,&QNetworkReply::finished,this,[this,reply,generation]{reply->deleteLater();if(generation!=searchGeneration_)return;if(reply->error()!=QNetworkReply::NoError){emit failed(reply->errorString());return;}QJsonParseError error;const auto doc=QJsonDocument::fromJson(reply->readAll(),&error);if(error.error!=QJsonParseError::NoError||!doc.object()["features"].isArray()){emit failed("Invalid place-search response");return;}emit results(doc.object()["features"].toArray());});}
void DataAccess::cancel(){if(cancelToken_)cancelToken_->store(true);++loadGeneration_;if(load_)load_->abort();emit busy(false);}
void DataAccess::load(const ga::Site &site,const QString &profile,bool refresh){start(site,profile,refresh);}
void DataAccess::enrich(ga::Project source,bool refresh){const auto site=source.site;start(site,"all",refresh,std::make_shared<ga::Project>(std::move(source)));}
void DataAccess::start(const ga::Site &site,QString profile,bool refresh,std::shared_ptr<ga::Project> base){
 cancel();setProperty("osmDeliveryDiagnostics",QJsonObject{});cancelToken_=std::make_shared<std::atomic_bool>(false);auto state=std::make_shared<Request>();state->generation=loadGeneration_;state->token=cancelToken_;state->site=site;state->profile=std::move(profile);state->base=std::move(base);
 try{state->query=ga::osmQuery(site,state->profile);}catch(const std::exception &e){emit failed(ga::errorText(e));return;}
 state->key=QString::fromLatin1(digest("osm-v1\n"+options_.endpoints.join('\n').toUtf8()+"\n"+state->query.toUtf8()));state->timer.start();emit busy(true);
 if(!refresh&&readCache(state)){emit phase("cache",0);consume(state);}
 else if(lastNetworkStarted_.isValid()&&lastNetworkStarted_.elapsed()<options_.minNetworkIntervalMs){emit busy(false);emit failed("Wait two seconds before another OSM request");}
 else{lastNetworkStarted_.start();attempt(state);}
}
bool DataAccess::readCache(std::shared_ptr<Request> state){
 if(options_.cacheDirectory.isEmpty())return false;
 const auto stem=options_.cacheDirectory+"/"+state->key;QFile meta(stem+".meta.json"),payload(stem+".json");if(!meta.open(QIODevice::ReadOnly)||meta.size()>8192||!payload.open(QIODevice::ReadOnly)||payload.size()>options_.maximumBytes)return false;
 auto record=QJsonDocument::fromJson(meta.readAll()).object();state->received=QDateTime::fromString(record["received_at"].toString(),Qt::ISODateWithMs);const auto age=state->received.secsTo(QDateTime::currentDateTimeUtc());if(record["format"].toString()!="groundatlas-osm-cache"||record["key"].toString()!=state->key||!state->received.isValid()||age<0||age>=21600)return false;
 const auto bytes=payload.readAll();if(digest(bytes)!=record["sha256"].toString().toLatin1())return false;state->bytes=bytes;state->hit=true;state->cacheAge=int(age);state->endpoint=record["endpoint"].toString();state->trace.append(QJsonObject{{"phase","cache"},{"age_seconds",int(age)},{"received_at",record["received_at"]}});record["last_access_at"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);atomicWrite(stem+".meta.json",QJsonDocument(record).toJson(QJsonDocument::Compact));return true;
}
void DataAccess::saveCache(const std::shared_ptr<Request> &state){
 if(state->hit||state->token->load()||options_.cacheDirectory.isEmpty())return;
 QDir directory(options_.cacheDirectory);if(!directory.mkpath("."))return;const auto stem=directory.filePath(state->key);
 QJsonObject meta{{"format","groundatlas-osm-cache"},{"key",state->key},{"received_at",state->received.toString(Qt::ISODateWithMs)},{"last_access_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"endpoint",state->endpoint},{"sha256",QString::fromLatin1(digest(state->bytes))}};
 if(!atomicWrite(stem+".json",state->bytes)||!atomicWrite(stem+".meta.json",QJsonDocument(meta).toJson(QJsonDocument::Compact)))return;
 // Only remove cache-owned, hash-named entries, never arbitrary user files.
 auto entries=directory.entryInfoList({"*.meta.json"},QDir::Files,QDir::Time);int owned=0;const QRegularExpression name("^[a-f0-9]{64}\\.meta\\.json$");
 for(const auto &entry:entries){if(!name.match(entry.fileName()).hasMatch())continue;QFile file(entry.absoluteFilePath());if(!file.open(QIODevice::ReadOnly)||file.size()>8192)continue;const auto item=QJsonDocument::fromJson(file.readAll()).object();file.close();if(item["format"].toString()!="groundatlas-osm-cache")continue;if(++owned>8){QFile::remove(directory.filePath(entry.fileName().left(64)+".json"));QFile::remove(entry.absoluteFilePath());}}
}
void DataAccess::attempt(std::shared_ptr<Request> state){
 if(state->generation!=loadGeneration_||state->token->load())return;
 const int remaining=options_.timeoutMs-int(state->timer.elapsed());
 const bool continuingSubTile=(!state->subBoxes.isEmpty()&&state->subBoxIndex<state->subBoxes.size());
 if(!continuingSubTile){
  if(remaining<=0||state->nextEndpoint>=options_.endpoints.size()){emit busy(false);emit failed("OSM providers are unavailable; previous projects are retained");return;}
  state->endpoint=options_.endpoints[state->nextEndpoint++];
  // OSM API v0.6 returns full dataset; skip it for specialized Overpass-only profiles when Overpass mirrors also exist.
  if(isOsmApiV06(state->endpoint)&&state->profile!="all"&&state->nextEndpoint<options_.endpoints.size()){attempt(state);return;}
  state->subBoxes.clear();state->subBoxIndex=0;state->mergedElements={};state->mergedSeen.clear();
 }else if(remaining<=0){
  emit busy(false);emit failed("OSM providers are unavailable; previous projects are retained");return;
 }
 state->bytes.clear();state->tooLarge=state->timedOut=false;
 emit phase(state->nextEndpoint==1&&!continuingSubTile?"network":"retry",state->nextEndpoint);
 const int stepTimeout=std::min(remaining,options_.attemptTimeoutMs);
 QUrl url(state->endpoint);
 QUrlQuery form;
 const bool v06=isOsmApiV06(state->endpoint);
 if(v06){
  const GeoBbox box=continuingSubTile?state->subBoxes[state->subBoxIndex]:siteBbox(state->site);
  form.addQueryItem("bbox",formatBbox(box));
 }else{
  form.addQueryItem("data",state->query);
 }
 url.setQuery(form);
 auto r=request(url,stepTimeout);
 auto reply=network_.get(r);load_=reply;
 const QString host=QUrl(state->endpoint).host()+(continuingSubTile?QString(" (%1/%2)").arg(state->subBoxIndex+1).arg(state->subBoxes.size()):QString());
 const auto read=[this,state,reply]{if(state->generation!=loadGeneration_)return;if(reply->bytesAvailable()>options_.maximumBytes-state->bytes.size()){state->tooLarge=true;reply->abort();return;}state->bytes.append(reply->readAll());};connect(reply,&QNetworkReply::readyRead,this,read);
 connect(reply,&QNetworkReply::downloadProgress,this,[this,state,reply,host](qint64 bytes,qint64 total){if(state->generation!=loadGeneration_)return;if(bytes>options_.maximumBytes||total>options_.maximumBytes){state->tooLarge=true;reply->abort();return;}emit downloadProgress(bytes,total,host);});
 QPointer<QNetworkReply> guarded=reply;QTimer::singleShot(stepTimeout,this,[this,weak=std::weak_ptr<Request>(state),guarded]{const auto state=weak.lock();if(state&&state->generation==loadGeneration_&&guarded&&guarded->isRunning()){state->timedOut=true;guarded->abort();}});
 connect(reply,&QNetworkReply::finished,this,[this,state,reply,read,v06,continuingSubTile]{
  reply->deleteLater();if(state->generation!=loadGeneration_||state->token->load())return;if(reply->isOpen())read();const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto error=reply->error();if(error==QNetworkReply::TimeoutError||(error==QNetworkReply::OperationCanceledError&&!state->tooLarge))state->timedOut=true;state->trace.append(QJsonObject{{"phase","network"},{"endpoint",state->endpoint},{"http_status",status},{"elapsed_ms",state->timer.elapsed()},{"timeout",state->timedOut},{"network_error",int(error)},{"network_message",error==QNetworkReply::NoError?QString():reply->errorString()}});setProperty("osmDeliveryDiagnostics",QJsonObject{{"endpoint",state->endpoint},{"profile",state->profile},{"trace",state->trace}});
  if(state->tooLarge){emit busy(false);emit failed("OSM response exceeds 35 MB; choose a smaller extent");return;}
  if(error!=QNetworkReply::NoError||status!=200){
   // If OSM API v0.6 single bbox hits the 50,000-node limit (HTTP 400), automatically split into 2x2 sub-tiles!
   if(v06&&status==400&&!continuingSubTile&&state->timer.elapsed()<options_.timeoutMs){
    const GeoBbox b=siteBbox(state->site);
    const double midLon=(b.west+b.east)*0.5,midLat=(b.south+b.north)*0.5;
    state->subBoxes={{b.west,b.south,midLon,midLat},{midLon,b.south,b.east,midLat},{b.west,midLat,midLon,b.north},{midLon,midLat,b.east,b.north}};
    state->subBoxIndex=0;state->mergedElements={};state->mergedSeen.clear();
    attempt(state);return;
   }
   state->subBoxes.clear();state->subBoxIndex=0;
   const QSet<int> retry={403,406,408,429,500,502,503,504,520,521,522,523,524};
   if((status==0||retry.contains(status)||(v06&&status==400))&&state->nextEndpoint<options_.endpoints.size()&&state->timer.elapsed()<options_.timeoutMs){attempt(state);return;}
   emit busy(false);emit failed(status?QString("OSM source unavailable (HTTP %1). Previous projects are retained.").arg(status):QString("OSM connection unavailable. Previous projects are retained."));return;
  }
  if(continuingSubTile){
   QJsonParseError pe;
   const auto doc=QJsonDocument::fromJson(state->bytes,&pe);
   if(pe.error==QJsonParseError::NoError&&doc.object()["elements"].isArray()){
    for(const auto &ev:doc.object()["elements"].toArray()){
     const auto eo=ev.toObject();
     const QString k=eo["type"].toString()+"/"+QString::number(eo["id"].toDouble(),'f',0);
     if(!state->mergedSeen.contains(k)){state->mergedSeen.insert(k);state->mergedElements.append(eo);}
    }
   }
   ++state->subBoxIndex;
   if(state->subBoxIndex<state->subBoxes.size()){attempt(state);return;}
   state->bytes=QJsonDocument(QJsonObject{{"version","0.6"},{"elements",state->mergedElements}}).toJson(QJsonDocument::Compact);
   state->subBoxes.clear();state->mergedElements={};state->mergedSeen.clear();
  }
  state->received=QDateTime::currentDateTimeUtc();consume(state);
 });
}
void DataAccess::consume(std::shared_ptr<Request> state){
 emit phase("convert",state->nextEndpoint);convert([state]{
  QJsonParseError error;const auto document=QJsonDocument::fromJson(state->bytes,&error);if(error.error!=QJsonParseError::NoError||!document.isObject())throw std::runtime_error("Invalid OSM JSON response");auto osm=ga::importOverpass(document.object(),state->site,state->token.get(),bool(state->base));
  const QJsonValue ts=document.object().value("osm3s").toObject().value("timestamp_osm_base");
  osm.source["source_timestamp"]=ts.isString()&&!ts.toString().isEmpty()?ts:QJsonValue(state->received.toString(Qt::ISODate));
  osm.source["profile"]=state->profile;osm.source["delivery"]=QJsonObject{{"cache",state->hit?"hit":"miss"},{"cache_age_seconds",state->cacheAge},{"cache_ttl_seconds",21600},{"received_at",state->received.toString(Qt::ISODateWithMs)},{"endpoint",state->endpoint},{"trace",state->trace}};
  return state->base?ga::mergeOsmContext(*state->base,osm,state->id,state->token.get()):osm;
 },state->generation,[this,state]{saveCache(state);});
}
void DataAccess::convert(std::function<ga::Project()> operation,quint64 generation,std::function<void()> accepted,bool overlay){auto watcher=new QFutureWatcher<Converted>(this);connect(watcher,&QFutureWatcher<Converted>::finished,this,[this,watcher,generation,accepted=std::move(accepted),overlay]{const auto value=watcher->result();watcher->deleteLater();if(generation!=loadGeneration_)return;emit busy(false);if(value.error.isEmpty()){if(accepted)accepted();if(overlay)emit overlayReady(value.project);else emit loaded(value.project);}else emit failed(value.error);});watcher->setFuture(QtConcurrent::run([operation=std::move(operation)]{Converted result;try{result.project=operation();}catch(const std::exception &error){result.error=ga::errorText(error);}return result;}));}
void DataAccess::loadSaved(const QString &file,const ga::Site &selection){cancel();cancelToken_=std::make_shared<std::atomic_bool>(false);const auto token=cancelToken_;emit busy(true);convert([file,selection,token]{if(token->load())throw std::runtime_error("Canceled");const auto source=ga::Project::load(file);auto clipped=ga::reclipProject(source,selection,token.get());clipped.view["selection_source"]=source.source["window_id"].toString("current");return clipped;},loadGeneration_);}
void DataAccess::loadCurrent(ga::Project source,const ga::Site &selection){cancel();cancelToken_=std::make_shared<std::atomic_bool>(false);const auto token=cancelToken_;emit busy(true);convert([source=std::move(source),selection,token]{auto clipped=ga::reclipProject(source,selection,token.get());clipped.view["selection_source"]="current";return clipped;},loadGeneration_);}
void DataAccess::importOverlay(ga::Project source,const QString &path,const QString &role){cancel();cancelToken_=std::make_shared<std::atomic_bool>(false);const auto token=cancelToken_;emit busy(true);emit phase("import",0);convert([source=std::move(source),path,role,token]{if(token->load())throw std::runtime_error("Canceled");QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("GeoJSON file could not be read");if(file.size()>20000000)throw std::runtime_error("GeoJSON file exceeds 20 MB; split the file");const auto bytes=file.read(20000001);if(file.error()!=QFileDevice::NoError)throw std::runtime_error("GeoJSON file could not be read");return ga::addGeoJsonOverlay(source,bytes,QFileInfo(path).fileName(),role,{},token.get());},loadGeneration_,{},true);}
