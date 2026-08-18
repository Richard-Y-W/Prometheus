#include "component_binding_controller.hpp"

#include "execution_component_variant.hpp"
#include "project_controller.hpp"

#include <prometheus/execution/package_consumer.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

std::string bytes(const QByteArray &value) {
  return {value.constData(), static_cast<std::size_t>(value.size())};
}

QString text(const std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

} // namespace

ComponentBindingController::ComponentBindingController(QObject *parent)
    : ComponentBindingController(
          QUrl(QStringLiteral("http://127.0.0.1:8000")), nullptr, parent) {}

ComponentBindingController::ComponentBindingController(QUrl serviceBaseUrl,
                                                        QObject *parent)
    : ComponentBindingController(std::move(serviceBaseUrl), nullptr, parent) {
}

ComponentBindingController::ComponentBindingController(
    QUrl serviceBaseUrl, ProjectController *project, QObject *parent)
    : QObject(parent), service_base_url_(std::move(serviceBaseUrl)),
      exact_package_download_(&network_), project_(project) {
  connect(&exact_package_download_,
          &prometheus::ExactPackageDownload::exactPackageAcquired, this,
          [this](QByteArray packageBytes, QString expectedObjectHash) {
            const auto storedBytes = bytes(packageBytes);
            const auto expectedHash = expectedObjectHash.toStdString();
            const auto inspection = prometheus::execution::
                inspect_execution_component(storedBytes, expectedHash);
            const auto cadEntityId = pending_cad_entity_id_;
            pending_cad_entity_id_.clear();
            if (!inspection.has_value()) {
              setError(text(inspection.diagnostic().message),
                       text(inspection.diagnostic().code));
              emit componentBindingFailed(cadEntityId, error_, error_code_);
              return;
            }
            error_.clear();
            error_code_.clear();
            fetchSupersessionAndEmit(
                cadEntityId,
                prometheus::executionComponentVariant(inspection.value()),
                inspection.value(), packageBytes);
          });
  connect(&exact_package_download_,
          &prometheus::ExactPackageDownload::downloadFailed, this,
          [this](const QString &message, const QString &code) {
            const auto cadEntityId = pending_cad_entity_id_;
            pending_cad_entity_id_.clear();
            setError(message, code);
            emit componentBindingFailed(cadEntityId, message, code);
          });
}

void ComponentBindingController::setBusy(const bool value) {
  if (busy_ == value)
    return;
  busy_ = value;
  emit changed();
}

void ComponentBindingController::setError(const QString &message,
                                          const QString &code,
                                          const bool clearBusy) {
  error_ = message;
  error_code_ = code;
  if (clearBusy) {
    setBusy(false);
  }
  emit changed();
}

void ComponentBindingController::bindRevision(const QString &cadEntityId,
                                              const QString &revisionId) {
  if (cadEntityId.trimmed().isEmpty() || revisionId.trimmed().isEmpty()) {
    setError("A CAD entity and a revision id are both required to bind a "
             "verified component.",
             "component_binding_invalid_request");
    return;
  }
  if (busy_ || exact_package_download_.busy()) {
    setError("Another component binding fetch is already active.",
             "component_binding_busy", /*clearBusy=*/false);
    return;
  }
  error_.clear();
  error_code_.clear();
  pending_cad_entity_id_ = cadEntityId;
  setBusy(true);
  emit changed();
  const auto encodedRevision =
      QString::fromLatin1(QUrl::toPercentEncoding(revisionId));
  QNetworkRequest request(service_base_url_.resolved(
      QUrl("/api/v2/revisions/" + encodedRevision + "/execution-package")));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  exact_package_download_.acquire(request);
}

void ComponentBindingController::fetchSupersessionAndEmit(
    const QString &cadEntityId, QVariantMap component,
    const prometheus::execution::PackageInspection &inspection,
    const QByteArray &bytes) {
  // The package bytes are immutable and never carry whether a newer
  // revision has since been published; that is a live fact checked
  // separately here. A failure of this secondary check must not discard an
  // already hash-verified binding -- it only leaves supersession unknown.
  const auto revisionId = component.value("revision_id").toString();
  const auto encodedRevision =
      QString::fromLatin1(QUrl::toPercentEncoding(revisionId));
  QNetworkRequest request(service_base_url_.resolved(
      QUrl("/api/v2/revisions/" + encodedRevision)));
  auto *reply = network_.get(request);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, cadEntityId, component, inspection, bytes]() mutable {
            if (reply->error() == QNetworkReply::NoError) {
              const auto document =
                  QJsonDocument::fromJson(reply->readAll()).object();
              const auto supersededBy =
                  document.value("superseded_by").toObject();
              if (!supersededBy.isEmpty()) {
                component["superseded_by_revision_id"] =
                    supersededBy.value("revision_id").toString();
                component["superseded_by_revision"] =
                    supersededBy.value("revision").toString();
              }
            }
            reply->deleteLater();
            persistBindingEdge(cadEntityId, inspection, bytes);
            setBusy(false);
            emit changed();
            emit componentBindingVerified(cadEntityId, component);
          });
}

void ComponentBindingController::persistBindingEdge(
    const QString &cadEntityId,
    const prometheus::execution::PackageInspection &inspection,
    const QByteArray &bytes) {
  // Phase 6 checkpoint 1: promotes the binding from transient display state
  // into a real, persisted, append-only graph edge, reusing the exact
  // content-addressed supersede-chain mechanism Program 01B already built
  // and proved for the fixture-to-execution binding path -- no new schema.
  // Best-effort and silent: a project that is not open, not writable, or a
  // CAD entity that does not exist in it must never discard the
  // hash-verified binding this session already established, and must never
  // surface an error of its own -- so this checks read-only accessors
  // rather than calling ensureExecutionWritable(), which would overwrite
  // ProjectController's shared error state for an operation the user did
  // not initiate.
  if (project_ == nullptr || !project_->project().has_value() ||
      project_->saveAsRequired() || !project_->executionStoreAvailable() ||
      !project_->hasCadEntityId(cadEntityId)) {
    return;
  }
  const auto storedBytes = bytes.toStdString();
  const auto reference = prometheus::executionComponentReference(
      inspection, storedBytes.size());
  const auto installed = prometheus::run_store::install_package_binding(
      project_->projectPath(), cadEntityId.toStdString(), reference,
      storedBytes);
  if (installed.has_value()) {
    project_->acceptProject(installed.value());
  }
}

void ComponentBindingController::cancel() {
  exact_package_download_.cancel();
  pending_cad_entity_id_.clear();
  setBusy(false);
}
