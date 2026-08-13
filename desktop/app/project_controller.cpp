#include "project_controller.hpp"

#include "cad_controller.hpp"
#include "engineering_controller.hpp"

#include <prometheus/run_store/legacy_project_v1.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace {

namespace run_store = prometheus::run_store;

QString text(const std::string &value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string text(const QString &value) {
  const auto utf8 = value.toUtf8();
  return std::string(utf8.constData(),
                     static_cast<std::size_t>(utf8.size()));
}

std::filesystem::path nativePath(const QString &value) {
#ifdef _WIN32
  return std::filesystem::path(value.toStdWString());
#else
  return std::filesystem::path(text(value));
#endif
}

QString diagnosticMessage(const run_store::Diagnostic &diagnostic) {
  return text(diagnostic.message);
}

QString diagnosticCode(const run_store::Diagnostic &diagnostic) {
  return text(diagnostic.code);
}

QString localPath(const QUrl &value) {
  if (!value.isValid() || !value.isLocalFile()) {
    return {};
  }
  const QFileInfo information(value.toLocalFile());
  return information.absoluteFilePath();
}

QString resolvedCadPath(const QString &projectPath,
                        const std::string &storedPath) {
  const QFileInfo stored(text(storedPath));
  if (stored.isAbsolute()) {
    return stored.absoluteFilePath();
  }
  return QFileInfo(QFileInfo(projectPath).absoluteDir(), text(storedPath))
      .absoluteFilePath();
}

std::optional<std::string> optionalString(const QVariant &value) {
  if (!value.isValid() || value.isNull()) {
    return std::nullopt;
  }
  const auto result = value.toString();
  return result.isEmpty() ? std::nullopt
                          : std::optional<std::string>(text(result));
}

QVariant optionalValue(const std::optional<std::string> &value) {
  return value.has_value() ? QVariant(text(*value)) : QVariant{};
}

QVariantMap cadState(const run_store::ProjectV2 &project,
                     const QString &projectPath) {
  QVariantList bindings;
  for (const auto &value : project.component_bindings) {
    bindings.append(QVariantMap{
        {"cad_entity_id", text(value.cad_entity_id)},
        {"revision_id", text(value.revision_id)},
        {"label", text(value.label)},
    });
  }
  QVariantList placements;
  for (const auto &value : project.placement_overrides) {
    placements.append(QVariantMap{
        {"cad_entity_id", text(value.cad_entity_id)},
        {"translation_x_m", value.translation_x_m},
        {"translation_y_m", value.translation_y_m},
        {"translation_z_m", value.translation_z_m},
        {"rotation_x_deg", value.rotation_x_deg},
        {"rotation_y_deg", value.rotation_y_deg},
        {"rotation_z_deg", value.rotation_z_deg},
        {"rotation_convention", text(value.rotation_convention)},
    });
  }
  QVariantList connections;
  for (const auto &value : project.connections) {
    connections.append(QVariantMap{
        {"id", text(value.id)},
        {"source_part", text(value.source_part)},
        {"source_name", text(value.source_name)},
        {"source_anchor", text(value.source_anchor)},
        {"target_part", text(value.target_part)},
        {"target_name", text(value.target_name)},
        {"target_anchor", text(value.target_anchor)},
        {"connection_type", text(value.connection_type)},
        {"confirmed_by_user", value.confirmed_by_user},
        {"anchor_origin", text(value.anchor_origin)},
        {"semantic_status", text(value.semantic_status)},
    });
  }
  QVariantList classifications;
  for (const auto &value : project.interference_classifications) {
    classifications.append(QVariantMap{
        {"first_id", text(value.first_id)},
        {"second_id", text(value.second_id)},
        {"classification", text(value.classification)},
    });
  }
  return {
      {"name", text(project.name)},
      {"cad_source", text(project.cad_source)},
      {"resolved_cad_source", resolvedCadPath(projectPath, project.cad_source)},
      {"assembly_artifact_hash", text(project.assembly_artifact_hash)},
      {"coordinate_system", text(project.coordinate_system)},
      {"length_unit", text(project.length_unit)},
      {"component_bindings", bindings},
      {"placement_overrides", placements},
      {"connections", connections},
      {"interference_classifications", classifications},
  };
}

QVariantMap engineeringState(const run_store::EngineeringState &state) {
  QVariant joint;
  if (state.joint.has_value()) {
    const auto &value = *state.joint;
    joint = QVariantMap{
        {"type", text(value.type)},
        {"source_index", static_cast<qulonglong>(value.source_index)},
        {"target_index", static_cast<qulonglong>(value.target_index)},
        {"axis", text(value.axis)},
        {"minimum_deg", value.minimum_deg},
        {"maximum_deg", value.maximum_deg},
        {"pivot_x", value.pivot_x},
        {"pivot_y", value.pivot_y},
        {"pivot_z", value.pivot_z},
        {"confirmed_by_user", value.confirmed_by_user},
    };
  }
  QVariantList findings;
  for (const auto &value : state.geometry_findings) {
    findings.append(QVariantMap{
        {"finding_kind", text(value.finding_kind)},
        {"status", text(value.status)},
        {"severity", text(value.severity)},
        {"title", text(value.title)},
        {"mechanism", text(value.mechanism)},
        {"calculated", value.calculated},
        {"unit", text(value.unit)},
        {"available", value.available},
        {"margin_fraction", value.margin_fraction},
        {"evidence", text(value.evidence)},
        {"assumption", text(value.assumption)},
        {"estimated_range", text(value.estimated_range)},
        {"first_id", optionalValue(value.first_id)},
        {"second_id", optionalValue(value.second_id)},
    });
  }
  return {
      {"joint", joint},
      {"geometry_findings", findings},
      {"geometry_status", text(state.geometry_status)},
  };
}

run_store::EngineeringState typedEngineering(const QVariantMap &state) {
  run_store::EngineeringState result;
  const auto joint = state.value("joint").toMap();
  if (!joint.isEmpty()) {
    result.joint = run_store::RevoluteJoint{
        text(joint.value("type").toString()),
        joint.value("source_index").toULongLong(),
        joint.value("target_index").toULongLong(),
        text(joint.value("axis").toString()),
        joint.value("minimum_deg").toDouble(),
        joint.value("maximum_deg").toDouble(),
        joint.value("pivot_x").toDouble(),
        joint.value("pivot_y").toDouble(),
        joint.value("pivot_z").toDouble(),
        joint.value("confirmed_by_user").toBool(),
    };
  }
  for (const auto &item : state.value("geometry_findings").toList()) {
    const auto value = item.toMap();
    result.geometry_findings.push_back(run_store::GeometryFinding{
        text(value.value("finding_kind").toString()),
        text(value.value("status").toString()),
        text(value.value("severity").toString()),
        text(value.value("title").toString()),
        text(value.value("mechanism").toString()),
        value.value("calculated").toDouble(),
        text(value.value("unit").toString()),
        value.value("available").toDouble(),
        value.value("margin_fraction").toDouble(),
        text(value.value("evidence").toString()),
        text(value.value("assumption").toString()),
        text(value.value("estimated_range").toString()),
        optionalString(value.value("first_id")),
        optionalString(value.value("second_id")),
    });
  }
  result.geometry_status =
      text(state.value("geometry_status", "not_evaluated").toString());
  return result;
}

void applyCadSnapshot(run_store::ProjectV2 &project,
                      const QVariantMap &snapshot) {
  project.cad_source = text(snapshot.value("cad_source").toString());
  project.assembly_artifact_hash =
      text(snapshot.value("assembly_artifact_hash").toString());
  project.coordinate_system =
      text(snapshot.value("coordinate_system").toString());
  project.length_unit = text(snapshot.value("length_unit").toString());
  project.component_bindings.clear();
  for (const auto &item : snapshot.value("component_bindings").toList()) {
    const auto value = item.toMap();
    project.component_bindings.push_back(run_store::ComponentBinding{
        text(value.value("cad_entity_id").toString()),
        text(value.value("revision_id").toString()),
        text(value.value("label").toString()),
    });
  }
  project.placement_overrides.clear();
  for (const auto &item : snapshot.value("placement_overrides").toList()) {
    const auto value = item.toMap();
    project.placement_overrides.push_back(run_store::PlacementOverride{
        text(value.value("cad_entity_id").toString()),
        value.value("translation_x_m").toDouble(),
        value.value("translation_y_m").toDouble(),
        value.value("translation_z_m").toDouble(),
        value.value("rotation_x_deg").toDouble(),
        value.value("rotation_y_deg").toDouble(),
        value.value("rotation_z_deg").toDouble(),
        text(value.value("rotation_convention").toString()),
    });
  }
  project.connections.clear();
  for (const auto &item : snapshot.value("connections").toList()) {
    const auto value = item.toMap();
    project.connections.push_back(run_store::Connection{
        text(value.value("id").toString()),
        text(value.value("source_part").toString()),
        text(value.value("source_name").toString()),
        text(value.value("source_anchor").toString()),
        text(value.value("target_part").toString()),
        text(value.value("target_name").toString()),
        text(value.value("target_anchor").toString()),
        text(value.value("connection_type").toString()),
        value.value("confirmed_by_user").toBool(),
        text(value.value("anchor_origin").toString()),
        text(value.value("semantic_status").toString()),
    });
  }
  project.interference_classifications.clear();
  for (const auto &item :
       snapshot.value("interference_classifications").toList()) {
    const auto value = item.toMap();
    project.interference_classifications.push_back(
        run_store::InterferenceClassification{
            text(value.value("first_id").toString()),
            text(value.value("second_id").toString()),
            text(value.value("classification").toString()),
        });
  }
}

run_store::ProjectV2 initialProject() {
  run_store::ProjectV2 result;
  result.coordinate_system = "right-handed Z-up";
  result.length_unit = "m";
  result.engineering.geometry_status = "not_evaluated";
  return result;
}

} // namespace

ProjectController::ProjectController(CadController *cad,
                                     EngineeringController *engineering,
                                     QObject *parent)
    : QObject(parent), cad_(cad), engineering_(engineering) {
  Q_ASSERT(cad_ != nullptr);
  Q_ASSERT(engineering_ != nullptr);
}

int ProjectController::committedRunCount() const {
  return project_.has_value()
             ? static_cast<int>(project_->execution.committed_runs.size())
             : 0;
}

QVariantMap ProjectController::legacyEngineeringState() const {
  if (!project_.has_value() ||
      !project_->legacy_v1_engineering_state.has_value()) {
    return {};
  }
  const auto bytes =
      QByteArray::fromStdString(*project_->legacy_v1_engineering_state);
  return QJsonDocument::fromJson(bytes).object().toVariantMap();
}

std::filesystem::path ProjectController::projectPath() const {
  return nativePath(current_project_path_);
}

bool ProjectController::hasCadEntityId(const QString &entityId) const {
  const auto candidate = entityId.trimmed();
  if (candidate.isEmpty()) {
    return false;
  }
  const auto parts = cad_->parts();
  for (const auto &partValue : parts) {
    const auto *part = qobject_cast<CadPart *>(partValue.value<QObject *>());
    if (part != nullptr && part->persistentId() == candidate) {
      return true;
    }
  }
  // A no-OCCT build cannot reconstruct the part list, so it relies on the
  // reviewed stable entity binding stored in the versioned project. When a
  // part list is available, unresolved/retired bindings are not accepted.
  if (!parts.isEmpty() || !project_.has_value()) {
    return false;
  }
  const auto id = text(candidate);
  for (const auto &binding : project_->component_bindings) {
    if (binding.cad_entity_id == id) {
      return true;
    }
  }
  return false;
}

void ProjectController::clearError() {
  error_.clear();
  error_code_.clear();
}

void ProjectController::setError(QString message, QString code) {
  error_ = std::move(message);
  error_code_ = std::move(code);
  emit changed();
}

void ProjectController::acceptProject(run_store::ProjectV2 project) {
  project_ = std::move(project);
  emit changed();
}

void ProjectController::restoreProject(const run_store::ProjectV2 &project,
                                       const QString &projectPath,
                                       const QString &degradedStoreCode) {
  project_ = project;
  current_project_path_ = projectPath;
  const auto state = cadState(project, projectPath);
  const auto resolved = state.value("resolved_cad_source").toString();
  const QFileInfo cadInformation(resolved);
  cad_available_ = cadInformation.exists() && cadInformation.isFile() &&
                   !cadInformation.isSymbolicLink();
  assembly_artifact_current_ = false;
  engineering_->restoreGeometryState(engineeringState(project.engineering));
  cad_->restoreCadState(state);
  const bool has_captured_assembly_hash = schema_version_ == "2.0.0";
  if (cad_available_ && has_captured_assembly_hash) {
    assembly_artifact_current_ =
        cad_->recheckAssemblyArtifact(text(project.assembly_artifact_hash));
  }
  clearError();
  if (!cad_available_) {
    error_ = "The project opened, but its referenced CAD artifact is missing "
             "or unsafe.";
    error_code_ = "cad_missing";
  } else if (has_captured_assembly_hash && !assembly_artifact_current_) {
    error_ = "The project opened, but the external CAD bytes no longer match "
             "the captured assembly hash.";
    error_code_ = "assembly_artifact_changed";
  } else if (!degradedStoreCode.isEmpty()) {
    error_ = "The project index and CAD opened, but its execution sidecar is "
             "missing.";
    error_code_ = degradedStoreCode;
  }
  emit changed();
  emit projectOpened();
}

void ProjectController::openProject(const QUrl &path) {
  const auto target = localPath(path);
  if (target.isEmpty()) {
    setError("Choose a local project file.", "invalid_project_url");
    return;
  }

  auto opened = run_store::open_read_only(nativePath(target));
  std::optional<run_store::Diagnostic> degraded_index_failure;
  if (opened.has_value()) {
    source_v1_path_.clear();
    schema_version_ = "2.0.0";
    save_as_required_ = false;
    execution_store_available_ = true;
    restoreProject(opened.value(), target);
    return;
  }
  if (opened.diagnostic().code == "execution_store_missing") {
    const auto degraded =
        run_store::open_project_index_read_only(nativePath(target));
    if (degraded.has_value()) {
      source_v1_path_.clear();
      schema_version_ = "2.0.0";
      save_as_required_ = false;
      execution_store_available_ = false;
      restoreProject(degraded.value(), target, "execution_store_missing");
      return;
    }
    degraded_index_failure = degraded.diagnostic();
  }

  const auto legacy = run_store::open_legacy_project_v1(nativePath(target));
  if (!legacy.has_value()) {
    const auto &failure = degraded_index_failure.has_value()
                              ? *degraded_index_failure
                              : opened.diagnostic();
    setError(diagnosticMessage(failure), diagnosticCode(failure));
    return;
  }
  source_v1_path_ = target;
  schema_version_ = "1.0.0";
  save_as_required_ = true;
  execution_store_available_ = false;
  restoreProject(legacy.value().project, target);
}

void ProjectController::saveAsVersion2(const QUrl &destination) {
  const auto target = localPath(destination);
  if (target.isEmpty()) {
    setError("Choose a local Save As destination.", "invalid_project_url");
    return;
  }
  if (!source_v1_path_.isEmpty() &&
      QFileInfo(target).absoluteFilePath() ==
          QFileInfo(source_v1_path_).absoluteFilePath()) {
    setError("Save As from a v1 project requires a different destination.",
             "save_as_destination_same_as_source");
    return;
  }
  if (!save_as_required_) {
    setError("This v2 project already has a writable project path.",
             "save_as_not_required");
    return;
  }

  auto candidate = project_.value_or(initialProject());
  auto snapshot = cad_->snapshotCadState();
  const auto capturedHash = cad_->captureAssemblyArtifactHash();
  if (capturedHash.isEmpty()) {
    setError("Save As requires an available regular CAD artifact.",
             "cad_missing");
    return;
  }
  snapshot["assembly_artifact_hash"] = capturedHash;
  if (!source_v1_path_.isEmpty()) {
    snapshot["cad_source"] = snapshot.value("resolved_cad_source");
  }
  if (candidate.name.empty()) {
    auto default_name = snapshot.value("name").toString().trimmed();
    if (default_name.isEmpty()) {
      default_name = QFileInfo(target).completeBaseName();
    }
    candidate.name = text(default_name);
  }
  applyCadSnapshot(candidate, snapshot);
  candidate.engineering =
      typedEngineering(engineering_->snapshotGeometryState());
  const auto created =
      run_store::create_project_v2(nativePath(target), candidate);
  if (!created.has_value()) {
    setError(diagnosticMessage(created.diagnostic()),
             diagnosticCode(created.diagnostic()));
    return;
  }
  project_ = created.value();
  current_project_path_ = target;
  source_v1_path_.clear();
  schema_version_ = "2.0.0";
  save_as_required_ = false;
  cad_available_ = true;
  execution_store_available_ = true;
  assembly_artifact_current_ = true;
  clearError();
  emit changed();
  emit projectSaved();
}

void ProjectController::saveCurrentProject() {
  if (!ensureExecutionWritable() || !verifyAssemblyArtifactCurrent()) {
    return;
  }
  auto candidate = *project_;
  applyCadSnapshot(candidate, cad_->snapshotCadState());
  candidate.engineering =
      typedEngineering(engineering_->snapshotGeometryState());
  const auto saved = run_store::save_project_snapshot(
      nativePath(current_project_path_), candidate);
  if (!saved.has_value()) {
    setError(diagnosticMessage(saved.diagnostic()),
             diagnosticCode(saved.diagnostic()));
    return;
  }
  project_ = saved.value();
  clearError();
  emit changed();
  emit projectSaved();
}

bool ProjectController::ensureExecutionWritable() {
  if (save_as_required_ || schema_version_ != "2.0.0" ||
      current_project_path_.isEmpty()) {
    setError("Save this project explicitly as version 2 before changing "
             "execution state.",
             "save_as_required");
    return false;
  }
  if (!execution_store_available_) {
    setError("The execution sidecar is missing.", "execution_store_missing");
    return false;
  }
  return true;
}

bool ProjectController::verifyAssemblyArtifactCurrent() {
  if (!project_.has_value()) {
    setError("No project is open.", "project_missing");
    return false;
  }
  if (!cad_available_) {
    setError("The referenced CAD artifact is missing or unsafe.",
             "cad_missing");
    return false;
  }
  assembly_artifact_current_ =
      cad_->recheckAssemblyArtifact(text(project_->assembly_artifact_hash));
  if (!assembly_artifact_current_) {
    setError("The external CAD bytes changed after project capture.",
             "assembly_artifact_changed");
    return false;
  }
  clearError();
  emit changed();
  return true;
}
