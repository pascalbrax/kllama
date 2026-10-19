#include "ollamaconnection.h"
#include "processmemory.h"
#include <KLocalizedString>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QtConcurrentRun>
#include <algorithm>

OllamaConnection::OllamaConnection(QObject *parent) : QObject(parent) {
    m_endpoint = m_settings.value("connection/endpoint", "http://localhost:11434").toString();
    m_local = m_settings.value("connection/local", true).toBool();
    m_animations = m_settings.value("appearance/animations", true).toBool();
    m_selectedModel = m_settings.value("chat/model").toString();
    m_message = i18n("Connect to Ollama to discover your models.");
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(10000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        invalidateReply();
        setStatus("error", i18n("Ollama did not respond within 10 seconds. Check the address and try again."));
    });
    m_rateClock.start();
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        const auto ms = m_rateClock.restart();
        m_rate = ms > 0 ? (m_sent + m_received - m_previousReceived) * 1000 / ms : 0;
        m_previousReceived = m_sent + m_received;
        emit statisticsChanged();
    });
    m_tick.start();
    connect(&m_memoryWatcher, &QFutureWatcher<qint64>::finished, this, [this] {
        m_memory = m_memoryWatcher.result();
        emit statisticsChanged();
    });
    m_memoryTimer.setInterval(5000);
    connect(&m_memoryTimer, &QTimer::timeout, this, &OllamaConnection::sampleMemory);
    m_memoryTimer.start();
    sampleMemory();
}

void OllamaConnection::setStatus(const QString &state, const QString &message) {
    m_state = state;
    m_message = message;
    emit statusChanged();
}

QString OllamaConnection::configure(const QString &input, bool local) {
    const QUrl url(input.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") || url.host().isEmpty()
        || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() || (url.path() != "" && url.path() != "/") || url.port() == 0) {
        return i18n("Use an http:// or https:// address with an optional port, without credentials, a path, or query parameters.");
    }
    const QString host = url.host().toLower();
    if (local && host != "localhost" && host != "127.0.0.1" && host != "::1")
        return i18n("Choose Remote server for a hostname or IP outside this computer's loopback address.");
    invalidateReply();
    m_endpoint = url.adjusted(QUrl::StripTrailingSlash).toString();
    m_local = local;
    m_sent = m_received = m_previousReceived = m_rate = 0;
    m_rateClock.restart();
    m_memory = -1;
    m_settings.setValue("connection/endpoint", m_endpoint);
    m_settings.setValue("connection/local", m_local);
    m_models.clear();
    m_modelIdentities.clear();
    emit modelsChanged();
    emit configurationChanged();
    emit statisticsChanged();
    sampleMemory();
    refresh();
    return {};
}

void OllamaConnection::setAnimations(bool enabled) {
    if (m_animations == enabled) return;
    m_animations = enabled;
    m_settings.setValue("appearance/animations", enabled);
    emit configurationChanged();
}

void OllamaConnection::setSelectedModel(const QString &model) {
    if (m_selectedModel == model || (!model.isEmpty() && !m_models.contains(model))) return;
    m_selectedModel = model;
    m_settings.setValue("chat/model", model);
    emit selectedModelChanged();
}

void OllamaConnection::invalidateReply() {
    ++m_generation;
    m_timeout.stop();
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void OllamaConnection::refresh() {
    invalidateReply();
    m_models.clear();
    m_modelIdentities.clear();
    emit modelsChanged();
    m_elapsed.start();
    setStatus("connecting", i18n("Connecting to Ollama and discovering models…"));
    QNetworkRequest request(QUrl(m_endpoint + "/api/tags"));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_network.get(request);
    m_reply = reply;
    const auto generation = m_generation;
    auto data = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [this, reply, data] {
        auto chunk = reply->readAll();
        m_received += chunk.size();
        data->append(chunk);
        if (data->size() > 4 * 1024 * 1024) {
            invalidateReply();
            setStatus("error", i18n("Model list exceeds the 4 MiB response limit."));
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, data, generation] {
        if (generation != m_generation) { reply->deleteLater(); return; }
        m_timeout.stop();
        const auto remaining = reply->readAll();
        m_received += remaining.size();
        data->append(remaining);
        m_reply = nullptr;
        const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || code != 200) {
            setStatus("error", i18n("Cannot reach Ollama: %1. Check the server address and that Ollama is running.", reply->errorString()));
            reply->deleteLater();
            return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(*data, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject() || !doc.object().value("models").isArray()) {
            setStatus("error", i18n("The server returned an invalid Ollama model list."));
            reply->deleteLater();
            return;
        }
        QStringList models;
        QHash<QString,QString> identities;
        for (const auto &value : doc.object().value("models").toArray()) {
            const auto name = value.toObject().value("name").toString();
            if (!value.isObject() || name.isEmpty()) {
                setStatus("error", i18n("The server returned an invalid model entry."));
                reply->deleteLater();
                return;
            }
            if (!models.contains(name)) models.append(name);
            const auto object=value.toObject();
            auto identity=object.value("digest").toString();
            if(identity.isEmpty() && object.contains("modified_at")) identity=object.value("modified_at").toString()+":"+QString::number(object.value("size").toInteger());
            identities.insert(name,identity);
        }
        models.sort(Qt::CaseInsensitive);
        m_models = models;
        m_modelIdentities = identities;
        emit modelsChanged();
        if (!m_models.contains(m_selectedModel)) setSelectedModel(m_models.value(0));
        setStatus(m_models.isEmpty() ? "empty" : "ready", m_models.isEmpty()
            ? i18n("Connected, but no models are installed. Install one with ollama pull <model>, then refresh.")
            : i18np("Connected · %1 model available", "Connected · %1 models available", m_models.size()));
        emit statisticsChanged();
        reply->deleteLater();
    });
    m_timeout.start();
}

void OllamaConnection::cancel() {
    if (!busy()) return;
    invalidateReply();
    setStatus("cancelled", i18n("Connection check cancelled. You can retry when ready."));
}

QString OllamaConnection::statistics() const {
    if (m_local) {
        if (m_memory < 0) return i18n("Ollama RAM unavailable · no readable local Ollama process");
        return i18n("Ollama RAM: %1 GiB", QString::number(double(m_memory) / (1024*1024*1024), 'f', 2));
    }
    return i18n("↑ %1 KiB · ↓ %2 KiB · %3 KiB/s", QString::number(double(m_sent)/1024, 'f', 1), QString::number(double(m_received)/1024, 'f', 1), QString::number(double(m_rate)/1024, 'f', 1));
}

void OllamaConnection::sampleMemory() {
    if (!m_local || m_memoryWatcher.isRunning()) return;
    m_memoryWatcher.setFuture(QtConcurrent::run([] { return ollamaResidentMemory(); }));
}

void OllamaConnection::recordTraffic(qint64 sent, qint64 received) { m_sent += sent; m_received += received; }
