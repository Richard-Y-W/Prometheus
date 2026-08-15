#include "project_intake.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace {

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
  if (readySteps.size() == 1)
    result.primary_step_path = readySteps.front();
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
