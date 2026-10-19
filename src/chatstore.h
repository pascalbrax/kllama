#pragma once
#include <QSqlDatabase>
#include <QVariantList>
#include <QJsonArray>

struct Conversation {
    QString id, title, model, draft;
    QJsonArray messages;
    QJsonArray attachments;
};

class ChatStore {
public:
    explicit ChatStore(const QString &directory);
    ~ChatStore();
    bool ready() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }
    bool save(const Conversation &conversation);
    bool load(const QString &id, Conversation &conversation);
    bool remove(const QString &id);
    QVariantList history();
private:
    bool fail(const QString &detail);
    QSqlDatabase m_db;
    QString m_name, m_error;
};
