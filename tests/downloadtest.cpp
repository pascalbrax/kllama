#include "modeldownloader.h"
#include "ollamaconnection.h"
#include "httpfixture.h"
#include <QtTest>
#include <QSettings>
#include <QTemporaryDir>
class DownloadTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("KllamaDownloadTests");
        QCoreApplication::setApplicationName("download");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    }
    void init(){QSettings().clear();}
    void accessAndDownload() {
        Fixture server;QVERIFY(server.listen(QHostAddress::LocalHost));
        OllamaConnection connection;QVERIFY(connection.configure(server.endpoint(),false).isEmpty());
        ModelDownloader downloader(&connection);
        downloader.download("example:latest");QVERIFY(server.pullRequests.isEmpty());
        downloader.checkAccess();QTRY_COMPARE(downloader.access(),QString("available"));
        QCOMPARE(server.probeCount,1);QVERIFY(server.pullRequests.isEmpty());
        downloader.download("../bad");QCOMPARE(downloader.state(),QString("error"));
        QVERIFY(server.pullRequests.isEmpty());
        server.pulls.append(Response{{"{\"status\":\"pulling\",\"total\":100,\"completed\":", "25}\n", "{\"status\":\"verifying\"}\n", "{\"status\":\"success\"}"},200,true,150});
        server.modelNames.append("example:latest");
        downloader.download("example:latest");
        QTRY_COMPARE(downloader.progress(),0.25);
        QTRY_COMPARE(downloader.state(),QString("complete"));
        QVERIFY(!downloader.busy());
        QTRY_VERIFY(connection.models().contains("example:latest"));
        QCOMPARE(server.pullRequests.size(),1);
        QVERIFY(server.pullRequests.first().value("stream").toBool());
    }
    void deniedAndUnexpected() {
        for(int http : {401,403,404,405,200,500}) {
            Fixture server;QVERIFY(server.listen(QHostAddress::LocalHost));
            server.probes.append(Response{{"{\"error\":\"denied\"}"},http});
            OllamaConnection connection;connection.configure(server.endpoint(),false);
            ModelDownloader downloader(&connection);downloader.checkAccess();
            QTRY_VERIFY(!downloader.busy());
            QVERIFY(downloader.access()!="available");
            downloader.download("example");QVERIFY(server.pullRequests.isEmpty());
        }
    }
    void errorsAndCancel() {
        Fixture server;QVERIFY(server.listen(QHostAddress::LocalHost));
        OllamaConnection connection;connection.configure(server.endpoint(),false);
        ModelDownloader downloader(&connection);downloader.checkAccess();
        QTRY_COMPARE(downloader.access(),QString("available"));
        for(const QByteArray &body : {QByteArray("{\"error\":\"model not found\"}\n"),QByteArray("bad JSON\n"),QByteArray("{\"status\":\"pulling\",\"total\":10,\"completed\":11}\n"),QByteArray("{\"status\":\"pulling\"}\n"),QByteArray(65537,'x')}) {
            server.pulls.append(Response{{body}});
            downloader.download("example");QTRY_VERIFY(!downloader.busy());
            QCOMPARE(downloader.state(),QString("error"));
        }
        server.pulls.append(Response{{"{\"status\":\"pulling\"}\n"},200,false});
        downloader.download("example");QTRY_COMPARE(downloader.message(),QString("pulling"));
        downloader.cancel();QVERIFY(!downloader.busy());QCOMPARE(downloader.state(),QString("cancelled"));
        server.pulls.append(Response{{"{\"error\":\"forbidden\"}"},403});
        downloader.download("example");QTRY_COMPARE(downloader.access(),QString("denied"));
        Fixture other;QVERIFY(other.listen(QHostAddress::LocalHost));
        connection.configure(other.endpoint(),false);
        QCOMPARE(downloader.access(),QString("unknown"));
    }
};
QTEST_GUILESS_MAIN(DownloadTest)
#include "downloadtest.moc"
