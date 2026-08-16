#include "structural_setup_controller.hpp"

#include "prometheus/structural/structural_case.hpp"
#include "prometheus/structural/structural_request.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace ps = prometheus::structural;

namespace {

constexpr qint64 maximumCandidateBytes = 4 * 1024 * 1024;
constexpr qint64 maximumMeshBytes = 128 * 1024 * 1024;
constexpr qint64 maximumGeometryBytes = 512 * 1024 * 1024;

class ControllerFailure final : public std::runtime_error {
public:
  ControllerFailure(QString code, QString message)
      : std::runtime_error(message.toStdString()), code_(std::move(code)),
        message_(std::move(message)) {}

  const QString &code() const { return code_; }
  const QString &message() const { return message_; }

private:
  QString code_;
  QString message_;
};

[[noreturn]] void reject(QString code, QString message) {
  throw ControllerFailure(std::move(code), std::move(message));
}

fs::path filesystem_path(const QString &path) {
#ifdef Q_OS_WIN
  return fs::path(path.toStdWString());
#else
  return fs::path(path.toUtf8().constData());
#endif
}

QString qt_path(const fs::path &path) {
#ifdef Q_OS_WIN
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native().c_str());
#endif
}

bool strict_sha256(const QString &value) {
  if (value.size() != 71 || !value.startsWith("sha256:"))
    return false;
  for (const auto character : QStringView(value).sliced(7))
    if (!((character.unicode() >= '0' && character.unicode() <= '9') ||
          (character.unicode() >= 'a' && character.unicode() <= 'f')))
      return false;
  return true;
}

void require_bounded_regular_file(const QString &path,
                                  const qint64 maximumBytes) {
  const QFileInfo information(path);
  if (!information.exists() || !information.isFile() ||
      information.isSymbolicLink())
    reject("candidate_path_rejected",
           "Candidate artifacts must be regular non-symlink files.");
  if (information.size() < 0 || information.size() > maximumBytes)
    reject("candidate_too_large", "Candidate artifact exceeds its byte limit.");
}

QString stable_hash(const QString &path, const qint64 maximumBytes) {
  require_bounded_regular_file(path, maximumBytes);
  const auto first = QString::fromStdString(
      prometheus::integrity::sha256_file(filesystem_path(path)));
  require_bounded_regular_file(path, maximumBytes);
  const auto second = QString::fromStdString(
      prometheus::integrity::sha256_file(filesystem_path(path)));
  require_bounded_regular_file(path, maximumBytes);
  if (first != second)
    reject("candidate_identity_mismatch",
           "An artifact changed while its identity was verified.");
  return first;
}

QByteArray read_regular_file(const QString &path, const qint64 maximumBytes) {
  require_bounded_regular_file(path, maximumBytes);
  const QFileInfo information(path);
  QFile file(information.absoluteFilePath());
  if (!file.open(QIODevice::ReadOnly))
    reject("candidate_read_failed", "Candidate artifact could not be opened.");
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError || bytes.size() != information.size())
    reject("candidate_read_failed", "Candidate artifact could not be read exactly.");
  return bytes;
}

QJsonObject parse_json_object(const QByteArray &bytes, const QString &field) {
  std::string canonical;
  try {
    canonical = prometheus::integrity::canonicalize_json_bytes(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
  } catch (const std::exception &error) {
    reject("candidate_invalid",
           field + " is not strict JSON: " + QString::fromUtf8(error.what()));
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(
      QByteArray::fromStdString(canonical), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
    reject("candidate_invalid", field + " must contain one JSON object.");
  return document.object();
}

void require_keys(const QJsonObject &object,
                  const std::initializer_list<QString> expected,
                  const QString &field) {
  QSet<QString> expectedKeys;
  for (const auto &key : expected)
    expectedKeys.insert(key);
  const auto keys = object.keys();
  const QSet<QString> actualKeys(keys.begin(), keys.end());
  if (actualKeys != expectedKeys)
    reject("candidate_invalid", field + " has missing or unknown members.");
}

QString required_string(const QJsonObject &object, const QString &key,
                        const QString &field) {
  const auto value = object.value(key);
  const auto text = value.toString();
  const auto unsafe = std::ranges::any_of(text, [](const QChar character) {
    return character.unicode() < 0x20U || character.unicode() == 0x7fU;
  });
  if (!value.isString() || text.isEmpty() || text.toUtf8().size() > 4096 ||
      unsafe)
    reject("candidate_invalid", field + " must be a bounded nonempty string.");
  return text;
}

double required_number(const QJsonObject &object, const QString &key,
                       const QString &field) {
  const auto value = object.value(key);
  if (!value.isDouble() || !std::isfinite(value.toDouble()))
    reject("candidate_invalid", field + " must be a finite number.");
  return value.toDouble();
}

QString resolve_relative_artifact(const QString &manifestDirectory,
                                  const QString &relativePath) {
  const auto normalized = QDir::fromNativeSeparators(relativePath);
  if (normalized.isEmpty() || QDir::isAbsolutePath(normalized))
    reject("candidate_path_rejected",
           "Candidate artifact paths must be relative.");
  const auto parts = normalized.split('/', Qt::KeepEmptyParts);
  if (std::ranges::any_of(parts, [](const QString &part) {
        return part.isEmpty() || part == "." || part == "..";
      }))
    reject("candidate_path_rejected",
           "Candidate artifact path traversal is forbidden.");

  std::error_code error;
  const auto base = fs::canonical(filesystem_path(manifestDirectory), error);
  if (error)
    reject("candidate_path_rejected",
           "Candidate manifest directory could not be resolved.");
  auto current = base;
  for (const auto &part : parts) {
    current /= filesystem_path(part);
    const auto status = fs::symlink_status(current, error);
    if (error || fs::is_symlink(status))
      reject("candidate_path_rejected",
             "Candidate artifact paths cannot traverse symlinks.");
  }
  if (!fs::is_regular_file(current, error) || error)
    reject("candidate_path_rejected",
           "Candidate artifact is not a regular file.");
  const auto resolved = fs::canonical(current, error);
  if (error)
    reject("candidate_path_rejected",
           "Candidate artifact path could not be resolved.");
  const auto relative = fs::relative(resolved, base, error);
  if (error || relative.empty() || *relative.begin() == "..")
    reject("candidate_path_rejected",
           "Candidate artifact resolves outside its manifest directory.");
  return qt_path(resolved);
}

QVariantList vector_variant(const std::array<double, 3> &value) {
  return {value[0], value[1], value[2]};
}

QVariantMap surface_map(const ps::SurfaceGroup &group,
                        const QStringList &restraints,
                        const QStringList &loads) {
  const auto name = QString::fromStdString(group.name);
  return {{"name", name},
          {"triangle_count", static_cast<qlonglong>(group.triangles.size())},
          {"node_count", static_cast<qlonglong>(group.node_ids.size())},
          {"area_m2", group.area_m2},
          {"centroid_m", vector_variant(group.centroid_m)},
          {"normal_m", vector_variant(group.representative_normal)},
          {"normal_defined", group.representative_normal_defined},
          {"restrained", restraints.contains(name)},
          {"loaded", loads.contains(name)}};
}

bool positive_finite(const double value) {
  return std::isfinite(value) && value > 0.0;
}

bool valid_poisson(const double value) {
  return std::isfinite(value) && value > -1.0 && value < 0.5;
}

bool finite_direction(const std::array<double, 3> &direction) {
  if (!std::ranges::all_of(direction, [](const double value) {
        return std::isfinite(value);
      }))
    return false;
  return std::hypot(direction[0], direction[1], direction[2]) > 0.0;
}

} // namespace

StructuralDisplayGeometry::StructuralDisplayGeometry(
    const ps::VolumeMesh &mesh, const std::vector<std::string> &groups,
    QObject *parent)
    : QQuick3DGeometry(nullptr) {
  setParent(parent);
  std::map<int, const ps::Node *> nodeById;
  for (const auto &node : mesh.nodes)
    nodeById.emplace(node.id, &node);

  std::vector<const ps::SurfaceTriangle *> triangles;
  std::set<int> selectedNodeIds;
  for (const auto &name : groups) {
    const auto group =
        std::ranges::find(mesh.surface_groups, name, &ps::SurfaceGroup::name);
    if (group == mesh.surface_groups.end())
      throw std::invalid_argument("Visualization references an unknown group");
    for (const auto &triangle : group->triangles) {
      triangles.push_back(&triangle);
      selectedNodeIds.insert(triangle.node_ids.begin(), triangle.node_ids.end());
    }
  }
  std::ranges::sort(triangles, {}, &ps::SurfaceTriangle::id);

  std::map<int, std::uint32_t> packedIndex;
  std::vector<float> positions;
  positions.reserve(selectedNodeIds.size() * 3U);
  for (const int nodeId : selectedNodeIds) {
    const auto node = nodeById.find(nodeId);
    if (node == nodeById.end())
      throw std::invalid_argument("Visualization surface node is missing");
    packedIndex.emplace(nodeId,
                        static_cast<std::uint32_t>(packedIndex.size()));
    for (const auto coordinate : node->second->position_m) {
      if (!std::isfinite(coordinate) ||
          std::abs(coordinate) > std::numeric_limits<float>::max())
        throw std::invalid_argument(
            "Visualization coordinate is not representable");
      positions.push_back(static_cast<float>(coordinate));
    }
  }

  std::vector<std::uint32_t> indices;
  indices.reserve(triangles.size() * 3U);
  for (const auto *triangle : triangles)
    for (const int nodeId : triangle->node_ids)
      indices.push_back(packedIndex.at(nodeId));

  std::vector<QVector3D> normals(positions.size() / 3U, QVector3D{});
  for (std::size_t index = 0; index + 2U < indices.size(); index += 3U) {
    const auto first = indices[index];
    const auto second = indices[index + 1U];
    const auto third = indices[index + 2U];
    const QVector3D a(positions[first * 3U], positions[first * 3U + 1U],
                      positions[first * 3U + 2U]);
    const QVector3D b(positions[second * 3U], positions[second * 3U + 1U],
                      positions[second * 3U + 2U]);
    const QVector3D c(positions[third * 3U], positions[third * 3U + 1U],
                      positions[third * 3U + 2U]);
    const auto normal = QVector3D::crossProduct(b - a, c - a);
    normals[first] += normal;
    normals[second] += normal;
    normals[third] += normal;
  }

  std::vector<float> vertices;
  vertices.reserve(normals.size() * 6U);
  for (std::size_t index = 0; index < normals.size(); ++index) {
    const auto normal = normals[index].normalized();
    vertices.insert(vertices.end(),
                    {positions[index * 3U], positions[index * 3U + 1U],
                     positions[index * 3U + 2U], normal.x(), normal.y(),
                     normal.z()});
  }

  clear();
  setStride(6 * sizeof(float));
  setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
  addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
               QQuick3DGeometry::Attribute::F32Type);
  addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * sizeof(float),
               QQuick3DGeometry::Attribute::F32Type);
  addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
               QQuick3DGeometry::Attribute::U32Type);
  setVertexData(QByteArray(
      reinterpret_cast<const char *>(vertices.data()),
      static_cast<qsizetype>(vertices.size() * sizeof(float))));
  setIndexData(QByteArray(
      reinterpret_cast<const char *>(indices.data()),
      static_cast<qsizetype>(indices.size() * sizeof(std::uint32_t))));

  if (!positions.empty()) {
    QVector3D minimum(std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max());
    QVector3D maximum(std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest());
    for (std::size_t index = 0; index < positions.size(); index += 3U) {
      minimum.setX(std::min(minimum.x(), positions[index]));
      minimum.setY(std::min(minimum.y(), positions[index + 1U]));
      minimum.setZ(std::min(minimum.z(), positions[index + 2U]));
      maximum.setX(std::max(maximum.x(), positions[index]));
      maximum.setY(std::max(maximum.y(), positions[index + 1U]));
      maximum.setZ(std::max(maximum.z(), positions[index + 2U]));
    }
    setBounds(minimum, maximum);
  }
  update();
}

StructuralSetupController::StructuralSetupController(QObject *parent)
    : QObject(parent) {
  recompute();
}

QVariantList StructuralSetupController::surfaceGroups() const {
  QVariantList result;
  for (const auto &group : mesh_.surface_groups)
    result.append(surface_map(group, restraint_groups_, load_groups_));
  return result;
}

QVariantMap StructuralSetupController::activeSurfaceGroup() const {
  const auto found = std::ranges::find(
      mesh_.surface_groups, active_surface_group_.toStdString(),
      &ps::SurfaceGroup::name);
  if (found == mesh_.surface_groups.end())
    return {};
  return surface_map(*found, restraint_groups_, load_groups_);
}

QStringList StructuralSetupController::restraintGroups() const {
  return restraint_groups_;
}

QStringList StructuralSetupController::loadGroups() const {
  return load_groups_;
}

double StructuralSetupController::selectedLoadAreaM2() const {
  return compiled_setup_.has_value() ? compiled_setup_->selected_load_area_m2
                                     : 0.0;
}

QVariantList StructuralSetupController::compiledResultantN() const {
  return compiled_setup_.has_value()
             ? vector_variant(compiled_setup_->resultant_force_n)
             : QVariantList{};
}

void StructuralSetupController::resetCandidateState() {
  source_path_.clear();
  analysis_id_.clear();
  component_name_.clear();
  geometry_path_.clear();
  geometry_sha256_.clear();
  mesh_path_.clear();
  mesh_sha256_.clear();
  material_evidence_path_.clear();
  material_evidence_sha256_.clear();
  candidate_mesh_target_size_m_ = 0.0;
  coordinate_scale_to_m_ = 0.0;
  mesh_ = {};
  mesh_geometry_.reset();
  highlight_geometry_.reset();
  active_surface_group_.clear();
  restraint_groups_.clear();
  load_groups_.clear();
  material_designation_.clear();
  material_temper_.clear();
  material_product_form_.clear();
  material_applicability_.clear();
  youngs_modulus_pa_ = 0.0;
  poisson_ratio_ = 0.0;
  material_reviewed_ = false;
  material_candidates_.clear();
  force_magnitude_n_ = 0.0;
  force_direction_ = {};
  force_reviewed_ = false;
  displacement_limit_m_.reset();
  von_mises_limit_pa_.reset();
  displacement_limit_basis_.clear();
  von_mises_limit_basis_.clear();
  requirements_reviewed_ = false;
  mesh_target_size_m_ = 0.0;
  minimum_mean_ratio_threshold_ = 0.0;
  mesh_reviewed_ = false;
  scenario_confirmed_ = false;
  compiled_setup_.reset();
  ready_to_export_ = false;
}

bool StructuralSetupController::loadCandidate(const QUrl &manifest) {
  operation_issue_.reset();
  try {
    if (!manifest.isLocalFile())
      reject("candidate_path_rejected",
             "Structural candidate must be a local file URL.");
    const QFileInfo manifestInfo(manifest.toLocalFile());
    if (!manifestInfo.isAbsolute() || !manifestInfo.exists() ||
        !manifestInfo.isFile() || manifestInfo.isSymbolicLink())
      reject("candidate_path_rejected",
             "Structural candidate must be an absolute regular file.");
    const auto manifestBytes = read_regular_file(
        manifestInfo.absoluteFilePath(), maximumCandidateBytes);
    const auto object = parse_json_object(manifestBytes, "candidate manifest");
    require_keys(object,
                 {"$schema", "analysis_id", "component_name", "geometry",
                  "material_evidence", "mesh"},
                 "candidate manifest");
    if (required_string(object, "$schema", "candidate.$schema") !=
        "urn:prometheus:structural-candidate:0.1.0")
      reject("candidate_invalid", "Structural candidate schema is unsupported.");
    const auto analysisId =
        required_string(object, "analysis_id", "candidate.analysis_id");
    const auto componentName =
        required_string(object, "component_name", "candidate.component_name");

    const auto geometryValue = object.value("geometry");
    if (!geometryValue.isObject())
      reject("candidate_invalid", "candidate.geometry must be an object.");
    const auto geometryObject = geometryValue.toObject();
    require_keys(geometryObject, {"path", "sha256"}, "candidate.geometry");
    const auto geometryExpected = required_string(
        geometryObject, "sha256", "candidate.geometry.sha256");
    if (!strict_sha256(geometryExpected))
      reject("candidate_invalid", "Candidate geometry SHA-256 is malformed.");
    const auto geometryPath = resolve_relative_artifact(
        manifestInfo.absolutePath(),
        required_string(geometryObject, "path", "candidate.geometry.path"));
    if (stable_hash(geometryPath, maximumGeometryBytes) != geometryExpected)
      reject("candidate_identity_mismatch",
             "Candidate geometry bytes do not match the declared identity.");

    const auto meshValue = object.value("mesh");
    if (!meshValue.isObject())
      reject("candidate_invalid", "candidate.mesh must be an object.");
    const auto meshObject = meshValue.toObject();
    require_keys(meshObject,
                 {"coordinate_scale_to_m", "path", "sha256",
                  "target_size_m"},
                 "candidate.mesh");
    const auto meshExpected =
        required_string(meshObject, "sha256", "candidate.mesh.sha256");
    if (!strict_sha256(meshExpected))
      reject("candidate_invalid", "Candidate mesh SHA-256 is malformed.");
    const auto meshPath = resolve_relative_artifact(
        manifestInfo.absolutePath(),
        required_string(meshObject, "path", "candidate.mesh.path"));
    const auto meshHashBefore = stable_hash(meshPath, maximumMeshBytes);
    if (meshHashBefore != meshExpected)
      reject("candidate_identity_mismatch",
             "Candidate mesh bytes do not match the declared identity.");
    const auto coordinateScale = required_number(
        meshObject, "coordinate_scale_to_m",
        "candidate.mesh.coordinate_scale_to_m");
    const auto targetSize = required_number(
        meshObject, "target_size_m", "candidate.mesh.target_size_m");
    if (!positive_finite(coordinateScale) || !positive_finite(targetSize))
      reject("candidate_invalid",
             "Candidate mesh scale and target size must be positive.");
    const auto meshBytes = read_regular_file(meshPath, maximumMeshBytes);
    auto parsedMesh = ps::parse_gmsh_abaqus_mesh(
        std::string_view(meshBytes.constData(),
                         static_cast<std::size_t>(meshBytes.size())),
        coordinateScale);
    if (stable_hash(meshPath, maximumMeshBytes) != meshHashBefore)
      reject("candidate_identity_mismatch",
             "Candidate mesh changed while it was parsed.");

    QString materialEvidencePath;
    QString materialEvidenceHash;
    const auto materialEvidence = object.value("material_evidence");
    if (!materialEvidence.isNull()) {
      if (!materialEvidence.isObject())
        reject("candidate_invalid",
               "candidate.material_evidence must be null or an object.");
      const auto evidenceObject = materialEvidence.toObject();
      require_keys(evidenceObject, {"path", "sha256"},
                   "candidate.material_evidence");
      materialEvidenceHash = required_string(
          evidenceObject, "sha256", "candidate.material_evidence.sha256");
      if (!strict_sha256(materialEvidenceHash))
        reject("candidate_invalid",
               "Candidate material-evidence SHA-256 is malformed.");
      materialEvidencePath = resolve_relative_artifact(
          manifestInfo.absolutePath(), required_string(
                                           evidenceObject, "path",
                                           "candidate.material_evidence.path"));
      if (stable_hash(materialEvidencePath, maximumCandidateBytes) !=
          materialEvidenceHash)
        reject("candidate_identity_mismatch",
               "Material-evidence bytes do not match the declared identity.");
    }

    std::vector<std::string> groupNames;
    groupNames.reserve(parsedMesh.surface_groups.size());
    for (const auto &group : parsedMesh.surface_groups)
      groupNames.push_back(group.name);
    auto geometry = std::make_unique<StructuralDisplayGeometry>(
        parsedMesh, groupNames, this);

    resetCandidateState();
    source_path_ = manifestInfo.absoluteFilePath();
    analysis_id_ = analysisId;
    component_name_ = componentName;
    geometry_path_ = geometryPath;
    geometry_sha256_ = geometryExpected;
    mesh_path_ = meshPath;
    mesh_sha256_ = meshExpected;
    material_evidence_path_ = materialEvidencePath;
    material_evidence_sha256_ = materialEvidenceHash;
    candidate_mesh_target_size_m_ = targetSize;
    coordinate_scale_to_m_ = coordinateScale;
    mesh_ = std::move(parsedMesh);
    mesh_geometry_ = std::move(geometry);
    recompute();
    emit meshLoaded();
    return true;
  } catch (const ControllerFailure &failure) {
    resetCandidateState();
    operation_issue_ = std::pair(failure.code(), failure.message());
  } catch (const std::exception &error) {
    resetCandidateState();
    operation_issue_ = std::pair(
        QStringLiteral("candidate_invalid"), QString::fromUtf8(error.what()));
  }
  recompute();
  return false;
}

bool StructuralSetupController::hasSurfaceGroup(const QString &name) const {
  return std::ranges::any_of(mesh_.surface_groups, [&](const auto &group) {
    return QString::fromStdString(group.name) == name;
  });
}

void StructuralSetupController::setActiveSurfaceGroup(const QString &name) {
  operation_issue_.reset();
  if (name.isEmpty()) {
    active_surface_group_.clear();
    highlight_geometry_.reset();
    emit changed();
    return;
  }
  if (!hasSurfaceGroup(name)) {
    operation_issue_ = std::pair(
        QStringLiteral("surface_group_unknown"),
        QStringLiteral("The active surface group is not present in the mesh."));
    recompute();
    return;
  }
  try {
    active_surface_group_ = name;
    highlight_geometry_ = std::make_unique<StructuralDisplayGeometry>(
        mesh_, std::vector<std::string>{name.toStdString()}, this);
  } catch (const std::exception &error) {
    active_surface_group_.clear();
    highlight_geometry_.reset();
    operation_issue_ =
        std::pair(QStringLiteral("visualization_failed"),
                  QString::fromUtf8(error.what()));
  }
  recompute();
}

void StructuralSetupController::invalidateScenario() {
  scenario_confirmed_ = false;
}

void StructuralSetupController::setSurfaceRole(const QString &name,
                                               const QString &role,
                                               const bool selected) {
  operation_issue_.reset();
  if (!hasSurfaceGroup(name)) {
    operation_issue_ = std::pair(
        QStringLiteral("surface_group_unknown"),
        QStringLiteral("Selected surface group is not present in the mesh."));
    recompute();
    return;
  }
  QStringList *groups = nullptr;
  if (role == "restraint")
    groups = &restraint_groups_;
  else if (role == "load")
    groups = &load_groups_;
  else {
    operation_issue_ =
        std::pair(QStringLiteral("surface_role_invalid"),
                  QStringLiteral("Surface role must be restraint or load."));
    recompute();
    return;
  }
  if (selected && !groups->contains(name))
    groups->append(name);
  if (!selected)
    groups->removeAll(name);
  groups->sort();
  invalidateScenario();
  recompute();
}

bool StructuralSetupController::loadMaterialEvidence(const QUrl &source) {
  operation_issue_.reset();
  try {
    if (!source.isLocalFile())
      reject("material_evidence_invalid",
             "Material evidence must be a local file URL.");
    const QFileInfo information(source.toLocalFile());
    if (!information.isAbsolute())
      reject("material_evidence_invalid",
             "Material evidence path must be absolute.");
    const auto before = stable_hash(information.absoluteFilePath(),
                                    maximumCandidateBytes);
    const auto bytes =
        read_regular_file(information.absoluteFilePath(), maximumCandidateBytes);
    const auto object = parse_json_object(bytes, "material evidence");
    const auto candidates = object.value("candidates");
    if (!candidates.isArray())
      reject("material_evidence_invalid",
             "Material evidence requires a candidates array.");
    QVariantList decoded;
    for (const auto &value : candidates.toArray()) {
      if (!value.isObject() ||
          value.toObject().value("candidate_id").toString().isEmpty())
        reject("material_evidence_invalid",
               "Material candidate identity is missing.");
      decoded.append(value.toObject().toVariantMap());
    }
    if (stable_hash(information.absoluteFilePath(), maximumCandidateBytes) !=
        before)
      reject("candidate_identity_mismatch",
             "Material evidence changed while it was parsed.");
    material_evidence_path_ = information.absoluteFilePath();
    material_evidence_sha256_ = before;
    material_candidates_ = std::move(decoded);
    invalidateScenario();
    recompute();
    return true;
  } catch (const ControllerFailure &failure) {
    operation_issue_ = std::pair(failure.code(), failure.message());
  } catch (const std::exception &error) {
    operation_issue_ = std::pair(QStringLiteral("material_evidence_invalid"),
                                 QString::fromUtf8(error.what()));
  }
  recompute();
  return false;
}

void StructuralSetupController::selectMaterialCandidate(
    const QString &candidateId, const QString &applicability) {
  operation_issue_.reset();
  for (const auto &value : material_candidates_) {
    auto candidate = value.toMap();
    if (candidate.value("candidate_id").toString() != candidateId)
      continue;
    candidate["applicability"] = applicability;
    candidate["evidence_sha256"] = material_evidence_sha256_;
    setMaterialReview(candidate);
    return;
  }
  operation_issue_ =
      std::pair(QStringLiteral("material_candidate_unknown"),
                QStringLiteral("Selected material candidate is unavailable."));
  invalidateScenario();
  recompute();
}

void StructuralSetupController::setMaterialReview(const QVariantMap &review) {
  operation_issue_.reset();
  const auto evidence = review.value("evidence_sha256").toString();
  if (material_evidence_sha256_.isEmpty()) {
    operation_issue_ = std::pair(
        QStringLiteral("material_evidence_unverified"),
        QStringLiteral("Load exact material-evidence bytes before review."));
    recompute();
    return;
  }
  if (evidence != material_evidence_sha256_) {
    operation_issue_ = std::pair(
        QStringLiteral("material_evidence_identity_mismatch"),
        QStringLiteral("Material review does not cite the verified evidence bytes."));
    recompute();
    return;
  }
  material_designation_ = review.value("designation").toString();
  material_temper_ = review.value("temper").toString();
  material_product_form_ = review.value("product_form").toString();
  material_applicability_ = review.value("applicability").toString();
  youngs_modulus_pa_ = review.value("youngs_modulus_pa").toDouble();
  poisson_ratio_ = review.value("poisson_ratio").toDouble();
  material_reviewed_ = true;
  invalidateScenario();
  recompute();
}

void StructuralSetupController::setForce(const double magnitudeN,
                                         const double directionX,
                                         const double directionY,
                                         const double directionZ) {
  operation_issue_.reset();
  force_magnitude_n_ = magnitudeN;
  force_direction_ = {directionX, directionY, directionZ};
  force_reviewed_ = true;
  invalidateScenario();
  recompute();
}

void StructuralSetupController::setLimits(const QVariantMap &limits) {
  operation_issue_.reset();
  displacement_limit_m_.reset();
  von_mises_limit_pa_.reset();
  if (limits.contains("displacement_limit_m") &&
      !limits.value("displacement_limit_m").isNull())
    displacement_limit_m_ = limits.value("displacement_limit_m").toDouble();
  if (limits.contains("von_mises_limit_pa") &&
      !limits.value("von_mises_limit_pa").isNull())
    von_mises_limit_pa_ = limits.value("von_mises_limit_pa").toDouble();
  displacement_limit_basis_ =
      limits.value("displacement_limit_basis").toString();
  von_mises_limit_basis_ =
      limits.value("von_mises_limit_basis").toString();
  requirements_reviewed_ = true;
  invalidateScenario();
  recompute();
}

void StructuralSetupController::setMeshReview(const QVariantMap &review) {
  operation_issue_.reset();
  mesh_target_size_m_ = review.value("target_size_m").toDouble();
  minimum_mean_ratio_threshold_ =
      review.value("minimum_mean_ratio_threshold").toDouble();
  mesh_reviewed_ = review.value("confirmed").toBool();
  invalidateScenario();
  recompute();
}

void StructuralSetupController::confirmScenario(const bool confirmed) {
  operation_issue_.reset();
  scenario_confirmed_ = confirmed;
  recompute();
}

ps::StructuralRequest StructuralSetupController::requestFromState() const {
  const auto &setup = compiled_setup_.value();
  std::vector<std::string> restraintGroups;
  std::vector<std::string> loadGroups;
  restraintGroups.reserve(restraint_groups_.size());
  loadGroups.reserve(load_groups_.size());
  for (const auto &group : restraint_groups_)
    restraintGroups.push_back(group.toStdString());
  for (const auto &group : load_groups_)
    loadGroups.push_back(group.toStdString());
  const auto directionMagnitude = std::hypot(
      force_direction_[0], force_direction_[1], force_direction_[2]);
  std::array<double, 3> normalizedDirection{};
  for (std::size_t axis = 0; axis < 3U; ++axis)
    normalizedDirection[axis] = force_direction_[axis] / directionMagnitude;

  return {
      .analysis_id = analysis_id_.toStdString(),
      .component_name = component_name_.toStdString(),
      .geometry_sha256 = geometry_sha256_.toStdString(),
      .nodes = mesh_.nodes,
      .elements = mesh_.elements,
      .youngs_modulus_pa = youngs_modulus_pa_,
      .poisson_ratio = poisson_ratio_,
      .fully_fixed_node_ids = setup.fully_fixed_node_ids,
      .nodal_forces = setup.nodal_forces,
      .displacement_limit_m = displacement_limit_m_,
      .von_mises_limit_pa = von_mises_limit_pa_,
      .material_reviewed = material_reviewed_,
      .loads_reviewed = force_reviewed_,
      .restraints_reviewed = !restraintGroups.empty(),
      .requirements_reviewed = requirements_reviewed_,
      .scenario_confirmed = scenario_confirmed_,
      .material_designation = material_designation_.toStdString(),
      .material_temper = material_temper_.toStdString(),
      .material_product_form = material_product_form_.toStdString(),
      .material_applicability = material_applicability_.toStdString(),
      .material_evidence_sha256 =
          material_evidence_sha256_.toStdString(),
      .mesh_sha256 = mesh_sha256_.toStdString(),
      .restraint_surface_groups = std::move(restraintGroups),
      .load_surface_groups = std::move(loadGroups),
      .reviewed_force_magnitude_n = force_magnitude_n_,
      .reviewed_force_direction = normalizedDirection,
      .selected_load_area_m2 = setup.selected_load_area_m2,
      .mesh_target_size_m = mesh_target_size_m_,
      .minimum_mean_ratio_threshold = minimum_mean_ratio_threshold_,
      .observed_minimum_mean_ratio = mesh_.diagnostics.minimum_mean_ratio,
      .displacement_limit_basis =
          displacement_limit_basis_.toStdString(),
      .von_mises_limit_basis = von_mises_limit_basis_.toStdString(),
      .mesh_reviewed = mesh_reviewed_,
      .mesh_coordinate_scale_to_m = coordinate_scale_to_m_,
  };
}

void StructuralSetupController::recompute() {
  blocking_issues_.clear();
  ready_to_export_ = false;
  compiled_setup_.reset();
  const auto addIssue = [&](const QString &code, const QString &message) {
    for (const auto &value : blocking_issues_)
      if (value.toMap().value("code").toString() == code)
        return;
    blocking_issues_.append(QVariantMap{{"code", code}, {"message", message}});
  };
  if (operation_issue_.has_value())
    addIssue(operation_issue_->first, operation_issue_->second);
  if (mesh_.nodes.empty()) {
    addIssue("candidate_not_loaded",
             "Load a verified structural candidate and mesh.");
    emit changed();
    return;
  }

  const bool materialApplicability =
      material_applicability_ == "known" ||
      material_applicability_ == "assumed";
  if (!materialApplicability)
    addIssue("material_applicability_unreviewed",
             "Choose known or assumed material applicability.");
  const bool materialInputs =
      material_reviewed_ && !material_designation_.isEmpty() &&
      !material_temper_.isEmpty() && !material_product_form_.isEmpty() &&
      positive_finite(youngs_modulus_pa_) && valid_poisson(poisson_ratio_) &&
      !material_evidence_path_.isEmpty() &&
      strict_sha256(material_evidence_sha256_);
  if (!materialInputs)
    addIssue("material_inputs_unreviewed",
             "Review material identity, elastic values, and exact evidence.");
  if (restraint_groups_.isEmpty())
    addIssue("restraint_surface_unselected",
             "Select at least one restraint surface group.");
  if (load_groups_.isEmpty())
    addIssue("load_surface_unselected",
             "Select at least one load surface group.");
  const bool forceInputs = force_reviewed_ &&
                           positive_finite(force_magnitude_n_) &&
                           finite_direction(force_direction_);
  if (!forceInputs)
    addIssue("force_unreviewed",
             "Review a positive total force and finite nonzero direction.");
  const bool displacementReady =
      displacement_limit_m_.has_value() &&
      positive_finite(*displacement_limit_m_) &&
      !displacement_limit_basis_.isEmpty();
  const bool stressReady = von_mises_limit_pa_.has_value() &&
                           positive_finite(*von_mises_limit_pa_) &&
                           !von_mises_limit_basis_.isEmpty();
  if (!requirements_reviewed_ || (!displacementReady && !stressReady))
    addIssue("requirements_unreviewed",
             "Review at least one positive limit and its basis.");
  const auto meshTargetTolerance =
      std::max(1.0, std::abs(candidate_mesh_target_size_m_)) * 1.0e-12;
  const bool meshTargetMatches =
      positive_finite(candidate_mesh_target_size_m_) &&
      std::abs(mesh_target_size_m_ - candidate_mesh_target_size_m_) <=
          meshTargetTolerance;
  if (mesh_reviewed_ && positive_finite(mesh_target_size_m_) &&
      !meshTargetMatches)
    addIssue("mesh_control_identity_mismatch",
             "Reviewed mesh target size differs from the verified candidate.");
  const bool meshInputs =
      mesh_reviewed_ && positive_finite(mesh_target_size_m_) &&
      meshTargetMatches &&
      positive_finite(minimum_mean_ratio_threshold_) &&
      minimum_mean_ratio_threshold_ <= 1.0 &&
      mesh_.diagnostics.minimum_mean_ratio + 1.0e-15 >=
          minimum_mean_ratio_threshold_;
  if (!meshInputs)
    addIssue("mesh_controls_unreviewed",
             "Review mesh target size and an achieved quality threshold.");
  if (!scenario_confirmed_)
    addIssue("scenario_unconfirmed",
             "Confirm the complete material, mesh, load, restraint, and limit scenario.");

  if (!restraint_groups_.isEmpty() && !load_groups_.isEmpty() && forceInputs) {
    try {
      std::vector<std::string> restraints;
      std::vector<std::string> loads;
      for (const auto &group : restraint_groups_)
        restraints.push_back(group.toStdString());
      for (const auto &group : load_groups_)
        loads.push_back(group.toStdString());
      compiled_setup_ = ps::compile_surface_setup(
          mesh_, restraints, loads, force_magnitude_n_, force_direction_);
    } catch (const std::exception &error) {
      addIssue("surface_setup_invalid", QString::fromUtf8(error.what()));
    }
  }

  if (blocking_issues_.isEmpty() && compiled_setup_.has_value()) {
    const auto issues = ps::validate_request(requestFromState());
    for (const auto &issue : issues)
      addIssue(QString::fromStdString(issue.code),
               QString::fromStdString(issue.message));
  }
  ready_to_export_ =
      blocking_issues_.isEmpty() && compiled_setup_.has_value();
  emit changed();
}

bool StructuralSetupController::exportReviewedCase(const QUrl &directory) {
  operation_issue_.reset();
  recompute();
  if (!ready_to_export_)
    return false;
  try {
    if (!directory.isLocalFile())
      reject("export_path_rejected", "Export directory must be local.");
    const QFileInfo information(directory.toLocalFile());
    if (!information.isAbsolute() || !information.exists() ||
        !information.isDir() || information.isSymbolicLink())
      reject("export_path_rejected",
             "Export directory must be an existing regular directory.");
    bool sourceIdentitiesMatch = false;
    try {
      sourceIdentitiesMatch =
          !geometry_path_.isEmpty() && !mesh_path_.isEmpty() &&
          !material_evidence_path_.isEmpty() &&
          stable_hash(geometry_path_, maximumGeometryBytes) ==
              geometry_sha256_ &&
          stable_hash(mesh_path_, maximumMeshBytes) == mesh_sha256_ &&
          stable_hash(material_evidence_path_, maximumCandidateBytes) ==
              material_evidence_sha256_;
    } catch (...) {
      sourceIdentitiesMatch = false;
    }
    if (!sourceIdentitiesMatch)
      reject("export_identity_mismatch",
             "A reviewed source artifact changed before export.");
    const auto structuralCase = ps::build_structural_case(requestFromState());
    QSaveFile output(information.absoluteFilePath() +
                     "/reviewed-structural-case.json");
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(structuralCase.bytes.data(),
                     static_cast<qint64>(structuralCase.bytes.size())) !=
            static_cast<qint64>(structuralCase.bytes.size()) ||
        !output.commit())
      reject("export_failed", "Canonical structural case could not be saved.");
    return true;
  } catch (const ControllerFailure &failure) {
    operation_issue_ = std::pair(failure.code(), failure.message());
  } catch (const std::exception &error) {
    operation_issue_ =
        std::pair(QStringLiteral("export_failed"),
                  QString::fromUtf8(error.what()));
  }
  recompute();
  return false;
}
