#include "cad_controller.hpp"
#include "engineering_controller.hpp"
#include "project_controller.hpp"

#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/run_store/object_store.hpp>
#include <prometheus/run_store/project_v2.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

namespace integrity = prometheus::integrity;
namespace run_store = prometheus::run_store;

std::string utf8(const QString& value)
{
    const auto bytes = value.toUtf8();
    return std::string(bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
}

std::filesystem::path nativePath(const QString& value)
{
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(utf8(value));
#endif
}

[[noreturn]] void fail(const char* message)
{
    qCritical("FAILED: %s", message);
    std::exit(1);
}

void require(const bool condition, const char* message)
{
    if (!condition) {
        fail(message);
    }
}

void writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "test file could not be opened for writing");
    require(file.write(bytes) == bytes.size(),
        "test file write was incomplete");
    file.close();
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly),
        "test file could not be opened for reading");
    return file.readAll();
}

QString objectHash(const QByteArray& bytes)
{
    return QString::fromStdString(integrity::sha256_bytes(
        std::string_view(bytes.constData(),
            static_cast<std::size_t>(bytes.size()))));
}

run_store::ProjectV2 projectForCad(
    const QString& cadPath, const QString& hash)
{
    run_store::ProjectV2 project;
    project.name = "Motor arm";
    project.cad_source = utf8(cadPath);
    project.assembly_artifact_hash = utf8(hash);
    project.coordinate_system = "right-handed Z-up";
    project.length_unit = "m";
    project.engineering.geometry_status = "not_evaluated";
    return project;
}

run_store::StoredObjectReference manifestReference(const char digit)
{
    return run_store::StoredObjectReference{
        "sha256:" + std::string(64U, digit),
        1U,
        "application/vnd.prometheus.run-manifest+json;version=1.0.0",
        "urn:prometheus:schema:run-manifest:1.0.0",
        "1.0.0"};
}

run_store::ObjectToStore inventorySnapshot(const std::string &cadHash,
                                           const std::string &evidenceHash) {
    const auto bytes = integrity::canonicalize_json_bytes(
        std::string{"{\"$schema\":\""} +
        std::string(run_store::project_inventory_schema_id) +
        "\",\"artifacts\":[{\"analysis_state\":\"ready\","
        "\"byte_length\":10,\"category\":\"geometry\",\"detail\":\"STEP\","
        "\"relative_path\":\"assembly.step\",\"sha256\":\"" + cadHash +
        "\"},{\"analysis_state\":\"not_evaluated\",\"byte_length\":5,"
        "\"category\":\"document\",\"detail\":\"Not interpreted\","
        "\"relative_path\":\"docs/spec.pdf\",\"sha256\":\"" + evidenceHash +
        "\"}],\"root_label\":\"fixture\",\"schema_version\":\"1.0.0\","
        "\"snapshot_kind\":\"accounted_project_folder\"}");
    return {{integrity::sha256_bytes(bytes), bytes.size(),
             std::string(run_store::project_inventory_media_type),
             std::string(run_store::project_inventory_schema_id), "1.0.0"},
            bytes};
}

void replaceProject(
    const QString& path, const run_store::ProjectV2& project)
{
    const auto serialized = run_store::serialize_project_v2(project);
    require(serialized.has_value(), "test project did not serialize");
    writeBytes(path,
        QByteArray(serialized.value().data(),
            static_cast<qsizetype>(serialized.value().size())));
}

QVariantMap cadSnapshot(const QString& cadPath, const QString& hash)
{
    return {
        {"name", QStringLiteral("Motor arm")},
        {"cad_source", cadPath},
        {"resolved_cad_source", cadPath},
        {"assembly_artifact_hash", hash},
        {"coordinate_system", QStringLiteral("right-handed Z-up")},
        {"length_unit", QStringLiteral("m")},
        {"component_bindings", QVariantList{}},
        {"placement_overrides", QVariantList{}},
        {"connections", QVariantList{}},
        {"interference_classifications", QVariantList{}},
    };
}

QByteArray legacyProjectBytes(
    const QString& cadPath, const bool includeMotorState = true)
{
    QJsonArray findings;
    findings.append(QJsonObject{
        {"status", "information"},
        {"severity", "information"},
        {"title", "Intentional solid engagement"},
        {"mechanism", "Motor and base share volume."},
        {"calculated", 0.000001},
        {"unit", "m³"},
        {"available", 0.0},
        {"margin_fraction", -1.0},
        {"evidence", "Imported STEP B-Rep geometry"},
        {"assumption", "Reviewed intended fit"},
        {"estimated_range", ""},
        {"first_id", "motor"},
        {"second_id", "base"},
    });
    if (includeMotorState) {
        findings.append(QJsonObject{
            {"status", "fail"},
            {"severity", "critical"},
            {"title", "Continuous horizontal holding torque"},
            {"mechanism", "Historical fixed motor finding"},
            {"calculated", 0.224152},
            {"unit", "N·m"},
            {"available", 0.208},
            {"margin_fraction", -0.07765},
            {"evidence", "Historical fixture"},
            {"assumption", "Historical fixed component"},
            {"estimated_range", ""},
        });
    }
    const QJsonObject engineering{
        {"joint", QJsonObject{{"type", "revolute"},
                      {"source_index", 1},
                      {"target_index", 2},
                      {"axis", "Z"},
                      {"minimum_deg", 0.0},
                      {"maximum_deg", 90.0},
                      {"pivot_x", 0.0},
                      {"pivot_y", 0.0},
                      {"pivot_z", 0.0},
                      {"confirmed_by_user", true}}},
        {"scenario", QJsonObject{{"payload_kg", 8.0}, {"arm_m", 0.2}}},
        {"findings", findings},
        {"run_status", "Completed — 2 findings"},
    };
    const QJsonObject root{
        {"schema_version", "1.0.0"},
        {"name", "Motor arm"},
        {"cad_source", cadPath},
        {"coordinate_system", "right-handed Z-up"},
        {"length_unit", "m"},
        {"component_bindings",
            QJsonArray{QJsonObject{{"cad_entity_id", "motor"},
                {"revision_id", "revision-123"},
                {"label", "Fixture Works PM-36"}}}},
        {"placement_overrides",
            QJsonArray{QJsonObject{{"cad_entity_id", "arm"},
                {"translation_x_m", 0.01},
                {"translation_y_m", 0.0},
                {"translation_z_m", 0.0},
                {"rotation_x_deg", 0.0},
                {"rotation_y_deg", 0.0},
                {"rotation_z_deg", 30.0},
                {"rotation_convention",
                    "extrinsic_X_then_Y_then_Z_about_imported_bounds_center"}}}},
        {"connections",
            QJsonArray{QJsonObject{{"id", "arm:x_min->base:x_max"},
                {"source_part", "arm"},
                {"source_name", "Arm"},
                {"source_anchor", "x_min"},
                {"target_part", "base"},
                {"target_name", "Base"},
                {"target_anchor", "x_max"},
                {"connection_type", "fixed"},
                {"confirmed_by_user", true},
                {"anchor_origin", "derived_bounds"},
                {"semantic_status", "provisional_geometry_anchor"}}}},
        {"interference_classifications",
            QJsonArray{QJsonObject{{"first_id", "motor"},
                {"second_id", "base"},
                {"classification", "intended_engagement"}}}},
        {"engineering", engineering},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

void unsaved_and_legacy_require_explicit_save_as()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory unavailable");
    const auto cadPath = temporary.filePath(QString::fromUtf8("机械臂.step"));
    const QByteArray cadBytes("synthetic CAD bytes");
    writeBytes(cadPath, cadBytes);
    const auto hash = objectHash(cadBytes);

    CadController cad;
    EngineeringController engineering;
    ProjectController project(&cad, &engineering);
    cad.restoreCadState(cadSnapshot(cadPath, hash));

    require(project.saveAsRequired(),
        "unsaved project requires Save As before execution mutation");
    require(!project.ensureExecutionWritable(),
        "unsaved execution mutation is rejected");
    require(project.errorCode() == "save_as_required",
        "unsaved mutation reports save_as_required");

    const auto unsavedDestination =
        temporary.filePath(QString::fromUtf8("未保存.prometheus"));
    project.saveAsVersion2(QUrl::fromLocalFile(unsavedDestination));
    require(project.schemaVersion() == "2.0.0"
            && !project.saveAsRequired(),
        "explicit Save As creates the v2 project");
    require(QFileInfo(unsavedDestination).isFile(),
        "v2 project remains a regular file");
    require(QFileInfo(unsavedDestination + ".data").isDir(),
        "v2 execution store is the derived sibling");
    require(project.ensureExecutionWritable(),
        "saved v2 project permits execution mutation");

    const auto legacyPath = temporary.filePath("legacy.prometheus");
    const auto legacyBytes = legacyProjectBytes(QFileInfo(cadPath).fileName());
    writeBytes(legacyPath, legacyBytes);
    project.openProject(QUrl::fromLocalFile(legacyPath));
    require(project.schemaVersion() == "1.0.0"
            && project.saveAsRequired(),
        "opened v1 project remains read-only and requires Save As");
    require(project.errorCode().isEmpty()
            && !project.assemblyArtifactCurrent(),
        "legacy open defers assembly hashing to Save As without false tamper");
    require(project.committedRunCount() == 0,
        "legacy motor result is not presented as a recorded run");

    project.saveAsVersion2(QUrl::fromLocalFile(legacyPath));
    require(project.errorCode() == "save_as_destination_same_as_source",
        "v1 Save As rejects the source path");
    require(readBytes(legacyPath) == legacyBytes,
        "rejected Save As leaves every v1 source byte unchanged");

    const auto convertedDirectory = temporary.filePath("converted");
    require(QDir().mkpath(convertedDirectory),
        "conversion destination directory creates");
    const auto convertedPath = QDir(convertedDirectory).filePath(
        QString::fromUtf8("已转换.prometheus"));
    project.saveAsVersion2(QUrl::fromLocalFile(convertedPath));
    require(readBytes(legacyPath) == legacyBytes,
        "successful conversion leaves every v1 source byte unchanged");
    project.saveCurrentProject();
    require(project.errorCode().isEmpty(),
        "converted project remains writable after moving directories");
    const auto converted = run_store::open_read_only(nativePath(convertedPath));
    require(converted.has_value(), "converted v2 project reopens");
    require(converted.value().name == "Motor arm",
        "converted v2 retains project identity independently of CAD name");
    require(converted.value().legacy_v1_engineering_state.has_value(),
        "converted v2 retains legacy engineering state separately");
    require(converted.value().engineering.geometry_findings.size() == 1U,
        "converted v2 authoritative state contains geometry only");
    require(converted.value().cad_source == utf8(cadPath),
        "converted v2 retains the external CAD path");
    require(converted.value().component_bindings.size() == 1U
            && converted.value().component_bindings.front().cad_entity_id
                == "motor",
        "converted v2 retains stable CAD entity bindings");
    require(converted.value().placement_overrides.size() == 1U
            && converted.value().placement_overrides.front().cad_entity_id
                == "arm",
        "converted v2 retains placement state");
    require(converted.value().connections.size() == 1U
            && converted.value().connections.front().id
                == "arm:x_min->base:x_max",
        "converted v2 retains semantic connections");
    require(converted.value().interference_classifications.size() == 1U
            && converted.value().interference_classifications.front()
                   .classification
                == "intended_engagement",
        "converted v2 retains interference classifications");
    require(converted.value().engineering.joint.has_value()
            && converted.value().engineering.joint->axis == "Z",
        "converted v2 retains the reviewed joint");
    require(converted.value().execution.committed_runs.empty(),
        "converted v2 contains no fabricated legacy run");
}

void open_degrades_without_cad_or_sidecar()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory unavailable");
    const auto cadPath = temporary.filePath("arm.step");
    const QByteArray cadBytes("stable CAD bytes");
    writeBytes(cadPath, cadBytes);
    const auto hash = objectHash(cadBytes);

    const auto projectPath = temporary.filePath("missing-cad.prometheus");
    auto missingCad = projectForCad(
        temporary.filePath("absent.step"), hash);
    require(run_store::create_project_v2(nativePath(projectPath), missingCad)
                .has_value(),
        "missing-CAD project fixture creates");
    missingCad.execution.committed_runs.push_back(manifestReference('c'));
    replaceProject(projectPath, missingCad);
    CadController cad;
    EngineeringController engineering;
    ProjectController controller(&cad, &engineering);
    controller.openProject(QUrl::fromLocalFile(projectPath));
    require(controller.schemaVersion() == "2.0.0",
        "metadata opens when external CAD is missing");
    require(!controller.cadAvailable()
            && controller.errorCode() == "cad_missing",
        "missing CAD has a typed nonfatal state");
    require(controller.committedRunCount() == 1,
        "missing CAD does not hide recorded execution history");

    const auto noStorePath = temporary.filePath("missing-store.prometheus");
    require(run_store::create_project_v2(
                nativePath(noStorePath), projectForCad(cadPath, hash))
                .has_value(),
        "missing-store project fixture creates");
    std::error_code error;
    std::filesystem::remove_all(
        run_store::sidecar_path_for_project(nativePath(noStorePath)), error);
    require(!error, "test sidecar removal succeeds");
    controller.openProject(QUrl::fromLocalFile(noStorePath));
    require(controller.schemaVersion() == "2.0.0"
            && controller.cadAvailable(),
        "CAD metadata remains usable when execution sidecar is missing");
    require(!controller.executionStoreAvailable()
            && controller.errorCode() == "execution_store_missing",
        "missing sidecar has a typed nonfatal state");

    const auto malformedPath = temporary.filePath("malformed.prometheus");
    writeBytes(malformedPath, "{}");
    CadController malformedCad;
    EngineeringController malformedEngineering;
    ProjectController malformed(&malformedCad, &malformedEngineering);
    malformed.openProject(QUrl::fromLocalFile(malformedPath));
    require(malformed.errorCode() == "missing_field",
        "malformed index diagnostics take precedence over a missing sidecar");
}

void changed_cad_blocks_execution_and_history_remains_visible()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory unavailable");
    const auto cadPath = temporary.filePath("arm.step");
    const QByteArray original("original CAD bytes");
    writeBytes(cadPath, original);
    const auto hash = objectHash(original);
    const auto projectPath = temporary.filePath("history.prometheus");
    auto stored = projectForCad(cadPath, hash);
    require(run_store::create_project_v2(nativePath(projectPath), stored)
                .has_value(),
        "history project fixture creates");
    stored.execution.committed_runs.push_back(manifestReference('a'));
    replaceProject(projectPath, stored);

    CadController cad;
    EngineeringController engineering;
    ProjectController controller(&cad, &engineering);
    controller.openProject(QUrl::fromLocalFile(projectPath));
    require(controller.committedRunCount() == 1,
        "recorded history is visible before CAD recheck");
    writeBytes(cadPath, "tampered CAD bytes");
    require(!controller.verifyAssemblyArtifactCurrent(),
        "changed external CAD blocks a new execution mutation");
    require(controller.errorCode() == "assembly_artifact_changed",
        "changed external CAD has a typed diagnostic");
    require(controller.committedRunCount() == 1,
        "changed external CAD does not hide recorded history");
}

void current_save_preserves_newer_execution_references()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory unavailable");
    const auto cadPath = temporary.filePath("arm.step");
    const QByteArray cadBytes("stable CAD bytes");
    writeBytes(cadPath, cadBytes);
    const auto projectPath = temporary.filePath("concurrent.prometheus");
    auto stored = projectForCad(cadPath, objectHash(cadBytes));
    require(run_store::create_project_v2(nativePath(projectPath), stored)
                .has_value(),
        "concurrent-save project fixture creates");
    stored.execution.committed_runs.push_back(manifestReference('a'));
    replaceProject(projectPath, stored);

    CadController cad;
    EngineeringController engineering;
    ProjectController controller(&cad, &engineering);
    controller.openProject(QUrl::fromLocalFile(projectPath));
    require(controller.committedRunCount() == 1,
        "controller loads first run reference");

    stored.execution.committed_runs.push_back(manifestReference('b'));
    replaceProject(projectPath, stored);
    controller.saveCurrentProject();
    const auto reopened = run_store::open_read_only(nativePath(projectPath));
    require(reopened.has_value()
            && reopened.value().execution.committed_runs.size() == 2U,
        "CAD-only current save preserves newer run references under lock");
    require(controller.committedRunCount() == 2,
        "controller refreshes to the locked saved project");
}

void inventory_rescan_invalidates_only_changed_dependencies()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "inventory comparison root exists");
    const auto cadPath = temporary.filePath("assembly.step");
    const QByteArray cadBytes("CAD source");
    writeBytes(cadPath, cadBytes);
    const auto cadHash = utf8(objectHash(cadBytes));
    const auto projectPath = temporary.filePath("inventory.prometheus");
    require(run_store::create_project_v2(
                nativePath(projectPath), projectForCad(cadPath, QString::fromStdString(cadHash)))
                .has_value(),
        "inventory comparison project creates");
    CadController cad;
    EngineeringController engineering;
    ProjectController controller(&cad, &engineering);
    controller.openProject(QUrl::fromLocalFile(projectPath));

    const auto first = inventorySnapshot(cadHash, "sha256:" + std::string(64U, '1'));
    require(controller.assessInventorySnapshot(first, "assembly.step", true),
        "initial inventory anchors");
    const auto evidenceChanged =
        inventorySnapshot(cadHash, "sha256:" + std::string(64U, '2'));
    require(controller.assessInventorySnapshot(
                evidenceChanged, "assembly.step", true)
            && controller.inventoryChanges().size() == 1
            && !controller.inventoryChanges().front().toMap()
                    .value("affects_current_assembly").toBool()
            && controller.assemblyArtifactCurrent()
            && controller.committedRunCount() == 0
            && controller.inventorySnapshotCount() == 2,
        "non-CAD evidence change is recorded without revoking structural source");

    int invalidated = 0;
    QObject::connect(&controller,
        &ProjectController::assemblyArtifactInvalidated,
        &controller, [&invalidated] { ++invalidated; });
    const auto cadChanged = inventorySnapshot(
        "sha256:" + std::string(64U, '3'), "sha256:" + std::string(64U, '2'));
    require(!controller.assessInventorySnapshot(
                cadChanged, "assembly.step", false)
            && invalidated == 1
            && !controller.assemblyArtifactCurrent()
            && controller.errorCode() == "assembly_artifact_changed"
            && controller.inventorySnapshotCount() == 2,
        "CAD identity change revokes current source without anchoring it as valid");
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    unsaved_and_legacy_require_explicit_save_as();
    open_degrades_without_cad_or_sidecar();
    changed_cad_blocks_execution_and_history_remains_visible();
    current_save_preserves_newer_execution_references();
    inventory_rescan_invalidates_only_changed_dependencies();
    return 0;
}
