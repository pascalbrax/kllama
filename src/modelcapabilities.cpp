#include "modelcapabilities.h"
#include <KLocalizedString>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>

ModelCapabilities::ModelCapabilities(OllamaConnection *connection,QObject *parent):QObject(parent),m_connection(connection) {
    m_timeout.setSingleShot(true); m_timeout.setInterval(10000);
    connect(&m_timeout,&QTimer::timeout,this,[this]{ cancel(); m_state="error"; m_detail=i18n("Model inspection timed out. Refresh to retry."); emit changed(); });
    connect(connection,&OllamaConnection::modelsChanged,this,[this]{ inspect(m_model); });
    connect(connection,&OllamaConnection::configurationChanged,this,[this]{ if (m_endpoint!=m_connection->endpoint()) { m_cache.clear(); inspect(m_model,true); } });
}
void ModelCapabilities::cancel() {
    m_timeout.stop();
    if (m_reply) { disconnect(m_reply,nullptr,this,nullptr); m_reply->abort(); m_reply->deleteLater(); m_reply=nullptr; }
}
QString ModelCapabilities::summary() const {
    if (m_state=="loading") return i18n("Inspecting model…");
    if (m_state=="error") return i18n("Model inspection failed · Retry available");
    if (m_state!="ready") return i18n("Model input capabilities unknown");
    if(!m_features.contains("completion"))return i18n("No text-generation capability reported");
    return m_features.contains("vision")?i18n("Text · Images (PNG/JPEG)"):i18n("Text · No vision capability reported");
}
void ModelCapabilities::inspect(const QString &model,bool force) {
    const auto endpoint=m_connection->endpoint();
    const auto identity=m_connection->modelIdentity(model);
    if (!force && model==m_model && endpoint==m_endpoint && identity==m_identity && (m_state=="loading"||m_state=="ready")) return;
    cancel(); m_model=model; m_endpoint=endpoint; m_identity=identity; m_features.clear(); m_context=0; m_detail.clear();
    if (model.isEmpty() || !m_connection->models().contains(model)) { m_state="unknown"; emit changed(); return; }
    const auto key=endpoint+'\n'+model+'\n'+identity;
    if (!force && !identity.isEmpty() && m_cache.contains(key)) { apply(m_cache.value(key)); return; }
    if (force) m_cache.remove(key);
    m_state="loading"; emit changed();
    QNetworkRequest request(QUrl(endpoint+"/api/show"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    const auto payload=QJsonDocument(QJsonObject{{"model",model},{"verbose",false}}).toJson(QJsonDocument::Compact);
    auto *reply=m_network.post(request,payload); m_reply=reply;
    connect(reply,&QNetworkReply::uploadProgress,this,[this,previous=std::make_shared<qint64>(0)](qint64 sent,qint64){ if(sent>*previous)m_connection->recordTraffic(sent-*previous,0); *previous=qMax(*previous,sent); });
    auto bytes=std::make_shared<QByteArray>();
    connect(reply,&QIODevice::readyRead,this,[this,reply,bytes]{
        const auto data=reply->readAll(); m_connection->recordTraffic(0,data.size()); bytes->append(data);
        if(bytes->size()>2*1024*1024) { cancel(); m_state="error"; m_detail=i18n("Model metadata exceeds the 2 MiB limit."); emit changed(); }
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,key,identity]{
        if(m_reply!=reply)return;
        m_timeout.stop(); m_reply=nullptr;
        const auto tail=reply->readAll(); m_connection->recordTraffic(0,tail.size()); bytes->append(tail);
        QJsonParseError error; const auto doc=QJsonDocument::fromJson(*bytes,&error);
        if(reply->error()!=QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200 || error.error!=QJsonParseError::NoError || !doc.isObject()) {
            m_state="error"; m_detail=i18n("Unable to inspect this model. Text chat remains available; native file inputs are not enabled."); emit changed();
        } else {
            const auto object=doc.object();
            if(!identity.isEmpty() && object.value("capabilities").isArray()) m_cache.insert(key,object);
            apply(object);
        }
        reply->deleteLater();
    });
    m_timeout.start();
}
void ModelCapabilities::apply(const QJsonObject &object) {
    m_features.clear(); m_context=0;
    const auto metadata=object.value("model_info").toObject();
    const auto architecture=metadata.value("general.architecture").toString();
    const auto maximum=metadata.value(architecture+".context_length").toInteger();
    if(maximum>0 && maximum<=1024*1024) m_context=int(maximum);
    bool known=object.value("capabilities").isArray();
    for(const auto &value:object.value("capabilities").toArray()) {
        if(!value.isString()||!QRegularExpression(QStringLiteral("^[a-z][a-z0-9_-]{0,63}$")).match(value.toString()).hasMatch()) { known=false; break; }
        m_features.append(value.toString());
    }
    if(!known) m_features.clear();
    m_state=known?"ready":"unknown";
    m_detail=known?i18n("Reported features: %1. Kllama supports text, extracted PDF/DOCX content, and PNG/JPEG images when vision is reported. Audio and video inputs are not implemented.",m_features.join(", ")):i18n("The server did not report valid capability metadata. Extracted text remains usable; native input support is unknown.");
    emit changed();
}
