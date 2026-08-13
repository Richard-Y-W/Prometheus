#include "engineering_controller.hpp"

#include <utility>

namespace {

QVariantMap geometryFinding(QString kind, QString status, QString severity,
                            QString title, QString mechanism,
                            const double calculated, QString unit,
                            const double available, const double margin,
                            QString evidence, QString assumption = {},
                            QString range = {}) {
  return {
      {"finding_kind", std::move(kind)},
      {"status", std::move(status)},
      {"severity", std::move(severity)},
      {"title", std::move(title)},
      {"mechanism", std::move(mechanism)},
      {"calculated", calculated},
      {"unit", std::move(unit)},
      {"available", available},
      {"margin_fraction", margin},
      {"evidence", std::move(evidence)},
      {"assumption", std::move(assumption)},
      {"estimated_range", std::move(range)},
      {"first_id", QVariant{}},
      {"second_id", QVariant{}},
  };
}

} // namespace

void EngineeringController::defineRevoluteJoint(
    const int source, const int target, const QString &axis,
    const double minimumDeg, const double maximumDeg, const double pivotX,
    const double pivotY, const double pivotZ) {
  joint_ = {
      {"type", "revolute"},        {"source_index", source},
      {"target_index", target},    {"axis", axis},
      {"minimum_deg", minimumDeg}, {"maximum_deg", maximumDeg},
      {"pivot_x", pivotX},         {"pivot_y", pivotY},
      {"pivot_z", pivotZ},         {"confirmed_by_user", true},
  };
  findings_.clear();
  geometry_status_ = "not_evaluated";
  emit changed();
}

void EngineeringController::runGeometryChecks(const QVariantList &interferences,
                                              const QVariantList &sweepResults,
                                              const bool sweepEvaluated) {
  findings_.clear();
  for (const auto &value : interferences) {
    const auto hit = value.toMap();
    const auto classification =
        hit.value("classification", "unclassified").toString();
    const bool intended = classification == "intended_engagement";
    const bool prohibited = classification == "prohibited";
    auto collision = geometryFinding(
        "static_interference",
        intended     ? "information"
        : prohibited ? "fail"
                     : "caution",
        intended     ? "information"
        : prohibited ? "critical"
                     : "warning",
        intended     ? "Intentional solid engagement"
        : prohibited ? "Prohibited static part interference"
                     : "Unclassified static part interference",
        hit.value("first_name").toString() + " and " +
            hit.value("second_name").toString() +
            " share non-zero solid volume.",
        hit.value("volume_m3").toDouble(), "m³", 0, -1,
        "Imported STEP B-Rep geometry",
        intended     ? "User classified this overlap as an intended engagement"
        : prohibited ? "User classified this overlap as prohibited"
                     : "Classify an intended fit or connection before "
                       "escalating severity");
    collision["first_id"] = hit.value("first_id");
    collision["second_id"] = hit.value("second_id");
    findings_.append(collision);
  }

  if (sweepEvaluated && sweepResults.isEmpty()) {
    findings_.append(geometryFinding(
        "sampled_joint_sweep", "information", "information",
        "No collision found at sampled joint positions",
        "The moving part did not share solid volume with non-joint parts at 19 "
        "evenly spaced positions.",
        19, "samples", 0, 0, "Imported STEP B-Rep geometry",
        "This sampled result is not a continuous-clearance guarantee"));
  }
  for (const auto &value : sweepResults) {
    const auto hit = value.toMap();
    auto collision = geometryFinding(
        "sampled_joint_sweep", "fail", "critical",
        "Collision in sampled joint range",
        hit.value("moving_name").toString() + " intersects " +
            hit.value("other_name").toString() +
            " beginning at sampled angle " +
            QString::number(hit.value("first_angle_deg").toDouble(), 'f', 1) +
            "°.",
        hit.value("maximum_volume_m3").toDouble(), "m³", 0, -1,
        "Imported STEP B-Rep geometry",
        "OCCT common-volume at " + hit.value("samples_tested").toString() +
            " evenly spaced samples; connected joint pair excluded");
    collision["first_id"] = hit.value("moving_id");
    collision["second_id"] = hit.value("other_id");
    findings_.append(collision);
  }
  geometry_status_ = "completed";
  emit changed();
}

QVariantMap EngineeringController::snapshotGeometryState() const {
  return {
      {"joint", joint_.isEmpty() ? QVariant{} : QVariant(joint_)},
      {"geometry_findings", findings_},
      {"geometry_status", geometry_status_},
  };
}

void EngineeringController::restoreGeometryState(const QVariantMap &state) {
  joint_ = state.value("joint").toMap();
  findings_ = state.value("geometry_findings").toList();
  geometry_status_ = state.value("geometry_status", "not_evaluated").toString();
  emit changed();
}
