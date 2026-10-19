#include "ollamaconnection.h"
#include "processmemory.h"
#include <QtTest>
#include <KLocalizedString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

class ConnectionTest : public QObject {
    Q_OBJECT
private slots:
    void runnerMemory() {
        QTemporaryDir proc;
        QVERIFY(proc.isValid());
        QCOMPARE(ollamaResidentMemory(proc.path()), qint64(-1));
        auto add = [&](int pid, int parent, const QByteArray &executable, int rss) {
            const auto base = proc.path() + '/' + QString::number(pid);
            QVERIFY(QDir().mkpath(base));
            QFile cmd(base + "/cmdline");
            QVERIFY(cmd.open(QIODevice::WriteOnly));
            cmd.write(executable + '\0'); cmd.close();
            QFile status(base + "/status");
            QVERIFY(status.open(QIODevice::WriteOnly));
            status.write("PPid:\t" + QByteArray::number(parent) + "\nVmRSS:\t" + QByteArray::number(rss) + " kB\n");
        };
        add(100, 1, "/usr/local/bin/ollama", 80);
        add(20, 100, "/usr/local/lib/ollama/llama-server", 10000);
        add(10, 20, "/some/future/helper", 200);
        add(200, 1, "/unrelated/llama-server", 90000);
        add(300, 1, "/old/ollama_llama_server", 500);
        QCOMPARE(ollamaResidentMemory(proc.path()), qint64(10780) * 1024);
        QVERIFY(QFile::remove(proc.path() + "/20/status"));
        QCOMPARE(ollamaResidentMemory(proc.path()), qint64(580) * 1024);
    }
    void initTestCase() {
        KLocalizedString::setApplicationDomain("kllama");
        QCoreApplication::setOrganizationName("KllamaTests");
        QCoreApplication::setApplicationName("connection");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    }
    void validation() {
        OllamaConnection client;
        for (const auto &url : {"file:///etc/passwd", "http://", "http://user:pass@localhost:11434", "http://localhost/api", "http://localhost?x=1", "http://localhost:0"})
            QVERIFY2(!client.configure(url, true).isEmpty(), url);
        QVERIFY(!client.configure("http://192.168.1.50:11434", true).isEmpty());
        QCOMPARE(client.state(), QString("idle"));
    }
    void modelList_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("state");
        QTest::addColumn<QStringList>("models");
        QTest::newRow("models") << QByteArray(R"({"models":[{"name":"z:1"},{"name":"a:2"},{"name":"z:1"}]})") << QString("ready") << QStringList({"a:2", "z:1"});
        QTest::newRow("empty") << QByteArray(R"({"models":[]})") << QString("empty") << QStringList();
        QTest::newRow("invalid-json") << QByteArray("not json") << QString("error") << QStringList();
        QTest::newRow("wrong-shape") << QByteArray(R"({"models":{}})") << QString("error") << QStringList();
        QTest::newRow("bad-entry") << QByteArray(R"({"models":[{"name":"good"},{}]})") << QString("error") << QStringList();
    }
    void modelList() {
        QFETCH(QByteArray, body);
        QFETCH(QString, state);
        QFETCH(QStringList, models);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, body] {
                const auto request = socket->readAll();
                if (request.startsWith("GET /api/ps HTTP/1.1")) {
                    const QByteArray empty="{\"models\":[]}";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(empty.size()) + "\r\nConnection: close\r\n\r\n" + empty);
                    socket->disconnectFromHost();
                    return;
                }
                QVERIFY(request.startsWith("GET /api/tags HTTP/1.1"));
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        OllamaConnection client;
        QVERIFY(client.configure(QString("http://127.0.0.1:%1").arg(server.serverPort()), false).isEmpty());
        QTRY_COMPARE(client.state(), state);
        QCOMPARE(client.models(), models);
        if (!models.isEmpty()) {
            QCOMPARE(client.selectedModel(), models.first());
            client.setSelectedModel(models.last());
            OllamaConnection restored;
            QCOMPARE(restored.selectedModel(), models.last());
            QCOMPARE(restored.endpoint(), client.endpoint());
            QCOMPARE(restored.local(), false);
        }
        QVERIFY(client.statistics().contains("KiB"));
    }
    void cancellationAndReplacement() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        OllamaConnection client;
        const auto address = QString("http://127.0.0.1:%1").arg(server.serverPort());
        QVERIFY(client.configure(address, true).isEmpty());
        QCOMPARE(client.state(), QString("connecting"));
        client.refresh();
        client.cancel();
        QTest::qWait(80);
        QCOMPARE(client.state(), QString("cancelled"));
        QVERIFY(!client.busy());
        QVERIFY(client.models().isEmpty());
    }
    void connectionFailure() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const auto port = server.serverPort();
        server.close();
        OllamaConnection client;
        QVERIFY(client.configure(QString("http://127.0.0.1:%1").arg(port), true).isEmpty());
        QTRY_COMPARE(client.state(), QString("error"));
        QVERIFY(!client.message().isEmpty());
    }
private:
    QTemporaryDir directory;
};
QTEST_GUILESS_MAIN(ConnectionTest)
#include "connectiontest.moc"
