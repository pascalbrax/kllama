#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QVariantList>

class OllamaConnection : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY configurationChanged)
    Q_PROPERTY(bool local READ local NOTIFY configurationChanged)
    Q_PROPERTY(bool animations READ animations WRITE setAnimations NOTIFY configurationChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel WRITE setSelectedModel NOTIFY selectedModelChanged)
    Q_PROPERTY(QStringList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(QString message READ message NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(QString statistics READ statistics NOTIFY statisticsChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY statisticsChanged)
    Q_PROPERTY(QVariantList runningModels READ runningModels NOTIFY runtimeChanged)
    Q_PROPERTY(QString runtimeStatus READ runtimeStatus NOTIFY runtimeChanged)
public:
    explicit OllamaConnection(QObject *parent = nullptr);
    ~OllamaConnection() override { invalidateReply(); invalidateRuntime(); }
    QVariantList runningModels() const { return m_runningModels; }
    QString runtimeStatus() const { return m_runtimeStatus; }
    QString endpoint() const { return m_endpoint; }
    bool local() const { return m_local; }
    bool animations() const { return m_animations; }
    QString selectedModel() const { return m_selectedModel; }
    QStringList models() const { return m_models; }
    QString state() const { return m_state; }
    QString message() const { return m_message; }
    bool busy() const { return m_state == "connecting"; }
    QString statistics() const;
    int elapsedSeconds() const { return busy() ? int(m_elapsed.elapsed()/1000) : 0; }
    QString modelIdentity(const QString &model) const { return m_modelIdentities.value(model); }
    void recordTraffic(qint64 sent, qint64 received);
    void setAnimations(bool enabled);
    void setSelectedModel(const QString &model);
    Q_INVOKABLE QString configure(const QString &endpoint, bool local);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void refreshRuntime();
signals:
    void configurationChanged();
    void selectedModelChanged();
    void modelsChanged();
    void statusChanged();
    void statisticsChanged();
    void runtimeChanged();
private:
    void setStatus(const QString &state, const QString &message);
    void invalidateReply();
    void sampleMemory();
    void invalidateRuntime();
    void runtimeUnavailable();
    QSettings m_settings;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_runtimeReply;
    QTimer m_runtimeTimer;
    QVariantList m_runningModels;
    QString m_runtimeStatus;
    QTimer m_timeout, m_tick, m_memoryTimer;
    QFutureWatcher<qint64> m_memoryWatcher;
    QElapsedTimer m_elapsed, m_rateClock;
    QString m_endpoint, m_selectedModel, m_state = "idle", m_message;
    QStringList m_models;
    QHash<QString,QString> m_modelIdentities;
    bool m_local = true, m_animations = true;
    qint64 m_sent = 0, m_received = 0, m_previousReceived = 0, m_rate = 0, m_memory = -1;
    quint64 m_generation = 0;
};
