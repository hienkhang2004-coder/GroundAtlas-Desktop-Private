#include "sessionstore.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class SessionTests:public QObject {
 Q_OBJECT
 std::unique_ptr<QTemporaryDir> temp_;
 ga::Project project(const QString &notes){ga::Project p;p.site.boundary=ga::circleBoundary(500);p.notes=notes;p.source={{"provider","Controlled recovery fixture"}};ga::Feature f;f.id=f.sourceId="one";f.layer="buildings";f.tags={{"building","yes"}};f.rings={QPolygonF{{-20,-20},{20,-20},{20,20},{-20,20}},QPolygonF{{-5,-5},{5,-5},{5,5},{-5,5}}};p.features={f};return p;}
 SessionSnapshot snapshot(QString notes="first",quint64 revision=1){return {{{"one","user.gatlas",project(notes),true,revision}},0};}
 QJsonObject manifest(const QString &path){QFile file(path+"/session.json");if(!file.open(QIODevice::ReadOnly))return{};return QJsonDocument::fromJson(file.readAll()).object();}
private slots:
 void initTestCase(){temp_=std::make_unique<QTemporaryDir>(QDir(QCoreApplication::applicationDirPath()).filePath("../validation/session-tests-XXXXXX"));QVERIFY(temp_->isValid());}
 void allDocumentsAndActiveAliasRecover(){const auto path=temp_->filePath("all");SessionStore store(path);auto s=snapshot();s.documents.append({"two","second.gatlas",project("second"),false,0});s.active=1;store.save(s);store.flush();const auto m=manifest(path);QCOMPARE(m["schema"].toInt(),1);QCOMPARE(m["active"].toInt(),1);QCOMPARE(m["documents"].toArray().size(),2);const auto p=ga::Project::load(path+"/sessions/one.gatlas");QCOMPARE(p.notes,QString("first"));QCOMPARE(p.stats().holeCount,1);QCOMPARE(p.stats().knownHeights,0);QCOMPARE(ga::Project::load(path+"/autosave.gatlas").notes,QString("second"));}
 void unchangedDirtyDocumentsAreNotRewritten(){const auto path=temp_->filePath("unchanged");SessionStore store(path);QSignalSpy saved(&store,&SessionStore::saved);auto s=snapshot();store.save(s);store.flush();const auto before=QFileInfo(path+"/sessions/one.gatlas").lastModified();QTest::qWait(60);s.documents[0].path="renamed-user-file.gatlas";store.save(s);store.flush();QCOMPARE(saved.size(),2);QVERIFY(saved[0][0].toBool());QVERIFY(!saved[1][0].toBool());QCOMPARE(QFileInfo(path+"/sessions/one.gatlas").lastModified(),before);QCOMPARE(manifest(path)["documents"].toArray()[0].toObject()["path"].toString(),QString("renamed-user-file.gatlas"));}
 void latestQueuedSnapshotWinsAndClosedTabsStayClosed(){const auto path=temp_->filePath("queued");SessionStore store(path);auto first=snapshot(QString(3000000,'a'));first.documents.append({"closed","",project("closed"),true,1});store.save(first);store.save(snapshot("intermediate",2));store.save(snapshot("latest",3));store.flush();QVERIFY(!store.busy());QCOMPARE(manifest(path)["documents"].toArray().size(),1);QCOMPARE(ga::Project::load(path+"/sessions/one.gatlas").notes,QString("latest"));QCOMPARE(ga::Project::load(path+"/autosave.gatlas").notes,QString("latest"));}
 void snapshotDoesNotFollowSubsequentLiveEdits(){const auto path=temp_->filePath("immutable");SessionStore store(path);auto s=snapshot("captured");store.save(s);s.documents[0].project.notes="later live edit";s.documents[0].project.features[0].tags["building"]="changed";store.flush();const auto saved=ga::Project::load(path+"/sessions/one.gatlas");QCOMPARE(saved.notes,QString("captured"));QCOMPARE(saved.features[0].tags["building"].toString(),QString("yes"));}
 void badIdentityCannotReplaceTheManifest(){const auto path=temp_->filePath("bad-id");SessionStore store(path);store.save(snapshot());store.flush();const auto before=manifest(path);QSignalSpy failed(&store,&SessionStore::failed);auto bad=snapshot("invalid",2);bad.documents[0].id="../escape";store.save(bad);store.flush();QCOMPARE(failed.size(),1);QCOMPARE(manifest(path),before);QCOMPARE(ga::Project::load(path+"/sessions/one.gatlas").notes,QString("first"));}
 void destructionFinishesTheLatestQueuedSnapshot(){const auto path=temp_->filePath("destructor");{SessionStore store(path);store.save(snapshot(QString(1000000,'a')));store.save(snapshot("final queued",2));}QCOMPARE(ga::Project::load(path+"/sessions/one.gatlas").notes,QString("final queued"));QCOMPARE(manifest(path)["documents"].toArray().size(),1);}
 void changingActiveTabUpdatesAliasWithoutRewritingBodies(){const auto path=temp_->filePath("active");SessionStore store(path);auto s=snapshot();s.documents.append({"two","",project("two"),true,1});store.save(s);store.flush();const auto before=QFileInfo(path+"/sessions/two.gatlas").lastModified();QTest::qWait(50);s.active=1;store.save(s);store.flush();QCOMPARE(QFileInfo(path+"/sessions/two.gatlas").lastModified(),before);QCOMPARE(ga::Project::load(path+"/autosave.gatlas").notes,QString("two"));}
};
QTEST_GUILESS_MAIN(SessionTests)
#include "session_tests.moc"
