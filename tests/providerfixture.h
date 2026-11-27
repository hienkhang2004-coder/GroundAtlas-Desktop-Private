#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QTimer>
#include <QUrlQuery>
#include <memory>
namespace SourceFixture {
QByteArray payload(int id=7){return QJsonDocument(QJsonObject{{"osm3s",QJsonObject{{"timestamp_osm_base","2026-10-27T00:00:00Z"}}},{"elements",QJsonArray{QJsonObject{{"type","node"},{"id",id},{"lat",21.0288},{"lon",105.8525},{"tags",QJsonObject{{"natural","tree"}}}}}}}).toJson(QJsonDocument::Compact);}
struct Response {int status=200;QByteArray body=payload();int delay=0;};
class Provider:public QTcpServer {
public:
 QVector<Response> replies;QStringList queries;int requests=0;
 Provider(){listen(QHostAddress::LocalHost,0);connect(this,&QTcpServer::newConnection,this,[this]{while(hasPendingConnections()){auto socket=nextPendingConnection();auto buffer=std::make_shared<QByteArray>();auto sent=std::make_shared<bool>(false);connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);connect(socket,&QTcpSocket::readyRead,socket,[=,this]{buffer->append(socket->readAll());if(*sent)return;const int end=buffer->indexOf("\r\n\r\n");if(end<0)return;const auto headers=buffer->left(end).split('\n');int length=0;for(const auto &line:headers)if(line.toLower().startsWith("content-length:"))length=line.mid(15).trimmed().toInt();if(buffer->size()<end+4+length)return;*sent=true;const auto firstLine=buffer->left(buffer->indexOf("\r\n")).split(' ');const auto query=firstLine.value(0)=="GET"?QUrl(QString::fromUtf8(firstLine.value(1))).query():QString::fromUtf8(buffer->mid(end+4,length));queries.append(QUrlQuery(query).queryItemValue("data",QUrl::FullyDecoded));const auto response=requests<replies.size()?replies[requests]:Response{};++requests;QTimer::singleShot(response.delay,socket,[socket,response]{if(socket->state()!=QAbstractSocket::ConnectedState)return;socket->write("HTTP/1.1 "+QByteArray::number(response.status)+" Test\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(response.body.size())+"\r\nConnection: close\r\n\r\n"+response.body);socket->disconnectFromHost();});});}});}
 QString url()const{return "http://127.0.0.1:"+QString::number(serverPort())+"/interpreter";}
};
}
