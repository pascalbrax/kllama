#pragma once
#include "ollamaconnection.h"
#include <QJsonObject>
#include <QHash>

class ModelCapabilities : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    Q_PROPERTY(QStringList features READ features NOTIFY changed)
    Q_PROPERTY(int maximumContext READ maximumContext NOTIFY changed)
public:
    explicit ModelCapabilities(OllamaConnection *connection, QObject *parent=nullptr);
    ~ModelCapabilities() override { cancel(); }
    QString state() const { return m_state; }
    QString summary() const;
    QString detail() const { return m_detail; }
    QStringList features() const { return m_features; }
    int maximumContext() const { return m_context; }
    void inspect(const QString &model, bool force=false);
    Q_INVOKABLE void refresh() { m_cache.clear(); inspect(m_model,true); }
signals:
    void changed();
private:
    void cancel();
    void apply(const QJsonObject &object);
    OllamaConnection *m_connection;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QHash<QString,QJsonObject> m_cache;
    QString m_model, m_endpoint, m_identity, m_state="unknown", m_detail;
    QStringList m_features;
    int m_context=0;
};
