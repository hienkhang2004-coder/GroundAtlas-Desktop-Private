#include "sessionstore.h"
#include <QtConcurrent/QtConcurrent>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <stdexcept>

namespace {
void fail(const QString &message){throw std::runtime_error(message.toUtf8().constData());}
void copyAtomic(const QString &source,const QString &target){QFile in(source);QSaveFile out(target);if(!in.open(QIODevice::ReadOnly)||!out.open(QIODevice::WriteOnly))fail("Cannot open recovery file");while(!in.atEnd()){const auto bytes=in.read(262144);if(bytes.isEmpty()&&in.error()!=QFileDevice::NoError)fail(in.errorString());if(out.write(bytes)!=bytes.size())fail(out.errorString());}if(!out.commit())fail(out.errorString());}
SessionResult writeSession(const QString &directory,const SessionSnapshot &snapshot,const QHash<QString,quint64> &written,const QString &lastActive,quint64 lastRevision){
 SessionResult result;try{
  QSet<QString> ids;const QRegularExpression valid("^[a-zA-Z0-9-]{1,80}$");for(const auto &doc:snapshot.documents)if(!valid.match(doc.id).hasMatch()||ids.contains(doc.id))fail("Invalid recovery document identity");else ids.insert(doc.id);
  if(snapshot.documents.isEmpty()||snapshot.documents.size()>32||snapshot.active<0||snapshot.active>=snapshot.documents.size())fail("Invalid recovery session");
  QDir sessions(directory+"/sessions");if(!sessions.mkpath("."))fail("Cannot create recovery directory");QJsonArray rows;
  for(const auto &doc:snapshot.documents){const auto file=doc.id+".gatlas";if(!written.contains(doc.id)||written[doc.id]!=doc.revision||!sessions.exists(file)){doc.project.save(sessions.filePath(file));result.written[doc.id]=doc.revision;result.bodiesWritten=true;}rows.append(QJsonObject{{"id",doc.id},{"file",file},{"path",doc.path},{"modified",doc.modified},{"revision",qint64(doc.revision)}});}
  const auto &active=snapshot.documents[snapshot.active];if(result.written.contains(active.id)||active.id!=lastActive||active.revision!=lastRevision||!QFile::exists(directory+"/autosave.gatlas"))copyAtomic(sessions.filePath(active.id+".gatlas"),directory+"/autosave.gatlas");result.activeId=active.id;result.activeRevision=active.revision;
  QSaveFile manifest(directory+"/session.json");if(!manifest.open(QIODevice::WriteOnly))fail("Cannot write session manifest");const auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"active",snapshot.active},{"documents",rows},{"saved_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}).toJson();if(manifest.write(bytes)!=bytes.size()||!manifest.commit())fail("Cannot commit session manifest");
 }catch(const std::exception &error){result.error=ga::errorText(error);}return result;
}
}
SessionStore::SessionStore(QString directory,QObject *parent):QObject(parent),directory_(std::move(directory)){}
SessionStore::~SessionStore(){if(watcher_){disconnect(watcher_,nullptr,this,nullptr);watcher_->waitForFinished();accept(watcher_->result());}if(pending_)writeSession(directory_,*pending_,written_,activeId_,activeRevision_);}
void SessionStore::accept(const SessionResult &result){for(auto i=result.written.begin();i!=result.written.end();++i)written_[i.key()]=i.value();if(result.error.isEmpty()){activeId_=result.activeId;activeRevision_=result.activeRevision;}}
void SessionStore::save(SessionSnapshot snapshot){if(watcher_){pending_=std::move(snapshot);return;}start(std::move(snapshot));}
void SessionStore::start(SessionSnapshot snapshot){
 watcher_=new QFutureWatcher<SessionResult>(this);const auto watcher=watcher_;connect(watcher,&QFutureWatcher<SessionResult>::finished,this,[this,watcher]{const auto result=watcher->result();accept(result);watcher_=nullptr;watcher->deleteLater();if(pending_){auto next=std::move(*pending_);pending_.reset();start(std::move(next));}if(result.error.isEmpty())emit saved(result.bodiesWritten);else emit failed(result.error);if(!watcher_)emit idle();});
 watcher->setFuture(QtConcurrent::run([directory=directory_,snapshot=std::move(snapshot),written=written_,active=activeId_,revision=activeRevision_]{return writeSession(directory,snapshot,written,active,revision);}));
}
void SessionStore::flush(){while(watcher_){QEventLoop loop;connect(this,&SessionStore::idle,&loop,&QEventLoop::quit);loop.exec(QEventLoop::ExcludeUserInputEvents);}}
