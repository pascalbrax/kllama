#include "chatsession.h"
#include "markdownrenderer.h"
#include "httpfixture.h"
#include <QtTest>
#include <KLocalizedString>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTextDocument>
#include <QTextCursor>
#include <QSqlQuery>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUuid>

class M3Test : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    static void writeFile(const QString &path,const QByteArray &bytes) {
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),bytes.size());
    }
    static QJsonObject details(const QJsonArray &features) {
        return {{"capabilities",features},{"model_info",QJsonObject{{"general.architecture","fixture"},{"fixture.context_length",32768}}}};
    }
private slots:
    void initTestCase() {
        KLocalizedString::setApplicationDomain("kllama");
        QCoreApplication::setOrganizationName("KllamaM3Tests");QCoreApplication::setApplicationName("m3");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    }
    void init(){QSettings().clear();}
    void runtimePlacement() {
        Fixture server;QVERIFY(server.listen(QHostAddress::LocalHost));
        server.runningModels={QJsonObject{{"name","mixed"},{"size",10000},{"size_vram",8100}},
            QJsonObject{{"name","cpu-only"},{"size",10000},{"size_vram",0}},
            QJsonObject{{"name","gpu-only"},{"size",10000},{"size_vram",10000}}};
        OllamaConnection connection;
        QVERIFY(connection.configure(server.endpoint(),false).isEmpty());
        QTRY_COMPARE(connection.runningModels().size(),3);
        QCOMPARE(connection.runningModels().at(0).toMap().value("cpu").toDouble(),1.0-0.81);
        QCOMPARE(connection.runningModels().at(0).toMap().value("gpu").toDouble(),0.81);
        QCOMPARE(connection.runningModels().at(1).toMap().value("cpu").toDouble(),1.0);
        QCOMPARE(connection.runningModels().at(2).toMap().value("gpu").toDouble(),1.0);
        QVERIFY(connection.runtimeStatus().isEmpty());
        server.runningModels={};connection.refreshRuntime();
        QTRY_VERIFY(!connection.runtimeStatus().isEmpty());QVERIFY(connection.runningModels().isEmpty());
        for(const auto &bad : {QByteArray("not JSON"),QByteArray("{\"models\":{}}"),
            QByteArray("{\"models\":[{\"name\":\"missing-metric\",\"size\":100}]}"),
            QByteArray("{\"models\":[{\"name\":\"invalid\",\"size\":100,\"size_vram\":101}]}"),
            QByteArray("{\"models\":[{\"name\":\"invalid\",\"size\":0,\"size_vram\":0}]}")}) {
            QSignalSpy changed(&connection,&OllamaConnection::runtimeChanged);
            server.runtimeResponses.append(Response{{bad}});connection.refreshRuntime();
            QTRY_VERIFY(!changed.isEmpty());QVERIFY(connection.runningModels().isEmpty());
            QVERIFY(connection.runtimeStatus().contains("unavailable"));QCOMPARE(connection.state(),QString("ready"));
        }
        server.runtimeResponses.append(Response{{"unsupported"},404});
        QSignalSpy changed(&connection,&OllamaConnection::runtimeChanged);connection.refreshRuntime();
        QTRY_VERIFY(!changed.isEmpty());QVERIFY(connection.runtimeStatus().contains("unavailable"));
        // An old endpoint's delayed response must never populate a new connection.
        server.runtimeResponses.append(Response{{"{\"models\":[", "{\"name\":\"old\",\"size\":100,\"size_vram\":10}]}"},200,true,200});
        const auto count=server.runtimeCount;connection.refreshRuntime();QTRY_VERIFY(server.runtimeCount>count);
        Fixture next;QVERIFY(next.listen(QHostAddress::LocalHost));
        next.runningModels={QJsonObject{{"name","new"},{"size",100},{"size_vram",100}}};
        QVERIFY(connection.configure(next.endpoint(),true).isEmpty());
        QTRY_COMPARE(connection.runningModels().size(),1);
        QCOMPARE(connection.runningModels().first().toMap().value("name").toString(),QString("new"));
        QTest::qWait(250);
        QCOMPARE(connection.runningModels().first().toMap().value("name").toString(),QString("new"));
    }
    void automaticContext() {
        Fixture server;QVERIFY(server.listen(QHostAddress::LocalHost));
        OllamaConnection connection;
        QVERIFY(connection.configure(server.endpoint(),false).isEmpty());QTRY_COMPARE(connection.state(),QString("ready"));
        QTemporaryDir data;
        {
            ChatSession chat(&connection,data.path());
            auto *caps=qobject_cast<ModelCapabilities*>(chat.capabilities());QVERIFY(caps);
            QTRY_COMPARE(caps->state(),QString("ready"));
            QVERIFY(!chat.autoContext());chat.setContextTokens(8192);
            auto metadata=details({"completion"});
            metadata["model_info"]=QJsonObject{{"general.architecture","fixture"},{"fixture.context_length",524288}};
            server.modelDetails.insert(chat.model(),metadata);caps->refresh();
            QTRY_COMPARE(caps->maximumContext(),524288);
            chat.setAutoContext(true);QCOMPARE(chat.contextTokens(),524288);
            server.responses.append(Response{{Fixture::record("OK",true)}});
            chat.setDraft("Hello");chat.send();QTRY_COMPARE(chat.state(),QString("complete"));
            QCOMPARE(server.requests.last().value("options").toObject().value("num_ctx").toInt(),524288);
            chat.setAutoContext(false);QCOMPARE(chat.contextTokens(),8192);
            chat.setAutoContext(true);
            server.modelDetails.insert(chat.model(),QJsonObject{});caps->refresh();
            QTRY_COMPARE(caps->state(),QString("unknown"));QCOMPARE(chat.contextTokens(),8192);
            server.modelDetails.insert(chat.model(),details({"completion"}));caps->refresh();
            QTRY_COMPARE(caps->maximumContext(),32768);QCOMPARE(chat.contextTokens(),32768);
        }
        ChatSession restored(&connection,data.path());QVERIFY(restored.autoContext());
        QTRY_COMPARE(qobject_cast<ModelCapabilities*>(restored.capabilities())->state(),QString("ready"));
        QCOMPARE(restored.contextTokens(),32768);
        restored.setAutoContext(false);QCOMPARE(restored.contextTokens(),8192);
    }
    void capabilitiesCacheAndUnknown() {
        Fixture server;server.modelNames={"qwen-text:latest","another-model:latest"};
        server.modelDetails.insert("qwen-text:latest",details({"completion"}));
        server.modelDetails.insert("another-model:latest",details({"completion","vision"}));
        QVERIFY(server.listen(QHostAddress::LocalHost));OllamaConnection connection;
        QVERIFY(connection.configure(server.endpoint(),true).isEmpty());QTRY_COMPARE(connection.state(),QString("ready"));
        ModelCapabilities caps(&connection);
        caps.inspect("qwen-text:latest");QTRY_COMPARE(caps.state(),QString("ready"));
        QVERIFY(!caps.features().contains("vision"));QCOMPARE(caps.maximumContext(),32768);
        caps.inspect("another-model:latest");QTRY_COMPARE(caps.state(),QString("ready"));QVERIFY(caps.features().contains("vision"));
        const auto inspected=server.inspectionCount;
        caps.inspect("qwen-text:latest");QCOMPARE(caps.state(),QString("ready"));QCOMPARE(server.inspectionCount,inspected);
        server.modelDetails.insert("qwen-text:latest",QJsonObject{});caps.refresh();QTRY_COMPARE(caps.state(),QString("unknown"));QVERIFY(caps.features().isEmpty());
        server.inspections.append(Response{{"unavailable"},503});caps.refresh();QTRY_COMPARE(caps.state(),QString("error"));
        server.modelDetails.insert("qwen-text:latest",details({"completion"}));caps.refresh();QTRY_COMPARE(caps.state(),QString("ready"));
        const auto before=server.inspectionCount;server.digest="replacement-digest";connection.refresh();
        QTRY_COMPARE(connection.state(),QString("ready"));QTRY_COMPARE(caps.state(),QString("ready"));QVERIFY(server.inspectionCount>before);
        // Switching while a response is split discards the old model's metadata.
        const auto bytes=QJsonDocument(details({"completion","vision"})).toJson(QJsonDocument::Compact);
        server.inspections.append(Response{{bytes.left(12),bytes.mid(12)},200,true,100});
        caps.inspect("another-model:latest",true);QTRY_VERIFY(server.inspectionCount>before+1);
        caps.inspect("qwen-text:latest",true);QTRY_COMPARE(caps.state(),QString("ready"));QTest::qWait(130);
        QVERIFY(!caps.features().contains("vision"));
        Fixture remote;remote.modelDetails.insert("qwen-text:latest",details({"completion","vision"}));remote.modelNames={"qwen-text:latest"};QVERIFY(remote.listen(QHostAddress::LocalHost));
        QVERIFY(connection.configure(remote.endpoint(),false).isEmpty());QTRY_COMPARE(connection.state(),QString("ready"));QTRY_COMPARE(caps.state(),QString("ready"));QVERIFY(caps.features().contains("vision"));
    }
    void attachmentsContextAndPersistence() {
        Fixture server;QVERIFY(server.listen(QHostAddress::LocalHost));OllamaConnection connection;
        QVERIFY(connection.configure(server.endpoint(),false).isEmpty());QTRY_COMPARE(connection.state(),QString("ready"));
        QTemporaryDir files,data;
        const auto source=files.filePath("example.cpp");const QByteArray text="// local source\nconst int answer = 42;\n";writeFile(source,text);
        QString id;
        {
            ChatSession chat(&connection,data.path());id=chat.conversationId();
            auto *attachments=qobject_cast<AttachmentManager*>(chat.attachments());QVERIFY(attachments);
            attachments->addFiles({QUrl::fromLocalFile(source)});QTRY_VERIFY(!attachments->busy());
            QCOMPARE(attachments->entries().size(),1);const auto entry=attachments->entries().first().toMap();
            QCOMPARE(entry.value("state").toString(),QString("ready"));QCOMPARE(attachments->preview(entry.value("id").toString()),QString::fromUtf8(text));
            attachments->addFiles({QUrl::fromLocalFile(source)});QCOMPARE(attachments->entries().size(),1);
            QTRY_COMPARE(qobject_cast<ModelCapabilities*>(chat.capabilities())->state(),QString("ready"));
            QVERIFY(chat.contextIssue().isEmpty());
            server.responses.append(Response{{Fixture::record("# Result\n\n```cpp\nconst int answer = 42;\n```",true)}});
            chat.setDraft("Explain this file");chat.send();QTRY_COMPARE(chat.state(),QString("complete"));
            QCOMPARE(server.requests.size(),1);const auto wire=QJsonDocument(server.requests.first()).toJson();
            QVERIFY(server.requests.first().value("messages").toArray().at(1).toObject().value("content").toString().contains(QString::fromUtf8(text.trimmed())));QVERIFY(wire.contains("BEGIN FILE"));QVERIFY(wire.contains("example.cpp"));QVERIFY(!wire.contains(source.toUtf8()));
            QCOMPARE(server.requests.first().value("options").toObject().value("num_ctx").toInt(),4096);
            const auto target=data.filePath("export.md");QVERIFY(chat.exportMarkdown(QUrl::fromLocalFile(target)));
            QFile exported(target);QVERIFY(exported.open(QIODevice::ReadOnly));const auto markdown=exported.readAll();QVERIFY(markdown.contains("# Result"));QVERIFY(markdown.contains("example.cpp"));
            // A large attachment blocks sending without mutating the draft or history.
            const auto large=files.filePath("large.txt");writeFile(large,QByteArray(6000,'x'));
            attachments->addFiles({QUrl::fromLocalFile(large)});QTRY_VERIFY(!attachments->busy());
            chat.setDraft("Another question");QVERIFY(!chat.contextIssue().isEmpty());
            const int count=chat.messages()->rowCount();chat.send();QCOMPARE(chat.state(),QString("blocked"));QCOMPARE(chat.messages()->rowCount(),count);QCOMPARE(chat.draft(),QString("Another question"));
            chat.setContextTokens(32768);QVERIFY(chat.contextIssue().isEmpty());
            QVERIFY(QFile::remove(source));QVERIFY(QFile::remove(large));
        }
        {
            ChatSession reopened(&connection,data.path());QCOMPARE(reopened.conversationId(),id);
            auto *attachments=qobject_cast<AttachmentManager*>(reopened.attachments());QTRY_VERIFY(!attachments->busy());
            QCOMPARE(attachments->entries().size(),1);
            QCOMPARE(attachments->allEntries().size(),2);
            for(const auto &entry:attachments->allEntries())QCOMPARE(entry.toMap().value("state").toString(),QString("ready"));
            QVERIFY(attachments->referenceText().contains("const int answer"));
            const auto copies=data.path()+"/attachments/"+id;QVERIFY(QDir(copies).exists());
            reopened.deleteConversation();QVERIFY(!QDir(copies).exists());
        }
    }
    void invalidFilesAndCancellation() {
        QTemporaryDir root,files;AttachmentManager attachments(root.path());attachments.restore(QUuid::createUuid().toString(QUuid::WithoutBraces),{});
        const QList<QPair<QString,QByteArray>> invalid={{"bad.txt",QByteArray::fromHex("c3")},{"binary.bin",QByteArray("abc\0def",7)},{"huge.txt",QByteArray(512*1024+1,'x')},{"empty.txt",{}},{"document.pdf","%PDF-1.4\n1 0 obj\n"}};
        for(const auto &item:invalid) {
            const auto path=files.filePath(item.first);writeFile(path,item.second);attachments.addFiles({QUrl::fromLocalFile(path)});QTRY_VERIFY(!attachments.busy());
            QCOMPARE(attachments.entries().last().toMap().value("state").toString(),QString("error"));QVERIFY(!attachments.issue().isEmpty());
            attachments.remove(attachments.entries().last().toMap().value("id").toString());
        }
        const auto path=files.filePath("cancel.txt");writeFile(path,QByteArray(256*1024,'x'));attachments.addFiles({QUrl::fromLocalFile(path)});
        const auto id=attachments.entries().first().toMap().value("id").toString();attachments.cancel(id);QTRY_VERIFY(!attachments.busy());QCOMPARE(attachments.entries().first().toMap().value("state").toString(),QString("cancelled"));
        attachments.retry(id);QTRY_VERIFY(!attachments.busy());QCOMPARE(attachments.entries().first().toMap().value("state").toString(),QString("ready"));
        const auto duplicate=files.filePath("duplicate.txt");writeFile(duplicate,QByteArray(256*1024,'x'));attachments.addFiles({QUrl::fromLocalFile(duplicate)});QTRY_VERIFY(!attachments.busy());
        QCOMPARE(attachments.entries().last().toMap().value("state").toString(),QString("error"));
        attachments.remove(attachments.entries().last().toMap().value("id").toString());
        auto *mime=new QMimeData;mime->setUrls({QUrl::fromLocalFile(path)});QGuiApplication::clipboard()->setMimeData(mime);QVERIFY(attachments.pasteFiles());QCOMPARE(attachments.entries().size(),1);
        attachments.remove(id);QVERIFY(attachments.pasteFiles(false));QCOMPARE(attachments.entries().size(),0);
        attachments.remove(id);QCOMPARE(attachments.entries().size(),0);
    }
    void markdownFormattingAndIsolation() {
        MarkdownRenderer renderer;
        const auto html=renderer.render("# Heading\n\n**bold** and *emphasis* with `inline`\n\n> quote\n\n- list item\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n```cpp\nconst int value = 42; // comment\n```\n\n![remote](https://example.invalid/image.png)\n\n![local](file:///etc/passwd)\n\n<script>alert(1)</script>\n\n[x](javascript:alert(1))");
        QVERIFY(!html.contains("<img",Qt::CaseInsensitive));QVERIFY(!html.contains("href=\"javascript:",Qt::CaseInsensitive));QVERIFY(!html.contains("<script",Qt::CaseInsensitive));
        QTextDocument document;document.setHtml(html);const auto plain=document.toPlainText();
        QVERIFY(plain.contains("Heading"));QVERIFY(plain.contains("quote"));QVERIFY(plain.contains("list item"));QVERIFY(html.contains("<table"));QVERIFY(plain.contains("[Image omitted]"));
        QTextCursor code=document.find("const int");QVERIFY(!code.isNull());code.setPosition(code.selectionStart()+1);QVERIFY(code.charFormat().fontWeight()>=QFont::Bold);
        QTextDocument partial;partial.setHtml(renderer.render("```python\ndef f():\n  return 1"));QVERIFY(partial.toPlainText().contains("return 1"));
        QVERIFY(!renderer.openLink("file:///etc/passwd"));QVERIFY(!renderer.openLink("javascript:alert(1)"));
    }
    void liveAttachment() {
        if(!qEnvironmentVariableIsSet("KLLAMA_LIVE_TEST"))QSKIP("Opt-in real local Ollama attachment check");
        OllamaConnection connection;QVERIFY(connection.configure("http://localhost:11434",true).isEmpty());QTRY_COMPARE_WITH_TIMEOUT(connection.state(),QString("ready"),10000);
        const auto requested=QString::fromUtf8(qgetenv("KLLAMA_LIVE_MODEL"));
        if(!requested.isEmpty()){QVERIFY(connection.models().contains(requested));connection.setSelectedModel(requested);}
        QTemporaryDir files,data;const auto path=files.filePath("verification.txt");writeFile(path,"The Kllama verification number is 731.\n");
        ChatSession chat(&connection,data.path());auto *attachments=qobject_cast<AttachmentManager*>(chat.attachments());
        QTRY_COMPARE_WITH_TIMEOUT(qobject_cast<ModelCapabilities*>(chat.capabilities())->state(),QString("ready"),10000);
        attachments->addFiles({QUrl::fromLocalFile(path)});QTRY_VERIFY(!attachments->busy());
        chat.setDraft("What verification number is in the attached file? Reply with only the number.");chat.send();
        QTRY_VERIFY_WITH_TIMEOUT(!chat.busy(),180000);QVERIFY2(chat.state()=="complete",qPrintable(chat.status()));QVERIFY(chat.messageText(1).contains("731"));
        qInfo().noquote()<<"Local attachment check:"<<chat.model()<<chat.messageText(1)<<chat.contextSummary();
    }
    void schemaOneMigration() {
        QTemporaryDir directory;const auto name=QUuid::createUuid().toString();
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(directory.filePath("conversations.sqlite"));QVERIFY(db.open());
            QSqlQuery q(db);QVERIFY(q.exec("CREATE TABLE conversations(id TEXT PRIMARY KEY,title TEXT NOT NULL,model TEXT NOT NULL,draft TEXT NOT NULL,messages BLOB NOT NULL,updated INTEGER NOT NULL)"));
            QVERIFY(q.exec("INSERT INTO conversations VALUES('old','Old chat','fixture:latest','draft','[]',1)"));QVERIFY(q.exec("PRAGMA user_version=1"));
        }
        QSqlDatabase::removeDatabase(name);
        {ChatStore store(directory.path());QVERIFY2(store.ready(),qPrintable(store.error()));Conversation c;QVERIFY(store.load("old",c));QCOMPARE(c.draft,QString("draft"));QVERIFY(c.attachments.isEmpty());QVERIFY(QFile::exists(directory.filePath("conversations-v1-backup.sqlite")));}
    }
};
QTEST_MAIN(M3Test)
#include "m3test.moc"
