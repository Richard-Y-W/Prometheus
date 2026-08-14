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

QVariantMap unknownQuestion(QString question, QString reason, QString unlock) {
  return {{"status", "not_evaluated"},
          {"question", std::move(question)},
          {"reason", std::move(reason)},
          {"unlock", std::move(unlock)}};
}

void addCommonUnknowns(QVariantList &unknowns, const bool sweepEvaluated) {
  if (!sweepEvaluated) {
    unknowns.append(unknownQuestion(
        "Motion clearance",
        "No reviewed trajectory was evaluated by the geometry engine.",
        "Confirm a joint, axis, limits, and moving component, then run the "
        "sampled sweep."));
  }
  unknowns.append(unknownQuestion(
      "Material and mass",
      "Neutral geometry does not establish material, density, or component "
      "mass.",
      "Review material or mass evidence for the parts that affect the "
      "requirement."));
  unknowns.append(unknownQuestion(
      "Loads and restraints",
      "The imported folder does not yet define authoritative loads, contacts, "
      "or supports.",
      "Select the operating scenario and review loads, contacts, and "
      "restraints."));
  unknowns.append(unknownQuestion(
      "Structural strength",
      "No structural solver was run for this project.",
      "Provide material, loads, contacts, and restraints for a bounded linear "
      "static analysis."));
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
  unknowns_.clear();
  coverage_.clear();
  geometry_status_ = "not_evaluated";
  emit changed();
}

void EngineeringController::runGeometryChecks(const QVariantList &interferences,
                                              const QVariantList &sweepResults,
                                              const bool sweepEvaluated,
                                              const bool staticInterferenceEvaluated) {
  findings_.clear();
  unknowns_.clear();
  int evaluated = 0;
  if (staticInterferenceEvaluated) {
    ++evaluated;
    if (interferences.isEmpty()) {
      findings_.append(geometryFinding(
          "static_interference", "information", "information",
          "No static solid interference found",
          "No pair of imported solids shared non-zero volume in the current "
          "assembled pose.",
          0, "m³", 0, 0,
          "Imported STEP B-Rep geometry; OCCT Boolean common-volume",
          "Current imported pose only; this result does not cover motion, "
          "contact gaps, or manufacturing tolerances"));
    }
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
          intended
              ? "User classified this overlap as an intended engagement"
          : prohibited ? "User classified this overlap as prohibited"
                       : "Classify an intended fit or connection before "
                         "escalating severity");
      collision["first_id"] = hit.value("first_id");
      collision["second_id"] = hit.value("second_id");
      findings_.append(collision);
    }
  } else {
    unknowns_.append(unknownQuestion(
        "Static solid interference",
        "Exact all-pairs interference was deferred for this assembly.",
        "Select a smaller scope or run an explicit bounded interference "
        "check."));
  }

  if (sweepEvaluated) {
    ++evaluated;
    if (sweepResults.isEmpty()) {
      findings_.append(geometryFinding(
          "sampled_joint_sweep", "information", "information",
          "No collision found at sampled joint positions",
          "The moving part did not share solid volume with non-joint parts at "
          "19 evenly spaced positions.",
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
  }

  addCommonUnknowns(unknowns_, sweepEvaluated);
  coverage_ = {{"evaluated", evaluated},
               {"not_evaluated", unknowns_.size()},
               {"status", unknowns_.isEmpty() ? "complete" : "partial"}};
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
  unknowns_.clear();
  coverage_.clear();
  if (geometry_status_ == "completed") {
    bool staticEvaluated = false;
    bool sweepEvaluated = false;
    for (const auto &value : findings_) {
      const auto kind = value.toMap().value("finding_kind").toString();
      staticEvaluated = staticEvaluated || kind == "static_interference";
      sweepEvaluated = sweepEvaluated || kind == "sampled_joint_sweep";
    }
    if (!staticEvaluated) {
      unknowns_.append(unknownQuestion(
          "Static solid interference",
          "The saved project contains no evaluated static-interference result.",
          "Run the static geometry screen on the current assembly bytes."));
    }
    addCommonUnknowns(unknowns_, sweepEvaluated);
    coverage_ = {{"evaluated", static_cast<int>(staticEvaluated) +
                                   static_cast<int>(sweepEvaluated)},
                 {"not_evaluated", unknowns_.size()},
                 {"status", "partial"}};
  }
  emit changed();
}
