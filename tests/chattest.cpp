#include "chatsession.h"
#include "markdownrenderer.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QClipboard>
#include <QGuiApplication>
#include <KLocalizedString>
#include <KLocalizedQmlContext>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFile>
#include <QSqlQuery>
#include <QDir>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>

#include "httpfixture.h"

static QQuickItem *findVisual(QQuickItem *item, const QString &name) {
    if (item->objectName()==name) return item;
    for (auto *child:item->childItems()) if (auto *found=findVisual(child,name)) return found;
    return nullptr;
}

static QString plainText(QQuickItem *item) {
    QString value;
    QMetaObject::invokeMethod(item,"getText",Q_RETURN_ARG(QString,value),Q_ARG(int,0),Q_ARG(int,item->property("length").toInt()));
    return value;
}

class ChatTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
private slots:
    void initTestCase() {
        KLocalizedString::setApplicationDomain("kllama");
        QCoreApplication::setOrganizationName("KllamaChatTests"); QCoreApplication::setApplicationName("chat");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    }
    void init() { QSettings().clear(); }
    void streamHistoryEditAndPersistence() {
        Fixture server; QVERIFY(server.listen(QHostAddress::LocalHost));
        OllamaConnection connection; QVERIFY(connection.configure(server.endpoint(),false).isEmpty());
        QTRY_COMPARE(connection.state(),QString("ready"));
        QTemporaryDir directory;
        const auto first=Fixture::record(QString::fromUtf8("hé🙂"));
        const auto split=first.indexOf(QByteArray::fromHex("f09f9982"))+2;
        server.responses.append(Response{{first.left(split),first.mid(split)+Fixture::record("\n```cpp\n  int x = 1;\n```\n",true)}});
        QString id;
        {
            ChatSession chat(&connection,directory.path());
            QVERIFY2(chat.storageError().isEmpty(),qPrintable(chat.storageError()));
            chat.setDraft("  question\n\twith whitespace 🙂  "); chat.send();
            QVERIFY(chat.busy());
            QTRY_COMPARE(chat.state(),QString("complete"));
            QCOMPARE(chat.messageText(0),QString::fromUtf8("  question\n\twith whitespace 🙂  "));
            QCOMPARE(chat.messageText(1),QString::fromUtf8("hé🙂\n```cpp\n  int x = 1;\n```\n"));
            QCOMPARE(server.requests.size(),1);
            QCOMPARE(server.requests[0].value("messages").toArray().size(),1);
            const auto blocks=chat.codeBlocks(1);
            QCOMPARE(blocks.size(),1);
            QCOMPARE(blocks.first().toMap().value("text").toString(),QString("  int x = 1;\n"));
            chat.copy(blocks.first().toMap().value("text").toString());
            QCOMPARE(QGuiApplication::clipboard()->text(),QString("  int x = 1;\n"));
            server.responses.append(Response{{Fixture::record("second",true)}});
            chat.setDraft("follow up"); chat.send(); QTRY_COMPARE(chat.state(),QString("complete"));
            QCOMPARE(server.requests[1].value("messages").toArray().size(),3);
            server.responses.append(Response{{Fixture::record("replacement",true)}});
            chat.editAndResend(0,"edited question"); QTRY_COMPARE(chat.state(),QString("complete"));
            QCOMPARE(chat.messages()->rowCount(),2);
            QCOMPARE(server.requests[2].value("messages").toArray().size(),1);
            QCOMPARE(chat.messageText(0),QString("edited question"));
            server.responses.append(Response{{Fixture::record("regenerated",true)}});
            chat.retry(); QTRY_COMPARE(chat.state(),QString("complete"));
            QCOMPARE(chat.messageText(1),QString("regenerated"));
            QCOMPARE(chat.messages()->rowCount(),2);
            chat.renameConversation("Saved title"); chat.setDraft("saved\ndraft");
            id=chat.conversationId();
        }
        {
            ChatSession reopened(&connection,directory.path());
            QCOMPARE(reopened.conversationId(),id); QCOMPARE(reopened.title(),QString("Saved title"));
            QCOMPARE(reopened.draft(),QString("saved\ndraft")); QCOMPARE(reopened.messageText(1),QString("regenerated"));
            QCOMPARE(reopened.model(),QString("fixture:latest"));
            reopened.newConversation(); QCOMPARE(reopened.history().size(),2);
            reopened.openConversation(id); reopened.deleteConversation();
            QCOMPARE(reopened.history().size(),1); QVERIFY(reopened.conversationId()!=id);
        }
    }
    void failures_data() {
        QTest::addColumn<QByteArray>("body"); QTest::addColumn<int>("http");
        QTest::newRow("malformed") << QByteArray("not JSON\n") << 200;
        QTest::newRow("truncated") << Fixture::record("partial") << 200;
        QTest::newRow("server-error") << QByteArray("{\"error\":\"model missing\"}") << 404;
        QTest::newRow("stream-error") << QByteArray("{\"error\":\"out of memory\"}\n") << 200;
        QTest::newRow("invalid-record") << QByteArray("{\"done\":false,\"message\":{\"content\":12}}\n") << 200;
    }
    void failures() {
        QFETCH(QByteArray,body); QFETCH(int,http);
        Fixture server; QVERIFY(server.listen(QHostAddress::LocalHost));
        OllamaConnection connection; QVERIFY(connection.configure(server.endpoint(),true).isEmpty());
        QTRY_COMPARE(connection.state(),QString("ready"));
        QTemporaryDir dir; ChatSession chat(&connection,dir.path());
        server.responses.append(Response{{body},http}); chat.setDraft("hello"); chat.send();
        QTRY_COMPARE(chat.state(),QString("error")); QVERIFY(!chat.busy()); QVERIFY(chat.canRetry());
        server.responses.append(Response{{Fixture::record("recovered",true)}}); chat.retry();
        QTRY_COMPARE(chat.state(),QString("complete")); QCOMPARE(chat.messages()->rowCount(),2);
    }
    void liveOllama() {
        if (!qEnvironmentVariableIsSet("KLLAMA_LIVE_TEST")) QSKIP("Opt-in local Ollama inference check");
        OllamaConnection connection;
        QVERIFY(connection.configure("http://localhost:11434",true).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(),QString("ready"),10000);
        QVERIFY(!connection.models().isEmpty());
        const auto preferred=QString::fromUtf8(qgetenv("KLLAMA_LIVE_MODEL"));
        if (!preferred.isEmpty()) { QVERIFY(connection.models().contains(preferred)); connection.setSelectedModel(preferred); }
        QTemporaryDir directory; ChatSession chat(&connection,directory.path());
        chat.setDraft("Reply with only the two words: Kllama works"); chat.send();
        QTRY_VERIFY_WITH_TIMEOUT(!chat.busy(),180000);
        QVERIFY2(chat.state()=="complete",qPrintable(chat.status()));
        QVERIFY(!chat.messageText(1).trimmed().isEmpty());
        qInfo().noquote()<<"Local model:"<<chat.model()<<"Reply:"<<chat.messageText(1)<<"Status:"<<chat.status();
    }
    void interfaceInteraction() {
        QTest::failOnWarning(QRegularExpression(".*(ToolBarLayout|ActionToolBar\\.qml).*"));
        Fixture server; QVERIFY(server.listen(QHostAddress::LocalHost));
        OllamaConnection connection; QVERIFY(connection.configure(server.endpoint(),true).isEmpty());
        QTRY_COMPARE(connection.state(),QString("ready"));
        QTemporaryDir directory; ChatSession chat(&connection,directory.path());
        MarkdownRenderer markdown;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("markdown",&markdown);
        KLocalization::setupLocalizedContext(&engine);
        engine.rootContext()->setContextProperty("ollama",&connection);
        engine.rootContext()->setContextProperty("chat",&chat);
        engine.rootContext()->setContextProperty("appInfo",QVariantMap{{"version","test"},{"qtVersion",qVersion()},{"frameworksVersion","test"},{"dataDirectory",directory.path()}});
        QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(KLLAMA_SOURCE_DIR)+"/src/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(QMetaObject::invokeMethod(window,"openAbout"));
        auto *about=window->findChild<QObject*>("aboutDialog"); QVERIFY(about);
        QTRY_VERIFY(about->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(about,"close"));
        QTRY_VERIFY(!about->property("visible").toBool());
        auto *editor=window->findChild<QQuickItem*>("draftEditor"); QVERIFY(editor);
        editor->forceActiveFocus(); editor->setProperty("text","  first line"); editor->setProperty("cursorPosition",12);
        QTest::keyClick(window,Qt::Key_Return,Qt::ShiftModifier);
        QTest::keyClick(window,Qt::Key_A);
        QVERIFY(chat.draft().contains('\n')); QVERIFY(chat.draft().startsWith("  first line"));
        server.responses.append(Response{{Fixture::record("first "),Fixture::record("second "),Fixture::record("third",true)},200,true,150});
        QTest::keyClick(window,Qt::Key_Return);
        QTRY_VERIFY(chat.busy());
        QTRY_VERIFY_WITH_TIMEOUT(findVisual(window->contentItem(),"messageText_1")!=nullptr,3000);
        auto *response=findVisual(window->contentItem(),"messageText_1");
        QTRY_VERIFY(plainText(response).startsWith("first"));
        response->setProperty("cursorPosition",0);
        response->forceActiveFocus();
        QTRY_VERIFY(response->hasActiveFocus());
        QVERIFY(QMetaObject::invokeMethod(response,"select",Q_ARG(int,0),Q_ARG(int,5)));
        QTRY_COMPARE(chat.state(),QString("complete"));
        QCOMPARE(response->property("selectedText").toString(),QString("first"));
        QCOMPARE(chat.messages()->rowCount(),2);
        // QTest synthesizes Qt events, not compositor input serials. Wayland
        // clipboard ownership requires a real seat event; verify these round trips
        // offscreen and leave cross-application Wayland transfer to manual acceptance.
        if (QGuiApplication::platformName() != "wayland") {
            QTest::keyClick(window,Qt::Key_C,Qt::ControlModifier);
            QTRY_COMPARE(QGuiApplication::clipboard()->text(),QString("first"));
            editor->forceActiveFocus(); editor->setProperty("text",QStringLiteral(""));
            QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
            QTRY_COMPARE(chat.draft(),QString("first"));
            chat.copy("selection clipboard check");
            if (QGuiApplication::clipboard()->supportsSelection())
                QTRY_COMPARE(QGuiApplication::clipboard()->text(QClipboard::Selection),QString("selection clipboard check"));
        } else {
            qInfo("Wayland clipboard round trips require real compositor input; offscreen coverage is used for synthetic events.");
        }
        auto *view=window->findChild<QObject*>("transcript"); QVERIFY(view);
        QCOMPARE(view->property("following").toBool(),false);
        // Reverse Enter behavior: plain Enter must insert a newline.
        chat.setEnterSends(false); editor->forceActiveFocus(); editor->setProperty("text","draft"); editor->setProperty("cursorPosition",5);
        QTest::keyClick(window,Qt::Key_Return); QVERIFY(chat.draft().contains('\n'));
        server.responses.append(Response{{Fixture::record("done",true)}});
        QTest::keyClick(window,Qt::Key_Return,Qt::ShiftModifier);
        QTRY_COMPARE(chat.state(),QString("complete")); QCOMPARE(chat.messages()->rowCount(),4);
        // Long content must never occupy the scrollbar's lane, even when narrow.
        const auto longText=QString("A long line with wrapping text ").repeated(8)+"\n";
        server.responses.append(Response{{Fixture::record(longText.repeated(50),true)}});
        view->setProperty("following",true);
        chat.setDraft("Check long response layout"); chat.send();
        QTRY_COMPARE(chat.state(),QString("complete"));
        QVERIFY(QMetaObject::invokeMethod(view,"positionViewAtEnd"));
        auto verifyGutters=[&] {
            auto *answer=findVisual(window->contentItem(),"messageText_5");
            auto *bar=findVisual(window->contentItem(),"transcriptScrollBar");
            QVERIFY(answer); QVERIFY(bar);
            QVERIFY(view->property("contentHeight").toReal()>view->property("height").toReal());
            const auto textRight=answer->mapToItem(window->contentItem(),QPointF(answer->width(),0)).x();
            const auto barLeft=bar->mapToItem(window->contentItem(),QPointF(0,0)).x();
            QVERIFY2(textRight < barLeft,"Transcript text overlaps the scrollbar lane");
            editor->setProperty("text",longText.repeated(15));
            QTest::qWait(30);
            auto *composerBar=findVisual(window->contentItem(),"composerScrollBar");
            QVERIFY(composerBar);
            const auto editorRight=editor->mapToItem(window->contentItem(),QPointF(editor->width()-editor->property("rightPadding").toReal(),0)).x();
            const auto composerBarLeft=composerBar->mapToItem(window->contentItem(),QPointF(0,0)).x();
            QVERIFY2(editorRight < composerBarLeft,"Composer text overlaps the scrollbar lane");
        };
        QTest::qWait(40); verifyGutters();
        window->resize(640,680); QTest::qWait(60);
        QVERIFY(QMetaObject::invokeMethod(view,"positionViewAtEnd"));
        QTest::qWait(40); verifyGutters();
        if (qEnvironmentVariableIsSet("KLLAMA_UI_SCREENSHOT"))
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("KLLAMA_UI_SCREENSHOT")));
        QCOMPARE(warnings.count(),0);
    }
    void markdownAndAttachmentInterface() {
        QTest::failOnWarning(QRegularExpression(".*(ToolBarLayout|ActionToolBar\\.qml).*"));
        Fixture server;server.modelDetails.insert("fixture:latest",QJsonObject{{"capabilities",QJsonArray{"completion","vision"}},{"model_info",QJsonObject{{"general.architecture","fixture"},{"fixture.context_length",32768}}}});
        server.runningModels={QJsonObject{{"name","mistral:latest"},{"size",8700000000LL},{"size_vram",7047000000LL}}};
        QVERIFY(server.listen(QHostAddress::LocalHost));OllamaConnection connection;QVERIFY(connection.configure(server.endpoint(),true).isEmpty());QTRY_COMPARE(connection.state(),QString("ready"));
        QTemporaryDir data,files;ChatSession chat(&connection,data.path());MarkdownRenderer markdown;
        QQmlApplicationEngine engine;KLocalization::setupLocalizedContext(&engine);
        engine.rootContext()->setContextProperty("ollama",&connection);engine.rootContext()->setContextProperty("chat",&chat);engine.rootContext()->setContextProperty("markdown",&markdown);
        engine.rootContext()->setContextProperty("appInfo",QVariantMap{{"version","M3 test"},{"qtVersion",qVersion()},{"frameworksVersion","test"},{"dataDirectory",data.path()}});
        QSignalSpy warnings(&engine,&QQmlEngine::warnings);engine.load(QUrl::fromLocalFile(QStringLiteral(KLLAMA_SOURCE_DIR)+"/src/qml/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(connection.runningModels().size(),1);
        const auto path=files.filePath("notes.md");{QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("# Project notes\nThe test value is 731.\n");}
        auto *editor=window->findChild<QQuickItem*>("draftEditor");QVERIFY(editor);
        auto *attachments=qobject_cast<AttachmentManager*>(chat.attachments());
        if(QGuiApplication::platformName()!="wayland") {
            auto *mime=new QMimeData;mime->setUrls({QUrl::fromLocalFile(path)});QGuiApplication::clipboard()->setMimeData(mime);
            editor->forceActiveFocus();QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
            QTRY_COMPARE(attachments->entries().size(),1);QVERIFY(chat.draft().isEmpty());
        } else attachments->addFiles({QUrl::fromLocalFile(path)});
        QTRY_VERIFY(!attachments->busy());QTRY_COMPARE(qobject_cast<ModelCapabilities*>(chat.capabilities())->state(),QString("ready"));
        const auto csv=files.filePath("metrics.csv");{QFile file(csv);QVERIFY(file.open(QIODevice::WriteOnly));file.write("metric,value\nchecks,2\n");}
        QMimeData dropMime;dropMime.setUrls({QUrl::fromLocalFile(csv)});
        QDragEnterEvent enter(QPoint(360,220),Qt::CopyAction,&dropMime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&enter);QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(360,220),Qt::CopyAction,&dropMime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&drop);QTRY_COMPARE(attachments->entries().size(),2);QTRY_VERIFY(!attachments->busy());
        const auto entry=attachments->entries().first().toMap();QCOMPARE(entry.value("state").toString(),QString("ready"));
        QVERIFY(QMetaObject::invokeMethod(window,"previewAttachment",Q_ARG(QVariant,entry.value("id")),Q_ARG(QVariant,entry.value("name"))));
        auto *preview=window->findChild<QObject*>("attachmentPreviewDialog");QVERIFY(preview);QTRY_VERIFY(preview->property("visible").toBool());
        auto *previewText=window->findChild<QObject*>("attachmentPreviewText");QVERIFY(previewText);QVERIFY(previewText->property("text").toString().contains("731"));
        QVERIFY(QMetaObject::invokeMethod(preview,"close"));QTRY_VERIFY(!preview->property("visible").toBool());
        const auto answer=QString("# Findings\n\nThe value is **731**.\n\n| Source | Value |\n|---|---|\n| notes.md | 731 |\n\n```cpp\nconst int value = 731; // ")+QString("long code ").repeated(24)+"\n```\n\n![blocked](https://example.invalid/never-fetch.png)";
        server.responses.append(Response{{Fixture::record(answer.left(90)),Fixture::record(answer.mid(90),true)},200,true,120});
        chat.setDraft("Summarize the attached notes");chat.send();QTRY_COMPARE(chat.state(),QString("complete"));
        auto *view=window->findChild<QObject*>("transcript");QVERIFY(view);QVERIFY(QMetaObject::invokeMethod(view,"positionViewAtEnd"));QTest::qWait(100);
        auto *response=findVisual(window->contentItem(),"messageText_1");QVERIFY(response);QVERIFY(plainText(response).contains("Findings"));QVERIFY(plainText(response).contains("731"));QVERIFY(!response->property("text").toString().contains("<img",Qt::CaseInsensitive));
        QVERIFY(response->property("text").toString().contains("<table"));
        if(qEnvironmentVariableIsSet("KLLAMA_UI_SCREENSHOT"))QVERIFY(window->grabWindow().save(qEnvironmentVariable("KLLAMA_UI_SCREENSHOT")));
        window->resize(640,760);QTest::qWait(100);
        QVERIFY2(response->property("contentWidth").toReal()<=response->width()+2,"Rendered code/table content extends beyond the message width");
        QVERIFY(QMetaObject::invokeMethod(window,"openSettings"));QTest::qWait(80);
        auto *settingsDialog=window->findChild<QObject*>("settingsDialog");QVERIFY(settingsDialog);
        QVERIFY(settingsDialog->property("height").toReal()<window->height());
        QCOMPARE(warnings.count(),0);
    }
    void stopAndInterruptedRecovery() {
        Fixture server; QVERIFY(server.listen(QHostAddress::LocalHost));
        OllamaConnection connection; QVERIFY(connection.configure(server.endpoint(),true).isEmpty());
        QTRY_COMPARE(connection.state(),QString("ready"));
        QTemporaryDir dir;
        {
            ChatSession chat(&connection,dir.path());
            server.responses.append(Response{{Fixture::record("partial")},200,false});
            chat.setDraft("hello"); chat.send(); QTRY_COMPARE(chat.messageText(1),QString("partial"));
            chat.stop(); QCOMPARE(chat.state(),QString("stopped")); QVERIFY(!chat.busy());
            QTest::qWait(50); QCOMPARE(chat.messageText(1),QString("partial"));
        }
        QTemporaryDir recovery;
        {
            ChatStore store(recovery.path());
            QVERIFY(store.save({"crashed","Recovered","fixture:latest","draft",QJsonArray{
                QJsonObject{{"role","user"},{"content","q"},{"state","complete"}},
                QJsonObject{{"role","assistant"},{"content","partial"},{"state","streaming"}}}}));
        }
        {
            ChatSession chat(&connection,recovery.path());
            QCOMPARE(chat.state(),QString("interrupted")); QCOMPARE(chat.messageText(1),QString("partial"));
            QCOMPARE(chat.draft(),QString("draft"));
        }
    }
};
QTEST_MAIN(ChatTest)
#include "chattest.moc"
