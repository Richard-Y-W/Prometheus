#include "project_intake.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

std::filesystem::path nativePath(const QString &path) {
#ifdef _WIN32
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toStdString());
#endif
}

struct Classification final {
  QString category;
  QString state;
  QString detail;
};

Classification classify(const QString &extension) {
  static const QSet<QString> otherGeometry{
      "iges", "igs", "stl", "obj", "3mf", "sldprt", "sldasm", "f3d",
      "fcstd"};
  static const QSet<QString> tables{"csv", "tsv", "xlsx", "xls"};
  static const QSet<QString> documents{"pdf", "md", "txt", "doc", "docx"};
  static const QSet<QString> structured{"json", "yaml", "yml", "toml", "xml"};
  static const QSet<QString> source{
      "c",   "cc",   "cpp", "cxx", "h",   "hh", "hpp", "py",
      "js",  "ts",   "tsx", "java", "cs",  "rs", "go",  "ino",
      "m",   "mm",   "f",   "f90", "vhd", "v",  "sv"};

  if (extension == "step" || extension == "stp")
    return {"geometry", "ready", "Ready for Open Cascade STEP import"};
  if (otherGeometry.contains(extension))
    return {"geometry", "not_evaluated",
            "Recognized geometry format; this prototype imports STEP only"};
  if (tables.contains(extension))
    return {"table", "not_evaluated",
            "Recognized table or BOM candidate; content was not parsed"};
  if (documents.contains(extension))
    return {"document", "not_evaluated",
            "Recognized document; content was not parsed"};
  if (structured.contains(extension))
    return {"structured_data", "not_evaluated",
            "Recognized structured data; semantics were not interpreted"};
  if (source.contains(extension))
    return {"source_code", "not_evaluated",
            "Recognized source code; behavior was not analyzed"};
  return {"other", "unsupported",
          "No analysis capability is registered for this format"};
}

bool hashFile(const QFileInfo &information, QString &digest, QString &error) {
  QFile file(information.absoluteFilePath());
  if (!file.open(QIODevice::ReadOnly)) {
    error = "File could not be opened: " + file.errorString();
    return false;
  }

  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto chunk = file.read(1024 * 1024);
    if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
      error = "File could not be read: " + file.errorString();
      return false;
    }
    hash.addData(chunk);
  }
  digest = "sha256:" + QString::fromLatin1(hash.result().toHex());
  return true;
}

int countState(const QVariantList &artifacts, const QString &state) {
  return static_cast<int>(std::count_if(
      artifacts.cbegin(), artifacts.cend(), [&](const QVariant &value) {
        return value.toMap().value("analysis_state").toString() == state;
      }));
}

} // namespace

QVariantMap readCandidateComponentManifest(const QVariantMap &manifestArtifact,
                                           const QVariantList &artifacts,
                                           QString &detail);

ProjectIntakeResult scanProjectFolder(const QString &rootPath) {
  ProjectIntakeResult result;
  const QFileInfo rootInformation(rootPath);
  if (!rootInformation.exists() || !rootInformation.isDir() ||
      rootInformation.isSymbolicLink()) {
    result.error = "Choose an existing regular project folder.";
    return result;
  }

  result.ok = true;
  result.root_path = rootInformation.absoluteFilePath();
  const QDir root(result.root_path);
  QDirIterator iterator(result.root_path,
                        QDir::Files | QDir::Hidden | QDir::System |
                            QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
  QStringList readySteps;
  while (iterator.hasNext()) {
    iterator.next();
    const QFileInfo information = iterator.fileInfo();
    const auto absolutePath = information.absoluteFilePath();
    const auto relativePath =
        QDir::fromNativeSeparators(root.relativeFilePath(absolutePath));
    const auto extension = information.suffix().toLower();
    auto classification = classify(extension);
    QString digest;

    if (information.isSymbolicLink()) {
      classification.state = "unsupported";
      classification.detail = "Symbolic link was accounted for but not followed";
    } else {
      const auto sizeBefore = information.size();
      const auto modifiedBefore = information.lastModified();
      QString hashError;
      if (!hashFile(information, digest, hashError)) {
        classification.state = "unreadable";
        classification.detail = std::move(hashError);
      } else {
        const QFileInfo after(absolutePath);
        if (after.size() != sizeBefore || after.lastModified() != modifiedBefore) {
          digest.clear();
          classification.state = "unreadable";
          classification.detail = "File changed while it was being hashed";
        }
      }
    }

    QVariantMap artifact{{"relative_path", relativePath},
                         {"absolute_path", absolutePath},
                         {"name", information.fileName()},
                         {"extension", extension},
                         {"byte_size", information.size()},
                         {"sha256", digest},
                         {"category", classification.category},
                         {"analysis_state", classification.state},
                         {"detail", classification.detail},
                         {"loadable", classification.state == "ready"}};
    result.artifacts.append(artifact);
    if (classification.state == "ready")
      readySteps.append(absolutePath);
  }

  std::sort(result.artifacts.begin(), result.artifacts.end(),
            [](const QVariant &left, const QVariant &right) {
              return left.toMap().value("relative_path").toString() <
                     right.toMap().value("relative_path").toString();
            });

  QHash<QString, int> digestCounts;
  for (const auto &value : result.artifacts) {
    const auto digest = value.toMap().value("sha256").toString();
    if (!digest.isEmpty())
      ++digestCounts[digest];
  }
  QSet<QString> seenDigests;
  for (auto &value : result.artifacts) {
    auto row = value.toMap();
    const auto digest = row.value("sha256").toString();
    const auto copies = digestCounts.value(digest);
    const bool duplicateCopy = !digest.isEmpty() && copies > 1 &&
                               seenDigests.contains(digest);
    row.insert("identical_file_count", copies > 1 ? copies : 1);
    row.insert("duplicate_copy", duplicateCopy);
    value = row;
    if (!digest.isEmpty())
      seenDigests.insert(digest);
  }
  for (auto &value : result.artifacts) {
    auto row = value.toMap();
    if (row.value("relative_path").toString() !=
        "prometheus-trial-source-manifest.json")
      continue;
    QString detail;
    const auto candidate =
        readCandidateComponentManifest(row, result.artifacts, detail);
    row.insert("detail", detail);
    value = row;
    if (!candidate.isEmpty())
      result.candidate_components.append(candidate);
  }
  if (readySteps.size() == 1)
    result.primary_step_path = readySteps.front();
  result.inventory_snapshot = buildProjectInventorySnapshot(result);
  std::vector<prometheus::run_store::ProjectEvidenceInput> evidenceInputs;
  evidenceInputs.reserve(static_cast<std::size_t>(result.artifacts.size()));
  for (const auto &value : result.artifacts) {
    const auto artifact = value.toMap();
    const auto hashText = artifact.value("sha256").toString();
    evidenceInputs.push_back({
        artifact.value("relative_path").toString().toStdString(),
        nativePath(artifact.value("absolute_path").toString()),
        artifact.value("byte_size").toULongLong(),
        hashText.isEmpty()
            ? std::nullopt
            : std::optional<std::string>(hashText.toStdString()),
        artifact.value("category").toString().toStdString(),
        artifact.value("analysis_state").toString().toStdString()});
  }
  auto archive = prometheus::run_store::build_project_evidence_archive(
      result.inventory_snapshot->reference, evidenceInputs);
  if (!archive.has_value()) {
    result.ok = false;
    result.error = QString::fromStdString(archive.diagnostic().message);
    result.inventory_snapshot.reset();
  } else {
    result.evidence_archive = std::move(archive.value());
  }
  return result;
}

ProjectIntakeController::ProjectIntakeController(QObject *parent)
    : QObject(parent) {
  connect(&watcher_, &QFutureWatcher<ProjectIntakeResult>::finished, this,
          [this] { apply(watcher_.result()); });
}

int ProjectIntakeController::readyCount() const {
  return countState(result_.artifacts, "ready");
}

int ProjectIntakeController::notEvaluatedCount() const {
  return countState(result_.artifacts, "not_evaluated");
}

int ProjectIntakeController::unsupportedCount() const {
  return countState(result_.artifacts, "unsupported");
}

int ProjectIntakeController::unreadableCount() const {
  return countState(result_.artifacts, "unreadable");
}

prometheus::run_store::ObjectToStore
buildProjectInventorySnapshot(const ProjectIntakeResult &result) {
  using Json = nlohmann::json;
  Json artifacts = Json::array();
  for (const auto &value : result.artifacts) {
    const auto artifact = value.toMap();
    const auto hash = artifact.value("sha256").toString().toStdString();
    artifacts.push_back(
        {{"relative_path", artifact.value("relative_path").toString().toStdString()},
         {"byte_length", artifact.value("byte_size").toULongLong()},
         {"sha256", hash.empty() ? Json(nullptr) : Json(hash)},
         {"category", artifact.value("category").toString().toStdString()},
         {"analysis_state",
          artifact.value("analysis_state").toString().toStdString()},
         {"detail", artifact.value("detail").toString().toStdString()}});
  }
  const auto bytes = prometheus::integrity::canonicalize_json_bytes(
      Json{{"$schema", prometheus::run_store::project_inventory_schema_id},
           {"schema_version", "1.0.0"},
           {"snapshot_kind", "accounted_project_folder"},
           {"root_label", QFileInfo(result.root_path).fileName().toStdString()},
           {"artifacts", std::move(artifacts)}}
          .dump());
  const prometheus::run_store::StoredObjectReference reference{
      prometheus::integrity::sha256_bytes(bytes), bytes.size(),
      std::string(prometheus::run_store::project_inventory_media_type),
      std::string(prometheus::run_store::project_inventory_schema_id), "1.0.0"};
  return {reference, bytes};
}

QVariantMap readCandidateComponentManifest(const QVariantMap &manifestArtifact,
                                           const QVariantList &artifacts,
                                           QString &detail) {
  constexpr qint64 maximumManifestBytes = 1024 * 1024;
  if (manifestArtifact.value("byte_size").toLongLong() > maximumManifestBytes) {
    detail = "Trial source manifest exceeds the 1 MiB intake limit";
    return {};
  }
  QFile file(manifestArtifact.value("absolute_path").toString());
  if (!file.open(QIODevice::ReadOnly)) {
    detail = "Trial source manifest could not be reopened";
    return {};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    detail = "Trial source manifest is not valid JSON object data";
    return {};
  }
  const auto root = document.object();
  const auto component = root.value("component").toObject();
  const auto review = root.value("review").toObject();
  const auto manufacturer = component.value("manufacturer").toString();
  const auto partNumber = component.value("part_number").toString();
  const auto sourceFile = QDir::fromNativeSeparators(
      component.value("source_file").toString());
  const auto sourceHash = component.value("source_sha256").toString().toLower();
  if (root.value("schema").toString() !=
          "urn:prometheus:trial-source-manifest:0.1.0" ||
      root.value("status").toString() != "candidate_evidence_only" ||
      manufacturer.isEmpty() || partNumber.isEmpty() || sourceFile.isEmpty() ||
      sourceFile.startsWith('/') || sourceFile.contains("../") ||
      sourceHash.size() != 64 || review.value("published_component").toBool() ||
      review.value("geometry_binding_confirmed").toBool() ||
      review.value("specification_claims_reviewed").toBool()) {
    detail = "Trial source manifest is outside the candidate-only intake contract";
    return {};
  }
  const auto expectedDigest = "sha256:" + sourceHash;
  const auto source = std::find_if(
      artifacts.cbegin(), artifacts.cend(), [&](const QVariant &value) {
        const auto row = value.toMap();
        return row.value("relative_path").toString() == sourceFile &&
               row.value("sha256").toString() == expectedDigest;
      });
  if (source == artifacts.cend()) {
    detail = "Candidate component source file is missing or has the wrong hash";
    return {};
  }
  QVariantList claims;
  QSet<QString> claimIds;
  const auto claimValues = root.value("candidate_claims").toArray();
  if (claimValues.size() > 32) {
    detail = "Trial source manifest has too many candidate claims";
    return {};
  }
  for (const auto &claimValue : claimValues) {
    const auto claim = claimValue.toObject();
    const auto id = claim.value("id").toString();
    const auto label = claim.value("label").toString();
    const auto quantity = claim.value("quantity").toString();
    const auto originalValue = claim.value("original_value").toString();
    const auto originalUnit = claim.value("original_unit").toString();
    const auto siUnit = claim.value("si_unit").toString();
    const auto valueSi = claim.value("value_si").toDouble(
        std::numeric_limits<double>::quiet_NaN());
    const auto sourcePage = claim.value("source_page").toInt();
    if (id.isEmpty() || id.size() > 80 || claimIds.contains(id) ||
        label.isEmpty() || quantity.isEmpty() || originalValue.isEmpty() ||
        originalUnit.isEmpty() || siUnit.isEmpty() || !std::isfinite(valueSi) ||
        sourcePage <= 0) {
      detail = "Trial source manifest contains an invalid candidate claim";
      return {};
    }
    claimIds.insert(id);
    claims.append(QVariantMap{{"id", id},
                              {"label", label},
                              {"quantity", quantity},
                              {"original_value", originalValue},
                              {"original_unit", originalUnit},
                              {"value_si", valueSi},
                              {"si_unit", siUnit},
                              {"source_file", sourceFile},
                              {"source_page", sourcePage},
                              {"review_state", "unreviewed"}});
  }
  detail = "Candidate component source verified; specifications remain unreviewed";
  return {{"id", "candidate:" + manifestArtifact.value("sha256").toString()},
          {"manufacturer", manufacturer},
          {"part_number", partNumber},
          {"relationship", component.value("relationship").toString()},
          {"source_file", sourceFile},
          {"source_sha256", expectedDigest},
          {"candidate_claims", claims},
          {"review_state", "candidate_evidence_only"}};
}

int ProjectIntakeController::duplicateCopyCount() const {
  return static_cast<int>(std::count_if(
      result_.artifacts.cbegin(), result_.artifacts.cend(),
      [](const QVariant &value) {
        return value.toMap().value("duplicate_copy").toBool();
      }));
}

QString ProjectIntakeController::status() const {
  if (busy_)
    return "Accounting for project files…";
  if (result_.artifacts.isEmpty())
    return result_.root_path.isEmpty() ? QString{} : "No files found";
  return QString::number(result_.artifacts.size()) +
         (result_.artifacts.size() == 1 ? " file accounted for"
                                        : " files accounted for");
}

void ProjectIntakeController::scanFolder(const QUrl &folder) {
  if (busy_)
    return;
  if (!folder.isLocalFile()) {
    result_.error = "Choose a local project folder.";
    emit changed();
    emit scanFinished(false);
    return;
  }
  result_.error.clear();
  busy_ = true;
  emit changed();
  watcher_.setFuture(QtConcurrent::run(
      [path = folder.toLocalFile()] { return scanProjectFolder(path); }));
}

void ProjectIntakeController::reviewCandidateClaim(const QString &candidateId,
                                                   const QString &claimId,
                                                   const QString &decision) {
  if (decision != "accepted" && decision != "rejected" &&
      decision != "unreviewed") {
    return;
  }
  bool changed = false;
  for (auto &candidateValue : result_.candidate_components) {
    auto candidate = candidateValue.toMap();
    if (candidate.value("id").toString() != candidateId)
      continue;
    auto claims = candidate.value("candidate_claims").toList();
    for (auto &claimValue : claims) {
      auto claim = claimValue.toMap();
      if (claim.value("id").toString() != claimId)
        continue;
      if (claim.value("review_state").toString() == decision)
        return;
      claim.insert("review_state", decision);
      claimValue = claim;
      changed = true;
      break;
    }
    if (changed) {
      candidate.insert("candidate_claims", claims);
      candidateValue = candidate;
    }
    break;
  }
  if (changed)
    emit this->changed();
}

void ProjectIntakeController::apply(ProjectIntakeResult result) {
  busy_ = false;
  const bool success = result.ok;
  if (success) {
    result_ = std::move(result);
  } else {
    result_.error = std::move(result.error);
  }
  emit changed();
  emit scanFinished(success);
}
