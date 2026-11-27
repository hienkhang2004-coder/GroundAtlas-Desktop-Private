#include "dataaccess.h"
#include "geoimport.h"
#include "osmworkflow.h"
#include "sitegeometry.h"
#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QUrlQuery>
#include <QPointer>
#include "providerfixture.h"
using namespace SourceFixture;
namespace {
ga::Site site(){ga::Site s;s.radius=500;s.boundary=ga::primitiveBoundary(s);return s;}
DataAccess::Options options(const QString &directory,const Provider &provider){DataAccess::Options o;o.cacheDirectory=directory;o.endpoints={provider.url()};o.timeoutMs=2000;o.attemptTimeoutMs=1000;o.minNetworkIntervalMs=0;return o;}
ga::Project result(const QSignalSpy &spy){return qvariant_cast<ga::Project>(spy.last()[0]);}
}
class DataTests:public QObject {
 Q_OBJECT
private slots:
 void combinedSourcesCannotExceedProjectRoundTripLimit(){ga::Project base,osm;base.site=osm.site=site();ga::Feature first;first.id=first.sourceId="primary";first.geometry=ga::Geometry::Line;first.rings={QPolygonF(QVector<QPointF>(ga::Project::maximumGeometryPoints-300000,QPointF(0,0)))};base.features={first};auto second=first;second.id=second.sourceId="way/1";second.tags={{"highway","cycleway"}};second.rings={QPolygonF(QVector<QPointF>(300001,QPointF(0,0)))};osm.features={second};QVERIFY_EXCEPTION_THROWN(ga::mergeOsmContext(base,osm,"limit"),std::runtime_error);QCOMPARE(base.features[0].rings[0].size(),ga::Project::maximumGeometryPoints-300000);}

 void closedWayAreasMatchActualWeb(){QFile file(GA_SOURCE_FIXTURE);QVERIFY(file.open(QIODevice::ReadOnly));const auto cases=QJsonDocument::fromJson(file.readAll()).object()["geometry"].toArray();QCOMPARE(cases.size(),6);for(const auto &value:cases){const auto row=value.toObject();const auto p=ga::importOverpass(QJsonObject{{"elements",QJsonArray{row["row"]}}},site());QCOMPARE(p.features.size(),1);QCOMPARE(p.features[0].geometry==ga::Geometry::Polygon,row["geometry"].toString()=="Polygon");}}

 void requestPacingDoesNotBlockCacheHits(){QTemporaryDir dir;Provider server;auto o=options(dir.path(),server);o.minNetworkIntervalMs=2000;DataAccess access(o);QSignalSpy loaded(&access,&DataAccess::loaded),failed(&access,&DataAccess::failed);access.load(site());QTRY_COMPARE(loaded.size(),1);access.load(site());QTRY_COMPARE(loaded.size(),2);access.load(site(),"all",true);QCOMPARE(failed.size(),1);QCOMPARE(server.requests,1);QTest::qWait(2050);access.load(site(),"all",true);QTRY_COMPARE(loaded.size(),3);QCOMPARE(server.requests,2);}

 void initTestCase(){Provider provider;QVERIFY2(provider.isListening(),qPrintable(provider.errorString()));}
 void sourceRulesMatchActualWeb(){QFile file(GA_SOURCE_FIXTURE);QVERIFY(file.open(QIODevice::ReadOnly));const auto fixture=QJsonDocument::fromJson(file.readAll()).object();const auto selectors=fixture["selectors"].toObject();for(const auto &key:selectors.keys())QCOMPARE(ga::osmSelectors(key,"20,100,21,101"),selectors[key].toString());for(const auto &value:fixture["heights"].toArray()){const auto row=value.toObject();const auto p=ga::importOverpass(QJsonObject{{"elements",QJsonArray{row["row"]}}},site());QCOMPARE(p.features.size(),1);if(row["height"].isNull())QVERIFY(!p.features[0].knownHeight());else{QVERIFY(p.features[0].knownHeight());QVERIFY(std::abs(p.features[0].height-row["height"].toDouble())<1e-9);QCOMPARE(p.features[0].heightSource,row["height_source"].toString());}QCOMPARE(ga::Project::fromJson(p.toJson()).features[0].knownHeight(),p.features[0].knownHeight());}}
 void providerFallbackRetainsDeliveryTrace(){QTemporaryDir dir;Provider first,second;first.replies={{503,"{}",0}};auto o=options(dir.path(),first);o.endpoints.append(second.url());DataAccess access(o);QSignalSpy loaded(&access,&DataAccess::loaded),failed(&access,&DataAccess::failed);access.load(site());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,4000);QCOMPARE(failed.size(),0);QCOMPARE(first.requests,1);QCOMPARE(second.requests,1);const auto delivery=result(loaded).source["delivery"].toObject();QCOMPARE(delivery["trace"].toArray().size(),2);QCOMPARE(delivery["endpoint"].toString(),second.url());QCOMPARE(result(loaded).source["source_timestamp"].toString(),QString("2026-10-27T00:00:00Z"));}
 void persistentCacheAndExplicitRefresh(){QTemporaryDir dir;Provider server;server.replies={{200,payload(7),0},{200,payload(8),0}};auto o=options(dir.path(),server);{DataAccess access(o);QSignalSpy loaded(&access,&DataAccess::loaded);access.load(site());QTRY_COMPARE(loaded.size(),1);QCOMPARE(result(loaded).source["delivery"].toObject()["cache"].toString(),QString("miss"));}DataAccess fresh(o);QSignalSpy loaded(&fresh,&DataAccess::loaded);fresh.load(site());QTRY_COMPARE(loaded.size(),1);QCOMPARE(server.requests,1);QCOMPARE(result(loaded).source["delivery"].toObject()["cache"].toString(),QString("hit"));loaded.clear();fresh.load(site(),"all",true);QTRY_COMPARE(loaded.size(),1);QCOMPARE(server.requests,2);QCOMPARE(result(loaded).features[0].sourceId,QString("node/8"));}
 void expiredOrCorruptCacheIsNotServed(){QTemporaryDir dir;Provider server;DataAccess access(options(dir.path(),server));QSignalSpy loaded(&access,&DataAccess::loaded);access.load(site());QTRY_COMPARE(loaded.size(),1);auto metas=QDir(dir.path()).entryList({"*.meta.json"},QDir::Files);QCOMPARE(metas.size(),1);QFile meta(dir.filePath(metas[0]));QVERIFY(meta.open(QIODevice::ReadOnly));auto record=QJsonDocument::fromJson(meta.readAll()).object();meta.close();record["received_at"]=QDateTime::currentDateTimeUtc().addSecs(-21601).toString(Qt::ISODateWithMs);QVERIFY(meta.open(QIODevice::WriteOnly));meta.write(QJsonDocument(record).toJson());meta.close();loaded.clear();access.load(site());QTRY_COMPARE(loaded.size(),1);QCOMPARE(server.requests,2);const auto data=metas[0].left(64)+".json";QFile corrupt(dir.filePath(data));QVERIFY(corrupt.open(QIODevice::WriteOnly));corrupt.write("corrupt cache");corrupt.close();loaded.clear();access.load(site());QTRY_COMPARE(loaded.size(),1);QCOMPARE(server.requests,3);}
 void profilesHaveSeparateCacheEntries(){QTemporaryDir dir;Provider server;DataAccess access(options(dir.path(),server));QSignalSpy loaded(&access,&DataAccess::loaded);for(const auto &profile:QStringList{"all","context","all"}){loaded.clear();access.load(site(),profile);QTRY_COMPARE(loaded.size(),1);}QCOMPARE(server.requests,2);QVERIFY(server.queries[0].contains("nwr[building]"));QVERIFY(!server.queries[1].contains("nwr[building]"));QVERIFY(server.queries[1].contains("nwr[healthcare]"));}
 void cacheEvictsOnlyOwnedEntriesAfterEightWindows(){QTemporaryDir dir;Provider server;QFile unrelated(dir.filePath("notes.json"));QVERIFY(unrelated.open(QIODevice::WriteOnly));unrelated.write("user note");unrelated.close();DataAccess access(options(dir.path(),server));QSignalSpy loaded(&access,&DataAccess::loaded);for(int i=0;i<9;++i){auto selection=site();selection.lon+=i*.0001;loaded.clear();access.load(selection);QTRY_COMPARE(loaded.size(),1);}QCOMPARE(QDir(dir.path()).entryList({"*.meta.json"},QDir::Files).size(),8);QVERIFY(QFile::exists(unrelated.fileName()));loaded.clear();access.load(site());QTRY_COMPARE(loaded.size(),1);QCOMPARE(server.requests,10);}
 void nonRetryableErrorsDoNotChangeProvider(){QTemporaryDir dir;Provider first,second;first.replies={{401,"{}",0}};auto o=options(dir.path(),first);o.endpoints.append(second.url());DataAccess access(o);QSignalSpy loaded(&access,&DataAccess::loaded),failed(&access,&DataAccess::failed);access.load(site());QTRY_COMPARE(failed.size(),1);QCOMPARE(loaded.size(),0);QCOMPARE(second.requests,0);QCOMPARE(QDir(dir.path()).entryList({"*.meta.json"},QDir::Files).size(),0);}
 void attemptDeadlineAllowsFallbackWithinTotalBudget(){QTemporaryDir dir;Provider first,second;first.replies={{200,payload(),800}};auto o=options(dir.path(),first);o.endpoints.append(second.url());o.attemptTimeoutMs=150;o.timeoutMs=700;DataAccess access(o);QSignalSpy loaded(&access,&DataAccess::loaded);QElapsedTimer clock;clock.start();access.load(site());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,1500);QVERIFY(clock.elapsed()<1000);const auto trace=result(loaded).source["delivery"].toObject()["trace"].toArray();QVERIFY(trace[0].toObject()["timeout"].toBool());}
 void cancelledReplyCannotDeliverAndCanRestart(){QTemporaryDir dir;Provider server;server.replies={{200,payload(),400},{200,payload(8),0}};DataAccess access(options(dir.path(),server));QSignalSpy loaded(&access,&DataAccess::loaded),failed(&access,&DataAccess::failed);access.load(site());QTRY_COMPARE(server.requests,1);access.cancel();QTest::qWait(500);QCOMPARE(loaded.size(),0);QCOMPARE(failed.size(),0);access.load(site());QTRY_COMPARE(loaded.size(),1);QCOMPARE(result(loaded).features[0].sourceId,QString("node/8"));}
 void oversizedResponseAndInvalidPayloadAreNotCached(){QTemporaryDir dir;Provider server;server.replies={{200,QByteArray(1024,'x'),0},{200,"not-json",0},{200,"{\"elements\":[],\"remark\":\"incomplete\"}",0}};auto o=options(dir.path(),server);o.maximumBytes=512;DataAccess access(o);QSignalSpy loaded(&access,&DataAccess::loaded),failed(&access,&DataAccess::failed);for(int i=1;i<=3;++i){access.load(site());QTRY_COMPARE(failed.size(),i);}QCOMPARE(loaded.size(),0);QCOMPARE(QDir(dir.path()).entryList({"*.meta.json"},QDir::Files).size(),0);}
 void enrichmentMatchesWebWithoutReplacingPrimaryGeometry(){QFile file(GA_SOURCE_FIXTURE);QVERIFY(file.open(QIODevice::ReadOnly));const auto fixture=QJsonDocument::fromJson(file.readAll()).object();ga::Project base;base.site=site();base.notes="Design notes";base.style="ga-style-porcelain";base.source={{"attribution","Primary source"},{"coverage_fraction",.7}};ga::Feature building;building.id=building.sourceId="overture-1";building.layer="buildings";building.tags={{"building","yes"}};building.rings={QPolygonF{{0,0},{30,0},{30,30},{0,30}},QPolygonF{{5,5},{5,10},{10,10},{10,5}}};building.height=12;building.heightSource="google_ai_2023";base.features={building};ga::Project osm;osm.site=base.site;
  for(const auto &value:fixture["candidates"].toArray()){const auto row=value.toObject();ga::Feature f;f.id=f.sourceId=row["id"].toString();f.tags=row["tags"].toObject();f.layer=ga::classify(f.tags);f.geometry=ga::Geometry::Point;f.rings={QPolygonF{{0,0}}};f.provenance=QJsonArray{QJsonObject{{"dataset","OpenStreetMap"},{"record_id",f.sourceId},{"license","ODbL-1.0"}}};osm.features.append(f);}base.features.append(osm.features[0]);const auto original=base.toJson();const auto merged=ga::mergeOsmContext(base,osm,"test");QCOMPARE(base.toJson(),original);QCOMPARE(merged.features[0].rings,building.rings);QCOMPARE(merged.features[0].height,12.);QCOMPARE(merged.features[0].heightSource,QString("google_ai_2023"));QCOMPARE(merged.notes,base.notes);QCOMPARE(merged.style,base.style);QCOMPARE(merged.source["coverage_fraction"],base.source["coverage_fraction"]);QStringList added;for(int i=base.features.size();i<merged.features.size();++i)added.append(merged.features[i].sourceId);QCOMPARE(QJsonArray::fromStringList(added),fixture["enrichment_added_ids"].toArray());const auto repeated=ga::mergeOsmContext(merged,osm,"repeat");QCOMPARE(repeated.features.size(),merged.features.size());QCOMPARE(repeated.source["enrichments"].toArray().last().toObject()["added_geometry_parts"].toInt(),0);QCOMPARE(ga::Project::fromJson(merged.toJson()).features[0].rings,building.rings);}
 void enrichmentUsesSameAsyncCacheAndSupportsEmptyResult(){QTemporaryDir dir;Provider server;server.replies={{200,"{\"elements\":[]}",0}};ga::Project base;base.site=site();ga::Feature f;f.id=f.sourceId="base";f.layer="buildings";f.tags={{"building","yes"}};f.rings={QPolygonF{{0,0},{10,0},{10,10}}};base.features={f};DataAccess access(options(dir.path(),server));QSignalSpy loaded(&access,&DataAccess::loaded),failed(&access,&DataAccess::failed);access.enrich(base);QTRY_COMPARE(loaded.size(),1);QCOMPARE(failed.size(),0);const auto first=result(loaded);QCOMPARE(first.features.size(),1);QVERIFY(!first.features[0].knownHeight());loaded.clear();access.enrich(first);QTRY_COMPARE(loaded.size(),1);QCOMPARE(server.requests,1);QCOMPARE(result(loaded).source["enrichments"].toArray().last().toObject()["source"].toObject()["delivery"].toObject()["cache"].toString(),QString("hit"));}
 void rawOsmApiV06NodesWaysAndMultipolygonsImportCleanly(){
  const auto s=site();
  const auto ll=[&](double x,double y){return ga::geographicPoint({x,y},s);};
  const QPointF p0=ll(-40,-40),p1=ll(40,-40),p2=ll(40,40),p3=ll(-40,40);
  const QPointF h0=ll(-10,-10),h1=ll(10,-10),h2=ll(10,10),h3=ll(-10,10);
  const QPointF r0=ll(-80,60),r1=ll(80,60);
  const QJsonObject v06{
   {"version","0.6"},
   {"elements",QJsonArray{
    QJsonObject{{"type","node"},{"id",1},{"lon",p0.x()},{"lat",p0.y()}},
    QJsonObject{{"type","node"},{"id",2},{"lon",p1.x()},{"lat",p1.y()}},
    QJsonObject{{"type","node"},{"id",3},{"lon",p2.x()},{"lat",p2.y()}},
    QJsonObject{{"type","node"},{"id",4},{"lon",p3.x()},{"lat",p3.y()}},
    QJsonObject{{"type","node"},{"id",5},{"lon",h0.x()},{"lat",h0.y()}},
    QJsonObject{{"type","node"},{"id",6},{"lon",h1.x()},{"lat",h1.y()}},
    QJsonObject{{"type","node"},{"id",7},{"lon",h2.x()},{"lat",h2.y()}},
    QJsonObject{{"type","node"},{"id",8},{"lon",h3.x()},{"lat",h3.y()}},
    QJsonObject{{"type","node"},{"id",9},{"lon",r0.x()},{"lat",r0.y()}},
    QJsonObject{{"type","node"},{"id",10},{"lon",r1.x()},{"lat",r1.y()}},
    QJsonObject{{"type","way"},{"id",100},{"nodes",QJsonArray{1,2,3,4,1}}},
    QJsonObject{{"type","way"},{"id",101},{"nodes",QJsonArray{5,6,7,8,5}}},
    QJsonObject{{"type","way"},{"id",102},{"nodes",QJsonArray{9,10}},{"tags",QJsonObject{{"highway","primary"},{"name","Hoang Minh Thao"}}}},
    QJsonObject{{"type","relation"},{"id",200},{"tags",QJsonObject{{"type","multipolygon"},{"building","commercial"},{"height","28"}}},
     {"members",QJsonArray{
      QJsonObject{{"type","way"},{"ref",100},{"role","outer"}},
      QJsonObject{{"type","way"},{"ref",101},{"role","inner"}}
     }}}
   }}
  };
  const auto p=ga::importOverpass(v06,s);
  QCOMPARE(p.features.size(),2);
  bool sawBuilding=false,sawRoad=false;
  for(const auto &f:p.features){
   if(f.building()){sawBuilding=true;QCOMPARE(f.rings.size(),2);QCOMPARE(f.height,28.0);}
   if(f.tags["highway"].toString()=="primary")sawRoad=true;
  }
  QVERIFY(sawBuilding&&sawRoad);
  QCOMPARE(p.source["provider"].toString(),QString("OpenStreetMap API v0.6"));
 }
};
QTEST_GUILESS_MAIN(DataTests)
#include "data_tests.moc"
