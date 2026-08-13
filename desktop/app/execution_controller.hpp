#pragma once

#include <prometheus/execution/contracts.hpp>
#include <prometheus/execution/package_consumer.hpp>
#include <prometheus/run_store/project_v2.hpp>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <optional>
#include <stop_token>
#include <vector>

class ProjectController;
class ServiceController;

class ExecutionController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString pendingCadEntityId READ pendingCadEntityId WRITE
                 setPendingCadEntityId NOTIFY changed)
  Q_PROPERTY(QVariantMap activePackage READ activePackage NOTIFY changed)
  Q_PROPERTY(QVariantMap scenarioDraft READ scenarioDraft WRITE setScenarioDraft
                 NOTIFY changed)
  Q_PROPERTY(QVariantMap scenarioPreview READ scenarioPreview NOTIFY changed)
  Q_PROPERTY(bool scenarioConfirmed READ scenarioConfirmed NOTIFY changed)
  Q_PROPERTY(
      QString confirmedScenarioHash READ confirmedScenarioHash NOTIFY changed)
  Q_PROPERTY(bool canRun READ canRun NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)
  Q_PROPERTY(QVariantList runHistory READ runHistory NOTIFY changed)
  Q_PROPERTY(int selectedRunIndex READ selectedRunIndex NOTIFY changed)
  Q_PROPERTY(QVariantMap selectedResult READ selectedResult NOTIFY changed)
  Q_PROPERTY(QString replayState READ replayState NOTIFY changed)

public:
  explicit ExecutionController(ProjectController *project,
                               ServiceController *service = nullptr,
                               QObject *parent = nullptr);
  ~ExecutionController() override;

  QString pendingCadEntityId() const { return pending_cad_entity_id_; }
  QVariantMap activePackage() const { return active_package_; }
  QVariantMap scenarioDraft() const { return scenario_draft_; }
  QVariantMap scenarioPreview() const { return scenario_preview_; }
  bool scenarioConfirmed() const { return scenario_confirmed_; }
  QString confirmedScenarioHash() const { return confirmed_scenario_hash_; }
  bool canRun() const;
  bool busy() const { return busy_; }
  QString status() const { return status_; }
  QString error() const { return error_; }
  QString errorCode() const { return error_code_; }
  QVariantList runHistory() const { return run_history_; }
  int selectedRunIndex() const { return selected_run_index_; }
  QVariantMap selectedResult() const { return selected_result_; }
  QString replayState() const { return replay_state_; }

  Q_INVOKABLE void setPendingCadEntityId(const QString &entityId);
  Q_INVOKABLE void setScenarioDraft(const QVariantMap &draft);
  Q_INVOKABLE void previewScenarioDegrees();
  Q_INVOKABLE void confirmScenario(const QString &intent);
  Q_INVOKABLE void runAnalysis();
  Q_INVOKABLE void cancelExecution();
  Q_INVOKABLE void selectRun(int index);
  Q_INVOKABLE void replaySelected();
  Q_INVOKABLE void reloadProject();

public slots:
  void acceptExactPackage(QByteArray bytes, QString expectedObjectHash);

signals:
  void changed();

private:
  struct RecordedRun final {
    prometheus::run_store::StoredObjectReference manifest;
    QVariantMap result;
  };

  ProjectController *project_;
  QString pending_cad_entity_id_;
  QVariantMap active_package_;
  QVariantMap scenario_draft_;
  QVariantMap scenario_preview_;
  bool scenario_confirmed_{false};
  QString confirmed_scenario_hash_;
  QString scenario_load_error_;
  QString scenario_load_error_code_;
  std::optional<prometheus::execution::ScenarioPreview> typed_preview_;
  std::optional<prometheus::run_store::StoredObjectReference>
      active_package_reference_;
  std::optional<prometheus::run_store::StoredObjectReference>
      confirmed_scenario_reference_;
  bool busy_{false};
  QString status_;
  QString error_;
  QString error_code_;
  QVariantList run_history_;
  std::vector<RecordedRun> recorded_runs_;
  int selected_run_index_{-1};
  QVariantMap selected_result_;
  QString replay_state_;
  std::uint64_t generation_{0U};
  std::stop_source stop_source_;

  void clearError();
  void setError(QString message, QString code, QString status = "blocked");
  void clearSelection();
  void clearScenarioConfirmation();
  void loadActiveBinding();
  void loadCurrentScenario();
  void loadRecordedRuns();
};
