#include "chatsession.h"
#include "httpfixture.h"
#include <QtTest>
#include <KLocalizedString>
#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>
#include <QPdfWriter>
#include <QPainter>
#include <QImage>
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <QDesktopServices>

namespace {
void write(const QString &path,const QByteArray &bytes) {QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),bytes.size());}
QByteArray read(const QString &path) {QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
quint32 crc32(const QByteArray &bytes) {
    quint32 crc=0xffffffff;
    for(const auto ch:bytes){crc^=quint8(ch);for(int i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));}
    return ~crc;
}
// Minimal stored ZIP writer keeps document fixtures independent of archive libraries.
QByteArray zip(const QList<QPair<QByteArray,QByteArray>> &files) {
    QByteArray bytes,central;QDataStream out(&bytes,QIODevice::WriteOnly),dir(&central,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);dir.setByteOrder(QDataStream::LittleEndian);
    for(const auto &[name,data]:files) {
        const auto offset=quint32(bytes.size()),size=quint32(data.size()),crc=crc32(data);
        out<<quint32(0x04034b50)<<quint16(20)<<quint16(0)<<quint16(0)<<quint16(0)<<quint16(0)<<crc<<size<<size<<quint16(name.size())<<quint16(0);
        out.writeRawData(name.constData(),name.size());out.writeRawData(data.constData(),data.size());
        dir<<quint32(0x02014b50)<<quint16(20)<<quint16(20)<<quint16(0)<<quint16(0)<<quint16(0)<<quint16(0)<<crc<<size<<size<<quint16(name.size())<<quint16(0)<<quint16(0)<<quint16(0)<<quint16(0)<<quint32(0)<<offset;
        dir.writeRawData(name.constData(),name.size());
    }
    const auto offset=quint32(bytes.size());out.writeRawData(central.constData(),central.size());
    out<<quint32(0x06054b50)<<quint16(0)<<quint16(0)<<quint16(files.size())<<quint16(files.size())<<quint32(central.size())<<offset<<quint16(0);
    return bytes;
}
QByteArray docx(const QByteArray &body) {
    return zip({{"[Content_Types].xml","<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>"},
        {"word/document.xml","<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"+body+"</w:body></w:document>"}});
}
void pdf(const QString &path,bool blank=false,int pages=2) {
    QPdfWriter writer(path);writer.setResolution(96);
    QPainter painter(&writer);painter.setFont(QFont("Sans Serif",14));
    for(int page=1;page<=pages;++page){if(page>1)QVERIFY(writer.newPage());if(blank)painter.fillRect(50,50,200,200,Qt::black);else painter.drawText(60,100,QString("Page %1: the reference value is %2.").arg(page).arg(730+page));}
    painter.end();
}
QVariantMap entry(AttachmentManager &manager,int index=0) {return manager.entries().at(index).toMap();}
QJsonObject capabilities(bool vision) {return {{"capabilities",vision?QJsonArray{"completion","vision"}:QJsonArray{"completion"}},{"model_info",QJsonObject{{"general.architecture","fixture"},{"fixture.context_length",32768}}}};}
}

class M4Test:public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    QUrl openedSource;
private slots:
    void captureSource(const QUrl &url) { openedSource=url; }
    void initTestCase() {
        KLocalizedString::setApplicationDomain("kllama");
        QCoreApplication::setOrganizationName("KllamaM4Tests");QCoreApplication::setApplicationName("m4");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    }
    void init(){QSettings().clear();}
    void messageScopedAttachments() {
        Fixture server;QVERIFY(server.listen(QHostAddress::LocalHost));OllamaConnection connection;
        QVERIFY(connection.configure(server.endpoint(),false).isEmpty());QTRY_COMPARE(connection.state(),QString("ready"));
        QTemporaryDir files,data;const auto first=files.filePath("first.txt"),second=files.filePath("second.txt"),pending=files.filePath("pending.txt");
        write(first,"FIRST_PAYLOAD_731");write(second,"SECOND_PAYLOAD_732");write(pending,QByteArray(6000,'x'));
        QString conversation;
        {
            ChatSession chat(&connection,data.path());conversation=chat.conversationId();auto *manager=qobject_cast<AttachmentManager*>(chat.attachments());
            manager->addFiles({QUrl::fromLocalFile(first)});QTRY_VERIFY(!manager->busy());const auto firstId=entry(*manager).value("id").toString();
            server.responses.append(Response{{Fixture::record("First answer",true)}});chat.setDraft("Read the first attachment");chat.send();
            QVERIFY(manager->entries().isEmpty());QTRY_COMPARE(chat.state(),QString("complete"));QCOMPARE(manager->allEntries().size(),1);
            QCOMPARE(chat.messages()->data(chat.messages()->index(0,0),MessageModel::Files).toList().size(),1);
            server.responses.append(Response{{Fixture::record("Follow-up",true)}});chat.setDraft("Tell me more");chat.send();QTRY_COMPARE(chat.state(),QString("complete"));
            const auto follow=server.requests.last().value("messages").toArray();QCOMPARE(follow.size(),4);
            QVERIFY(follow.at(1).toObject().value("content").toString().contains("FIRST_PAYLOAD_731"));QVERIFY(!follow.last().toObject().value("content").toString().contains("FIRST_PAYLOAD_731"));
            QCOMPARE(QJsonDocument(server.requests.last()).toJson().count("FIRST_PAYLOAD_731"),1);
            manager->addFiles({QUrl::fromLocalFile(second)});QTRY_VERIFY(!manager->busy());const auto secondId=entry(*manager).value("id").toString();
            server.responses.append(Response{{"{\"error\":\"temporary failure\"}"},500});chat.setDraft("Read the second attachment");chat.send();QVERIFY(manager->entries().isEmpty());QTRY_COMPARE(chat.state(),QString("error"));
            manager->addFiles({QUrl::fromLocalFile(pending)});QTRY_VERIFY(!manager->busy());QVERIFY(!chat.contextIssue().isEmpty());
            server.responses.append(Response{{Fixture::record("Retried",true)}});chat.retry();QTRY_COMPARE(chat.state(),QString("complete"));
            const auto retried=QJsonDocument(server.requests.last()).toJson();QCOMPARE(retried.count("FIRST_PAYLOAD_731"),1);QCOMPARE(retried.count("SECOND_PAYLOAD_732"),1);QVERIFY(!retried.contains("pending.txt"));QCOMPARE(manager->entries().size(),1);
            chat.setDraft("Pending draft");const auto count=chat.messages()->rowCount();chat.send();QCOMPARE(chat.state(),QString("blocked"));QCOMPARE(chat.draft(),QString("Pending draft"));QCOMPARE(chat.messages()->rowCount(),count);QCOMPARE(manager->entries().size(),1);
            server.responses.append(Response{{Fixture::record("Edited",true)}});chat.editAndResend(0,"Edited first question");QTRY_COMPARE(chat.state(),QString("complete"));
            const auto edited=QJsonDocument(server.requests.last()).toJson();QCOMPARE(edited.count("FIRST_PAYLOAD_731"),1);QVERIFY(!edited.contains("SECOND_PAYLOAD_732"));QVERIFY(!edited.contains("pending.txt"));
            QCOMPARE(manager->entries().size(),1);QVERIFY(!QDir(data.filePath("attachments/"+conversation+"/"+secondId)).exists());
            QVERIFY(QFile::remove(first));chat.removeMessageAttachment(0,firstId);QVERIFY(chat.messages()->data(chat.messages()->index(0,0),MessageModel::Files).toList().isEmpty());
            QVERIFY(!QDir(data.filePath("attachments/"+conversation+"/"+firstId)).exists());
        }
        ChatSession restored(&connection,data.path());auto *manager=qobject_cast<AttachmentManager*>(restored.attachments());QTRY_VERIFY(!manager->busy());
        QCOMPARE(manager->entries().size(),1);QCOMPARE(manager->allEntries().size(),1);QCOMPARE(restored.draft(),QString("Pending draft"));
        QVERIFY(restored.messages()->data(restored.messages()->index(0,0),MessageModel::Files).toList().isEmpty());
    }
    void legacyAttachmentMigration() {
        Fixture server;QVERIFY(server.listen(QHostAddress::LocalHost));OllamaConnection connection;
        QVERIFY(connection.configure(server.endpoint(),false).isEmpty());QTRY_COMPARE(connection.state(),QString("ready"));
        QTemporaryDir files,data;const auto source=files.filePath("legacy.txt");write(source,"LEGACY_PAYLOAD_731");
        const auto conversation=QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            AttachmentManager manager(data.filePath("attachments"));manager.restore(conversation,{});manager.addFiles({QUrl::fromLocalFile(source)});QTRY_VERIFY(!manager.busy());
            auto metadata=manager.metadata();auto file=metadata.first().toObject();file.remove("sent");metadata[0]=file;
            ChatStore store(data.path());Conversation legacy{conversation,"Legacy files","fixture:latest","",QJsonArray{QJsonObject{{"role","user"},{"content","Old question"},{"state","complete"}},QJsonObject{{"role","assistant"},{"content","Old answer"},{"state","complete"}}},metadata};QVERIFY(store.save(legacy));
        }
        QVERIFY(QFile::remove(source));
        {
            ChatSession chat(&connection,data.path());auto *manager=qobject_cast<AttachmentManager*>(chat.attachments());QTRY_VERIFY(!manager->busy());QVERIFY(manager->entries().isEmpty());QCOMPARE(manager->allEntries().size(),1);
            QCOMPARE(chat.messages()->data(chat.messages()->index(0,0),MessageModel::Files).toList().size(),1);
            server.responses.append(Response{{Fixture::record("Follow-up",true)}});chat.setDraft("Continue");chat.send();QTRY_COMPARE(chat.state(),QString("complete"));
            const auto wire=QJsonDocument(server.requests.last()).toJson();QCOMPARE(wire.count("LEGACY_PAYLOAD_731"),1);QVERIFY(!server.requests.last().value("messages").toArray().last().toObject().value("content").toString().contains("LEGACY_PAYLOAD_731"));
        }
        ChatSession reopened(&connection,data.path());auto *manager=qobject_cast<AttachmentManager*>(reopened.attachments());QTRY_VERIFY(!manager->busy());QVERIFY(manager->entries().isEmpty());QCOMPARE(reopened.messages()->data(reopened.messages()->index(0,0),MessageModel::Files).toList().size(),1);
    }
    void documentsAndSources() {
        QTemporaryDir files,data;AttachmentManager manager(data.path());const auto conversation=QUuid::createUuid().toString(QUuid::WithoutBraces);manager.restore(conversation,{});
        const auto pdfPath=files.filePath("notes.pdf");pdf(pdfPath);
        const auto docxPath=files.filePath("notes.docx");write(docxPath,docx("<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Release notes</w:t></w:r></w:p><w:p><w:r><w:t>Value: 731</w:t><w:tab/><w:t>ready</w:t></w:r></w:p><w:tbl><w:tr><w:tc><w:p><w:r><w:t>Table cell</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"));
        manager.addFiles({QUrl::fromLocalFile(pdfPath),QUrl::fromLocalFile(docxPath)});QTRY_VERIFY(!manager.busy());
        QCOMPARE(manager.entries().size(),2);
        QCOMPARE(entry(manager).value("state").toString(),QString("ready"));QCOMPARE(entry(manager).value("kind").toString(),QString("pdf"));
        QCOMPARE(manager.sources(entry(manager).value("id").toString()).size(),2);
        const auto pdfId=entry(manager).value("id").toString();
        QVERIFY(manager.preview(pdfId).contains("[Page 2]"));QVERIFY(manager.sourceText(pdfId,1).contains("732"));QVERIFY(manager.sourceText(pdfId,99).isEmpty());
        struct ClearHandler { ~ClearHandler(){QDesktopServices::unsetUrlHandler("file");} } clearHandler;
        QDesktopServices::setUrlHandler("file",this,"captureSource");manager.openSource(pdfId,1);
        QCOMPARE(openedSource.fragment(),QString("page=2"));QVERIFY(openedSource.toLocalFile().endsWith("/source.pdf"));QVERIFY(QFile::exists(openedSource.toLocalFile()));QVERIFY(QFileInfo(openedSource.toLocalFile()).canonicalFilePath().endsWith("/original"));
        openedSource=QUrl();manager.openSource(pdfId,99);QVERIFY(openedSource.isEmpty());manager.openSource("unknown",0);QVERIFY(openedSource.isEmpty());
        QCOMPARE(entry(manager,1).value("state").toString(),QString("ready"));
        const auto docId=entry(manager,1).value("id").toString();
        QCOMPARE(manager.sources(docId).size(),3);QVERIFY(manager.preview(docId).contains("Release notes · Paragraph 2"));QVERIFY(manager.sourceText(docId,1).contains("731\tready"));QVERIFY(manager.preview(docId).contains("Table cell"));
        QVERIFY(manager.referenceText().contains("notes.pdf"));QVERIFY(manager.referenceText().contains("[Page 1]"));
        const auto metadata=manager.metadata();QVERIFY(!QJsonDocument(metadata).toJson().contains("Table cell"));
        QVERIFY(QFile::remove(pdfPath));QVERIFY(QFile::remove(docxPath));
        AttachmentManager restored(data.path());restored.restore(conversation,metadata);QTRY_VERIFY(!restored.busy());
        QVERIFY(restored.issue().isEmpty());QVERIFY(restored.preview(pdfId).contains("732"));QVERIFY(restored.preview(docId).contains("Table cell"));
        restored.deleteCopies(conversation);QVERIFY(!QDir(data.filePath(conversation)).exists());
    }
    void documentFailures() {
        QTemporaryDir files,data;AttachmentManager manager(data.path());manager.restore(QUuid::createUuid().toString(QUuid::WithoutBraces),{});
        auto check=[&](const QString &path,const QString &expected){manager.addFiles({QUrl::fromLocalFile(path)});QTRY_VERIFY(!manager.busy());QCOMPARE(manager.entries().size(),1);const auto row=entry(manager);QCOMPARE(row.value("state").toString(),QString("error"));QVERIFY2(row.value("error").toString().contains(expected,Qt::CaseInsensitive),qPrintable(row.value("error").toString()));manager.remove(row.value("id").toString());};
        const auto blank=files.filePath("scanned.pdf");pdf(blank,true);check(blank,"OCR");
        const auto damaged=files.filePath("damaged.pdf");write(damaged,"%PDF-1.7\nbroken");check(damaged,"extract");
        const auto archive=files.filePath("archive.zip");write(archive,zip({{"file.txt","hello"}}));check(archive,"DOCX");
        const auto empty=files.filePath("empty.docx");write(empty,docx("<w:p/>"));check(empty,"no extractable");
        const auto malformed=files.filePath("bad.docx");write(malformed,docx("<w:p>"));check(malformed,"XML");
        const auto entity=files.filePath("entity.docx");write(entity,zip({{"[Content_Types].xml","ok"},{"word/document.xml","<!DOCTYPE document [<!ENTITY x 'secret'>]><document/>"}}));check(entity,"entity");
        const auto big=files.filePath("expanded.docx");write(big,docx("<w:p><w:r><w:t>"+QByteArray(9*1024*1024,'x')+"</w:t></w:r></w:p>"));check(big,"limit");
        const auto largeText=files.filePath("text-limit.docx");write(largeText,docx("<w:p><w:r><w:t>"+QByteArray(513*1024,'x')+"</w:t></w:r></w:p>"));check(largeText,"512 KiB");
        const auto tooMany=files.filePath("pages.pdf");pdf(tooMany,false,201);check(tooMany,"200 pages");
    }
    void encryptedPdf_data() {QTest::addColumn<QString>("password");QTest::newRow("empty-password")<<QString("");QTest::newRow("password-required")<<QString("secret");}
    void encryptedPdf() {
        QFETCH(QString,password);
        if(QStandardPaths::findExecutable("qpdf").isEmpty())QSKIP("qpdf optional fixture generator is unavailable");
        QTemporaryDir files,data;const auto plain=files.filePath("plain.pdf"),encrypted=files.filePath("encrypted.pdf");pdf(plain);
        QProcess process;process.start("qpdf",{"--encrypt",password,"owner-password","256","--",plain,encrypted});QVERIFY(process.waitForFinished(10000));QCOMPARE(process.exitCode(),0);
        AttachmentManager manager(data.path());manager.restore(QUuid::createUuid().toString(QUuid::WithoutBraces),{});manager.addFiles({QUrl::fromLocalFile(encrypted)});QTRY_VERIFY(!manager.busy());
        QCOMPARE(entry(manager).value("state").toString(),QString("error"));QVERIFY(entry(manager).value("error").toString().contains("Encrypted"));
    }
    void visionRequestsAndRestoration() {
        Fixture server;server.modelDetails.insert("fixture:latest",capabilities(true));QVERIFY(server.listen(QHostAddress::LocalHost));
        OllamaConnection connection;QVERIFY(connection.configure(server.endpoint(),false).isEmpty());QTRY_COMPARE(connection.state(),QString("ready"));
        QTemporaryDir files,data;const auto imagePath=files.filePath("diagram.png");QImage image(32,24,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(imagePath));const auto imageBytes=read(imagePath);
        QString conversation;
        {
            ChatSession chat(&connection,data.path());conversation=chat.conversationId();auto *manager=qobject_cast<AttachmentManager*>(chat.attachments());auto *caps=qobject_cast<ModelCapabilities*>(chat.capabilities());QVERIFY(manager);QVERIFY(caps);
            QTRY_VERIFY(manager->visionAllowed());manager->addFiles({QUrl::fromLocalFile(imagePath)});QTRY_VERIFY(!manager->busy());QCOMPARE(entry(*manager).value("state").toString(),QString("ready"));QVERIFY(!manager->imageUrl(entry(*manager).value("id").toString()).isEmpty());
            QVERIFY(!chat.contextIssue().isEmpty());chat.setContextTokens(16384);QVERIFY(chat.contextIssue().isEmpty());
            server.modelDetails.insert(chat.model(),capabilities(false));caps->refresh();QTRY_VERIFY(!manager->visionAllowed());chat.setDraft("Describe this diagram");chat.send();QCOMPARE(chat.state(),QString("blocked"));QCOMPARE(chat.draft(),QString("Describe this diagram"));QVERIFY(server.requests.isEmpty());
            server.modelDetails.insert(chat.model(),capabilities(true));caps->refresh();QTRY_VERIFY(manager->visionAllowed());
            server.responses.append(Response{{Fixture::record("A red rectangle.",true)}});chat.send();QTRY_COMPARE(chat.state(),QString("complete"));QVERIFY(manager->entries().isEmpty());
            const auto messages=server.requests.last().value("messages").toArray();QCOMPARE(messages.at(1).toObject().value("images").toArray().size(),1);
            QCOMPARE(QByteArray::fromBase64(messages.at(1).toObject().value("images").toArray().first().toString().toLatin1()),imageBytes);
            QVERIFY(!QJsonDocument(server.requests.last()).toJson().contains(imagePath.toUtf8()));
            QVERIFY(!QJsonDocument(manager->metadata()).toJson().contains(imageBytes.toBase64()));
            QVERIFY(QFile::remove(imagePath));
        }
        ChatSession restored(&connection,data.path());auto *manager=qobject_cast<AttachmentManager*>(restored.attachments());
        QTRY_VERIFY(!manager->busy());QTRY_VERIFY(manager->visionAllowed());QVERIFY(restored.contextIssue().isEmpty());QCOMPARE(manager->images().size(),1);QVERIFY(manager->entries().isEmpty());
        server.responses.append(Response{{Fixture::record("Still red.",true)}});restored.setDraft("And its colour?");restored.send();QTRY_COMPARE(restored.state(),QString("complete"));
        QCOMPARE(server.requests.last().value("messages").toArray().at(1).toObject().value("images").toArray().size(),1);
        restored.deleteConversation();QVERIFY(!QDir(data.filePath("attachments/"+conversation)).exists());
    }
    void extractionCancellationAndMissingTools() {
        QTemporaryDir files,data,tools;const auto pdfPath=files.filePath("notes.pdf");pdf(pdfPath);
        struct RestorePath { QByteArray value=qgetenv("PATH"); ~RestorePath(){qputenv("PATH",value);} } restore;
        qputenv("PATH",tools.path().toLocal8Bit());
        AttachmentManager manager(data.path());manager.restore(QUuid::createUuid().toString(QUuid::WithoutBraces),{});
        manager.addFiles({QUrl::fromLocalFile(pdfPath)});QTRY_VERIFY(!manager.busy());
        QVERIFY(entry(manager).value("error").toString().contains("Install poppler-utils"));manager.remove(entry(manager).value("id").toString());
        const auto info=tools.filePath("pdfinfo"),extract=tools.filePath("pdftotext"),marker=tools.filePath("started");
        write(info,"#!/bin/sh\nprintf 'Pages: 1\\nEncrypted: no\\n'\n");
        write(extract,("#!/bin/sh\necho started > '"+marker+"'\nexec /usr/bin/sleep 60\n").toUtf8());
        for(const auto &path:{info,extract})QVERIFY(QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        manager.addFiles({QUrl::fromLocalFile(pdfPath)});QTRY_VERIFY(QFile::exists(marker));QVERIFY(manager.busy());
        const auto id=entry(manager).value("id").toString();manager.cancel(id);QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),3000);
        QCOMPARE(entry(manager).value("state").toString(),QString("cancelled"));manager.remove(id);
    }
    void liveDocumentAndVision() {
        if(!qEnvironmentVariableIsSet("KLLAMA_LIVE_TEST"))QSKIP("Opt-in real Ollama document and image inference");
        OllamaConnection connection;QVERIFY(connection.configure("http://localhost:11434",true).isEmpty());QTRY_COMPARE_WITH_TIMEOUT(connection.state(),QString("ready"),10000);
        const auto requested=QString::fromUtf8(qgetenv("KLLAMA_LIVE_MODEL"));QVERIFY(!requested.isEmpty());QVERIFY(connection.models().contains(requested));connection.setSelectedModel(requested);
        QTemporaryDir files,data;const auto pdfPath=files.filePath("reference.pdf"),imagePath=files.filePath("colour.png");pdf(pdfPath,false,1);
        QImage image(128,128,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(imagePath));
        ChatSession chat(&connection,data.path());chat.setContextTokens(32768);auto *manager=qobject_cast<AttachmentManager*>(chat.attachments());
        QTRY_VERIFY_WITH_TIMEOUT(manager->visionAllowed(),10000);manager->addFiles({QUrl::fromLocalFile(pdfPath),QUrl::fromLocalFile(imagePath)});QTRY_VERIFY(!manager->busy());QVERIFY(manager->issue().isEmpty());
        chat.setDraft("What reference value is on page 1 of the attached PDF, and what is the dominant colour in the attached image? Reply in one short sentence with the number and colour.");chat.send();
        QTRY_VERIFY_WITH_TIMEOUT(!chat.busy(),180000);QVERIFY2(chat.state()=="complete",qPrintable(chat.status()));
        qInfo().noquote()<<"Live M4:"<<chat.model()<<chat.messageText(1);
        QVERIFY(chat.messageText(1).contains("731"));QVERIFY(chat.messageText(1).contains("blue",Qt::CaseInsensitive));
    }
    void imageValidationAndClipboard() {
        QTemporaryDir files,data;AttachmentManager manager(data.path());manager.restore(QUuid::createUuid().toString(QUuid::WithoutBraces),{});
        const auto path=files.filePath("photo.jpg");QImage image(24,16,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(path));
        QSignalSpy notice(&manager,&AttachmentManager::notice);manager.addFiles({QUrl::fromLocalFile(path)});QVERIFY(manager.entries().isEmpty());QVERIFY(!notice.isEmpty());
        manager.setVisionAllowed(true);manager.addFiles({QUrl::fromLocalFile(path)});QTRY_VERIFY(!manager.busy());QCOMPARE(entry(manager).value("state").toString(),QString("ready"));manager.remove(entry(manager).value("id").toString());
        const auto huge=files.filePath("wide.png");QImage wide(8193,1,QImage::Format_RGB32);wide.fill(Qt::green);QVERIFY(wide.save(huge));
        const auto gif=files.filePath("animated.gif");write(gif,QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"));
        const auto oversized=files.filePath("oversized.png"),broken=files.filePath("broken.png"),animated=files.filePath("animated.png");
        const auto pngPath=files.filePath("valid.png");QVERIFY(image.save(pngPath));const auto png=read(pngPath);
        write(oversized,png+QByteArray(10*1024*1024,'x'));write(broken,png.left(24));
        QByteArray animationChunk;QDataStream animation(&animationChunk,QIODevice::WriteOnly);animation.setByteOrder(QDataStream::BigEndian);
        const QByteArray control=QByteArray::fromHex("0000000200000000");
        animation<<quint32(8);animation.writeRawData("acTL",4);animation.writeRawData(control.constData(),control.size());animation<<crc32("acTL"+control);
        auto animatedPng=png;animatedPng.insert(33,animationChunk);write(animated,animatedPng);
        for(const auto &invalid:{huge,gif,oversized,broken,animated}){manager.addFiles({QUrl::fromLocalFile(invalid)});QTRY_VERIFY(!manager.busy());QCOMPARE(entry(manager).value("state").toString(),QString("error"));manager.remove(entry(manager).value("id").toString());}
        auto *mime=new QMimeData;mime->setImageData(image);QGuiApplication::clipboard()->setMimeData(mime);
        QVERIFY(manager.pasteFiles(false));QVERIFY(manager.entries().isEmpty());
        QVERIFY(manager.pasteFiles());QTRY_VERIFY(!manager.busy());QCOMPARE(entry(manager).value("state").toString(),QString("ready"));QCOMPARE(manager.images().size(),1);
        manager.setVisionAllowed(false);QVERIFY(!manager.issue().isEmpty());manager.remove(entry(manager).value("id").toString());
        QGuiApplication::clipboard()->clear();
    }
};
QTEST_MAIN(M4Test)
#include "m4test.moc"
