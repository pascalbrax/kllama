#pragma once
#include "chatstore.h"
#include "ollamaconnection.h"
#include "modelcapabilities.h"
#include "attachmentmanager.h"
#include <QAbstractListModel>
#include <QNetworkAccessManager>
#include <QElapsedTimer>

class MessageModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { Speaker = Qt::UserRole + 1, Body, State, Model };
    explicit MessageModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    void replace(const QJsonArray &rows);
    void append(const QJsonObject &row);
    void truncate(int count);
    void updateLast(const QString &content, const QString &state);
    QJsonArray rows() const { return m_rows; }
private:
    QJsonArray m_rows;
};

class ChatSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* capabilities READ capabilities CONSTANT)
    Q_PROPERTY(QObject* attachments READ attachments CONSTANT)
    Q_PROPERTY(int contextTokens READ contextTokens WRITE setContextTokens NOTIFY contextChanged)
    Q_PROPERTY(bool autoContext READ autoContext WRITE setAutoContext NOTIFY contextChanged)
    Q_PROPERTY(QString contextSummary READ contextSummary NOTIFY contextChanged)
    Q_PROPERTY(QString contextIssue READ contextIssue NOTIFY contextChanged)
    Q_PROPERTY(QAbstractItemModel* messages READ messages CONSTANT)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QString conversationId READ conversationId NOTIFY conversationChanged)
    Q_PROPERTY(QString title READ title NOTIFY conversationChanged)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString status READ status NOTIFY activityChanged)
    Q_PROPERTY(QString state READ state NOTIFY activityChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY activityChanged)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY activityChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY tick)
    Q_PROPERTY(QString storageError READ storageError NOTIFY storageErrorChanged)
    Q_PROPERTY(bool enterSends READ enterSends WRITE setEnterSends NOTIFY preferencesChanged)
public:
    explicit ChatSession(OllamaConnection *connection, const QString &directory, QObject *parent = nullptr);
    ~ChatSession() override;
    QObject *capabilities() { return &m_capabilities; }
    QObject *attachments() { return &m_attachments; }
    int contextTokens() const;
    bool autoContext() const { return m_autoContext; }
    void setAutoContext(bool enabled);
    QString contextSummary() const;
    QString contextIssue() const;
    void setContextTokens(int tokens);
    Q_INVOKABLE bool exportMarkdown(const QUrl &url);
    QAbstractItemModel *messages() { return &m_messages; }
    QVariantList history() const { return m_history; }
    QString conversationId() const { return m_current.id; }
    QString title() const { return m_current.title; }
    QString draft() const { return m_current.draft; }
    QString model() const { return m_current.model; }
    QString status() const { return m_status; }
    QString state() const { return m_state; }
    bool busy() const { return m_reply != nullptr; }
    bool canRetry() const;
    int elapsedSeconds() const { return m_elapsed.isValid() ? int(m_elapsed.elapsed()/1000) : 0; }
    QString storageError() const { return m_store.error(); }
    bool enterSends() const { return m_enterSends; }
    void setEnterSends(bool enabled);
    void setDraft(const QString &text);
    void setModel(const QString &model);
    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void openConversation(const QString &id);
    Q_INVOKABLE void renameConversation(const QString &title);
    Q_INVOKABLE void deleteConversation();
    Q_INVOKABLE void send();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void editAndResend(int row, const QString &text);
    Q_INVOKABLE void copy(const QString &text);
    Q_INVOKABLE QVariantList codeBlocks(int row) const;
    Q_INVOKABLE QString messageText(int row) const;
signals:
    void contextChanged();
    void historyChanged();
    void conversationChanged();
    void draftChanged();
    void modelChanged();
    void activityChanged();
    void tick();
    void storageErrorChanged();
    void preferencesChanged();
    void feedback(const QString &text);
private:
    bool save();
    void refreshHistory();
    void startRequest();
    void readAvailable();
    void parseRecord(const QByteArray &record);
    void finish(const QString &state, const QString &message);
    QJsonArray preparedHistory(const QJsonArray &rows) const;
    QString contextProblem(const QJsonArray &rows,const QString &draft) const;
    qint64 estimatedTokens(const QJsonArray &rows,const QString &draft) const;
    void restoreAttachments();
    void setActivity(const QString &state, const QString &message);
    OllamaConnection *m_connection;
    ChatStore m_store;
    ModelCapabilities m_capabilities;
    AttachmentManager m_attachments;
    int m_contextTokens=4096;
    bool m_autoContext=false;
    bool m_restoringAttachments=false;
    Conversation m_current;
    MessageModel m_messages;
    QVariantList m_history;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_buffer, m_errorBody;
    QString m_content, m_status, m_state = "idle";
    QTimer m_idleTimeout, m_checkpoint, m_draftTimer, m_tick;
    QElapsedTimer m_elapsed;
    bool m_enterSends = true;
    qsizetype m_responseBytes = 0;
};
