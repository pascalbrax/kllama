#include "modeldownloader.h"
#include "ollamaconnection.h"
#include <KLocalizedString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>

ModelDownloader::ModelDownloader(OllamaConnection *connection,QObject *parent):QObject(parent),m_connection(connection),m_endpoint(connection->endpoint()) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout,&QTimer::timeout,this,[this]{if(m_probe)m_access="unknown";finish("error",m_probe?i18n("Access check timed out. Check the connection and retry."):i18n("No download update for five minutes. Check the server and retry."));});
    connect(connection,&OllamaConnection::configurationChanged,this,[this]{
        if(m_endpoint==m_connection->endpoint())return;
        invalidate();m_endpoint=m_connection->endpoint();m_access="unknown";m_state="idle";m_model.clear();m_total=m_completed=0;
        m_message=i18n("Connection changed. Check download access again.");emit changed();
    });
}
ModelDownloader::~ModelDownloader(){invalidate();}
void ModelDownloader::invalidate() {
    m_timeout.stop();
    if(m_reply){disconnect(m_reply,nullptr,this,nullptr);m_reply->abort();m_reply->deleteLater();m_reply=nullptr;}
}
void ModelDownloader::finish(const QString &state,const QString &message) {
    invalidate();m_state=state;m_message=message;emit changed();
}
QString ModelDownloader::progressText() const {
    return m_total>0?i18n("%1 / %2 MiB · current layer",QString::number(double(m_completed)/1048576,'f',1),QString::number(double(m_total)/1048576,'f',1)):QString();
}
bool ModelDownloader::validName(const QString &model) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*(?:/[A-Za-z0-9][A-Za-z0-9_.-]*){0,2}(?::[A-Za-z0-9][A-Za-z0-9_.-]*)?$"));
    return !model.isEmpty() && model.size()<=256 && pattern.match(model).hasMatch();
}
void ModelDownloader::checkAccess() {
    if(busy())return;
    m_access="checking";m_state="checking";m_message=i18n("Checking download access…");start(true);
}
void ModelDownloader::download(const QString &input) {
    if(busy())return;
    if(m_access!="available"){m_state="error";m_message=i18n("Check download access before starting.");emit changed();return;}
    const auto name=input.trimmed();
    if(!validName(name)){m_state="error";m_message=i18n("Enter model[:tag] or namespace/model[:tag], without spaces or a URL.");emit changed();return;}
    m_model=name;m_total=m_completed=0;m_state="downloading";m_message=i18n("Requesting %1…",name);start(false);
}
void ModelDownloader::start(bool probe) {
    m_probe=probe;m_buffer.clear();m_error.clear();
    QNetworkRequest request(QUrl(m_endpoint+"/api/pull"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    const auto payload=QJsonDocument(QJsonObject{{"model",probe?QString():m_model},{"stream",!probe}}).toJson(QJsonDocument::Compact);
    auto *reply=m_network.post(request,payload);m_reply=reply;
    connect(reply,&QNetworkReply::uploadProgress,this,[this,previous=qint64(0)](qint64 sent,qint64) mutable {if(sent>previous)m_connection->recordTraffic(sent-previous,0);previous=qMax(previous,sent);});
    connect(reply,&QIODevice::readyRead,this,&ModelDownloader::readAvailable);
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        if(m_reply!=reply)return;
        readAvailable();if(m_reply!=reply)return;
        const auto http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(m_probe){checkProbe(http,m_buffer);return;}
        if(http==401 || http==403){m_access="denied";finish("error",i18n("The server denied model-download access (HTTP %1). Ask the server administrator to allow this operation.",http));return;}
        if(reply->error()!=QNetworkReply::NoError || http!=200){
            const auto error=QJsonDocument::fromJson(m_error).object().value("error").toString();
            finish("error",i18n("Download failed (HTTP %1): %2",http,error.isEmpty()?reply->errorString():error.left(500)));return;
        }
        if(!m_buffer.trimmed().isEmpty()){const auto line=m_buffer;m_buffer.clear();parseRecord(line);}
        if(m_reply==reply)finish("error",i18n("The connection ended without download confirmation. Retry to resume server-side partial data."));
    });
    m_timeout.start(probe?10000:300000);emit changed();
}
void ModelDownloader::readAvailable() {
    if(!m_reply)return;
    const auto bytes=m_reply->readAll();m_connection->recordTraffic(0,bytes.size());
    if(!bytes.isEmpty())m_timeout.start(m_probe?10000:300000);
    if(m_probe){m_buffer+=bytes;if(m_buffer.size()>65536){m_access="unknown";finish("error",i18n("Unexpected response to the access check."));}return;}
    const auto http=m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(http!=200){m_error+=bytes;if(m_error.size()>65536)finish("error",i18n("The server returned an oversized error response."));return;}
    // Bound individual records, not total streamed progress over a long download.
    for(qsizetype offset=0;offset<bytes.size() && m_reply;) {
        const auto end=bytes.indexOf('\n',offset);
        const auto count=end<0?bytes.size()-offset:end-offset;
        if(m_buffer.size()+count>65536){finish("error",i18n("The server returned an oversized progress record."));return;}
        m_buffer+=bytes.mid(offset,count);offset+=count;
        if(end<0)break;
        ++offset;const auto record=m_buffer;m_buffer.clear();if(!record.trimmed().isEmpty())parseRecord(record);
    }
}
void ModelDownloader::checkProbe(int http,const QByteArray &body) {
    const auto error=QJsonDocument::fromJson(body).object().value("error").toString();
    if(http==401 || http==403){m_access="denied";finish("error",i18n("The server denies download access (HTTP %1). Ask its administrator to enable it.",http));return;}
    if(http==404 || http==405){m_access="unsupported";finish("error",i18n("This server does not expose the model-download endpoint."));return;}
    static const QRegularExpression rejectedName(QStringLiteral("(?:invalid.*(?:model|name)|(?:model|name).*(?:required|invalid))"),QRegularExpression::CaseInsensitiveOption);
    if(http==400 && rejectedName.match(error).hasMatch()){
        m_access="available";finish("ready",i18n("Download API accessible. The server will check model access and storage when you download."));return;
    }
    m_access="unknown";finish("error",i18n("Could not verify download access. Check the connection and server configuration, then retry."));
}
void ModelDownloader::parseRecord(const QByteArray &line) {
    QJsonParseError error;const auto document=QJsonDocument::fromJson(line,&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject()){finish("error",i18n("The server returned invalid download progress."));return;}
    const auto object=document.object();
    if(object.contains("error")){finish("error",object.value("error").toString().left(500));return;}
    if(!object.value("status").isString()){finish("error",i18n("The server returned invalid download progress."));return;}
    const auto status=object.value("status").toString();
    if(status=="success"){
        finish("complete",i18n("%1 is ready. The model list has been refreshed.",m_model));m_connection->refresh();return;
    }
    const auto total=object.value("total").toInteger(-1),completed=object.value("completed").toInteger(0);
    if((object.contains("total") && total<=0) || completed<0 || (total>0 && completed>total)){finish("error",i18n("The server returned invalid download byte counts."));return;}
    m_total=total>0?total:0;m_completed=completed;m_message=status.left(500);emit changed();
}
void ModelDownloader::cancel() {
    if(!busy())return;
    if(m_probe)m_access="unknown";
    finish("cancelled",i18n("Request cancelled. Server-side partial downloads may be reused when you retry."));
}
