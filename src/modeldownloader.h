#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
class OllamaConnection;
class QNetworkReply;

class ModelDownloader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString access READ access NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString modelName READ modelName NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString progressText READ progressText NOTIFY changed)
public:
    explicit ModelDownloader(OllamaConnection *connection,QObject *parent=nullptr);
    ~ModelDownloader() override;
    QString access() const { return m_access; }
    QString state() const { return m_state; }
    QString message() const { return m_message; }
    QString modelName() const { return m_model; }
    bool busy() const { return m_reply!=nullptr; }
    double progress() const { return m_total>0?double(m_completed)/m_total:-1; }
    QString progressText() const;
    Q_INVOKABLE void checkAccess();
    Q_INVOKABLE void download(const QString &model);
    Q_INVOKABLE void cancel();
    static bool validName(const QString &model);
signals:
    void changed();
private:
    void start(bool probe);
    void readAvailable();
    void parseRecord(const QByteArray &line);
    void finish(const QString &state,const QString &message);
    void invalidate();
    void checkProbe(int http,const QByteArray &body);
    OllamaConnection *m_connection;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QString m_endpoint,m_access="unknown",m_state="idle",m_message,m_model;
    QByteArray m_buffer,m_error;
    bool m_probe=false;
    qint64 m_completed=0,m_total=0;
};
