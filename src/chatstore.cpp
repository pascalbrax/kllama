#include "chatstore.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QDateTime>

ChatStore::ChatStore(const QString &directory) {
    m_name = QUuid::createUuid().toString();
    if (!QDir().mkpath(directory)) { fail("Cannot create the conversation directory."); return; }
    const auto path = directory + "/conversations.sqlite";
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_name);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=2000");
    if (!m_db.open()) { fail(m_db.lastError().text()); return; }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QSqlQuery query(m_db);
    if (!query.exec("PRAGMA user_version") || !query.next()) { fail(query.lastError().text()); return; }
    const int version = query.value(0).toInt();
    if (version > 2) { fail("Conversation database belongs to a newer Kllama version."); return; }
    if (!query.exec("CREATE TABLE IF NOT EXISTS conversations (id TEXT PRIMARY KEY, title TEXT NOT NULL, model TEXT NOT NULL, draft TEXT NOT NULL, messages BLOB NOT NULL, attachments BLOB NOT NULL DEFAULT '[]', updated INTEGER NOT NULL)")) {
        fail(query.lastError().text()); return;
    }
    if(version==1) {
        const auto backup=directory+"/conversations-v1-backup.sqlite";
        if(!QFile::exists(backup)) {
            QSqlQuery copy(m_db);copy.prepare("VACUUM INTO ?");copy.addBindValue(backup);
            if(!copy.exec()) {fail(copy.lastError().text());return;}
            QFile::setPermissions(backup,QFileDevice::ReadOwner|QFileDevice::WriteOwner);
        }
        if(!m_db.transaction()||!query.exec("ALTER TABLE conversations ADD COLUMN attachments BLOB NOT NULL DEFAULT '[]'")||!query.exec("PRAGMA user_version=2")||!m_db.commit()) {m_db.rollback();fail(query.lastError().text());return;}
    } else if (!query.exec("PRAGMA user_version=2")) { fail(query.lastError().text()); return; }
    // A previous process may have exited without a final stream event.
    const auto entries = history();
    for (const auto &entry : entries) {
        Conversation conversation;
        if (!load(entry.toMap().value("id").toString(), conversation)) return;
        bool changed = false;
        for (qsizetype i = 0; i < conversation.messages.size(); ++i) {
            auto message = conversation.messages.at(i).toObject();
            if (message.value("state").toString() == "streaming") {
                message["state"] = "interrupted";
                conversation.messages[i] = message;
                changed = true;
            }
        }
        if (changed && !save(conversation)) return;
    }
}
ChatStore::~ChatStore() {
    if (m_db.isValid()) m_db.close();
    m_db = QSqlDatabase();
    if (!m_name.isEmpty()) QSqlDatabase::removeDatabase(m_name);
}
bool ChatStore::fail(const QString &detail) { m_error = detail; return false; }
bool ChatStore::save(const Conversation &c) {
    if (!ready()) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO conversations(id,title,model,draft,messages,attachments,updated) VALUES(?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET title=excluded.title, model=excluded.model, draft=excluded.draft, messages=excluded.messages, attachments=excluded.attachments, updated=excluded.updated");
    q.addBindValue(c.id); q.addBindValue(c.title.isNull() ? QStringLiteral("") : c.title); q.addBindValue(c.model.isNull() ? QStringLiteral("") : c.model); q.addBindValue(c.draft.isNull() ? QStringLiteral("") : c.draft);
    q.addBindValue(QJsonDocument(c.messages).toJson(QJsonDocument::Compact));
    q.addBindValue(QJsonDocument(c.attachments).toJson(QJsonDocument::Compact));
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    return q.exec() || fail(q.lastError().text());
}
bool ChatStore::load(const QString &id, Conversation &c) {
    if (!ready()) return false;
    QSqlQuery q(m_db);
    q.prepare("SELECT id,title,model,draft,messages,attachments FROM conversations WHERE id=?"); q.addBindValue(id);
    if (!q.exec()) return fail(q.lastError().text());
    if (!q.next()) return false;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(q.value(4).toByteArray(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) return fail("Invalid conversation data; database has been preserved.");
    for (const auto &value : doc.array()) {
        const auto message = value.toObject();
        const auto role = message.value("role").toString();
        if (!value.isObject() || (role != "user" && role != "assistant") || !message.value("content").isString() || !message.value("state").isString())
            return fail("Invalid message data; database has been preserved.");
    }
    QJsonParseError attachmentError;
    const auto attachments=QJsonDocument::fromJson(q.value(5).toByteArray(),&attachmentError);
    if(attachmentError.error!=QJsonParseError::NoError||!attachments.isArray())return fail("Invalid attachment metadata; database has been preserved.");
    c = {q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(), doc.array(),attachments.array()};
    return true;
}
bool ChatStore::remove(const QString &id) {
    if (!ready()) return false;
    QSqlQuery q(m_db); q.prepare("DELETE FROM conversations WHERE id=?"); q.addBindValue(id);
    return q.exec() || fail(q.lastError().text());
}
QVariantList ChatStore::history() {
    QVariantList entries;
    if (!ready()) return entries;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id,title,model FROM conversations ORDER BY updated DESC,id")) { fail(q.lastError().text()); return entries; }
    while (q.next()) entries.append(QVariantMap{{"id",q.value(0)}, {"title",q.value(1)}, {"model",q.value(2)}});
    return entries;
}
