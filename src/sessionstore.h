#pragma once
#include "project.h"
#include <QObject>
#include <QFutureWatcher>
#include <QHash>
#include <optional>

struct SessionDocument {QString id,path;ga::Project project;bool modified=false;quint64 revision=0;};
struct SessionSnapshot {QVector<SessionDocument> documents;int active=0;};
struct SessionResult {QHash<QString,quint64> written;QString activeId,error;quint64 activeRevision=0;bool bodiesWritten=false;};
class SessionStore:public QObject {
 Q_OBJECT
public:
 explicit SessionStore(QString directory,QObject *parent=nullptr);
 ~SessionStore()override;
 void save(SessionSnapshot snapshot);
 bool busy()const{return watcher_!=nullptr;}
 // Drain the latest queued snapshot while continuing paint/future events.
 void flush();
signals:
 void saved(bool bodiesWritten);
 void failed(QString message);
 void idle();
private:
 QString directory_,activeId_;quint64 activeRevision_=0;
 QHash<QString,quint64> written_;
 QFutureWatcher<SessionResult> *watcher_=nullptr;
 std::optional<SessionSnapshot> pending_;
 void start(SessionSnapshot snapshot);
 void accept(const SessionResult &result);
};
