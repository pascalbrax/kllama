#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QPointer>
#include <memory>

struct Response { QList<QByteArray> parts; int http=200; bool complete=true; int interval=25; };
class Fixture : public QTcpServer {
public:
    QString digest="fixture-digest";
    QStringList modelNames={"fixture:latest"};
    QHash<QString,QJsonObject> modelDetails;
    QList<Response> inspections;
    int inspectionCount=0;
    QList<Response> responses;
    QJsonArray runningModels;
    QList<Response> runtimeResponses;
    int runtimeCount=0;
    QList<QJsonObject> requests;
    Fixture() {
        connect(this,&QTcpServer::newConnection,this,[this]{
            while (hasPendingConnections()) {
                auto *socket=nextPendingConnection();
                auto data=std::make_shared<QByteArray>();
                connect(socket,&QTcpSocket::readyRead,socket,[this,socket,data]{
                    data->append(socket->readAll());
                    const auto headerEnd=data->indexOf("\r\n\r\n");
                    if (headerEnd<0 || socket->property("handled").toBool()) return;
                    const bool post=data->startsWith("POST");
                    qint64 length=0;
                    for (const auto &line:data->left(headerEnd).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length=line.mid(15).trimmed().toLongLong();
                    if (data->size()<headerEnd+4+length) return;
                    socket->setProperty("handled",true);
                    Response response;
                    if (data->startsWith("GET /api/ps ")) {
                        ++runtimeCount;
                        if (!runtimeResponses.isEmpty()) response=runtimeResponses.takeFirst();
                        else response.parts={QJsonDocument(QJsonObject{{"models",runningModels}}).toJson(QJsonDocument::Compact)};
                    } else if (!post) {
                        QJsonArray models;for(const auto &name:modelNames)models.append(QJsonObject{{"name",name},{"digest",digest}});
                        response.parts={QJsonDocument(QJsonObject{{"models",models}}).toJson(QJsonDocument::Compact)};
                    } else if(data->startsWith("POST /api/show ")) {
                        ++inspectionCount;
                        const auto name=QJsonDocument::fromJson(data->mid(headerEnd+4)).object().value("model").toString();
                        if(!inspections.isEmpty())response=inspections.takeFirst();
                        else response.parts={QJsonDocument(modelDetails.value(name,QJsonObject{{"capabilities",QJsonArray{"completion"}},{"model_info",QJsonObject{{"general.architecture","fixture"},{"fixture.context_length",32768}}}})).toJson(QJsonDocument::Compact)};
                    }
                    else {
                        requests.append(QJsonDocument::fromJson(data->mid(headerEnd+4)).object());
                        if (responses.isEmpty()) response.parts={"{\"error\":\"No fixture response\"}\n"};
                        else response=responses.takeFirst();
                    }
                    qint64 bytes=0; for (const auto &part:response.parts) bytes+=part.size();
                    socket->write("HTTP/1.1 "+QByteArray::number(response.http)+" Test\r\nContent-Type: application/x-ndjson\r\nContent-Length: "+QByteArray::number(bytes+(response.complete?0:1000))+"\r\nConnection: close\r\n\r\n");
                    QPointer<QTcpSocket> guard(socket);
                    for (qsizetype i=0;i<response.parts.size();++i) {
                        const auto part=response.parts[i]; const bool last=i==response.parts.size()-1;
                        QTimer::singleShot(10+int(i)*response.interval,socket,[guard,part,last,response]{
                            if (!guard || guard->state()!=QAbstractSocket::ConnectedState) return;
                            guard->write(part);
                            if (last && response.complete) guard->disconnectFromHost();
                        });
                    }
                });
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
    }
    QString endpoint() const { return QString("http://127.0.0.1:%1").arg(serverPort()); }
    static QByteArray record(const QString &text,bool done=false) {
        return QJsonDocument(QJsonObject{{"message",QJsonObject{{"role","assistant"},{"content",text}}},{"done",done}}).toJson(QJsonDocument::Compact)+'\n';
    }
};
