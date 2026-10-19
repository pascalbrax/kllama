#pragma once
#include <QObject>
#include <QJsonArray>
#include <QVariantList>
#include <QFutureWatcher>
#include <QHash>
#include <QUrl>
#include <QThreadPool>
#include <QImage>
#include "fileextractor.h"

class AttachmentManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QVariantList allEntries READ allEntries NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool visionAllowed READ visionAllowed NOTIFY changed)
    Q_PROPERTY(QString issue READ issue NOTIFY changed)
public:
    explicit AttachmentManager(const QString &root,QObject *parent=nullptr);
    ~AttachmentManager() override;
    QVariantList entries() const;
    QVariantList allEntries() const;
    QJsonArray pendingFiles() const;
    void setSent(const QJsonArray &files, bool sent=true);
    void discardUnusedSent(const QJsonArray &used);
    QString issueFor(const QJsonArray &files) const;
    QString referenceTextFor(const QJsonArray &files) const;
    QJsonArray imagesFor(const QJsonArray &files) const;
    qint64 referenceByteEstimateFor(const QJsonArray &files) const;
    bool busy() const { return !m_jobs.isEmpty(); }
    QString issue() const;
    bool visionAllowed() const { return m_visionAllowed; }
    void setVisionAllowed(bool allowed);
    QJsonArray images() const;
    bool hasImages() const;
    Q_INVOKABLE QUrl imageUrl(const QString &id) const;
    Q_INVOKABLE QVariantList sources(const QString &id) const;
    Q_INVOKABLE QString sourceText(const QString &id, int index) const;
    Q_INVOKABLE void openSource(const QString &id, int index);
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
    void start(const QString &id,const QString &source,const QImage &clipboardImage=QImage());
    QString directory(const QString &id) const;
    QList<QVariantMap> selected(const QJsonArray &files) const;
    QThreadPool m_pool;
    QString m_root,m_conversation;
    bool m_visionAllowed=false;
    QList<QVariantMap> m_entries;
    QHash<QString,QFutureWatcher<IngestResult>*> m_jobs;
};
