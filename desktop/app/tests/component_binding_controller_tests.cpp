#include "component_binding_controller.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

#include <cstddef>
#include <memory>

namespace {

constexpr auto packageMediaType =
    "application/vnd.prometheus.execution-component+json;version=2.0.0";

QByteArray readFile(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    qFatal("Cannot open component-binding fixture");
  }
  return file.readAll();
}

// Routes by request path: an "/execution-package" path serves the
// configured sealed-bytes response; any other path (the plain revision
// metadata lookup ComponentBindingController issues after verification to
// check supersession) serves the configured metadata JSON body.
class RevisionPackageServer final : public QObject {
  Q_OBJECT

public:
  RevisionPackageServer(QByteArray packageHeaders, QByteArray packageBody,
                        QByteArray metadataBody = "{\"superseded_by\":null}",
                        bool failMetadata = false, QObject *parent = nullptr)
      : QObject(parent), package_headers_(std::move(packageHeaders)),
        package_body_(std::move(packageBody)),
        metadata_body_(std::move(metadataBody)),
        fail_metadata_(failMetadata) {
    connect(&server_, &QTcpServer::newConnection, this, [this] {
      while (server_.hasPendingConnections()) {
        auto *socket = server_.nextPendingConnection();
        auto request = std::make_shared<QByteArray>();
        auto responded = std::make_shared<bool>(false);
        connect(socket, &QTcpSocket::readyRead, socket,
                [this, socket, request, responded] {
                  *request += socket->readAll();
                  if (*responded || !request->contains("\r\n\r\n"))
                    return;
                  *responded = true;
                  ++request_count_;
                  if (request->contains("/execution-package")) {
                    socket->write(package_headers_);
                    socket->write(package_body_);
                    socket->flush();
                    socket->disconnectFromHost();
                  } else if (fail_metadata_) {
                    ++metadata_request_count_;
                    socket->disconnectFromHost();
                  } else {
                    ++metadata_request_count_;
                    socket->write(
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                        "Content-Length: " +
                        QByteArray::number(metadata_body_.size()) +
                        "\r\nConnection: close\r\n\r\n" + metadata_body_);
                    socket->flush();
                    socket->disconnectFromHost();
                  }
                });
      }
    });
    if (!server_.listen(QHostAddress::LocalHost, 0)) {
      qFatal("Cannot listen for component-binding test response: %s",
             qPrintable(server_.errorString()));
    }
  }

  QUrl baseUrl() const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()));
  }

  int requestCount() const noexcept { return request_count_; }
  int metadataRequestCount() const noexcept { return metadata_request_count_; }

private:
  QTcpServer server_;
  QByteArray package_headers_;
  QByteArray package_body_;
  QByteArray metadata_body_;
  bool fail_metadata_{false};
  int request_count_{0};
  int metadata_request_count_{0};
};

QByteArray validHeaders(const QByteArray &hash, const qsizetype length) {
  return "HTTP/1.1 200 OK\r\nContent-Type: " + QByteArray(packageMediaType) +
        "\r\nETag: \"" + hash + "\"\r\nContent-Length: " +
        QByteArray::number(length) + "\r\nConnection: close\r\n\r\n";
}

} // namespace

class ComponentBindingControllerTests final : public QObject {
  Q_OBJECT

private slots:
  void rejectsEmptyIdentifiers();
  void verifiesAndEmitsComponentOnValidPackage();
  void surfacesReportedSupersession();
  void supersessionCheckFailureDoesNotDiscardVerifiedBinding();
  void failsClosedOnHashMismatch();
  void failsClosedWhileBusy();
};

void ComponentBindingControllerTests::rejectsEmptyIdentifiers() {
  ComponentBindingController controller(
      QUrl(QStringLiteral("http://127.0.0.1:1")));
  QSignalSpy verified(&controller,
                      &ComponentBindingController::componentBindingVerified);
  QSignalSpy failed(&controller,
                    &ComponentBindingController::componentBindingFailed);

  controller.bindRevision("", "revision-1");
  QCOMPARE(verified.count(), 0);
  QCOMPARE(failed.count(), 0);
  QCOMPARE(controller.errorCode(), QStringLiteral("component_binding_invalid_request"));
  QVERIFY(!controller.busy());

  controller.bindRevision("entity-1", "  ");
  QCOMPARE(controller.errorCode(), QStringLiteral("component_binding_invalid_request"));
  QVERIFY(!controller.busy());
}

void ComponentBindingControllerTests::verifiesAndEmitsComponentOnValidPackage() {
  const auto bytes = readFile(QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) +
                              "/fixtures/contracts/execution-component-v2.motor-a.jcs");
  const auto hash = QByteArray::fromStdString(prometheus::integrity::sha256_bytes(
      std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
  RevisionPackageServer server(validHeaders(hash, bytes.size()), bytes);
  ComponentBindingController controller(server.baseUrl());
  QSignalSpy verified(&controller,
                      &ComponentBindingController::componentBindingVerified);
  QSignalSpy failed(&controller,
                    &ComponentBindingController::componentBindingFailed);

  controller.bindRevision("entity-1", "revision-1");
  QVERIFY(controller.busy());
  QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 1, 10000);
  QCOMPARE(failed.count(), 0);
  QVERIFY(!controller.busy());
  QCOMPARE(controller.errorCode(), QString{});
  QCOMPARE(verified.front().at(0).toString(), QStringLiteral("entity-1"));
  const auto component = verified.front().at(1).toMap();
  QVERIFY(!component.value("manufacturer").toString().isEmpty());
  QVERIFY(!component.value("revision_id").toString().isEmpty());
  QCOMPARE(component.value("package_hash").toString().toUtf8(), hash);
  QCOMPARE(component.value("superseded_by_revision_id").toString(), QString{});
  QCOMPARE(server.metadataRequestCount(), 1);
}

void ComponentBindingControllerTests::surfacesReportedSupersession() {
  const auto bytes = readFile(QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) +
                              "/fixtures/contracts/execution-component-v2.motor-a.jcs");
  const auto hash = QByteArray::fromStdString(prometheus::integrity::sha256_bytes(
      std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
  RevisionPackageServer server(
      validHeaders(hash, bytes.size()), bytes,
      "{\"superseded_by\":{\"revision_id\":\"newer-revision\","
      "\"revision\":\"nameplate-2\",\"object_hash\":\"sha256:" +
          QByteArray(64, 'b') + "\",\"published_at\":\"2026-08-17T00:00:00Z\"}}");
  ComponentBindingController controller(server.baseUrl());
  QSignalSpy verified(&controller,
                      &ComponentBindingController::componentBindingVerified);

  controller.bindRevision("entity-1", "revision-1");
  QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 1, 10000);
  const auto component = verified.front().at(1).toMap();
  QCOMPARE(component.value("superseded_by_revision_id").toString(),
          QStringLiteral("newer-revision"));
  QCOMPARE(component.value("superseded_by_revision").toString(),
          QStringLiteral("nameplate-2"));
}

void ComponentBindingControllerTests::supersessionCheckFailureDoesNotDiscardVerifiedBinding() {
  const auto bytes = readFile(QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) +
                              "/fixtures/contracts/execution-component-v2.motor-a.jcs");
  const auto hash = QByteArray::fromStdString(prometheus::integrity::sha256_bytes(
      std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
  RevisionPackageServer server(validHeaders(hash, bytes.size()), bytes,
                               /*metadataBody=*/{}, /*failMetadata=*/true);
  ComponentBindingController controller(server.baseUrl());
  QSignalSpy verified(&controller,
                      &ComponentBindingController::componentBindingVerified);
  QSignalSpy failed(&controller,
                    &ComponentBindingController::componentBindingFailed);

  controller.bindRevision("entity-1", "revision-1");
  QTRY_COMPARE_WITH_TIMEOUT(failed.count() + verified.count(), 1, 10000);
  QCOMPARE(failed.count(), 0);
  QCOMPARE(verified.count(), 1);
  const auto component = verified.front().at(1).toMap();
  QVERIFY(!component.value("manufacturer").toString().isEmpty());
  QCOMPARE(component.value("superseded_by_revision_id").toString(), QString{});
}

void ComponentBindingControllerTests::failsClosedOnHashMismatch() {
  const auto hash = QByteArray("sha256:") + QByteArray(64, 'a');
  RevisionPackageServer server(validHeaders(hash, 2), "{}");
  ComponentBindingController controller(server.baseUrl());
  QSignalSpy verified(&controller,
                      &ComponentBindingController::componentBindingVerified);
  QSignalSpy failed(&controller,
                    &ComponentBindingController::componentBindingFailed);

  controller.bindRevision("entity-1", "revision-1");
  QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
  QCOMPARE(verified.count(), 0);
  QVERIFY(!controller.busy());
  QCOMPARE(failed.front().at(0).toString(), QStringLiteral("entity-1"));
  QVERIFY(!failed.front().at(1).toString().isEmpty());
  QVERIFY(!controller.errorCode().isEmpty());
}

void ComponentBindingControllerTests::failsClosedWhileBusy() {
  const auto bytes = readFile(QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) +
                              "/fixtures/contracts/execution-component-v2.motor-a.jcs");
  const auto hash = QByteArray::fromStdString(prometheus::integrity::sha256_bytes(
      std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
  RevisionPackageServer server(validHeaders(hash, bytes.size()), bytes);
  ComponentBindingController controller(server.baseUrl());

  controller.bindRevision("entity-1", "revision-1");
  QVERIFY(controller.busy());
  controller.bindRevision("entity-2", "revision-2");
  QCOMPARE(controller.errorCode(), QStringLiteral("component_binding_busy"));
  QVERIFY(controller.busy());

  QSignalSpy verified(&controller,
                      &ComponentBindingController::componentBindingVerified);
  QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 1, 10000);
  QCOMPARE(verified.front().at(0).toString(), QStringLiteral("entity-1"));
  QCOMPARE(server.requestCount(), 2);
  QCOMPARE(server.metadataRequestCount(), 1);
}

QTEST_GUILESS_MAIN(ComponentBindingControllerTests)

#include "component_binding_controller_tests.moc"
