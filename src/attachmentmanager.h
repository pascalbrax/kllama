#pragma once
#include <QObject>
#include <QJsonArray>
#include <QVariantList>
#include <QFutureWatcher>
#include <QHash>
#include <QUrl>
#include <QThreadPool>

struct IngestResult { QString text,hash,error; qint64 size=0; };
class AttachmentManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString issue READ issue NOTIFY changed)
public:
    explicit AttachmentManager(const QString &root,QObject *parent=nullptr);
    ~AttachmentManager() override;
    QVariantList entries() const;
    bool busy() const { return !m_jobs.isEmpty(); }
    QString issue() const;
    QJsonArray metadata() const;
    void restore(const QString &conversation,const QJsonArray &metadata);
    qint64 referenceByteEstimate() const;
    QString referenceText(QString *error=nullptr) const;
    Q_INVOKABLE void addFiles(const QList<QUrl> &urls);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void retry(const QString &id);
    Q_INVOKABLE QString preview(const QString &id) const;
    Q_INVOKABLE bool pasteFiles(bool allowed=true);
    void deleteCopies(const QString &conversation);
signals:
    void changed();
    void notice(const QString &message);
private:
    void start(const QString &id,const QString &source);
    QString directory(const QString &id) const;
    QThreadPool m_pool;
    QString m_root,m_conversation;
    QList<QVariantMap> m_entries;
    QHash<QString,QFutureWatcher<IngestResult>*> m_jobs;
};
