#include "exact_package_download.hpp"
#include "service_controller.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

#include <chrono>
#include <cstddef>
#include <memory>

namespace {

constexpr auto packageMediaType =
    "application/vnd.prometheus.execution-component+json;version=2.0.0";

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("Cannot open exact-package fixture");
    }
    return file.readAll();
}

struct Response final {
    QByteArray headers;
    QByteArray body;
    bool closeAfterWrite{true};
};

class RawHttpServer final : public QObject {
    Q_OBJECT

public:
    explicit RawHttpServer(Response response, QObject* parent = nullptr)
        : QObject(parent)
        , response_(std::move(response))
    {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            auto* socket = server_.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket,
                [this, socket] {
                    request_ += socket->readAll();
                    if (responded_ || !request_.contains("\r\n\r\n")) {
                        return;
                    }
                    responded_ = true;
                    socket->write(response_.headers);
                    socket->write(response_.body);
                    socket->flush();
                    if (response_.closeAfterWrite) {
                        socket->disconnectFromHost();
                    }
                });
        });
        if (!server_.listen(QHostAddress::LocalHost, 0)) {
            qFatal("Cannot listen for exact-package test response: %s",
                qPrintable(server_.errorString()));
        }
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/package")
                        .arg(server_.serverPort()));
    }

private:
    QTcpServer server_;
    Response response_;
    QByteArray request_;
    bool responded_{false};
};

class ServiceStateServer final : public QObject {
    Q_OBJECT

public:
    explicit ServiceStateServer(
        const bool completePackages = false, QObject* parent = nullptr)
        : QObject(parent)
        , complete_packages_(completePackages)
    {
        if (complete_packages_) {
            package_bytes_ = readFile(
                QStringLiteral(PROMETHEUS_REPOSITORY_ROOT)
                + "/fixtures/contracts/execution-component-v2.motor-a.jcs");
            package_hash_ = QByteArray::fromStdString(
                prometheus::integrity::sha256_bytes(
                    std::string_view(package_bytes_.constData(),
                        static_cast<std::size_t>(package_bytes_.size()))));
        } else {
            package_hash_ = QByteArray("sha256:") + QByteArray(64, 'a');
        }
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (server_.hasPendingConnections()) {
                auto* socket = server_.nextPendingConnection();
                auto request = std::make_shared<QByteArray>();
                auto responded = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::readyRead, socket,
                    [this, socket, request, responded] {
                        *request += socket->readAll();
                        if (*responded || !request->contains("\r\n\r\n")) {
                            return;
                        }
                        *responded = true;
                        if (request->startsWith("GET /v1/health ")) {
                            writeJson(socket, "{}");
                            return;
                        }
                        if (request->startsWith(
                                "POST /api/v2/fixture-ingestions ")) {
                            const auto body = QByteArray(
                                "{\"id\":\"ingestion-1\",\"revision\":{"
                                "\"id\":\"revision-1\",\"component\":{},"
                                "\"parameters\":[],\"draft_version\":1,"
                                "\"status\":\"published\","
                                "\"execution_readiness\":\"ready\","
                                "\"publication_integrity\":\"sealed_v2\","
                                "\"object_hash\":\"")
                                + package_hash_ + "\"}}";
                            writeJson(socket, body);
                            return;
                        }
                        if (request->startsWith(
                                "GET /api/v2/revisions/revision-1/execution-package ")) {
                            package_requested_ = true;
                            ++package_request_count_;
                            socket->write(
                                "HTTP/1.1 200 OK\r\nContent-Type: "
                                + QByteArray(packageMediaType)
                                + "\r\nETag: \"" + package_hash_
                                + "\"\r\nContent-Length: "
                                + QByteArray::number(
                                    complete_packages_ ? package_bytes_.size() : 2)
                                + "\r\n\r\n");
                            if (complete_packages_) {
                                socket->write(package_bytes_);
                                socket->disconnectFromHost();
                            }
                            socket->flush();
                            return;
                        }
                        socket->write(
                            "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                            "Connection: close\r\n\r\n");
                        socket->disconnectFromHost();
                    });
            }
        });
        if (!server_.listen(QHostAddress::LocalHost, 0)) {
            qFatal("Cannot listen for service-state test response: %s",
                qPrintable(server_.errorString()));
        }
    }

    QUrl baseUrl() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1")
                        .arg(server_.serverPort()));
    }

    bool packageRequested() const noexcept { return package_requested_; }
    int packageRequestCount() const noexcept { return package_request_count_; }

private:
    static void writeJson(QTcpSocket* socket, const QByteArray& body)
    {
        socket->write(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: "
            + QByteArray::number(body.size())
            + "\r\nConnection: close\r\n\r\n" + body);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer server_;
    bool complete_packages_{false};
    QByteArray package_bytes_;
    QByteArray package_hash_;
    bool package_requested_{false};
    int package_request_count_{0};
};

QByteArray validHeaders(
    const QByteArray& hash, const qsizetype length,
    const QByteArray& extraHeaders = {})
{
    return "HTTP/1.1 200 OK\r\nContent-Type: "
        + QByteArray(packageMediaType) + "\r\nETag: \"" + hash
        + "\"\r\nContent-Length: " + QByteArray::number(length) + "\r\n"
        + extraHeaders + "Connection: close\r\n\r\n";
}

struct Outcome final {
    QList<QVariant> success;
    QList<QVariant> failure;
};

Outcome acquireUrl(const QUrl& url);

Outcome acquire(const Response& response)
{
    RawHttpServer server(response);
    return acquireUrl(server.url());
}

Outcome acquireUrl(const QUrl& url)
{
    QNetworkAccessManager network;
    prometheus::ExactPackageDownload download(&network);
    QSignalSpy success(&download,
        &prometheus::ExactPackageDownload::exactPackageAcquired);
    QSignalSpy failure(
        &download, &prometheus::ExactPackageDownload::downloadFailed);
    download.acquire(QNetworkRequest(url));
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(10);
    while (success.isEmpty() && failure.isEmpty()
        && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(1);
    }
    if (success.isEmpty() && failure.isEmpty()) {
        qFatal("Exact-package acquisition did not complete before timeout");
    }
    QTest::qWait(20);
    if (success.count() + failure.count() != 1) {
        qFatal("Exact-package acquisition emitted an invalid signal count");
    }
    return Outcome{
        success.isEmpty() ? QList<QVariant>{} : success.takeFirst(),
        failure.isEmpty() ? QList<QVariant>{} : failure.takeFirst(),
    };
}

void requireFailure(const Outcome& outcome)
{
    QVERIFY(outcome.success.isEmpty());
    QCOMPARE(outcome.failure.size(), 2);
    QVERIFY(!outcome.failure.at(0).toString().isEmpty());
    QVERIFY(!outcome.failure.at(1).toString().isEmpty());
    QVERIFY(outcome.failure.at(0).toString().toUtf8().size() <= 4096);
    QVERIFY(outcome.failure.at(1).toString().toUtf8().size() <= 128);
}

} // namespace

class ExactPackageDownloadTests final : public QObject {
    Q_OBJECT

private slots:
    void validExactResponse();
    void invalidHeaders_data();
    void invalidHeaders();
    void absentLengthStreamsExactBytes();
    void chunkedStreamsExactBytes();
    void networkError();
    void transportAndLengthFailures();
    void overflowAndHashDisagreement();
    void failureCanRetry();
    void repeatedServiceAcquirePreservesBusyState();
    void successfulServiceAcquireCanRetry();
};

void ExactPackageDownloadTests::validExactResponse()
{
    const auto bytes = readFile(
        QStringLiteral(PROMETHEUS_REPOSITORY_ROOT)
        + "/fixtures/contracts/execution-component-v2.motor-a.jcs");
    const auto hash = QByteArray::fromStdString(
        prometheus::integrity::sha256_bytes(
            std::string_view(bytes.constData(),
                static_cast<std::size_t>(bytes.size()))));
    const auto outcome = acquire(Response{validHeaders(hash, bytes.size()), bytes});
    QCOMPARE(outcome.failure.size(), 0);
    QCOMPARE(outcome.success.size(), 2);
    QCOMPARE(outcome.success.at(0).toByteArray(), bytes);
    QCOMPARE(outcome.success.at(1).toString().toUtf8(), hash);
}

void ExactPackageDownloadTests::invalidHeaders_data()
{
    QTest::addColumn<QByteArray>("headers");
    const auto hash = QByteArray("sha256:") + QByteArray(64, 'a');
    const auto base = QByteArray("HTTP/1.1 200 OK\r\n");
    const auto type = QByteArray("Content-Type: ") + packageMediaType + "\r\n";
    const auto etag = QByteArray("ETag: \"") + hash + "\"\r\n";
    const auto end = QByteArray("Content-Length: 2\r\nConnection: close\r\n\r\n");

    QTest::newRow("missing-etag") << base + type + end;
    QTest::newRow("weak-etag")
        << base + type + "ETag: W/\"" + hash + "\"\r\n" + end;
    QTest::newRow("unquoted-etag")
        << base + type + "ETag: " + hash + "\r\n" + end;
    QTest::newRow("uppercase-etag")
        << base + type + "ETag: \"sha256:" + QByteArray(64, 'A')
            + "\"\r\n" + end;
    QTest::newRow("malformed-etag")
        << base + type + "ETag: \"sha256:abc\"\r\n" + end;
    QTest::newRow("duplicate-etag")
        << base + type + etag + etag + end;
    QTest::newRow("wrong-content-type")
        << base + "Content-Type: application/json\r\n" + etag + end;
    QTest::newRow("parameter-reordered")
        << base
            + "Content-Type: application/vnd.prometheus.execution-component+json;charset=utf-8;version=2.0.0\r\n"
            + etag + end;
    QTest::newRow("duplicate-content-type")
        << base + type + type + etag + end;
    QTest::newRow("content-encoding")
        << base + type + etag + "Content-Encoding: identity\r\n" + end;
    QTest::newRow("redirect-status")
        << "HTTP/1.1 302 Found\r\nLocation: /elsewhere\r\n" + type + etag
            + end;
    QTest::newRow("redirect-target")
        << base + type + etag + "Location: /elsewhere\r\n" + end;
    QTest::newRow("partial-content")
        << "HTTP/1.1 206 Partial Content\r\n" + type + etag
            + "Content-Range: bytes 0-1/2\r\n" + end;
    QTest::newRow("duplicate-length")
        << base + type + etag + "Content-Length: 2\r\n" + end;
    QTest::newRow("noncanonical-length")
        << base + type + etag
            + "Content-Length: 02\r\nConnection: close\r\n\r\n";
    QTest::newRow("malformed-length")
        << base + type + etag
            + "Content-Length: 2x\r\nConnection: close\r\n\r\n";
    QTest::newRow("length-and-transfer-encoding")
        << base + type + etag
            + "Content-Length: 2\r\nTransfer-Encoding: chunked\r\n"
              "Connection: close\r\n\r\n";
    QTest::newRow("unsupported-transfer-encoding")
        << base + type + etag
            + "Transfer-Encoding: gzip\r\nConnection: close\r\n\r\n";
    QByteArray tooManyHeaders = base + type + etag;
    for (int index = 0; index < 129; ++index) {
        tooManyHeaders += "X-Prometheus-Test-" + QByteArray::number(index)
            + ": value\r\n";
    }
    tooManyHeaders += "Connection: close\r\n\r\n";
    QTest::newRow("too-many-headers") << tooManyHeaders;
}

void ExactPackageDownloadTests::invalidHeaders()
{
    QFETCH(QByteArray, headers);
    requireFailure(acquire(Response{headers, "{}"}));
}

void ExactPackageDownloadTests::absentLengthStreamsExactBytes()
{
    const auto bytes = readFile(
        QStringLiteral(PROMETHEUS_REPOSITORY_ROOT)
        + "/fixtures/contracts/execution-component-v2.motor-b.jcs");
    const auto hash = QByteArray::fromStdString(
        prometheus::integrity::sha256_bytes(
            std::string_view(bytes.constData(),
                static_cast<std::size_t>(bytes.size()))));
    const auto headers = QByteArray("HTTP/1.1 200 OK\r\nContent-Type: ")
        + packageMediaType + "\r\nETag: \"" + hash
        + "\"\r\nConnection: close\r\n\r\n";
    const auto outcome = acquire(Response{headers, bytes});
    QCOMPARE(outcome.failure.size(), 0);
    QCOMPARE(outcome.success.at(0).toByteArray(), bytes);
    QCOMPARE(outcome.success.at(1).toString().toUtf8(), hash);
}

void ExactPackageDownloadTests::chunkedStreamsExactBytes()
{
    const auto bytes = readFile(
        QStringLiteral(PROMETHEUS_REPOSITORY_ROOT)
        + "/fixtures/contracts/execution-component-v2.motor-a.jcs");
    const auto hash = QByteArray::fromStdString(
        prometheus::integrity::sha256_bytes(
            std::string_view(bytes.constData(),
                static_cast<std::size_t>(bytes.size()))));
    const auto headers = QByteArray("HTTP/1.1 200 OK\r\nContent-Type: ")
        + packageMediaType + "\r\nETag: \"" + hash
        + "\"\r\nTransfer-Encoding: chunked\r\n"
          "Connection: close\r\n\r\n";
    const auto chunked = QByteArray::number(bytes.size(), 16) + "\r\n" + bytes
        + "\r\n0\r\n\r\n";
    const auto outcome = acquire(Response{headers, chunked});
    QCOMPARE(outcome.failure.size(), 0);
    QCOMPARE(outcome.success.at(0).toByteArray(), bytes);
    QCOMPARE(outcome.success.at(1).toString().toUtf8(), hash);
}

void ExactPackageDownloadTests::networkError()
{
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const auto port = reservation.serverPort();
    reservation.close();
    requireFailure(acquireUrl(QUrl(
        QStringLiteral("http://127.0.0.1:%1/package").arg(port))));
}

void ExactPackageDownloadTests::transportAndLengthFailures()
{
    const auto hash = QByteArray("sha256:") + QByteArray(64, 'a');
    requireFailure(acquire(Response{
        validHeaders(hash, 200), "{}", true}));

    const auto unavailable = acquire(Response{
        "HTTP/1.1 200 OK\r\nContent-Type: "
            + QByteArray(packageMediaType) + "\r\nETag: \"" + hash
            + "\"\r\nContent-Length: 2\r\n\r\n",
        {}, true});
    requireFailure(unavailable);
}

void ExactPackageDownloadTests::overflowAndHashDisagreement()
{
    const auto hash = QByteArray("sha256:") + QByteArray(64, 'a');
    requireFailure(acquire(Response{
        validHeaders(hash, prometheus::ExactPackageDownload::maximumPackageBytes + 1),
        QByteArray()}));

    const auto unboundedHeaders =
        QByteArray("HTTP/1.1 200 OK\r\nContent-Type: ") + packageMediaType
        + "\r\nETag: \"" + hash
        + "\"\r\nConnection: close\r\n\r\n";
    requireFailure(acquire(Response{unboundedHeaders,
        QByteArray(prometheus::ExactPackageDownload::maximumPackageBytes + 1,
            'x')}));

    const auto bytes = readFile(
        QStringLiteral(PROMETHEUS_REPOSITORY_ROOT)
        + "/fixtures/contracts/execution-component-v2.motor-a.jcs");
    requireFailure(acquire(Response{validHeaders(hash, bytes.size()), bytes}));
}

void ExactPackageDownloadTests::failureCanRetry()
{
    const auto bytes = readFile(
        QStringLiteral(PROMETHEUS_REPOSITORY_ROOT)
        + "/fixtures/contracts/execution-component-v2.motor-a.jcs");
    const auto hash = QByteArray::fromStdString(
        prometheus::integrity::sha256_bytes(
            std::string_view(bytes.constData(),
                static_cast<std::size_t>(bytes.size()))));
    RawHttpServer invalid(Response{
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "ETag: \""
            + hash + "\"\r\nContent-Length: "
            + QByteArray::number(bytes.size())
            + "\r\nConnection: close\r\n\r\n",
        bytes});
    RawHttpServer valid(Response{validHeaders(hash, bytes.size()), bytes});
    QNetworkAccessManager network;
    prometheus::ExactPackageDownload download(&network);
    QSignalSpy success(&download,
        &prometheus::ExactPackageDownload::exactPackageAcquired);
    QSignalSpy failure(
        &download, &prometheus::ExactPackageDownload::downloadFailed);

    download.acquire(QNetworkRequest(invalid.url()));
    QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 10000);
    QCOMPARE(success.count(), 0);
    QVERIFY(!download.busy());

    download.acquire(QNetworkRequest(valid.url()));
    QTRY_COMPARE_WITH_TIMEOUT(success.count(), 1, 10000);
    QCOMPARE(failure.count(), 1);
    QCOMPARE(success.front().at(0).toByteArray(), bytes);
    QCOMPARE(success.front().at(1).toString().toUtf8(), hash);
    QVERIFY(!download.busy());
}

void ExactPackageDownloadTests::repeatedServiceAcquirePreservesBusyState()
{
    ServiceStateServer server;
    ServiceController controller(server.baseUrl());
    controller.loadFixture(
        QStringLiteral("prometheus.motor-a.fixture-1"));
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.status(), QStringLiteral("published"), 10000);
    QVERIFY(!controller.busy());

    controller.acquireExactPackage();
    QTRY_VERIFY_WITH_TIMEOUT(server.packageRequested(), 10000);
    QVERIFY(controller.busy());
    QCOMPARE(controller.status(),
        QStringLiteral("acquiring_exact_package"));

    controller.acquireExactPackage();
    QVERIFY(controller.busy());
    QCOMPARE(controller.status(),
        QStringLiteral("acquiring_exact_package"));
    QCOMPARE(controller.errorCode(), QStringLiteral("service_busy"));
    controller.reset();
}

void ExactPackageDownloadTests::successfulServiceAcquireCanRetry()
{
    ServiceStateServer server(true);
    ServiceController controller(server.baseUrl());
    QSignalSpy acquired(
        &controller, &ServiceController::exactPackageAcquired);
    controller.loadFixture(
        QStringLiteral("prometheus.motor-a.fixture-1"));
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.status(), QStringLiteral("published"), 10000);

    controller.acquireExactPackage();
    QTRY_COMPARE_WITH_TIMEOUT(acquired.count(), 1, 10000);
    QCOMPARE(controller.status(), QStringLiteral("exact_package_ready"));
    QCOMPARE(controller.errorCode(), QString{});

    controller.acquireExactPackage();
    QTRY_COMPARE_WITH_TIMEOUT(acquired.count(), 2, 10000);
    QCOMPARE(server.packageRequestCount(), 2);
    QCOMPARE(controller.status(), QStringLiteral("exact_package_ready"));
    QCOMPARE(controller.errorCode(), QString{});
}

QTEST_GUILESS_MAIN(ExactPackageDownloadTests)

#include "exact_package_download_tests.moc"
