#pragma once
#include "project.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QJsonArray>
#include <QElapsedTimer>
#include <functional>
#include <atomic>
#include <memory>
class QNetworkReply;
class DataAccess:public QObject {
 Q_OBJECT
public:
 struct Options {
  QString cacheDirectory;
  QStringList endpoints={
   "https://api.openstreetmap.org/api/0.6/map.json",
   "https://overpass-api.de/api/interpreter",
   "https://overpass.openstreetmap.fr/api/interpreter",
   "https://lz4.overpass-api.de/api/interpreter",
   "https://overpass.private.coffee/api/interpreter"
  };
  int timeoutMs=60000,attemptTimeoutMs=20000,minNetworkIntervalMs=2000;
  qint64 maximumBytes=35000000;
 };
 explicit DataAccess(QObject *parent=nullptr);
 DataAccess(QString cacheDirectory,QObject *parent=nullptr);
 DataAccess(Options options,QObject *parent=nullptr);
 ~DataAccess()override;
 void search(const QString &query,double lat,double lon,bool english);
 void load(const ga::Site &site,const QString &profile="all",bool refresh=false);
 void enrich(ga::Project source,bool refresh=false);
 void loadSaved(const QString &file,const ga::Site &selection);
 void loadCurrent(ga::Project source,const ga::Site &selection);
 void importOverlay(ga::Project source,const QString &file,const QString &role);
 void setOverpassEndpoint(const QString &endpoint);
 void cancel();
signals:
 void results(QJsonArray records);
 void loaded(ga::Project project);
 void overlayReady(ga::Project project);
 void failed(QString message);
 void busy(bool loading);
 void phase(QString name,int attempt);
 void downloadProgress(qint64 receivedBytes,qint64 totalBytes,QString endpointHost);
private:
 struct Request;
 QNetworkAccessManager network_;
 QPointer<QNetworkReply> search_,load_;
 quint64 searchGeneration_=0,loadGeneration_=0;
 Options options_;
 QElapsedTimer lastNetworkStarted_;
 std::shared_ptr<std::atomic_bool> cancelToken_=std::make_shared<std::atomic_bool>(false);
 void start(const ga::Site &site,QString profile,bool refresh,std::shared_ptr<ga::Project> base={});
 void attempt(std::shared_ptr<Request> state);
 void consume(std::shared_ptr<Request> state);
 bool readCache(std::shared_ptr<Request> state);
 void saveCache(const std::shared_ptr<Request> &state);
 void convert(std::function<ga::Project()> operation,quint64 generation,std::function<void()> accepted={},bool overlay=false);
};
