#include "engineering_controller.hpp"

#include <QCoreApplication>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void fail(const char *message) {
  std::cerr << "FAILED: " << message << '\n';
  std::exit(1);
}

void require(const bool condition, const char *message) {
  if (!condition)
    fail(message);
}

QVariantMap overlap() {
  return {{"first_id", "bracket"},
          {"second_id", "housing"},
          {"first_name", "Bracket"},
          {"second_name", "Housing"},
          {"volume_m3", 2.5e-7},
          {"classification", "unclassified"}};
}

QVariantMap sweepCollision() {
  return {{"moving_id", "arm"},
          {"other_id", "enclosure"},
          {"moving_name", "Arm"},
          {"other_name", "Enclosure"},
          {"first_angle_deg", 35.0},
          {"maximum_volume_m3", 1.0e-7},
          {"samples_tested", 19}};
}

void clearStaticScreenIsBounded() {
  EngineeringController controller;
  controller.runGeometryChecks({}, {}, false, true);

  require(controller.geometryStatus() == "completed",
          "clear static screen completes");
  require(controller.findings().size() == 1,
          "evaluated clear assembly has one bounded result");
  const auto finding = controller.findings().front().toMap();
  require(finding.value("finding_kind") == "static_interference" &&
              finding.value("status") == "information" &&
              finding.value("title") == "No static solid interference found" &&
              finding.value("calculated").toDouble() == 0.0,
          "clear result describes only static solid interference");
  require(finding.value("assumption").toString().contains("imported pose"),
          "clear result states its imported-pose boundary");
  require(controller.unknowns().size() == 4,
          "unevaluated mechanics stay prominent");
  require(controller.coverage().value("evaluated").toInt() == 1 &&
              controller.coverage().value("not_evaluated").toInt() == 4 &&
              controller.coverage().value("status") == "partial",
          "coverage does not collapse partial work into project pass");
}

void deferredStaticScreenCannotPass() {
  EngineeringController controller;
  controller.runGeometryChecks({}, {}, false, false);

  require(controller.findings().isEmpty(),
          "deferred static work produces no clear finding");
  require(controller.unknowns().size() == 5,
          "deferred static work adds another unknown");
  const auto unknown = controller.unknowns().front().toMap();
  require(unknown.value("question") == "Static solid interference" &&
              unknown.value("status") == "not_evaluated" &&
              !unknown.value("unlock").toString().isEmpty(),
          "deferred static work explains what remains and how to unlock it");
  require(controller.coverage().value("evaluated").toInt() == 0 &&
              controller.coverage().value("not_evaluated").toInt() == 5,
          "deferred work is excluded from evaluated coverage");
}

void existingCollisionBehaviorRemainsAuthoritative() {
  EngineeringController staticController;
  staticController.runGeometryChecks({overlap()}, {}, false, true);
  require(staticController.findings().size() == 1 &&
              staticController.findings().front().toMap().value("status") ==
                  "caution",
          "unclassified exact overlap remains a caution");

  EngineeringController motionController;
  motionController.runGeometryChecks({overlap()}, {sweepCollision()}, true,
                                     true);
  require(motionController.findings().size() == 2 &&
              motionController.findings()[1].toMap().value("status") ==
                  "fail",
          "sampled motion collision remains a failure");
  require(motionController.unknowns().size() == 3 &&
              motionController.coverage().value("evaluated").toInt() == 2,
          "evaluated motion replaces only the motion unknown");
}

// Phase 6 checkpoint 2: defining a joint stores the stable CAD entity ids
// additively alongside the existing index-based identity, and -- with no
// ProjectController ever attached via setProjectController, exactly like a
// session with no project open -- persistJointBindingEdge's best-effort,
// silent contract must not crash or alter the in-session joint state.
void defineRevoluteJointStoresAdditiveEntityIdsAndIsSafeWithoutAProject() {
  EngineeringController controller;
  controller.defineRevoluteJoint(1, 2, "Z", 0, 90, 0.01, 0.02, 0.03, "arm",
                                 "base");
  const auto joint = controller.joint();
  require(controller.jointConfigured(), "defining a joint marks it configured");
  require(joint.value("source_index").toInt() == 1 &&
              joint.value("target_index").toInt() == 2 &&
              joint.value("axis").toString() == "Z" &&
              joint.value("confirmed_by_user").toBool(),
          "joint retains its existing index-based fields unchanged");
  require(joint.value("source_entity_id").toString() == "arm" &&
              joint.value("target_entity_id").toString() == "base",
          "joint additively stores the stable CAD entity ids for graph-edge "
          "persistence");
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  clearStaticScreenIsBounded();
  deferredStaticScreenCannotPass();
  existingCollisionBehaviorRemainsAuthoritative();
  defineRevoluteJointStoresAdditiveEntityIdsAndIsSafeWithoutAProject();
  return 0;
}
