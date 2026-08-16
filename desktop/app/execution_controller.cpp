#include "execution_controller.hpp"

#include "project_controller.hpp"
#include "service_controller.hpp"

#include <prometheus/execution/execute.hpp>
#include <prometheus/replay/replay.hpp>
#include <prometheus/run_store/object_store.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace execution = prometheus::execution;
namespace replay = prometheus::replay;
namespace run_store = prometheus::run_store;

constexpr std::string_view packageMediaType =
    "application/vnd.prometheus.execution-component+json;version=2.0.0";
constexpr std::string_view packageSchemaId =
    "urn:prometheus:schema:execution-component:2.0.0";
constexpr std::string_view packageSchemaVersion = "2.0.0";

QString text(const std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string text(const QString &value) {
  const auto bytes = value.toUtf8();
  return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

std::string bytes(const QByteArray &value) {
  return {value.constData(), static_cast<std::size_t>(value.size())};
}

QByteArray bytes(const std::string_view value) {
  return QByteArray(value.data(), static_cast<qsizetype>(value.size()));
}

QVariantList strings(const std::vector<std::string> &values) {
  QVariantList result;
  result.reserve(static_cast<qsizetype>(values.size()));
  for (const auto &value : values) {
    result.append(text(value));
  }
  return result;
}

QVariantMap packageMap(const execution::PackageInspection &inspection) {
  return {
      {"package_hash", text(inspection.package_hash)},
      {"revision_id", text(inspection.revision_id)},
      {"component_id", text(inspection.component_id)},
      {"manufacturer", text(inspection.manufacturer)},
      {"part_number", text(inspection.part_number)},
      {"revision", text(inspection.revision)},
      {"component_class", text(inspection.component_class)},
      {"package_kind", text(inspection.package_kind)},
      {"capability_id", text(inspection.capability_id)},
      {"execution_readiness", text(inspection.execution_readiness)},
      {"limitations", strings(inspection.limitations)},
      {"blocked_reason", inspection.blocked_reason.has_value()
                             ? text(*inspection.blocked_reason)
                             : QString{}},
  };
}

run_store::StoredObjectReference
packageReference(const execution::PackageInspection &inspection,
                 const std::size_t byteLength) {
  return {inspection.package_hash, static_cast<std::uint64_t>(byteLength),
          std::string(packageMediaType), std::string(packageSchemaId),
          std::string(packageSchemaVersion)};
}

run_store::StoredObjectReference
objectReference(const execution::CanonicalObject &object) {
  return {object.object_hash, static_cast<std::uint64_t>(object.bytes.size()),
          object.media_type, object.schema_id, object.schema_version};
}

run_store::ObjectToStore
storedObject(const execution::CanonicalObject &object) {
  return {objectReference(object), object.bytes};
}

execution::ScenarioDraftDegrees typedScenarioDraft(const QVariantMap &draft) {
  return {
      draft.value("payload_mass_kg").toDouble(),
      draft.value("arm_radius_m").toDouble(),
      draft.value("rotation_degrees").toDouble(),
      draft.value("move_duration_s").toDouble(),
      draft.value("hold_duration_s").toDouble(),
      draft.value("cycle_duration_s").toDouble(),
      draft.value("ambient_temperature_c").toDouble(),
  };
}

QVariantMap scenarioPreviewMap(const execution::ScenarioPreview &preview) {
  return {
      {"payload_mass_kg", preview.payload_mass_kg},
      {"payload_mass_unit", "kg"},
      {"arm_radius_m", preview.arm_radius_m},
      {"arm_radius_unit", "m"},
      {"rotation_rad", preview.rotation_rad},
      {"rotation_unit", "rad"},
      {"move_duration_s", preview.move_duration_s},
      {"move_duration_unit", "s"},
      {"hold_duration_s", preview.hold_duration_s},
      {"hold_duration_unit", "s"},
      {"cycle_duration_s", preview.cycle_duration_s},
      {"cycle_duration_unit", "s"},
      {"ambient_temperature_c", preview.ambient_temperature_c},
      {"ambient_temperature_unit", "degC"},
      {"motion_profile", text(preview.motion_profile)},
      {"confirmed_by_user", preview.confirmed_by_user},
  };
}

QVariantMap scenarioDraftMap(const execution::MotorArmScenario &scenario) {
  constexpr double radiansToDegrees = 57.295779513082320876798154814105;
  return {
      {"payload_mass_kg", scenario.payload_mass_kg},
      {"arm_radius_m", scenario.arm_radius_m},
      {"rotation_degrees", scenario.rotation_rad * radiansToDegrees},
      {"move_duration_s", scenario.move_duration_s},
      {"hold_duration_s", scenario.hold_duration_s},
      {"cycle_duration_s", scenario.cycle_duration_s},
      {"ambient_temperature_c", scenario.ambient_temperature_c},
  };
}

QVariantMap scenarioPreviewMap(const execution::MotorArmScenario &scenario) {
  return scenarioPreviewMap(execution::ScenarioPreview{
      scenario.payload_mass_kg, scenario.arm_radius_m, scenario.rotation_rad,
      scenario.move_duration_s, scenario.hold_duration_s,
      scenario.cycle_duration_s, scenario.ambient_temperature_c,
      scenario.motion_profile, scenario.confirmed_by_user});
}

QString executionDiagnosticMessage(const execution::Diagnostic &diagnostic) {
  return text(diagnostic.message);
}

QString executionDiagnosticCode(const execution::Diagnostic &diagnostic) {
  return text(diagnostic.code);
}

QString storeDiagnosticMessage(const run_store::Diagnostic &diagnostic) {
  return text(diagnostic.message);
}

QString storeDiagnosticCode(const run_store::Diagnostic &diagnostic) {
  return text(diagnostic.code);
}

struct RecordedDisplay final {
  QVariantMap history;
  QVariantMap result;
};

RecordedDisplay
loadRecordedDisplay(const std::filesystem::path &projectPath,
                    const run_store::StoredObjectReference &manifestReference) {
  RecordedDisplay display;
  display.history = {
      {"manifest_hash", text(manifestReference.object_hash)},
      {"status", "Reproduction failed"},
  };
  const auto reject = [&display](const QString &code, const QString &message) {
    display.history["error_code"] = code;
    display.history["error"] = message;
    return display;
  };

  const auto inspected =
      replay::inspect_recorded(projectPath, manifestReference.object_hash);
  if (inspected.status != replay::RecordedRunStatus::recorded ||
      !inspected.package_hash.has_value() ||
      !inspected.result_hash.has_value() ||
      !inspected.result_bytes.has_value() ||
      !inspected.backend_id.has_value() ||
      !inspected.backend_contract_version.has_value()) {
    const auto code = inspected.diagnostic.has_value()
                          ? text(inspected.diagnostic->code)
                          : QStringLiteral("recorded_run_verification_failed");
    const auto message =
        inspected.diagnostic.has_value()
            ? text(inspected.diagnostic->message)
            : QStringLiteral("The recorded run graph could not be verified.");
    return reject(code, message);
  }
  QJsonParseError resultParseError;
  const auto resultDocument = QJsonDocument::fromJson(
      bytes(*inspected.result_bytes), &resultParseError);
  const auto resultJson = resultDocument.object();
  if (resultParseError.error != QJsonParseError::NoError ||
      !resultDocument.isObject()) {
    return reject("result_contract_invalid",
                  "The verified result could not be prepared for display.");
  }
  display.result = resultJson.toVariantMap();
  display.history = {
      {"manifest_hash", text(manifestReference.object_hash)},
      {"result_hash", text(*inspected.result_hash)},
      {"package_hash", text(*inspected.package_hash)},
      {"backend_id", text(*inspected.backend_id)},
      {"backend_contract_version", text(*inspected.backend_contract_version)},
      {"status", "Recorded"},
      {"coverage", resultJson.value("coverage").toObject().toVariantMap()},
      {"findings",
       resultJson.value("obligation_outcomes").toArray().toVariantList()},
  };
  return display;
}

struct RunSnapshot final {
  std::filesystem::path project_path;
  run_store::StoredObjectReference package;
  run_store::StoredObjectReference scenario;
  std::string assembly_hash;
  std::string cad_entity_id;
  std::stop_token stop_token;
};

struct RunWorkerResult final {
  bool completed{false};
  bool cancelled{false};
  std::string error_code;
  std::string error_message;
  std::optional<run_store::ProjectV2> project;
};

RunWorkerResult failed(const run_store::Diagnostic &diagnostic) {
  return {false, diagnostic.code == "operation_cancelled", diagnostic.code,
          diagnostic.message, std::nullopt};
}

RunWorkerResult failed(const execution::Diagnostic &diagnostic,
                       const bool cancelled = false) {
  return {false, cancelled, diagnostic.code, diagnostic.message, std::nullopt};
}

RunWorkerResult runAuthoritativeAnalysis(const RunSnapshot &snapshot) noexcept {
  try {
    if (snapshot.stop_token.stop_requested()) {
      return {false, true, "operation_cancelled",
              "execution was cancelled before calculation", std::nullopt};
    }
    const auto packageBytes =
        run_store::read_object(snapshot.project_path, snapshot.package);
    if (!packageBytes.has_value()) {
      return failed(packageBytes.diagnostic());
    }
    const auto scenarioBytes =
        run_store::read_object(snapshot.project_path, snapshot.scenario);
    if (!scenarioBytes.has_value()) {
      return failed(scenarioBytes.diagnostic());
    }
    if (snapshot.stop_token.stop_requested()) {
      return {
          false,
          true,
          "operation_cancelled",
          "execution was cancelled before request compilation",
          std::nullopt,
      };
    }

    std::vector<std::string> obligations;
    obligations.reserve(execution::motor_arm_obligation_ids.size());
    std::transform(
        execution::motor_arm_obligation_ids.begin(),
        execution::motor_arm_obligation_ids.end(),
        std::back_inserter(obligations),
        [](const std::string_view value) { return std::string(value); });
    const auto request =
        execution::build_analysis_request(execution::AnalysisRequestDraft{
            snapshot.package.object_hash, snapshot.scenario.object_hash,
            snapshot.assembly_hash, snapshot.cad_entity_id,
            std::string(execution::motor_arm_backend_id),
            std::string(execution::motor_arm_backend_contract_version),
            std::string(execution::supported_motor_consumer_contract_hash()),
            std::move(obligations)});
    if (!request.has_value()) {
      return failed(request.diagnostic());
    }
    if (snapshot.stop_token.stop_requested()) {
      return {false, true, "operation_cancelled",
              "execution was cancelled before calculation", std::nullopt};
    }
    const auto outcome = execution::execute(execution::ExecutionInput{
        packageBytes.value(), snapshot.package.object_hash,
        scenarioBytes.value(), snapshot.scenario.object_hash,
        request.value().bytes, request.value().object_hash});
    if (const auto *failure =
            std::get_if<execution::ExecutionFailure>(&outcome)) {
      if (failure->diagnostics.empty()) {
        return {false,
                failure->disposition ==
                    execution::ExecutionDisposition::cancelled,
                "execution_failed", "execution returned no diagnostic",
                std::nullopt};
      }
      return failed(failure->diagnostics.front(),
                    failure->disposition ==
                        execution::ExecutionDisposition::cancelled);
    }
    if (snapshot.stop_token.stop_requested()) {
      return {false, true, "operation_cancelled",
              "execution was cancelled before publication", std::nullopt};
    }
    const auto &completed = std::get<execution::CompletedExecution>(outcome);
    const auto publication = run_store::publish_completed_run(
        snapshot.project_path,
        run_store::CompletedRunObjects{
            {snapshot.package, packageBytes.value()},
            {snapshot.scenario, scenarioBytes.value()},
            storedObject(request.value()),
            storedObject(completed.result),
            storedObject(completed.manifest)},
        run_store::TransactionOptions{
            run_store::maximum_lock_wait, snapshot.stop_token, {}});
    if (!publication.has_value()) {
      return failed(publication.diagnostic());
    }
    return {true, false, {}, {}, publication.value().project};
  } catch (const std::exception &failure) {
    auto message = std::string(failure.what());
    message.resize(std::min<std::size_t>(message.size(), 4096U));
    return {false, false, "execution_worker_failed", std::move(message),
            std::nullopt};
  } catch (...) {
    return {false, false, "execution_worker_failed",
            "unknown execution worker failure", std::nullopt};
  }
}

QString replayLabel(const replay::ReplayStatus status) {
  switch (status) {
  case replay::ReplayStatus::exact_match:
    return "Exact match";
  case replay::ReplayStatus::execution_unavailable:
    return "Backend identity unavailable";
  case replay::ReplayStatus::verification_failed:
  case replay::ReplayStatus::execution_failed:
  case replay::ReplayStatus::mismatch:
    return "Reproduction failed";
  }
  return "Reproduction failed";
}

} // namespace

ExecutionController::ExecutionController(ProjectController *project,
                                         ServiceController *service,
                                         QObject *parent)
    : QObject(parent), project_(project) {
  Q_ASSERT(project_ != nullptr);
  connect(project_, &ProjectController::projectOpened, this, [this] {
    clearPendingSaveAsAction();
    reloadProject();
  });
  connect(project_, &ProjectController::projectSaved, this,
          &ExecutionController::resumePendingSaveAsAction);
  if (service != nullptr) {
    connect(service, &ServiceController::exactPackageAcquired, this,
            &ExecutionController::acceptExactPackage);
  }
}

ExecutionController::~ExecutionController() {
  ++generation_;
  stop_source_.request_stop();
}

bool ExecutionController::canRun() const {
  return !busy_ && project_->project().has_value() &&
         !project_->saveAsRequired() && project_->executionStoreAvailable() &&
         project_->assemblyArtifactCurrent() &&
         active_package_reference_.has_value() &&
         active_package_.value("execution_readiness") == "ready" &&
         scenario_confirmed_ && confirmed_scenario_reference_.has_value();
}

void ExecutionController::clearError() {
  error_.clear();
  error_code_.clear();
}

void ExecutionController::setError(QString message, QString code,
                                   QString status) {
  error_ = std::move(message);
  error_code_ = std::move(code);
  status_ = std::move(status);
  emit changed();
}

void ExecutionController::clearSelection() {
  selected_run_index_ = -1;
  selected_result_.clear();
  replay_state_.clear();
}

void ExecutionController::clearScenarioConfirmation() {
  scenario_confirmed_ = false;
  confirmed_scenario_hash_.clear();
  confirmed_scenario_reference_.reset();
}

void ExecutionController::setPendingCadEntityId(const QString &entityId) {
  const auto normalized = entityId.trimmed();
  if (pending_cad_entity_id_ == normalized) {
    return;
  }
  if (busy_) {
    setError("Cannot change the bound CAD entity during execution.",
             "execution_busy");
    return;
  }
  pending_cad_entity_id_ = normalized;
  clearPendingSaveAsAction();
  active_package_.clear();
  active_package_reference_.reset();
  clearSelection();
  clearError();
  status_ = "entity_selected";
  loadActiveBinding();
  emit changed();
}

void ExecutionController::acceptExactPackage(QByteArray packageBytes,
                                             QString expectedObjectHash) {
  if (busy_) {
    setError("Cannot bind a package while an execution is active.",
             "execution_busy");
    return;
  }
  const auto storedBytes = bytes(packageBytes);
  const auto expectedHash = text(expectedObjectHash);
  const auto inspection =
      execution::inspect_execution_component(storedBytes, expectedHash);
  if (!inspection.has_value()) {
    setError(executionDiagnosticMessage(inspection.diagnostic()),
             executionDiagnosticCode(inspection.diagnostic()));
    return;
  }
  if (!project_->ensureExecutionWritable()) {
    if (project_->errorCode() == "save_as_required") {
      pending_package_binding_ = PendingPackageBinding{
          std::move(packageBytes), std::move(expectedObjectHash),
          pending_cad_entity_id_};
      pending_scenario_confirmation_.reset();
      pending_save_as_action_ = "package_binding";
    }
    setError(project_->error(), project_->errorCode());
    return;
  }
  if (!project_->verifyAssemblyArtifactCurrent()) {
    setError(project_->error(), project_->errorCode());
    return;
  }
  if (!project_->hasCadEntityId(pending_cad_entity_id_)) {
    setError("Select a stable CAD entity that exists in the current project.",
             "cad_entity_not_found");
    return;
  }
  const auto reference =
      packageReference(inspection.value(), storedBytes.size());
  const auto installed = run_store::install_package_binding(
      project_->projectPath(), text(pending_cad_entity_id_), reference,
      storedBytes);
  if (!installed.has_value()) {
    setError(storeDiagnosticMessage(installed.diagnostic()),
             storeDiagnosticCode(installed.diagnostic()));
    return;
  }
  project_->acceptProject(installed.value());
  active_package_reference_ = reference;
  active_package_ = packageMap(inspection.value());
  clearPendingSaveAsAction();
  clearSelection();
  clearError();
  status_ = inspection.value().execution_readiness == "ready"
                ? QStringLiteral("package_bound")
                : QStringLiteral("package_blocked");
  emit changed();
}

void ExecutionController::setScenarioDraft(const QVariantMap &draft) {
  if (scenario_draft_ == draft) {
    return;
  }
  if (busy_) {
    setError("Cannot edit a scenario while an execution is active.",
             "execution_busy");
    return;
  }
  scenario_draft_ = draft;
  scenario_preview_.clear();
  typed_preview_.reset();
  clearScenarioConfirmation();
  clearPendingSaveAsAction();
  scenario_load_error_.clear();
  scenario_load_error_code_.clear();
  clearSelection();
  clearError();
  status_ = "scenario_edited";
  emit changed();
}

void ExecutionController::previewScenarioDegrees() {
  if (busy_) {
    setError("Cannot preview a scenario while an execution is active.",
             "execution_busy");
    return;
  }
  const auto preview = execution::preview_motor_arm_scenario(
      typedScenarioDraft(scenario_draft_));
  if (!preview.has_value()) {
    typed_preview_.reset();
    scenario_preview_.clear();
    clearScenarioConfirmation();
    setError(executionDiagnosticMessage(preview.diagnostic()),
             executionDiagnosticCode(preview.diagnostic()), "scenario_invalid");
    return;
  }
  typed_preview_ = preview.value();
  scenario_preview_ = scenarioPreviewMap(preview.value());
  clearScenarioConfirmation();
  clearSelection();
  clearError();
  status_ = "scenario_previewed";
  emit changed();
}

void ExecutionController::confirmScenario(const QString &intent) {
  if (busy_) {
    setError("Cannot confirm a scenario while an execution is active.",
             "execution_busy");
    return;
  }
  if (!typed_preview_.has_value()) {
    setError("Preview valid scenario values before confirmation.",
             "scenario_preview_missing");
    return;
  }
  auto confirmedPreview = *typed_preview_;
  confirmedPreview.confirmed_by_user = true;
  const auto confirmed =
      execution::confirm_motor_arm_scenario(confirmedPreview, text(intent));
  if (!confirmed.has_value()) {
    setError(executionDiagnosticMessage(confirmed.diagnostic()),
             executionDiagnosticCode(confirmed.diagnostic()),
             "scenario_invalid");
    return;
  }
  if (!project_->ensureExecutionWritable()) {
    if (project_->errorCode() == "save_as_required") {
      pending_scenario_confirmation_ =
          PendingScenarioConfirmation{confirmed.value(), confirmedPreview};
      pending_package_binding_.reset();
      pending_save_as_action_ = "scenario_confirmation";
    }
    setError(project_->error(), project_->errorCode());
    return;
  }
  static_cast<void>(
      installConfirmedScenario(confirmed.value(), confirmedPreview));
}

bool ExecutionController::installConfirmedScenario(
    const execution::CanonicalObject &object,
    const execution::ScenarioPreview &preview) {
  if (!project_->verifyAssemblyArtifactCurrent()) {
    setError(project_->error(), project_->errorCode());
    return false;
  }
  const auto reference = objectReference(object);
  const auto stored = run_store::set_current_scenario(project_->projectPath(),
                                                      reference, object.bytes);
  if (!stored.has_value()) {
    setError(storeDiagnosticMessage(stored.diagnostic()),
             storeDiagnosticCode(stored.diagnostic()));
    return false;
  }
  project_->acceptProject(stored.value());
  typed_preview_ = preview;
  scenario_preview_ = scenarioPreviewMap(preview);
  scenario_confirmed_ = true;
  confirmed_scenario_reference_ = reference;
  confirmed_scenario_hash_ = text(reference.object_hash);
  scenario_load_error_.clear();
  scenario_load_error_code_.clear();
  clearPendingSaveAsAction();
  clearSelection();
  clearError();
  status_ = "scenario_confirmed";
  emit changed();
  return true;
}

void ExecutionController::clearPendingSaveAsAction() {
  pending_package_binding_.reset();
  pending_scenario_confirmation_.reset();
  pending_save_as_action_.clear();
}

void ExecutionController::resumePendingSaveAsAction() {
  if (project_->saveAsRequired() || pending_save_as_action_.isEmpty()) {
    return;
  }
  if (pending_package_binding_.has_value()) {
    auto pending = std::move(*pending_package_binding_);
    clearPendingSaveAsAction();
    pending_cad_entity_id_ = pending.cad_entity_id;
    acceptExactPackage(std::move(pending.bytes),
                       std::move(pending.expected_object_hash));
    return;
  }
  if (pending_scenario_confirmation_.has_value()) {
    auto pending = std::move(*pending_scenario_confirmation_);
    clearPendingSaveAsAction();
    static_cast<void>(
        installConfirmedScenario(pending.object, pending.preview));
  }
}

void ExecutionController::cancelPendingSaveAsAction() {
  if (pending_save_as_action_.isEmpty()) {
    return;
  }
  clearPendingSaveAsAction();
  clearError();
  status_ = "pending_action_cancelled";
  emit changed();
}

void ExecutionController::runAnalysis() {
  if (busy_) {
    setError("An execution or replay is already active.", "execution_busy");
    return;
  }
  if (!project_->ensureExecutionWritable()) {
    setError(project_->error(), project_->errorCode());
    return;
  }
  if (!project_->verifyAssemblyArtifactCurrent()) {
    setError(project_->error(), project_->errorCode());
    return;
  }
  if (!active_package_reference_.has_value()) {
    setError("Bind an exact component package before execution.",
             "package_missing");
    return;
  }
  const auto packageLoadErrorCode =
      active_package_.value("error_code").toString();
  if (!packageLoadErrorCode.isEmpty()) {
    setError(active_package_.value("error").toString(), packageLoadErrorCode);
    return;
  }
  if (active_package_.value("execution_readiness") != "ready") {
    setError("The bound reviewed package is not execution-ready.",
             "package_blocked");
    return;
  }
  if (!scenario_load_error_code_.isEmpty()) {
    setError(scenario_load_error_, scenario_load_error_code_);
    return;
  }
  if (!scenario_confirmed_ || !confirmed_scenario_reference_.has_value()) {
    setError("Explicitly confirm the current scenario before execution.",
             "scenario_unconfirmed");
    return;
  }
  const auto &project = *project_->project();
  const auto binding = std::find_if(
      project.execution.package_bindings.rbegin(),
      project.execution.package_bindings.rend(), [this](const auto &candidate) {
        return candidate.cad_entity_id == text(pending_cad_entity_id_);
      });
  if (binding == project.execution.package_bindings.rend() ||
      binding->package != *active_package_reference_) {
    setError("The active package binding changed before execution.",
             "package_binding_stale");
    return;
  }
  if (!project.execution.current_scenario.has_value() ||
      *project.execution.current_scenario != *confirmed_scenario_reference_) {
    setError("The confirmed scenario changed before execution.",
             "scenario_binding_stale");
    return;
  }

  stop_source_ = std::stop_source{};
  const auto currentGeneration = ++generation_;
  busy_ = true;
  clearError();
  clearSelection();
  status_ = "running";
  emit changed();

  const RunSnapshot snapshot{
      project_->projectPath(),        *active_package_reference_,
      *confirmed_scenario_reference_, project.assembly_artifact_hash,
      text(pending_cad_entity_id_),   stop_source_.get_token()};
  auto *watcher = new QFutureWatcher<RunWorkerResult>(this);
  connect(watcher, &QFutureWatcher<RunWorkerResult>::finished, this,
          [this, watcher, currentGeneration] {
            const auto outcome = watcher->result();
            watcher->deleteLater();
            if (currentGeneration != generation_) {
              return;
            }
            busy_ = false;
            if (!outcome.completed || !outcome.project.has_value()) {
              setError(text(outcome.error_message), text(outcome.error_code),
                       outcome.cancelled ? QStringLiteral("cancelled")
                                         : QStringLiteral("failed"));
              return;
            }
            project_->acceptProject(*outcome.project);
            loadRecordedRuns();
            if (!recorded_runs_.empty()) {
              selectRun(static_cast<int>(recorded_runs_.size() - 1U));
            }
            clearError();
            status_ = "completed";
            emit changed();
          });
  watcher->setFuture(QtConcurrent::run(
      [snapshot] { return runAuthoritativeAnalysis(snapshot); }));
}

void ExecutionController::cancelExecution() {
  if (!busy_) {
    setError("There is no active execution or replay to cancel.",
             "operation_not_active");
    return;
  }
  stop_source_.request_stop();
  clearError();
  status_ = "cancelling";
  emit changed();
}

void ExecutionController::selectRun(const int index) {
  if (index < 0 || index >= static_cast<int>(recorded_runs_.size())) {
    setError("Choose a recorded run from the project history.",
             "run_selection_invalid");
    return;
  }
  selected_run_index_ = index;
  selected_result_ = recorded_runs_[static_cast<std::size_t>(index)].result;
  replay_state_ = run_history_.at(index).toMap().value("status").toString();
  clearError();
  status_ = "recorded_result_selected";
  emit changed();
}

void ExecutionController::replaySelected() {
  if (busy_) {
    setError("An execution or replay is already active.", "execution_busy");
    return;
  }
  if (selected_run_index_ < 0 ||
      selected_run_index_ >= static_cast<int>(recorded_runs_.size())) {
    setError("Choose a recorded run before replay.", "run_selection_missing");
    return;
  }
  if (!project_->project().has_value() || project_->projectPath().empty()) {
    setError("Open the recorded project before replay.", "project_missing");
    return;
  }
  stop_source_ = std::stop_source{};
  const auto currentGeneration = ++generation_;
  const auto projectPath = project_->projectPath();
  const auto manifestHash =
      recorded_runs_[static_cast<std::size_t>(selected_run_index_)]
          .manifest.object_hash;
  const auto stopToken = stop_source_.get_token();
  busy_ = true;
  clearError();
  status_ = "replaying";
  emit changed();

  auto *watcher = new QFutureWatcher<replay::ReplayReport>(this);
  connect(watcher, &QFutureWatcher<replay::ReplayReport>::finished, this,
          [this, watcher, currentGeneration] {
            const auto report = watcher->result();
            watcher->deleteLater();
            if (currentGeneration != generation_) {
              return;
            }
            busy_ = false;
            if (report.status == replay::ReplayStatus::exact_match) {
              replay_state_ = replayLabel(report.status);
              clearError();
              status_ = "replay_completed";
              emit changed();
              return;
            }
            const auto code = report.diagnostic.has_value()
                                  ? text(report.diagnostic->code)
                                  : QStringLiteral("replay_failed");
            const auto message = report.diagnostic.has_value()
                                     ? text(report.diagnostic->message)
                                     : QStringLiteral("Replay failed.");
            if (code == "operation_cancelled") {
              setError(message, code, "cancelled");
              return;
            }
            replay_state_ = replayLabel(report.status);
            setError(message, code, "replay_failed");
          });
  watcher->setFuture(QtConcurrent::run([projectPath, manifestHash, stopToken] {
    return replay::replay_exact(
        projectPath, manifestHash,
        run_store::TransactionOptions{
            run_store::maximum_lock_wait, stopToken, {}});
  }));
}

void ExecutionController::loadActiveBinding() {
  active_package_.clear();
  active_package_reference_.reset();
  if (!project_->project().has_value()) {
    return;
  }
  const auto &bindings = project_->project()->execution.package_bindings;
  auto selected = bindings.rend();
  if (!pending_cad_entity_id_.isEmpty()) {
    selected = std::find_if(
        bindings.rbegin(), bindings.rend(), [this](const auto &candidate) {
          return candidate.cad_entity_id == text(pending_cad_entity_id_);
        });
  } else if (!bindings.empty()) {
    selected = bindings.rbegin();
    pending_cad_entity_id_ = text(selected->cad_entity_id);
  }
  if (selected == bindings.rend()) {
    return;
  }
  active_package_reference_ = selected->package;
  const auto stored =
      run_store::read_object(project_->projectPath(), selected->package);
  if (!stored.has_value()) {
    active_package_ = {
        {"package_hash", text(selected->package.object_hash)},
        {"execution_readiness", "unavailable"},
        {"error_code", storeDiagnosticCode(stored.diagnostic())},
        {"error", storeDiagnosticMessage(stored.diagnostic())},
    };
    return;
  }
  const auto inspection = execution::inspect_execution_component(
      stored.value(), selected->package.object_hash);
  if (!inspection.has_value()) {
    active_package_ = {
        {"package_hash", text(selected->package.object_hash)},
        {"execution_readiness", "unavailable"},
        {"error_code", executionDiagnosticCode(inspection.diagnostic())},
        {"error", executionDiagnosticMessage(inspection.diagnostic())},
    };
    return;
  }
  active_package_ = packageMap(inspection.value());
}

void ExecutionController::loadCurrentScenario() {
  scenario_draft_.clear();
  scenario_preview_.clear();
  typed_preview_.reset();
  clearScenarioConfirmation();
  scenario_load_error_.clear();
  scenario_load_error_code_.clear();
  if (!project_->project().has_value() ||
      !project_->project()->execution.current_scenario.has_value()) {
    return;
  }
  const auto &reference = *project_->project()->execution.current_scenario;
  const auto stored =
      run_store::read_object(project_->projectPath(), reference);
  if (!stored.has_value()) {
    scenario_load_error_ = storeDiagnosticMessage(stored.diagnostic());
    scenario_load_error_code_ = storeDiagnosticCode(stored.diagnostic());
    return;
  }
  const auto parsed = execution::parse_motor_arm_scenario(stored.value());
  if (!parsed.has_value()) {
    scenario_load_error_ = executionDiagnosticMessage(parsed.diagnostic());
    scenario_load_error_code_ = executionDiagnosticCode(parsed.diagnostic());
    return;
  }
  scenario_draft_ = scenarioDraftMap(parsed.value());
  scenario_preview_ = scenarioPreviewMap(parsed.value());
  typed_preview_ = execution::ScenarioPreview{
      parsed.value().payload_mass_kg,       parsed.value().arm_radius_m,
      parsed.value().rotation_rad,          parsed.value().move_duration_s,
      parsed.value().hold_duration_s,       parsed.value().cycle_duration_s,
      parsed.value().ambient_temperature_c, parsed.value().motion_profile,
      parsed.value().confirmed_by_user};
  scenario_confirmed_ = parsed.value().confirmed_by_user;
  if (scenario_confirmed_) {
    confirmed_scenario_reference_ = reference;
    confirmed_scenario_hash_ = text(reference.object_hash);
  }
}

void ExecutionController::loadRecordedRuns() {
  run_history_.clear();
  recorded_runs_.clear();
  clearSelection();
  if (!project_->project().has_value()) {
    return;
  }
  for (const auto &manifest : project_->project()->execution.committed_runs) {
    // Structural project-run manifests are owned by StructuralController and
    // must not be misclassified as failed motor-backend replays.
    if (manifest.schema_id != "urn:prometheus:schema:run-manifest:1.0.0")
      continue;
    auto display = loadRecordedDisplay(project_->projectPath(), manifest);
    run_history_.append(display.history);
    recorded_runs_.push_back(RecordedRun{manifest, std::move(display.result)});
  }
}

void ExecutionController::reloadProject() {
  if (busy_) {
    stop_source_.request_stop();
    ++generation_;
    busy_ = false;
  }
  clearError();
  status_ = "project_loaded";
  loadActiveBinding();
  loadCurrentScenario();
  loadRecordedRuns();
  const auto packageLoadErrorCode =
      active_package_.value("error_code").toString();
  if (!packageLoadErrorCode.isEmpty()) {
    error_ = active_package_.value("error").toString();
    error_code_ = packageLoadErrorCode;
    status_ = "project_degraded";
  } else if (!scenario_load_error_code_.isEmpty()) {
    error_ = scenario_load_error_;
    error_code_ = scenario_load_error_code_;
    status_ = "project_degraded";
  }
  emit changed();
}
