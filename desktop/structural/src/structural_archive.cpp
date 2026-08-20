#include "prometheus/structural/structural_archive.hpp"

#include "prometheus/structural/gmsh_mesh.hpp"

#include "prometheus/integrity/canonical_json.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace prometheus::structural {
namespace {

using Json = nlohmann::json;
constexpr auto archiveName = "prometheus-structural-run.json";
constexpr auto archiveSchemaV1 =
    "urn:prometheus:schema:structural-run-archive:1.0.0";
constexpr auto archiveSchemaV2 =
    "urn:prometheus:schema:structural-run-archive:2.0.0";
constexpr auto archiveSchemaV3 =
    "urn:prometheus:schema:structural-run-archive:3.0.0";
constexpr auto archiveSchemaV4 =
    "urn:prometheus:schema:structural-run-archive:4.0.0";
constexpr auto setupSchemaV1 =
    "urn:prometheus:schema:reviewed-structural-setup:1.0.0";
constexpr auto setupSchemaV2 =
    "urn:prometheus:schema:reviewed-structural-setup:2.0.0";
constexpr auto compiledSetupSchemaV1 =
    "urn:prometheus:schema:compiled-structural-setup:1.0.0";
constexpr integrity::Limits structuralSetupEvidenceLimits{
    8U * 1024U * 1024U, 64U, 500000U, 10000U, 500000U,
    4U * 1024U * 1024U};
static_assert(structuralSetupEvidenceLimits.array_elements >= 480000U);

std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("archive artifact is missing");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path &path, const std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  if (!output) throw std::runtime_error("archive artifact could not be written");
}

bool exact_keys(const Json &value,
                const std::initializer_list<std::string_view> keys) {
  if (!value.is_object() || value.size() != keys.size()) return false;
  return std::ranges::all_of(keys, [&](const auto key) {
    return value.contains(std::string(key));
  });
}

bool safe_file(const std::string &name) {
  return !name.empty() && name.size() <= 128 &&
         name.find('/') == std::string::npos &&
         name.find('\\') == std::string::npos && name != "." && name != "..";
}

StructuralArchiveVerification failure(std::string code, std::string detail) {
  return {false, std::move(code), std::move(detail), std::nullopt, 0, 0,
          {}, {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
}

class ArchiveVerificationError final : public std::runtime_error {
public:
  ArchiveVerificationError(std::string code, std::string detail)
      : std::runtime_error(std::move(detail)), code_(std::move(code)) {}

  [[nodiscard]] const std::string &code() const noexcept { return code_; }

private:
  std::string code_;
};

[[noreturn]] void reject(std::string code, std::string detail) {
  throw ArchiveVerificationError(std::move(code), std::move(detail));
}

bool strict_sha256(const std::string_view value) {
  return value.size() == 71U && value.starts_with("sha256:") &&
         std::ranges::all_of(value.substr(7), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool safe_job_name(const std::string_view value) {
  return !value.empty() && value.size() <= 128U &&
         std::ranges::all_of(value, [](const unsigned char character) {
           return (character >= 'a' && character <= 'z') ||
                  (character >= 'A' && character <= 'Z') ||
                  (character >= '0' && character <= '9') ||
                  character == '_' || character == '-';
         });
}

std::string bounded_read(const std::filesystem::path &path,
                         const std::uintmax_t maximumBytes) {
  std::error_code error;
  const auto length = std::filesystem::file_size(path, error);
  if (error || !std::filesystem::is_regular_file(path))
    reject("archive_artifact_missing", "archive artifact is missing");
  if (length > maximumBytes)
    reject("archive_artifact_too_large", "archive artifact exceeds its bound");
  return read(path);
}

std::string json_string(const Json &object, const std::string_view key) {
  const auto name = std::string(key);
  if (!object.contains(name) || !object.at(name).is_string())
    reject("archive_contract_invalid", name + " must be a string");
  return object.at(name).get<std::string>();
}

bool json_bool(const Json &object, const std::string_view key) {
  const auto name = std::string(key);
  if (!object.contains(name) || !object.at(name).is_boolean())
    reject("archive_contract_invalid", name + " must be a Boolean");
  return object.at(name).get<bool>();
}

double json_number(const Json &object, const std::string_view key) {
  const auto name = std::string(key);
  if (!object.contains(name) || !object.at(name).is_number())
    reject("archive_contract_invalid", name + " must be a number");
  const auto value = object.at(name).get<double>();
  if (!std::isfinite(value))
    reject("archive_contract_invalid", name + " must be finite");
  return value;
}

std::optional<double> json_optional_number(const Json &object,
                                           const std::string_view key) {
  const auto name = std::string(key);
  if (!object.contains(name))
    reject("archive_contract_invalid", name + " is missing");
  if (object.at(name).is_null()) return std::nullopt;
  return json_number(object, key);
}

Json artifact_json(const std::string &name,
                   const CalculixArtifactIdentity &identity) {
  return {{"file", name},
          {"byte_length", identity.byte_length},
          {"sha256", identity.sha256}};
}

Json metrics_json(const CalculixMetrics &metrics) {
  return {{"maximum_displacement_m", metrics.maximum_displacement_m},
          {"maximum_von_mises_pa", metrics.maximum_von_mises_pa},
          {"displacement_rows", metrics.displacement_rows},
          {"stress_rows", metrics.stress_rows}};
}

Json convergence_json(const CalculixConvergenceEvidence &convergence) {
  return {{"step", convergence.step},
          {"increment", convergence.increment},
          {"attempt", convergence.attempt},
          {"iterations", convergence.iterations},
          {"total_time", convergence.total_time},
          {"step_time", convergence.step_time},
          {"increment_time", convergence.increment_time}};
}

bool nonnegative_json_integer(const Json &value) {
  return value.is_number_integer() &&
         (value.is_number_unsigned() || value.get<std::int64_t>() >= 0);
}

void validate_stored_result_fields(const Json &convergence,
                                   const Json &metrics,
                                   const std::string_view context) {
  if (!exact_keys(convergence,
                  {"step", "increment", "attempt", "iterations",
                   "total_time", "step_time", "increment_time"}) ||
      !convergence.at("step").is_number_integer() ||
      !convergence.at("increment").is_number_integer() ||
      !convergence.at("attempt").is_number_integer() ||
      !convergence.at("iterations").is_number_integer())
    reject("archive_contract_invalid",
           std::string(context) + " convergence is invalid");
  (void)json_number(convergence, "total_time");
  (void)json_number(convergence, "step_time");
  (void)json_number(convergence, "increment_time");

  if (!exact_keys(metrics,
                  {"maximum_displacement_m", "maximum_von_mises_pa",
                   "displacement_rows", "stress_rows"}) ||
      !nonnegative_json_integer(metrics.at("displacement_rows")) ||
      !nonnegative_json_integer(metrics.at("stress_rows")))
    reject("archive_contract_invalid",
           std::string(context) + " metrics are invalid");
  (void)json_number(metrics, "maximum_displacement_m");
  (void)json_number(metrics, "maximum_von_mises_pa");
}

constexpr double derivedReplayMultiplier = 64.0;

bool derived_number_equivalent(const double stored,
                               const double replayed) {
  if (!std::isfinite(stored) || !std::isfinite(replayed)) return false;
  if (stored == replayed) return true;
  const double scale = std::max(std::abs(stored), std::abs(replayed));
  return std::abs(stored - replayed) <=
         derivedReplayMultiplier * std::numeric_limits<double>::epsilon() *
             scale;
}

std::string round_trip_number(const double value) {
  std::ostringstream encoded;
  encoded.imbue(std::locale::classic());
  encoded << std::setprecision(std::numeric_limits<double>::max_digits10)
          << value;
  return encoded.str();
}

void reconcile_derived_number(const Json &storedDocument,
                              Json &replayedDocument,
                              const Json::json_pointer &pointer,
                              const std::string_view fieldPath) {
  try {
    const auto &storedValue = storedDocument.at(pointer);
    const auto &replayedValue = replayedDocument.at(pointer);
    if (!storedValue.is_number() || !replayedValue.is_number())
      reject("archive_contract_invalid",
             std::string(fieldPath) + " must be a finite number");
    const auto stored = storedValue.get<double>();
    const auto replayed = replayedValue.get<double>();
    if (!derived_number_equivalent(stored, replayed))
      reject("replay_numeric_mismatch",
             std::string(fieldPath) + " differs: stored=" +
                 round_trip_number(stored) +
                 " replayed=" + round_trip_number(replayed));
    replayedDocument[pointer] = storedValue;
  } catch (const Json::exception &) {
    reject("archive_contract_invalid",
           std::string(fieldPath) + " is missing or invalid");
  }
}

void require_metrics_replay(const Json &stored, Json replayed,
                            const std::string_view fieldPrefix) {
  if (!exact_keys(stored,
                  {"maximum_displacement_m", "maximum_von_mises_pa",
                   "displacement_rows", "stress_rows"}) ||
      !exact_keys(replayed,
                  {"maximum_displacement_m", "maximum_von_mises_pa",
                   "displacement_rows", "stress_rows"}) ||
      !nonnegative_json_integer(stored.at("displacement_rows")) ||
      !nonnegative_json_integer(stored.at("stress_rows")))
    reject("archive_contract_invalid",
           std::string(fieldPrefix) + " contract is invalid");
  reconcile_derived_number(
      stored, replayed,
      Json::json_pointer{"/maximum_displacement_m"},
      std::string(fieldPrefix) + ".maximum_displacement_m");
  reconcile_derived_number(
      stored, replayed,
      Json::json_pointer{"/maximum_von_mises_pa"},
      std::string(fieldPrefix) + ".maximum_von_mises_pa");
  if (stored != replayed)
    reject("replay_result_mismatch",
           std::string(fieldPrefix) +
               " non-derived fields differ from replay");
}

void require_findings_replay(const Json &stored, Json replayed,
                             const std::string_view fieldPrefix) {
  if (!stored.is_array() || !replayed.is_array() ||
      stored.size() != replayed.size())
    reject("replay_finding_mismatch",
           std::string(fieldPrefix) + " count differs from replay");
  for (std::size_t index = 0; index < stored.size(); ++index) {
    const auto pointerPrefix = "/" + std::to_string(index);
    const auto fieldPathPrefix =
        std::string(fieldPrefix) + "[" + std::to_string(index) + "]";
    reconcile_derived_number(
        stored, replayed,
        Json::json_pointer{pointerPrefix + "/measured"},
        fieldPathPrefix + ".measured");
    reconcile_derived_number(
        stored, replayed,
        Json::json_pointer{pointerPrefix + "/margin"},
        fieldPathPrefix + ".margin");
  }
  if (stored != replayed)
    reject("replay_finding_mismatch",
           std::string(fieldPrefix) +
               " non-derived fields differ from replay");
}

std::string legacy_v2_result_identity(
    const std::string &setupIdentity,
    const std::string &geometryIdentity,
    const Json &backend,
    const Json &artifacts,
    const Json &convergence,
    const Json &metrics) {
  const Json document{
      {"$schema",
       "urn:prometheus:schema:compiled-calculix-result:2.0.0"},
      {"schema_version", "2.0.0"},
      {"compiler_version", "calculix-evidence-compiler-v2"},
      {"compiled_setup_identity", setupIdentity},
      {"request_geometry_sha256", geometryIdentity},
      {"backend",
       {{"executable_sha256",
         json_string(backend, "executable_sha256")},
        {"version", json_string(backend, "version")}}},
      {"artifacts",
       {{"deck", json_string(artifacts.at("deck"), "sha256")},
        {"sta", json_string(artifacts.at("sta"), "sha256")},
        {"dat", json_string(artifacts.at("dat"), "sha256")},
        {"frd", json_string(artifacts.at("frd"), "sha256")},
        {"stdout", json_string(artifacts.at("stdout"), "sha256")},
        {"stderr", json_string(artifacts.at("stderr"), "sha256")}}},
      {"convergence",
       {{"step", convergence.at("step")},
        {"increment", convergence.at("increment")},
        {"attempt", convergence.at("attempt")},
        {"iterations", convergence.at("iterations")},
        {"total_time", convergence.at("total_time")},
        {"step_time", convergence.at("step_time")},
        {"increment_time", convergence.at("increment_time")}}},
      {"metrics",
       {{"maximum_displacement_m",
         metrics.at("maximum_displacement_m")},
        {"maximum_von_mises_pa", metrics.at("maximum_von_mises_pa")},
        {"displacement_rows", metrics.at("displacement_rows")},
        {"stress_rows", metrics.at("stress_rows")}}}};
  return integrity::sha256_bytes(
      integrity::canonicalize_json_bytes(document.dump()));
}

Json requirements_json(const StructuralRequest &request) {
  return {{"displacement_limit_m", request.displacement_limit_m},
          {"von_mises_limit_pa", request.von_mises_limit_pa},
          {"displacement_limit_basis", request.displacement_limit_basis},
          {"von_mises_limit_basis", request.von_mises_limit_basis}};
}

struct LegacyV2RefinementRecord final {
  bool complete{};
  bool criteria_satisfied{};
  double coarse_to_fine_change_fraction{};
  double maximum_allowed_change_fraction{};
  std::vector<std::string> result_sha256;
};

Json legacy_v2_refinement_json(const LegacyV2RefinementRecord &refinement) {
  return {{"complete", refinement.complete},
          {"criteria_satisfied", refinement.criteria_satisfied},
          {"coarse_to_fine_change_fraction",
           refinement.coarse_to_fine_change_fraction},
          {"maximum_allowed_change_fraction",
           refinement.maximum_allowed_change_fraction},
          {"result_sha256", refinement.result_sha256}};
}

Json findings_json(const StructuralEvaluation &evaluation) {
  Json findings = Json::array();
  for (const auto &finding : evaluation.findings)
    findings.push_back(
        {{"obligation", finding.obligation},
         {"disposition",
          finding.disposition == StructuralFindingDisposition::violated
              ? "violated"
              : "no_violation_detected_within_scope"},
         {"measured", finding.measured_value},
         {"limit", finding.limit_value},
         {"margin", finding.margin_to_limit},
         {"unit", finding.unit},
         {"scope", finding.scope},
         {"evidence_sha256", finding.evidence_sha256},
         {"assumptions", finding.assumptions}});
  return findings;
}

Json legacy_v1_findings_json(const StructuralEvaluation &evaluation) {
  Json findings = Json::array();
  for (const auto &finding : evaluation.findings)
    findings.push_back(
        {{"obligation", finding.obligation},
         {"disposition",
          finding.disposition == StructuralFindingDisposition::violated
              ? "violated"
              : "no_violation_detected_within_scope"},
         {"measured", finding.measured_value},
         {"limit", finding.limit_value},
         {"margin", finding.margin_to_limit},
         {"unit", finding.unit},
         {"scope", finding.scope}});
  return findings;
}

std::string refinement_status_string(
    const StructuralRefinementStatus status) {
  return status == StructuralRefinementStatus::accepted
             ? "accepted"
             : "indeterminate";
}

Json criterion_json(const StructuralRefinementCriterion &criterion) {
  return {{"identity", criterion.identity()},
          {"maximum_change_fraction",
           criterion.maximum_change_fraction()}};
}

std::string observable_quantity_string(
    const StructuralObservableQuantity quantity) {
  switch (quantity) {
  case StructuralObservableQuantity::displacement_magnitude_m:
    return "displacement_magnitude_m";
  case StructuralObservableQuantity::von_mises_stress_pa:
    return "von_mises_stress_pa";
  }
  throw std::invalid_argument("unsupported structural observable quantity");
}

Json observable_region_json(const StructuralObservableRegion &region) {
  switch (region.kind) {
  case StructuralObservableRegionKind::all_nodes:
    return {{"kind", "all_nodes"}};
  case StructuralObservableRegionKind::all_elements:
    return {{"kind", "all_elements"}};
  case StructuralObservableRegionKind::element_centroid_box_m:
    return {{"kind", "element_centroid_box_m"},
            {"minimum_m", region.element_centroid_box_m.minimum_m},
            {"maximum_m", region.element_centroid_box_m.maximum_m}};
  }
  throw std::invalid_argument("unsupported structural observable region");
}

Json observable_definition_json(
    const StructuralObservableDefinition &definition) {
  return {{"identity", definition.identity},
          {"id", definition.spec.id},
          {"quantity",
           observable_quantity_string(definition.spec.quantity)},
          {"reduction", "maximum"},
          {"region", observable_region_json(definition.spec.region)},
          {"maximum_change_fraction",
           definition.spec.maximum_change_fraction}};
}

Json criterion_v4_json(const StructuralRefinementCriterion &criterion) {
  Json observables = Json::array();
  for (const auto &definition : criterion.observables())
    observables.push_back(observable_definition_json(definition));
  return {{"identity", criterion.identity()},
          {"observables", std::move(observables)}};
}

Json boundary_correspondence_json(
    const ReviewedBoundaryCorrespondence &correspondence) {
  return {
      {"coarse_setup_identity", correspondence.coarse_setup_identity()},
      {"fine_setup_identity", correspondence.fine_setup_identity()},
      {"load_region_confirmed", correspondence.load_region_confirmed()},
      {"restraint_region_confirmed",
       correspondence.restraint_region_confirmed()},
      {"coarse_load_area_m2", correspondence.coarse_load_area_m2()},
      {"fine_load_area_m2", correspondence.fine_load_area_m2()},
      {"coarse_restraint_area_m2",
       correspondence.coarse_restraint_area_m2()},
      {"fine_restraint_area_m2",
       correspondence.fine_restraint_area_m2()}};
}

Json comparison_json(const VerifiedStructuralRefinement &refinement) {
  return {
      {"status", refinement_status_string(refinement.status())},
      {"displacement_change_fraction",
       refinement.displacement_change_fraction()},
      {"stress_change_fraction", refinement.stress_change_fraction()},
      {"maximum_change_fraction", refinement.maximum_change_fraction()},
      {"maximum_allowed_change_fraction",
       refinement.coarse().criterion().maximum_change_fraction()},
      {"setup_sha256",
       {refinement.coarse().setup().identity,
        refinement.fine().setup().identity}},
      {"result_sha256",
       {refinement.coarse().run().validated_result->identity,
        refinement.fine().run().validated_result->identity}}};
}

Json comparison_json(const StructuralRefinementSummary &summary) {
  return {
      {"status", refinement_status_string(summary.status)},
      {"displacement_change_fraction",
       summary.displacement_change_fraction},
      {"stress_change_fraction", summary.stress_change_fraction},
      {"maximum_change_fraction", summary.maximum_change_fraction},
      {"maximum_allowed_change_fraction",
       summary.maximum_allowed_change_fraction},
      {"setup_sha256", summary.setup_sha256},
      {"result_sha256", summary.result_sha256}};
}

std::string observable_status_string(
    const StructuralObservableConvergenceStatus status) {
  return status == StructuralObservableConvergenceStatus::accepted
             ? "accepted"
             : "indeterminate";
}

Json observable_comparison_json(
    const StructuralObservableComparison &comparison) {
  return {
      {"definition_identity", comparison.definition.identity},
      {"coarse_value", comparison.coarse_value},
      {"fine_value", comparison.fine_value},
      {"coarse_selected_rows", comparison.coarse_selected_rows},
      {"fine_selected_rows", comparison.fine_selected_rows},
      {"change_fraction", comparison.change_fraction},
      {"maximum_allowed_change_fraction",
       comparison.definition.spec.maximum_change_fraction},
      {"status", observable_status_string(comparison.status)}};
}

std::array<double, 3> deck_precision_position(
    const std::array<double, 3> &position) {
  std::array<double, 3> result{};
  for (std::size_t axis = 0; axis < result.size(); ++axis) {
    std::ostringstream encoded;
    encoded.imbue(std::locale::classic());
    encoded << std::scientific << std::setprecision(10) << position[axis];
    std::istringstream decoded(encoded.str());
    decoded.imbue(std::locale::classic());
    decoded >> result[axis];
    if (!decoded || !std::isfinite(result[axis]))
      throw std::invalid_argument(
          "structural diagnostic position is invalid");
  }
  return result;
}

Json global_extremum_json(
    const StructuralGlobalExtremumDiagnostic &diagnostic) {
  return {
      {"quantity", observable_quantity_string(diagnostic.quantity)},
      {"coarse_value", diagnostic.coarse_value},
      {"fine_value", diagnostic.fine_value},
      {"coarse_entity_id", diagnostic.coarse_entity_id},
      {"fine_entity_id", diagnostic.fine_entity_id},
      // The authoritative deck stores node coordinates at ten-digit
      // scientific precision. Persist locations at that same precision so
      // replayed element centroids compare exactly across the trust boundary.
      {"coarse_position_m",
       deck_precision_position(diagnostic.coarse_position_m)},
      {"fine_position_m",
       deck_precision_position(diagnostic.fine_position_m)},
      {"change_fraction", diagnostic.change_fraction},
      {"comparison_threshold", diagnostic.comparison_threshold},
      {"participated_in_acceptance",
       diagnostic.participated_in_acceptance},
      {"within_threshold", diagnostic.within_threshold}};
}

template <typename RefinementLike>
Json comparison_v4_json(const RefinementLike &refinement,
                        const std::vector<std::string> &setupIdentities,
                        const std::vector<std::string> &resultIdentities) {
  Json observables = Json::array();
  for (const auto &comparison : refinement.observable_comparisons())
    observables.push_back(observable_comparison_json(comparison));
  Json globalExtrema = Json::array();
  for (const auto &diagnostic : refinement.global_extremum_diagnostics())
    globalExtrema.push_back(global_extremum_json(diagnostic));
  return {{"status", refinement_status_string(refinement.status())},
          {"observables", std::move(observables)},
          {"global_extrema", std::move(globalExtrema)},
          {"setup_sha256", setupIdentities},
          {"result_sha256", resultIdentities}};
}

Json comparison_v4_json(const VerifiedStructuralRefinement &refinement) {
  return comparison_v4_json(
      refinement,
      {refinement.coarse().setup().identity,
       refinement.fine().setup().identity},
      {refinement.coarse().run().validated_result->identity,
       refinement.fine().run().validated_result->identity});
}

Json comparison_v4_json(const StructuralRefinementSummary &summary) {
  struct SummaryView final {
    const StructuralRefinementSummary &summary;
    [[nodiscard]] StructuralRefinementStatus status() const noexcept {
      return summary.status;
    }
    [[nodiscard]] const std::vector<StructuralObservableComparison> &
    observable_comparisons() const noexcept {
      return summary.observables;
    }
    [[nodiscard]] const std::vector<StructuralGlobalExtremumDiagnostic> &
    global_extremum_diagnostics() const noexcept {
      return summary.global_extrema;
    }
  };
  return comparison_v4_json(SummaryView{summary}, summary.setup_sha256,
                            summary.result_sha256);
}

void require_v3_comparison_replay(const Json &stored, Json replayed) {
  if (!exact_keys(stored,
                  {"status", "displacement_change_fraction",
                   "stress_change_fraction", "maximum_change_fraction",
                   "maximum_allowed_change_fraction", "setup_sha256",
                   "result_sha256"}) ||
      !exact_keys(replayed,
                  {"status", "displacement_change_fraction",
                   "stress_change_fraction", "maximum_change_fraction",
                   "maximum_allowed_change_fraction", "setup_sha256",
                   "result_sha256"}))
    reject("replay_finding_mismatch",
           "v3 comparison contract differs from replay");
  for (const auto *field : {"displacement_change_fraction",
                            "stress_change_fraction",
                            "maximum_change_fraction"})
    reconcile_derived_number(
        stored, replayed, Json::json_pointer{"/" + std::string(field)},
        "comparison." + std::string(field));
  if (stored != replayed)
    reject("replay_finding_mismatch",
           "v3 non-derived comparison fields differ from replay");
}

void require_v4_comparison_replay(const Json &stored, Json replayed) {
  if (!exact_keys(stored, {"status", "observables", "global_extrema",
                           "setup_sha256", "result_sha256"}) ||
      !exact_keys(replayed, {"status", "observables", "global_extrema",
                             "setup_sha256", "result_sha256"}) ||
      !stored.at("observables").is_array() ||
      !replayed.at("observables").is_array() ||
      !stored.at("global_extrema").is_array() ||
      !replayed.at("global_extrema").is_array() ||
      stored.at("observables").size() !=
          replayed.at("observables").size() ||
      stored.at("global_extrema").size() !=
          replayed.at("global_extrema").size())
    reject("replay_finding_mismatch",
           "v4 comparison contract differs from replay");

  const auto reconcileArray = [&](const std::string_view arrayName,
                                  const std::size_t size) {
    for (std::size_t index = 0; index < size; ++index)
      for (const auto *field : {"coarse_value", "fine_value",
                                "change_fraction"}) {
        const auto pointer = "/" + std::string(arrayName) + "/" +
                             std::to_string(index) + "/" + field;
        const auto fieldPath = "comparison." + std::string(arrayName) +
                               "[" + std::to_string(index) + "]." + field;
        reconcile_derived_number(stored, replayed,
                                 Json::json_pointer{pointer}, fieldPath);
      }
  };
  reconcileArray("observables", stored.at("observables").size());
  reconcileArray("global_extrema", stored.at("global_extrema").size());
  if (stored != replayed)
    reject("replay_finding_mismatch",
           "v4 non-derived comparison fields differ from replay");
}

Json unknowns_json(const StructuralEvaluation &evaluation) {
  Json unknowns = Json::array();
  for (const auto &unknown : evaluation.unknowns)
    unknowns.push_back({{"obligation", unknown.obligation},
                        {"code", unknown.code},
                        {"detail", unknown.detail}});
  return unknowns;
}

Json mesh_json(const StructuralSetup &setup) {
  return {
      {"source_sha256", setup.mesh_controls.mesh_sha256},
      {"coordinate_scale_to_m",
       setup.mesh_controls.coordinate_scale_to_m},
      {"node_count", setup.mesh.nodes.size()},
      {"element_count", setup.mesh.elements.size()},
      {"boundary_face_count", setup.boundary_faces.size()},
      {"minimum_size_m", setup.mesh_controls.minimum_size_m},
      {"maximum_size_m", setup.mesh_controls.maximum_size_m},
      {"target_size_m", setup.mesh_controls.target_size_m},
      {"minimum_mean_ratio_threshold",
       setup.mesh_controls.minimum_mean_ratio_threshold},
      {"observed_minimum_mean_ratio",
       setup.mesh_controls.observed_minimum_mean_ratio},
      {"mesher_identity", setup.mesh_controls.mesher_identity},
      {"reviewed", setup.mesh_controls.reviewed}};
}

struct PreparedV3Sample final {
  const CompletedStructuralSample *sample{};
  std::string role;
  std::string setup_name;
  std::string deck_name;
  std::string dat_name;
  std::string frd_name;
  std::string sta_name;
  std::string stdout_name;
  std::string stderr_name;
  std::string setup_bytes;
  CalculixArtifactIdentity setup_identity;
};

bool file_matches(const std::filesystem::path &path,
                  const CalculixArtifactIdentity &identity) {
  std::error_code error;
  const auto length = std::filesystem::file_size(path, error);
  return !error && std::filesystem::is_regular_file(path) &&
         length == identity.byte_length && strict_sha256(identity.sha256) &&
         integrity::sha256_file(path) == identity.sha256;
}

std::string deck_difference_detail(const std::string &stored,
                                   const std::string &replayed);

std::string_view trimmed(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r");
  if (first == std::string_view::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r");
  return value.substr(first, last - first + 1U);
}

std::optional<double> deck_number(const std::string_view token) {
  std::istringstream input{std::string(token)};
  input.imbue(std::locale::classic());
  double value{};
  char trailing{};
  if (!(input >> value) || (input >> trailing) || !std::isfinite(value))
    return std::nullopt;
  return value;
}

std::vector<std::string_view> comma_tokens(const std::string_view line) {
  std::vector<std::string_view> tokens;
  std::size_t start = 0U;
  while (true) {
    const auto comma = line.find(',', start);
    tokens.push_back(trimmed(line.substr(
        start, comma == std::string_view::npos
                   ? std::string_view::npos
                   : comma - start)));
    if (comma == std::string_view::npos)
      return tokens;
    start = comma + 1U;
  }
}

bool deck_lines_equivalent(const std::string_view stored,
                           const std::string_view replayed) {
  if (stored == replayed)
    return true;
  const auto storedTokens = comma_tokens(stored);
  const auto replayedTokens = comma_tokens(replayed);
  if (storedTokens.size() != replayedTokens.size() ||
      storedTokens.size() < 2U)
    return false;
  for (std::size_t index = 0; index < storedTokens.size(); ++index) {
    const auto storedToken = storedTokens[index];
    const auto replayedToken = replayedTokens[index];
    if (storedToken == replayedToken)
      continue;
    const bool floatingToken =
        storedToken.find_first_of(".eEdD") != std::string_view::npos ||
        replayedToken.find_first_of(".eEdD") != std::string_view::npos;
    const auto storedNumber = deck_number(storedToken);
    const auto replayedNumber = deck_number(replayedToken);
    if (!floatingToken || !storedNumber || !replayedNumber)
      return false;
    const double scale =
        std::max(std::abs(*storedNumber), std::abs(*replayedNumber));
    if (scale == 0.0)
      continue;
    // Node coordinates are authoritative at the deck's ten-digit scientific
    // precision. Recomputed surface areas and distributed nodal forces can
    // accumulate a few such round-off units during setup replay.
    constexpr double roundTripRelativeTolerance = 5.0e-10;
    if (std::abs(*storedNumber - *replayedNumber) >
        scale * roundTripRelativeTolerance)
      return false;
  }
  return true;
}

bool decks_round_trip_equivalent(const std::string &stored,
                                 const std::string &replayed) {
  std::istringstream storedInput(stored);
  std::istringstream replayedInput(replayed);
  std::string storedLine;
  std::string replayedLine;
  while (true) {
    const bool hasStored = static_cast<bool>(
        std::getline(storedInput, storedLine));
    const bool hasReplayed = static_cast<bool>(
        std::getline(replayedInput, replayedLine));
    if (hasStored != hasReplayed)
      return false;
    if (!hasStored)
      return true;
    if (storedLine.size() > 4096U || replayedLine.size() > 4096U)
      return false;
    if (!deck_lines_equivalent(storedLine, replayedLine))
      return false;
  }
}

std::string deck_difference_detail(const std::string &stored,
                                   const std::string &replayed) {
  std::istringstream storedInput(stored);
  std::istringstream replayedInput(replayed);
  std::string storedLine;
  std::string replayedLine;
  std::size_t lineNumber{};
  while (true) {
    const bool hasStored =
        static_cast<bool>(std::getline(storedInput, storedLine));
    const bool hasReplayed =
        static_cast<bool>(std::getline(replayedInput, replayedLine));
    ++lineNumber;
    if (hasStored != hasReplayed)
      return "v3 solver deck line count differs at line " +
             std::to_string(lineNumber);
    if (!hasStored)
      return "v3 solver deck differs after semantic comparison";
    if (!deck_lines_equivalent(storedLine, replayedLine))
      return "v3 solver deck differs at line " +
             std::to_string(lineNumber) + "; stored line [" +
             storedLine.substr(0U, 160U) + "]; replayed line [" +
             replayedLine.substr(0U, 160U) + "]";
  }
}

std::string compiled_setup_v1_identity(
    const std::string_view canonicalSetupEvidence,
    const std::string_view calculixDeck) {
  const auto identityDocument = integrity::canonicalize_json_bytes(
      Json{{"$schema", compiledSetupSchemaV1},
           {"schema_version", "1.0.0"},
           {"compiler_version", "structural-setup-compiler-v1"},
           {"setup_evidence_sha256",
            integrity::sha256_bytes(canonicalSetupEvidence)},
           {"calculix_deck_sha256", integrity::sha256_bytes(calculixDeck)}}
          .dump());
  return integrity::sha256_bytes(identityDocument);
}

PreparedV3Sample prepare_v3_sample(
    const CompletedStructuralSample &sample,
    const StructuralSampleRole expectedRole,
    const std::filesystem::path &workingDirectory) {
  if (sample.role() != expectedRole ||
      sample.run().status != SolverRunStatus::completed ||
      !sample.run().validated_result ||
      !sample.run().validated_result->complete() ||
      !sample.run().validated_result->metrics ||
      !sample.run().validated_result->convergence)
    throw std::invalid_argument(
        "v3 archives require two completed structural samples");
  if (!safe_job_name(sample.options().job_name))
    throw std::invalid_argument("v3 sample job name is unsafe");
  const auto &setup = sample.setup();
  const auto &validated = *sample.run().validated_result;
  if (!strict_sha256(setup.identity) ||
      validated.compiled_setup_identity != setup.identity ||
      validated.artifacts.deck.sha256 !=
          integrity::sha256_bytes(setup.calculix_deck) ||
      validated.artifacts.deck.byte_length != setup.calculix_deck.size())
    throw std::invalid_argument(
        "v3 sample result is detached from its compiled setup");

  PreparedV3Sample prepared;
  prepared.sample = &sample;
  prepared.role = expectedRole == StructuralSampleRole::coarse
                      ? "coarse"
                      : "fine";
  const auto &job = sample.options().job_name;
  prepared.setup_name = job + ".reviewed-structural-setup.json";
  prepared.deck_name = job + ".inp";
  prepared.dat_name = job + ".dat";
  prepared.frd_name = job + ".frd";
  prepared.sta_name = job + ".sta";
  prepared.stdout_name = job + ".stdout.txt";
  prepared.stderr_name = job + ".stderr.txt";
  for (const auto *name : {&prepared.setup_name, &prepared.deck_name,
                           &prepared.dat_name, &prepared.frd_name,
                           &prepared.sta_name, &prepared.stdout_name,
                           &prepared.stderr_name})
    if (!safe_file(*name))
      throw std::invalid_argument(
          "v3 sample artifact filename is unsafe");

  prepared.setup_bytes = integrity::verify_canonical_bytes(
      setup.canonical_setup_evidence, structuralSetupEvidenceLimits);
  const auto setupJson = Json::parse(prepared.setup_bytes);
  if (!setupJson.is_object() ||
      setupJson.value("$schema", "") != setupSchemaV2 ||
      setupJson.value("schema_version", "") != "2.0.0" ||
      setupJson.value("analysis_id", "") != setup.request.analysis_id ||
      setupJson.value("component_name", "") !=
          setup.request.component_name ||
      setupJson.value("geometry_sha256", "") !=
          setup.request.geometry_sha256)
    throw std::invalid_argument(
        "v3 reviewed setup evidence binding is invalid");
  prepared.setup_identity = {
      integrity::sha256_bytes(prepared.setup_bytes),
      prepared.setup_bytes.size()};

  if (!file_matches(workingDirectory / prepared.deck_name,
                    validated.artifacts.deck) ||
      !file_matches(workingDirectory / prepared.dat_name,
                    validated.artifacts.dat) ||
      !file_matches(workingDirectory / prepared.frd_name,
                    validated.artifacts.frd) ||
      !file_matches(workingDirectory / prepared.sta_name,
                    validated.artifacts.sta) ||
      validated.artifacts.standard_output !=
          CalculixArtifactIdentity{
              integrity::sha256_bytes(sample.run().standard_output),
              sample.run().standard_output.size()} ||
      validated.artifacts.standard_error !=
          CalculixArtifactIdentity{
              integrity::sha256_bytes(sample.run().standard_error),
              sample.run().standard_error.size()})
    throw std::runtime_error(
        "active v3 solver artifacts changed before archiving");
  if (std::filesystem::exists(workingDirectory / prepared.setup_name) ||
      std::filesystem::exists(workingDirectory / prepared.stdout_name) ||
      std::filesystem::exists(workingDirectory / prepared.stderr_name))
    throw std::runtime_error("v3 structural archive output already exists");
  return prepared;
}

Json sample_json(const PreparedV3Sample &prepared) {
  const auto &sample = *prepared.sample;
  const auto &validated = *sample.run().validated_result;
  return {
      {"role", prepared.role},
      {"compiled_setup_identity", sample.setup().identity},
      {"validated_result_identity", validated.identity},
      {"mesh", mesh_json(sample.setup().reviewed_setup)},
      {"execution",
       {{"job_name", sample.options().job_name},
        {"exit_code", sample.run().exit_code},
        {"elapsed_ms", sample.run().elapsed.count()},
        {"status", "completed"}}},
      {"backend",
       {{"executable_sha256", validated.backend.executable_sha256},
        {"version", validated.backend.version}}},
      {"convergence", convergence_json(*validated.convergence)},
      {"artifacts",
       {{"setup", artifact_json(prepared.setup_name,
                                prepared.setup_identity)},
        {"deck", artifact_json(prepared.deck_name,
                               validated.artifacts.deck)},
        {"dat", artifact_json(prepared.dat_name,
                              validated.artifacts.dat)},
        {"frd", artifact_json(prepared.frd_name,
                              validated.artifacts.frd)},
        {"sta", artifact_json(prepared.sta_name,
                              validated.artifacts.sta)},
        {"stdout", artifact_json(prepared.stdout_name,
                                 validated.artifacts.standard_output)},
        {"stderr", artifact_json(prepared.stderr_name,
                                 validated.artifacts.standard_error)}}},
      {"metrics", metrics_json(*validated.metrics)}};
}

BoundarySelection selection_from_json(const Json &value) {
  if (!exact_keys(value, {"label", "face_node_ids", "node_ids", "area_m2"}) ||
      !value.at("face_node_ids").is_array() ||
      !value.at("node_ids").is_array())
    reject("setup_contract_invalid", "reviewed boundary selection is invalid");
  BoundarySelection selection;
  selection.label = json_string(value, "label");
  selection.area_m2 = json_number(value, "area_m2");
  for (const auto &face : value.at("face_node_ids")) {
    if (!face.is_array() || face.size() != 3U ||
        !std::ranges::all_of(face, [](const auto &id) {
          return id.is_number_integer();
        }))
      reject("setup_contract_invalid", "reviewed face identity is invalid");
    selection.face_node_ids.push_back(
        {face[0].get<int>(), face[1].get<int>(), face[2].get<int>()});
  }
  for (const auto &node : value.at("node_ids")) {
    if (!node.is_number_integer())
      reject("setup_contract_invalid", "reviewed node identity is invalid");
    selection.node_ids.push_back(node.get<int>());
  }
  return selection;
}

StructuralSetup deserialize_setup(const std::string &setupBytes,
                                  const std::string &deckBytes) {
  const auto canonical = integrity::verify_canonical_bytes(
      setupBytes, structuralSetupEvidenceLimits);
  const auto root = Json::parse(canonical);
  if (!exact_keys(root, {"$schema", "schema_version", "analysis_id",
                         "component_name", "geometry_sha256", "mesh",
                         "material", "load", "restraint", "requirement",
                         "scenario", "selection_patch_angle_degrees"}) ||
      root.at("$schema") != setupSchemaV2 ||
      root.at("schema_version") != "2.0.0")
    reject("setup_contract_invalid", "reviewed setup v2 root is invalid");

  const auto &mesh = root.at("mesh");
  if (!exact_keys(mesh, {"source_sha256", "coordinate_scale_to_m",
                         "node_count", "element_count", "boundary_face_count",
                         "node_ids", "element_ids", "minimum_size_m",
                         "maximum_size_m", "target_size_m",
                         "minimum_mean_ratio_threshold",
                         "observed_minimum_mean_ratio", "mesher_identity",
                         "reviewed"}))
    reject("setup_contract_invalid", "reviewed mesh evidence is invalid");
  const auto &material = root.at("material");
  if (!exact_keys(material, {"designation", "temper", "product_form",
                             "source_sha256", "applicability",
                             "youngs_modulus_pa", "poisson_ratio",
                             "reviewed"}))
    reject("setup_contract_invalid", "reviewed material evidence is invalid");
  const auto &load = root.at("load");
  if (!exact_keys(load, {"selection", "total_force_n", "reviewed"}) ||
      !load.at("total_force_n").is_array() ||
      load.at("total_force_n").size() != 3U)
    reject("setup_contract_invalid", "reviewed load evidence is invalid");
  const auto &restraint = root.at("restraint");
  if (!exact_keys(restraint, {"selection", "reviewed"}))
    reject("setup_contract_invalid", "reviewed restraint evidence is invalid");
  const auto &requirement = root.at("requirement");
  if (!exact_keys(requirement,
                  {"displacement_limit_m", "von_mises_limit_pa",
                   "source_or_exploratory_rationale", "displacement_limit_basis",
                   "von_mises_limit_basis", "reviewed"}))
    reject("setup_contract_invalid", "reviewed requirement evidence is invalid");
  const auto &scenario = root.at("scenario");
  if (!exact_keys(scenario, {"description", "confirmed"}))
    reject("setup_contract_invalid", "reviewed scenario evidence is invalid");

  StructuralSetup setup;
  setup.analysis_id = json_string(root, "analysis_id");
  setup.component_name = json_string(root, "component_name");
  setup.geometry_sha256 = json_string(root, "geometry_sha256");
  setup.mesh = parse_gmsh_abaqus_mesh(deckBytes, 1.0);
  setup.boundary_faces = extract_boundary_faces(setup.mesh);
  setup.material = {
      .designation = json_string(material, "designation"),
      .source_sha256 = json_string(material, "source_sha256"),
      .applicability = json_string(material, "applicability"),
      .youngs_modulus_pa = json_number(material, "youngs_modulus_pa"),
      .poisson_ratio = json_number(material, "poisson_ratio"),
      .reviewed = json_bool(material, "reviewed"),
      .temper = json_string(material, "temper"),
      .product_form = json_string(material, "product_form")};
  std::array<double, 3> force{};
  for (std::size_t index = 0; index < force.size(); ++index) {
    if (!load.at("total_force_n")[index].is_number())
      reject("setup_contract_invalid", "reviewed force vector is invalid");
    force[index] = load.at("total_force_n")[index].get<double>();
    if (!std::isfinite(force[index]))
      reject("setup_contract_invalid", "reviewed force vector is non-finite");
  }
  setup.load = {.selection = selection_from_json(load.at("selection")),
                .total_force_n = force,
                .reviewed = json_bool(load, "reviewed")};
  setup.restraint = {
      .selection = selection_from_json(restraint.at("selection")),
      .reviewed = json_bool(restraint, "reviewed")};
  setup.requirement = {
      .displacement_limit_m =
          json_optional_number(requirement, "displacement_limit_m"),
      .von_mises_limit_pa =
          json_optional_number(requirement, "von_mises_limit_pa"),
      .source_or_exploratory_rationale =
          json_string(requirement, "source_or_exploratory_rationale"),
      .reviewed = json_bool(requirement, "reviewed"),
      .displacement_limit_basis =
          json_string(requirement, "displacement_limit_basis"),
      .von_mises_limit_basis =
          json_string(requirement, "von_mises_limit_basis")};
  setup.mesh_controls = {
      .minimum_size_m = json_number(mesh, "minimum_size_m"),
      .maximum_size_m = json_number(mesh, "maximum_size_m"),
      .mesher_identity = json_string(mesh, "mesher_identity"),
      .reviewed = json_bool(mesh, "reviewed"),
      .mesh_sha256 = json_string(mesh, "source_sha256"),
      .coordinate_scale_to_m = json_number(mesh, "coordinate_scale_to_m"),
      .target_size_m = json_number(mesh, "target_size_m"),
      .minimum_mean_ratio_threshold =
          json_number(mesh, "minimum_mean_ratio_threshold"),
      .observed_minimum_mean_ratio =
          json_number(mesh, "observed_minimum_mean_ratio")};
  setup.scenario_description = json_string(scenario, "description");
  setup.scenario_confirmed = json_bool(scenario, "confirmed");
  setup.selection_patch_angle_degrees =
      json_number(root, "selection_patch_angle_degrees");
  return setup;
}

LegacyV2RefinementRecord legacy_v2_refinement_from_json(const Json &value) {
  if (!exact_keys(value, {"complete", "criteria_satisfied",
                          "coarse_to_fine_change_fraction",
                          "maximum_allowed_change_fraction",
                          "result_sha256"}) ||
      !value.at("result_sha256").is_array())
    reject("archive_contract_invalid", "refinement evidence is invalid");
  LegacyV2RefinementRecord result{
      .complete = json_bool(value, "complete"),
      .criteria_satisfied = json_bool(value, "criteria_satisfied"),
      .coarse_to_fine_change_fraction =
          json_number(value, "coarse_to_fine_change_fraction"),
      .maximum_allowed_change_fraction =
          json_number(value, "maximum_allowed_change_fraction")};
  for (const auto &identity : value.at("result_sha256")) {
    if (!identity.is_string() ||
        !strict_sha256(identity.get_ref<const std::string &>()))
      reject("archive_contract_invalid", "refinement result identity is invalid");
    result.result_sha256.push_back(identity.get<std::string>());
  }
  return result;
}

struct PersistedArtifacts final {
  std::string setup;
  std::string deck;
  std::string dat;
  std::string sta;
  std::string standard_output;
  std::string standard_error;
  CalculixArtifactIdentity frd;
};

PersistedArtifacts verify_and_load_artifacts(
    const std::filesystem::path &directory, const Json &artifacts,
    std::set<std::string> *allNames = nullptr) {
  if (!exact_keys(artifacts,
                  {"setup", "deck", "dat", "frd", "sta", "stdout", "stderr"}))
    reject("archive_contract_invalid", "archive artifact set is invalid");
  std::set<std::string> names;
  std::map<std::string, std::string> loaded;
  CalculixArtifactIdentity frd;
  const std::map<std::string, std::uintmax_t> bounds{
      {"setup", 8U * 1024U * 1024U},
      {"deck", 512U * 1024U * 1024U},
      {"dat", 512U * 1024U * 1024U},
      {"sta", 64U * 1024U * 1024U},
      {"stdout", 16U * 1024U * 1024U},
      {"stderr", 16U * 1024U * 1024U}};
  for (const auto key : {"setup", "deck", "dat", "frd", "sta", "stdout",
                         "stderr"}) {
    const auto &reference = artifacts.at(key);
    if (!exact_keys(reference, {"file", "byte_length", "sha256"}) ||
        !reference.at("byte_length").is_number_unsigned())
      reject("archive_contract_invalid", "artifact reference is invalid");
    const auto name = json_string(reference, "file");
    const auto hash = json_string(reference, "sha256");
    if (!safe_file(name) || !names.insert(name).second ||
        (allNames && !allNames->insert(name).second) ||
        !strict_sha256(hash))
      reject("archive_contract_invalid", "artifact identity is unsafe");
    const auto length = reference.at("byte_length").get<std::uintmax_t>();
    const auto path = directory / name;
    std::error_code error;
    const auto actualLength = std::filesystem::file_size(path, error);
    if (error || !std::filesystem::is_regular_file(path) ||
        actualLength != length)
      reject("artifact_identity_mismatch", name + " bytes changed");
    if (std::string_view(key) == "frd") {
      if (length == 0U)
        reject("archive_contract_invalid", "FRD evidence must be nonempty");
      if (integrity::sha256_file(path) != hash)
        reject("artifact_identity_mismatch", name + " bytes changed");
      frd = {.sha256 = hash, .byte_length = length};
    } else {
      auto bytes = bounded_read(path, bounds.at(key));
      if (integrity::sha256_bytes(bytes) != hash)
        reject("artifact_identity_mismatch", name + " bytes changed");
      loaded.emplace(key, std::move(bytes));
    }
  }
  return {.setup = std::move(loaded.at("setup")),
          .deck = std::move(loaded.at("deck")),
          .dat = std::move(loaded.at("dat")),
          .sta = std::move(loaded.at("sta")),
          .standard_output = std::move(loaded.at("stdout")),
          .standard_error = std::move(loaded.at("stderr")),
          .frd = std::move(frd)};
}

std::optional<std::string> legacy_v1_binding_issue(
    const StructuralRequest &request, const CalculixDat &result) {
  std::set<int> expectedNodes;
  for (const auto &node : request.nodes)
    expectedNodes.insert(node.id);
  std::set<int> actualNodes;
  for (const auto &row : result.displacements)
    if (!actualNodes.insert(row.node_id).second)
      return "duplicate_displacement_node";
  if (actualNodes != expectedNodes)
    return "displacement_mesh_mismatch";

  std::set<int> expectedElements;
  for (const auto &element : request.elements)
    expectedElements.insert(element.id);
  std::set<int> actualElements;
  std::set<std::pair<int, int>> integrationPoints;
  for (const auto &row : result.stresses) {
    actualElements.insert(row.element_id);
    if (!integrationPoints.emplace(row.element_id, row.integration_point).second)
      return "duplicate_stress_integration_point";
  }
  if (actualElements != expectedElements)
    return "stress_mesh_mismatch";
  return std::nullopt;
}

StructuralEvaluation replay_legacy_v1_findings(
    const StructuralRequest &request, const CalculixMetrics &metrics) {
  StructuralEvaluation result;
  result.execution_status = SolverRunStatus::completed;
  result.declared_obligations =
      static_cast<int>(request.displacement_limit_m.has_value()) +
      static_cast<int>(request.von_mises_limit_pa.has_value());
  result.limitation =
      "These findings do not establish safety, fatigue life, buckling, contact, "
      "fastener adequacy, nonlinear behavior, or project-wide correctness.";
  const auto append = [&](std::string obligation, const double measured,
                          const double limit, std::string unit) {
    result.findings.push_back(
        {.obligation = std::move(obligation),
         .disposition =
             measured <= limit
                 ? StructuralFindingDisposition::no_violation_detected_within_scope
                 : StructuralFindingDisposition::violated,
         .measured_value = measured,
         .limit_value = limit,
         .margin_to_limit = limit - measured,
         .unit = std::move(unit),
         .scope =
             "isotropic linear-elastic C3D4 model under the confirmed scenario"});
  };
  if (request.displacement_limit_m)
    append("maximum_displacement", metrics.maximum_displacement_m,
           *request.displacement_limit_m, "m");
  if (request.von_mises_limit_pa)
    append("maximum_von_mises_stress", metrics.maximum_von_mises_pa,
           *request.von_mises_limit_pa, "Pa");
  result.evaluated_obligations = static_cast<int>(result.findings.size());
  return result;
}

std::optional<StructuralEvaluation> replay_legacy_v2_findings(
    const StructuralRequest &request,
    const CompiledCalculixResult &validatedResult,
    const LegacyV2RefinementRecord &refinement) {
  if (!validatedResult.complete() || !validatedResult.metrics ||
      !refinement.complete || !refinement.criteria_satisfied ||
      !std::isfinite(refinement.coarse_to_fine_change_fraction) ||
      refinement.coarse_to_fine_change_fraction < 0.0 ||
      !std::isfinite(refinement.maximum_allowed_change_fraction) ||
      refinement.maximum_allowed_change_fraction <= 0.0 ||
      refinement.maximum_allowed_change_fraction > 1.0 ||
      refinement.coarse_to_fine_change_fraction >
          refinement.maximum_allowed_change_fraction ||
      refinement.result_sha256.size() < 2U ||
      refinement.result_sha256.size() > 16U)
    return std::nullopt;

  std::set<std::string> identities;
  for (const auto &identity : refinement.result_sha256)
    if (!strict_sha256(identity) || !identities.insert(identity).second)
      return std::nullopt;
  if (!identities.contains(validatedResult.identity))
    return std::nullopt;

  const auto validLimit = [](const std::optional<double> value,
                             const std::string &basis) {
    return !value || (std::isfinite(*value) && *value > 0.0 &&
                      !basis.empty());
  };
  if (!request.requirements_reviewed || !request.scenario_confirmed ||
      !validLimit(request.displacement_limit_m,
                  request.displacement_limit_basis) ||
      !validLimit(request.von_mises_limit_pa,
                  request.von_mises_limit_basis))
    return std::nullopt;

  StructuralEvaluation result;
  result.execution_status = SolverRunStatus::completed;
  result.declared_obligations =
      static_cast<int>(request.displacement_limit_m.has_value()) +
      static_cast<int>(request.von_mises_limit_pa.has_value());
  result.limitation =
      "These findings do not establish safety, fatigue life, buckling, contact, "
      "fastener adequacy, nonlinear behavior, or project-wide correctness.";
  auto evidence = refinement.result_sha256;
  evidence.push_back(validatedResult.identity);
  std::ranges::sort(evidence);
  evidence.erase(std::unique(evidence.begin(), evidence.end()), evidence.end());
  const auto append = [&](std::string obligation, const double measured,
                          const double limit, std::string unit,
                          std::vector<std::string> findingEvidence) {
    const auto margin = limit - measured;
    result.findings.push_back(
        {.obligation = std::move(obligation),
         .disposition =
             margin > 0.0
                 ? StructuralFindingDisposition::no_violation_detected_within_scope
                 : StructuralFindingDisposition::violated,
         .measured_value = measured,
         .limit_value = limit,
         .margin_to_limit = margin,
         .unit = std::move(unit),
         .scope =
             "isotropic linear-elastic C3D4 model under the confirmed scenario "
             "with accepted mesh-refinement evidence",
         .evidence_sha256 = std::move(findingEvidence),
         .assumptions =
             {"small-deformation linear static response",
              "isotropic linear-elastic material behavior",
              "reviewed loads and fully fixed restraints represent the scenario",
              "reported extrema are bounded by the submitted mesh and solver output"}});
  };
  if (request.displacement_limit_m)
    append("maximum_displacement",
           validatedResult.metrics->maximum_displacement_m,
           *request.displacement_limit_m, "m", evidence);
  if (request.von_mises_limit_pa)
    append("maximum_von_mises_stress",
           validatedResult.metrics->maximum_von_mises_pa,
           *request.von_mises_limit_pa, "Pa", std::move(evidence));
  result.evaluated_obligations = static_cast<int>(result.findings.size());
  return result;
}

} // namespace

StructuralArchive write_structural_refinement_archive(
    const VerifiedStructuralRefinement &refinement,
    const StructuralEvaluation &evaluation) {
  const auto &coarse = refinement.coarse();
  const auto &fine = refinement.fine();
  const bool typed = !coarse.criterion().legacy_global_extrema_only();
  if (coarse.options().working_directory.empty() ||
      fine.options().working_directory.empty() ||
      !std::filesystem::is_directory(coarse.options().working_directory) ||
      !std::filesystem::is_directory(fine.options().working_directory))
    throw std::invalid_argument(
        "v3 refinement samples require one existing study directory");
  const auto coarseDirectory =
      std::filesystem::canonical(coarse.options().working_directory);
  const auto fineDirectory =
      std::filesystem::canonical(fine.options().working_directory);
  if (coarseDirectory != fineDirectory ||
      coarse.options().job_name == fine.options().job_name)
    throw std::invalid_argument(
        "v3 refinement samples require one directory and distinct jobs");
  const auto manifestPath = coarseDirectory / archiveName;
  if (std::filesystem::exists(manifestPath))
    throw std::runtime_error("v3 structural archive manifest already exists");

  const int declaredObligations =
      static_cast<int>(fine.setup().request.displacement_limit_m.has_value()) +
      static_cast<int>(fine.setup().request.von_mises_limit_pa.has_value());
  const bool accepted =
      refinement.status() == StructuralRefinementStatus::accepted;
  const bool comparisonMatches =
      evaluation.comparison &&
      (typed ? comparison_v4_json(*evaluation.comparison) ==
                   comparison_v4_json(refinement)
             : comparison_json(*evaluation.comparison) ==
                   comparison_json(refinement));
  const bool typedCoverageMatches =
      evaluation.evaluated_obligations ==
          static_cast<int>(evaluation.findings.size()) &&
      evaluation.declared_obligations ==
          static_cast<int>(evaluation.findings.size() +
                           evaluation.unknowns.size()) &&
      (accepted ||
       (evaluation.evaluated_obligations == 0 &&
        evaluation.findings.empty() &&
        evaluation.unknowns.size() ==
            static_cast<std::size_t>(declaredObligations)));
  if (evaluation.execution_status != SolverRunStatus::completed ||
      !comparisonMatches ||
      evaluation.declared_obligations != declaredObligations ||
      evaluation.limitation.empty() ||
      (typed && !typedCoverageMatches) ||
      (!typed && accepted &&
       (evaluation.evaluated_obligations != declaredObligations ||
        evaluation.findings.size() !=
            static_cast<std::size_t>(declaredObligations))) ||
      (!typed && !accepted &&
       (evaluation.evaluated_obligations != 0 ||
        !evaluation.findings.empty())))
    throw std::invalid_argument(
        typed ? "v4 evaluation does not match the verified refinement status"
              : "v3 evaluation does not match the verified refinement status");

  auto coarsePrepared = prepare_v3_sample(
      coarse, StructuralSampleRole::coarse, coarseDirectory);
  auto finePrepared = prepare_v3_sample(
      fine, StructuralSampleRole::fine, coarseDirectory);
  std::set<std::string> artifactNames;
  for (const auto *prepared : {&coarsePrepared, &finePrepared})
    for (const auto *name : {
             &prepared->setup_name, &prepared->deck_name,
             &prepared->dat_name, &prepared->frd_name,
             &prepared->sta_name, &prepared->stdout_name,
             &prepared->stderr_name})
      if (!artifactNames.insert(*name).second)
        throw std::invalid_argument(
            "v3 sample artifact filenames must be globally unique");

  Json document{
      {"$schema", typed ? archiveSchemaV4 : archiveSchemaV3},
      {"schema_version", typed ? "4.0.0" : "3.0.0"},
      {"archive_kind", "linear_static_refinement_study"},
      {"analysis_id", fine.setup().request.analysis_id},
      {"component_name", fine.setup().request.component_name},
      {"geometry_sha256", fine.setup().request.geometry_sha256},
      {"criterion", typed ? criterion_v4_json(coarse.criterion())
                           : criterion_json(coarse.criterion())},
      {"boundary_correspondence",
       boundary_correspondence_json(refinement.boundary_correspondence())},
      {"samples",
       {{"coarse", sample_json(coarsePrepared)},
        {"fine", sample_json(finePrepared)}}},
      {"comparison", typed ? comparison_v4_json(refinement)
                            : comparison_json(refinement)},
      {"coverage",
       {{"declared_obligations", evaluation.declared_obligations},
        {"evaluated_obligations", evaluation.evaluated_obligations}}},
      {"findings", findings_json(evaluation)},
      {"limitation", evaluation.limitation}};
  if (typed) document["unknowns"] = unknowns_json(evaluation);
  const auto canonical = integrity::canonicalize_json_bytes(document.dump());

  std::vector<std::filesystem::path> created;
  try {
    for (const auto *prepared : {&coarsePrepared, &finePrepared}) {
      const auto setupPath = coarseDirectory / prepared->setup_name;
      created.push_back(setupPath);
      write(setupPath, prepared->setup_bytes);
      const auto stdoutPath = coarseDirectory / prepared->stdout_name;
      created.push_back(stdoutPath);
      write(stdoutPath, prepared->sample->run().standard_output);
      const auto stderrPath = coarseDirectory / prepared->stderr_name;
      created.push_back(stderrPath);
      write(stderrPath, prepared->sample->run().standard_error);
    }
    write(manifestPath, canonical);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(manifestPath, ignored);
    for (const auto &path : created)
      std::filesystem::remove(path, ignored);
    throw;
  }
  return {manifestPath,
          integrity::sha256_bytes(canonical),
          typed ? "4.0.0" : "3.0.0",
          fine.run().validated_result->identity,
          coarse.run().validated_result->identity};
}

namespace {

StructuralRefinementCriterion criterion_from_v3_json(const Json &value) {
  if (!exact_keys(value, {"identity", "maximum_change_fraction"}))
    reject("archive_contract_invalid",
           "v3 refinement criterion is invalid");
  const auto identity = json_string(value, "identity");
  const auto maximum = json_number(value, "maximum_change_fraction");
  if (!strict_sha256(identity))
    reject("archive_contract_invalid",
           "v3 refinement criterion identity is invalid");
  try {
    auto criterion = compile_structural_refinement_criterion(maximum);
    if (criterion.identity() != identity ||
        criterion_json(criterion) != value)
      reject("archive_contract_invalid",
             "v3 refinement criterion does not reproduce its identity");
    return criterion;
  } catch (const ArchiveVerificationError &) {
    throw;
  } catch (const std::exception &error) {
    reject("archive_contract_invalid",
           std::string("v3 refinement criterion is invalid: ") +
               error.what());
  }
}

std::array<double, 3> vector3_from_json(const Json &value,
                                        const std::string_view field) {
  if (!value.is_array() || value.size() != 3U)
    reject("archive_contract_invalid",
           std::string(field) + " must contain three numbers");
  std::array<double, 3> result{};
  for (std::size_t axis = 0; axis < result.size(); ++axis) {
    if (!value[axis].is_number())
      reject("archive_contract_invalid",
             std::string(field) + " must contain three numbers");
    result[axis] = value[axis].get<double>();
    if (!std::isfinite(result[axis]))
      reject("archive_contract_invalid",
             std::string(field) + " must contain finite numbers");
  }
  return result;
}

StructuralRefinementCriterion criterion_from_v4_json(const Json &value) {
  if (!exact_keys(value, {"identity", "observables"}) ||
      !value.at("observables").is_array() ||
      value.at("observables").empty() ||
      value.at("observables").size() > 128U)
    reject("archive_contract_invalid",
           "v4 refinement criterion is invalid");
  const auto identity = json_string(value, "identity");
  if (!strict_sha256(identity))
    reject("archive_contract_invalid",
           "v4 refinement criterion identity is invalid");

  std::vector<StructuralObservableSpec> specs;
  specs.reserve(value.at("observables").size());
  for (const auto &observable : value.at("observables")) {
    if (!exact_keys(observable,
                    {"identity", "id", "quantity", "reduction", "region",
                     "maximum_change_fraction"}))
      reject("archive_contract_invalid",
             "v4 observable definition is invalid");
    const auto definitionIdentity = json_string(observable, "identity");
    const auto id = json_string(observable, "id");
    const auto quantity = json_string(observable, "quantity");
    const auto reduction = json_string(observable, "reduction");
    if (!strict_sha256(definitionIdentity) || reduction != "maximum")
      reject("archive_contract_invalid",
             "v4 observable definition identity or reduction is invalid");

    StructuralObservableSpec spec;
    spec.id = id;
    if (quantity == "displacement_magnitude_m")
      spec.quantity =
          StructuralObservableQuantity::displacement_magnitude_m;
    else if (quantity == "von_mises_stress_pa")
      spec.quantity = StructuralObservableQuantity::von_mises_stress_pa;
    else
      reject("archive_contract_invalid",
             "v4 observable quantity is invalid");
    spec.reduction = StructuralObservableReduction::maximum;
    spec.maximum_change_fraction =
        json_number(observable, "maximum_change_fraction");

    const auto &region = observable.at("region");
    const auto kind = json_string(region, "kind");
    if (kind == "all_nodes" || kind == "all_elements") {
      if (!exact_keys(region, {"kind"}))
        reject("archive_contract_invalid",
               "v4 global observable region is invalid");
      spec.region.kind =
          kind == "all_nodes"
              ? StructuralObservableRegionKind::all_nodes
              : StructuralObservableRegionKind::all_elements;
    } else if (kind == "element_centroid_box_m") {
      if (!exact_keys(region, {"kind", "minimum_m", "maximum_m"}))
        reject("archive_contract_invalid",
               "v4 regional observable bounds are invalid");
      spec.region.kind =
          StructuralObservableRegionKind::element_centroid_box_m;
      spec.region.element_centroid_box_m.minimum_m =
          vector3_from_json(region.at("minimum_m"), "minimum_m");
      spec.region.element_centroid_box_m.maximum_m =
          vector3_from_json(region.at("maximum_m"), "maximum_m");
    } else {
      reject("archive_contract_invalid",
             "v4 observable region kind is invalid");
    }
    specs.push_back(std::move(spec));
  }

  try {
    auto criterion =
        compile_structural_refinement_criterion(std::move(specs));
    if (criterion.identity() != identity ||
        criterion_v4_json(criterion) != value)
      reject("archive_contract_invalid",
             "v4 refinement criterion does not reproduce its identity");
    return criterion;
  } catch (const ArchiveVerificationError &) {
    throw;
  } catch (const std::exception &error) {
    reject("archive_contract_invalid",
           std::string("v4 refinement criterion is invalid: ") +
               error.what());
  }
}

CompletedStructuralSamplePtr replay_v3_sample(
    const std::filesystem::path &directory,
    const Json &value,
    const StructuralSampleRole expectedRole,
    const StructuralRefinementCriterion &criterion,
    const std::string &analysisId,
    const std::string &componentName,
    const std::string &geometryIdentity,
    std::set<std::string> &allArtifactNames) {
  if (!exact_keys(value, {"role", "compiled_setup_identity",
                          "validated_result_identity", "mesh", "execution",
                          "backend", "convergence", "artifacts",
                          "metrics"}))
    reject("archive_contract_invalid",
           "v3 sample root contract is invalid");
  const auto expectedRoleName =
      expectedRole == StructuralSampleRole::coarse ? "coarse" : "fine";
  if (json_string(value, "role") != expectedRoleName)
    reject("archive_contract_invalid", "v3 sample role is invalid");
  const auto setupIdentity =
      json_string(value, "compiled_setup_identity");
  const auto resultIdentity =
      json_string(value, "validated_result_identity");
  if (!strict_sha256(setupIdentity) || !strict_sha256(resultIdentity))
    reject("archive_contract_invalid",
           "v3 sample identities are invalid");

  const auto &execution = value.at("execution");
  if (!exact_keys(execution,
                  {"job_name", "exit_code", "elapsed_ms", "status"}) ||
      !execution.at("exit_code").is_number_integer() ||
      !execution.at("elapsed_ms").is_number_integer() ||
      execution.at("exit_code").get<int>() != 0 ||
      execution.at("elapsed_ms").get<std::int64_t>() < 0 ||
      execution.at("status") != "completed")
    reject("archive_contract_invalid",
           "v3 sample execution evidence is invalid");
  const auto jobName = json_string(execution, "job_name");
  if (!safe_job_name(jobName))
    reject("archive_contract_invalid", "v3 sample job name is invalid");

  const auto &backend = value.at("backend");
  if (!exact_keys(backend, {"executable_sha256", "version"}))
    reject("archive_contract_invalid",
           "v3 sample backend identity is invalid");
  const auto backendHash = json_string(backend, "executable_sha256");
  const auto backendVersion = json_string(backend, "version");
  if (!strict_sha256(backendHash) || backendVersion.empty())
    reject("archive_contract_invalid",
           "v3 sample backend identity is invalid");
  validate_stored_result_fields(value.at("convergence"),
                                value.at("metrics"),
                                "v3 sample");

  const auto artifacts = verify_and_load_artifacts(
      directory, value.at("artifacts"), &allArtifactNames);
  auto reviewedSetup = deserialize_setup(artifacts.setup, artifacts.deck);
  auto compiledSetup = compile_structural_setup(reviewedSetup);
  if (compiledSetup.canonical_setup_evidence != artifacts.setup)
    reject("setup_binding_mismatch",
           "v3 reviewed setup bytes do not recompile exactly");
  if (!decks_round_trip_equivalent(artifacts.deck,
                                   compiledSetup.calculix_deck))
    reject("setup_binding_mismatch",
           deck_difference_detail(artifacts.deck,
                                  compiledSetup.calculix_deck));
  compiledSetup.calculix_deck = artifacts.deck;
  compiledSetup.identity =
      compiled_setup_v1_identity(artifacts.setup, artifacts.deck);
  if (compiledSetup.identity != setupIdentity)
    reject("setup_binding_mismatch",
           "v3 compiled setup identity does not replay exactly");
  if (compiledSetup.request.analysis_id != analysisId ||
      compiledSetup.request.component_name != componentName ||
      compiledSetup.request.geometry_sha256 != geometryIdentity)
    reject("setup_binding_mismatch",
           "v3 setup project identity differs from the archive root");
  if (value.at("mesh") != mesh_json(compiledSetup.reviewed_setup))
    reject("setup_binding_mismatch",
           "v3 sample mesh evidence differs from the reviewed setup");

  const CalculixRunEvidence evidence{
      .process_exit_code = execution.at("exit_code").get<int>(),
      .solver_executable_sha256 = backendHash,
      .solver_version = backendVersion,
      .deck_bytes = artifacts.deck,
      .standard_output = artifacts.standard_output,
      .standard_error = artifacts.standard_error,
      .status_bytes = artifacts.sta,
      .data_bytes = artifacts.dat,
      .frd_sha256 = artifacts.frd.sha256,
      .frd_byte_length = artifacts.frd.byte_length};
  auto replayedResult = compile_calculix_result(compiledSetup, evidence);
  if (!replayedResult.complete()) {
    const auto detail = replayedResult.issues.empty()
                            ? "persisted v3 solver evidence is incomplete"
                            : replayedResult.issues.front().code + ": " +
                                  replayedResult.issues.front().message;
    reject("replay_result_invalid", detail);
  }
  const auto legacyIdentity = legacy_v2_result_identity(
      setupIdentity, geometryIdentity, backend, value.at("artifacts"),
      value.at("convergence"), value.at("metrics"));
  if (replayedResult.compiled_setup_identity != setupIdentity ||
      (replayedResult.identity != resultIdentity &&
       legacyIdentity != resultIdentity))
    reject("replay_result_identity_mismatch",
           "v3 solver evidence matches neither supported result identity");
  if (backend !=
          Json{{"executable_sha256",
                replayedResult.backend.executable_sha256},
               {"version", replayedResult.backend.version}} ||
      value.at("convergence") !=
          convergence_json(*replayedResult.convergence))
    reject("replay_result_mismatch",
           "v3 solver evidence differs from stored result fields");
  require_metrics_replay(
      value.at("metrics"), metrics_json(*replayedResult.metrics),
      std::string("samples.") + expectedRoleName + ".metrics");
  replayedResult.identity = resultIdentity;

  SolverRunOptions options{
      .executable = {},
      .working_directory = directory,
      .job_name = jobName,
      .timeout = std::chrono::minutes(5)};
  SolverRunResult run{
      .status = SolverRunStatus::completed,
      .exit_code = execution.at("exit_code").get<int>(),
      .elapsed = std::chrono::milliseconds(
          execution.at("elapsed_ms").get<std::int64_t>()),
      .standard_output = artifacts.standard_output,
      .standard_error = artifacts.standard_error,
      .detail = "replayed_v3_archive",
      .validated_result = std::move(replayedResult)};
  return compile_completed_structural_sample(
      expectedRole, criterion, std::move(options),
      std::move(compiledSetup), std::move(run));
}

StructuralArchiveVerification verify_v4_archive(
    const std::filesystem::path &manifestPath, const Json &root) {
  if (!exact_keys(root, {"$schema", "schema_version", "archive_kind",
                         "analysis_id", "component_name", "geometry_sha256",
                         "criterion", "boundary_correspondence", "samples",
                         "comparison", "coverage", "findings", "unknowns",
                         "limitation"}) ||
      root.at("$schema") != archiveSchemaV4 ||
      root.at("schema_version") != "4.0.0" ||
      root.at("archive_kind") != "linear_static_refinement_study")
    return failure("archive_contract_invalid",
                   "archive v4 root contract is invalid");
  const auto analysisId = json_string(root, "analysis_id");
  const auto componentName = json_string(root, "component_name");
  const auto geometryIdentity = json_string(root, "geometry_sha256");
  if (analysisId.empty() || componentName.empty() ||
      !strict_sha256(geometryIdentity))
    return failure("archive_contract_invalid",
                   "archive v4 project identities are invalid");
  const auto criterion = criterion_from_v4_json(root.at("criterion"));

  const auto &samples = root.at("samples");
  if (!exact_keys(samples, {"coarse", "fine"}))
    return failure("archive_contract_invalid",
                   "archive v4 sample set is invalid");
  std::set<std::string> artifactNames;
  auto coarse = replay_v3_sample(
      manifestPath.parent_path(), samples.at("coarse"),
      StructuralSampleRole::coarse, criterion, analysisId, componentName,
      geometryIdentity, artifactNames);
  auto fine = replay_v3_sample(
      manifestPath.parent_path(), samples.at("fine"),
      StructuralSampleRole::fine, criterion, analysisId, componentName,
      geometryIdentity, artifactNames);
  if (artifactNames.size() != 14U)
    return failure("archive_contract_invalid",
                   "archive v4 must declare fourteen unique artifacts");

  const auto &persistedBoundary = root.at("boundary_correspondence");
  if (!exact_keys(
          persistedBoundary,
          {"coarse_setup_identity", "fine_setup_identity",
           "load_region_confirmed", "restraint_region_confirmed",
           "coarse_load_area_m2", "fine_load_area_m2",
           "coarse_restraint_area_m2", "fine_restraint_area_m2"}))
    return failure("archive_contract_invalid",
                   "archive v4 boundary correspondence is invalid");
  const auto boundary = review_structural_boundary_correspondence(
      coarse->setup(), fine->setup(),
      json_bool(persistedBoundary, "load_region_confirmed"),
      json_bool(persistedBoundary, "restraint_region_confirmed"));
  if (boundary_correspondence_json(boundary) != persistedBoundary)
    return failure("replay_refinement_mismatch",
                   "archive v4 boundary review differs from both setups");

  const auto compiled =
      compile_structural_refinement(coarse, fine, boundary);
  if (!compiled.complete()) {
    const auto detail = compiled.issues().empty()
                            ? "v4 refinement pair is invalid"
                            : compiled.issues().front().code + ": " +
                                  compiled.issues().front().message;
    return failure("replay_refinement_invalid", detail);
  }
  auto evaluation = compile_structural_findings(*compiled.value());
  const Json expectedCoverage{
      {"declared_obligations", evaluation.declared_obligations},
      {"evaluated_obligations", evaluation.evaluated_obligations}};
  require_v4_comparison_replay(root.at("comparison"),
                               comparison_v4_json(*compiled.value()));
  if (root.at("coverage") != expectedCoverage)
    return failure("replay_finding_mismatch",
                   "v4 raw evidence produces different coverage");
  require_findings_replay(root.at("findings"), findings_json(evaluation),
                          "findings");
  if (root.at("unknowns") != unknowns_json(evaluation))
    return failure("replay_finding_mismatch",
                   "v4 raw evidence produces different unknowns");
  if (json_string(root, "limitation") != evaluation.limitation)
    return failure("replay_finding_mismatch",
                   "v4 raw evidence produces a different limitation");

  const auto &fineResult = *fine->run().validated_result;
  return {true,
          "verified",
          "v4 two-sample setup, solver evidence, scoped comparison, unknowns, and findings replay verified",
          fineResult.metrics,
          evaluation.declared_obligations,
          evaluation.evaluated_obligations,
          "4.0.0",
          fineResult.identity,
          fineResult.normalized,
          fine->setup().reviewed_setup,
          fine->setup(),
          std::move(evaluation),
          compiled.value()};
}

StructuralArchiveVerification verify_v3_archive(
    const std::filesystem::path &manifestPath, const Json &root) {
  if (!exact_keys(root, {"$schema", "schema_version", "archive_kind",
                         "analysis_id", "component_name", "geometry_sha256",
                         "criterion", "boundary_correspondence", "samples",
                         "comparison", "coverage", "findings", "limitation"}) ||
      root.at("$schema") != archiveSchemaV3 ||
      root.at("schema_version") != "3.0.0" ||
      root.at("archive_kind") != "linear_static_refinement_study")
    return failure("archive_contract_invalid",
                   "archive v3 root contract is invalid");
  const auto analysisId = json_string(root, "analysis_id");
  const auto componentName = json_string(root, "component_name");
  const auto geometryIdentity = json_string(root, "geometry_sha256");
  if (analysisId.empty() || componentName.empty() ||
      !strict_sha256(geometryIdentity))
    return failure("archive_contract_invalid",
                   "archive v3 project identities are invalid");
  const auto criterion = criterion_from_v3_json(root.at("criterion"));

  const auto &samples = root.at("samples");
  if (!exact_keys(samples, {"coarse", "fine"}))
    return failure("archive_contract_invalid",
                   "archive v3 sample set is invalid");
  std::set<std::string> artifactNames;
  auto coarse = replay_v3_sample(
      manifestPath.parent_path(), samples.at("coarse"),
      StructuralSampleRole::coarse, criterion, analysisId, componentName,
      geometryIdentity, artifactNames);
  auto fine = replay_v3_sample(
      manifestPath.parent_path(), samples.at("fine"),
      StructuralSampleRole::fine, criterion, analysisId, componentName,
      geometryIdentity, artifactNames);
  if (artifactNames.size() != 14U)
    return failure("archive_contract_invalid",
                   "archive v3 must declare fourteen unique artifacts");

  const auto &persistedBoundary = root.at("boundary_correspondence");
  if (!exact_keys(
          persistedBoundary,
          {"coarse_setup_identity", "fine_setup_identity",
           "load_region_confirmed", "restraint_region_confirmed",
           "coarse_load_area_m2", "fine_load_area_m2",
           "coarse_restraint_area_m2", "fine_restraint_area_m2"}))
    return failure("archive_contract_invalid",
                   "archive v3 boundary correspondence is invalid");
  const auto boundary = review_structural_boundary_correspondence(
      coarse->setup(), fine->setup(),
      json_bool(persistedBoundary, "load_region_confirmed"),
      json_bool(persistedBoundary, "restraint_region_confirmed"));
  if (boundary_correspondence_json(boundary) != persistedBoundary)
    return failure("replay_refinement_mismatch",
                   "archive v3 boundary review differs from both setups");

  const auto compiled =
      compile_structural_refinement(coarse, fine, boundary);
  if (!compiled.complete()) {
    const auto detail = compiled.issues().empty()
                            ? "v3 refinement pair is invalid"
                            : compiled.issues().front().code + ": " +
                                  compiled.issues().front().message;
    return failure("replay_refinement_invalid", detail);
  }
  auto evaluation = compile_structural_findings(*compiled.value());
  const Json expectedCoverage{
      {"declared_obligations", evaluation.declared_obligations},
      {"evaluated_obligations", evaluation.evaluated_obligations}};
  require_v3_comparison_replay(root.at("comparison"),
                               comparison_json(*compiled.value()));
  if (root.at("coverage") != expectedCoverage ||
      json_string(root, "limitation") != evaluation.limitation)
    return failure("replay_finding_mismatch",
                   "v3 raw evidence produces different comparison or findings");
  require_findings_replay(root.at("findings"), findings_json(evaluation),
                          "findings");

  const auto &fineResult = *fine->run().validated_result;
  return {true,
          "verified",
          "v3 two-sample setup, solver evidence, comparison, and findings replay verified",
          fineResult.metrics,
          evaluation.declared_obligations,
          evaluation.evaluated_obligations,
          "3.0.0",
          fineResult.identity,
          fineResult.normalized,
          fine->setup().reviewed_setup,
          fine->setup(),
          std::move(evaluation),
          compiled.value()};
}

StructuralArchiveVerification verify_v2_archive(
    const std::filesystem::path &manifestPath, const Json &root) {
  if (!exact_keys(root, {"$schema", "schema_version", "archive_kind",
                         "analysis_id", "component_name", "geometry_sha256",
                         "job_name", "compiled_setup_identity",
                         "validated_result_identity", "execution", "backend",
                         "convergence", "artifacts", "metrics", "requirements",
                         "refinement", "coverage", "findings", "limitation"}) ||
      root.at("$schema") != archiveSchemaV2 ||
      root.at("schema_version") != "2.0.0" ||
      root.at("archive_kind") != "completed_linear_static_run")
    return failure("archive_contract_invalid", "archive v2 root contract is invalid");

  const auto analysisId = json_string(root, "analysis_id");
  const auto componentName = json_string(root, "component_name");
  const auto geometryIdentity = json_string(root, "geometry_sha256");
  const auto jobName = json_string(root, "job_name");
  const auto setupIdentity = json_string(root, "compiled_setup_identity");
  const auto resultIdentity = json_string(root, "validated_result_identity");
  if (analysisId.empty() || componentName.empty() ||
      !strict_sha256(geometryIdentity) || !safe_job_name(jobName) ||
      !strict_sha256(setupIdentity) || !strict_sha256(resultIdentity))
    return failure("archive_contract_invalid", "archive v2 identities are invalid");

  const auto &execution = root.at("execution");
  if (!exact_keys(execution, {"exit_code", "elapsed_ms", "status"}) ||
      !execution.at("exit_code").is_number_integer() ||
      !execution.at("elapsed_ms").is_number_integer() ||
      execution.at("exit_code").get<int>() != 0 ||
      execution.at("elapsed_ms").get<std::int64_t>() < 0 ||
      execution.at("status") != "completed")
    return failure("archive_contract_invalid", "archive execution evidence is invalid");
  const auto &backend = root.at("backend");
  if (!exact_keys(backend, {"executable_sha256", "version"}))
    return failure("archive_contract_invalid", "archive backend identity is invalid");
  const auto backendHash = json_string(backend, "executable_sha256");
  const auto backendVersion = json_string(backend, "version");
  if (!strict_sha256(backendHash) || backendVersion.empty())
    return failure("archive_contract_invalid", "archive backend identity is invalid");
  validate_stored_result_fields(root.at("convergence"),
                                root.at("metrics"), "archive v2");

  const auto artifacts = verify_and_load_artifacts(
      manifestPath.parent_path(), root.at("artifacts"));
  auto deserializedSetup =
      deserialize_setup(artifacts.setup, artifacts.deck);
  auto compiledSetup = compile_structural_setup(deserializedSetup);
  if (compiledSetup.canonical_setup_evidence != artifacts.setup ||
      compiledSetup.calculix_deck != artifacts.deck ||
      compiledSetup.identity != setupIdentity ||
      compiledSetup.request.analysis_id != analysisId ||
      compiledSetup.request.component_name != componentName ||
      compiledSetup.request.geometry_sha256 != geometryIdentity)
    return failure("setup_binding_mismatch",
                   "persisted setup does not reproduce the archived setup identity");
  if (root.at("requirements") != requirements_json(compiledSetup.request))
    return failure("setup_binding_mismatch",
                   "archive requirements differ from the reviewed setup");

  const CalculixRunEvidence evidence{
      .process_exit_code = execution.at("exit_code").get<int>(),
      .solver_executable_sha256 = backendHash,
      .solver_version = backendVersion,
      .deck_bytes = artifacts.deck,
      .standard_output = artifacts.standard_output,
      .standard_error = artifacts.standard_error,
      .status_bytes = artifacts.sta,
      .data_bytes = artifacts.dat,
      .frd_sha256 = artifacts.frd.sha256,
      .frd_byte_length = artifacts.frd.byte_length};
  auto replayedResult = compile_calculix_result(compiledSetup, evidence);
  if (!replayedResult.complete()) {
    const auto detail = replayedResult.issues.empty()
                            ? "persisted solver evidence is incomplete"
                            : replayedResult.issues.front().code + ": " +
                                  replayedResult.issues.front().message;
    return failure("replay_result_invalid", detail);
  }
  const auto legacyIdentity = legacy_v2_result_identity(
      setupIdentity, geometryIdentity, backend, root.at("artifacts"),
      root.at("convergence"), root.at("metrics"));
  if (replayedResult.compiled_setup_identity != setupIdentity ||
      legacyIdentity != resultIdentity)
    return failure("replay_result_identity_mismatch",
                   "persisted solver evidence produces a different result identity");
  if (root.at("backend") !=
          Json{{"executable_sha256", replayedResult.backend.executable_sha256},
               {"version", replayedResult.backend.version}} ||
      root.at("convergence") != convergence_json(*replayedResult.convergence))
    return failure("replay_result_mismatch",
                   "persisted solver evidence differs from archived result fields");
  require_metrics_replay(root.at("metrics"),
                         metrics_json(*replayedResult.metrics), "metrics");
  replayedResult.identity = resultIdentity;

  const auto refinement =
      legacy_v2_refinement_from_json(root.at("refinement"));
  auto evaluation = replay_legacy_v2_findings(
      compiledSetup.request, replayedResult, refinement);
  if (!evaluation)
    return failure(
        "replay_finding_mismatch",
        "legacy v2 refinement claim is invalid or detached from its result");
  const Json expectedCoverage{
      {"declared_obligations", evaluation->declared_obligations},
      {"evaluated_obligations", evaluation->evaluated_obligations}};
  if (evaluation->execution_status != SolverRunStatus::completed ||
      root.at("refinement") != legacy_v2_refinement_json(refinement) ||
      root.at("coverage") != expectedCoverage ||
      json_string(root, "limitation") != evaluation->limitation)
    return failure("replay_finding_mismatch",
                   "persisted evidence produces different scoped findings");
  require_findings_replay(root.at("findings"), findings_json(*evaluation),
                          "findings");
  return {true,
          "verified",
          "v2 artifact identities, setup, solver evidence, and findings replay verified",
          replayedResult.metrics,
          evaluation->declared_obligations,
          evaluation->evaluated_obligations,
          "2.0.0",
          replayedResult.identity,
          std::move(replayedResult.normalized),
          std::move(deserializedSetup),
          std::move(compiledSetup),
          std::move(*evaluation)};
}

} // namespace

StructuralArchiveVerification verify_structural_archive(
    const std::filesystem::path &manifestPath) noexcept {
  try {
    const auto bytes = bounded_read(manifestPath, 8U * 1024U * 1024U);
    const auto canonical = integrity::verify_canonical_bytes(bytes);
    const auto root = Json::parse(canonical);
    if (root.is_object() && root.value("$schema", "") == archiveSchemaV4 &&
        root.value("schema_version", "") == "4.0.0")
      return verify_v4_archive(manifestPath, root);
    if (root.is_object() && root.value("$schema", "") == archiveSchemaV3 &&
        root.value("schema_version", "") == "3.0.0")
      return verify_v3_archive(manifestPath, root);
    if (root.is_object() && root.value("$schema", "") == archiveSchemaV2 &&
        root.value("schema_version", "") == "2.0.0")
      return verify_v2_archive(manifestPath, root);
    if (!exact_keys(root, {"$schema", "schema_version", "archive_kind",
                           "analysis_id", "component_name", "geometry_sha256",
                           "solver_identity", "job_name", "execution",
                           "artifacts", "metrics", "requirements", "coverage", "findings",
                           "limitation"}) ||
        root.at("$schema") != archiveSchemaV1 ||
        root.at("schema_version") != "1.0.0" ||
        root.at("archive_kind") != "completed_linear_static_run")
      return failure("archive_contract_invalid", "archive root contract is invalid");
    const auto &artifacts = root.at("artifacts");
    if (!exact_keys(artifacts, {"setup", "deck", "dat", "frd", "sta", "stdout", "stderr"}))
      return failure("archive_contract_invalid", "archive artifact set is invalid");
    const auto directory = manifestPath.parent_path();
    for (const auto key : {"setup", "deck", "dat", "frd", "sta", "stdout", "stderr"}) {
      const auto &reference = artifacts.at(key);
      if (!exact_keys(reference, {"file", "byte_length", "sha256"}) ||
          !reference.at("file").is_string() ||
          !reference.at("byte_length").is_number_unsigned() ||
          !reference.at("sha256").is_string())
        return failure("archive_contract_invalid", "artifact reference is invalid");
      const auto name = reference.at("file").get<std::string>();
      if (!safe_file(name))
        return failure("unsafe_artifact_path", "artifact path is not a safe filename");
      const auto artifactBytes = read(directory / name);
      if (artifactBytes.size() != reference.at("byte_length").get<std::size_t>() ||
          integrity::sha256_bytes(artifactBytes) != reference.at("sha256").get<std::string>())
        return failure("artifact_identity_mismatch", name + " bytes changed");
    }
    const auto parsed = parse_calculix_dat(read(directory /
        artifacts.at("dat").at("file").get<std::string>()));
    const auto parsedMetrics = summarize_calculix_dat(parsed);
    const auto &metrics = root.at("metrics");
    require_metrics_replay(metrics, metrics_json(parsedMetrics), "metrics");
    const auto setupBytes = read(directory /
        artifacts.at("setup").at("file").get<std::string>());
    const auto setup = Json::parse(integrity::verify_canonical_bytes(setupBytes));
    if (!setup.is_object() || setup.value("$schema", "") != setupSchemaV1 ||
        setup.value("analysis_id", "") != root.value("analysis_id", "") ||
        setup.value("component_name", "") != root.value("component_name", "") ||
        setup.value("geometry_sha256", "") != root.value("geometry_sha256", ""))
      return failure("setup_binding_mismatch", "reviewed setup identity differs from archive");
    const auto &coverage = root.at("coverage");
    if (!exact_keys(coverage, {"declared_obligations", "evaluated_obligations"}) ||
        !coverage.at("declared_obligations").is_number_integer() ||
        !coverage.at("evaluated_obligations").is_number_integer())
      return failure("archive_contract_invalid", "archive coverage is invalid");
    const auto &requirements = root.at("requirements");
    if (!exact_keys(requirements, {"displacement_limit_m", "von_mises_limit_pa"}))
      return failure("archive_contract_invalid", "archive requirements are invalid");
    StructuralRequest replayRequest;
    if (setup.contains("mesh") && setup.at("mesh").is_object() &&
        setup.at("mesh").contains("node_ids") &&
        setup.at("mesh").contains("element_ids")) {
      if (!setup.at("mesh").at("node_ids").is_array() ||
          !setup.at("mesh").at("element_ids").is_array())
        return failure("setup_contract_invalid", "reviewed setup mesh identities are invalid");
      for (const auto &id : setup.at("mesh").at("node_ids")) {
        if (!id.is_number_integer())
          return failure("setup_contract_invalid", "reviewed setup node identity is invalid");
        replayRequest.nodes.push_back({id.get<int>(), {}});
      }
      for (const auto &id : setup.at("mesh").at("element_ids")) {
        if (!id.is_number_integer())
          return failure("setup_contract_invalid", "reviewed setup element identity is invalid");
        replayRequest.elements.push_back({id.get<int>(), {}});
      }
      if (const auto binding = legacy_v1_binding_issue(replayRequest, parsed);
          binding.has_value())
        return failure("replay_mesh_binding_mismatch",
                       *binding);
    }
    if (!requirements.at("displacement_limit_m").is_null())
      replayRequest.displacement_limit_m = requirements.at("displacement_limit_m").get<double>();
    if (!requirements.at("von_mises_limit_pa").is_null())
      replayRequest.von_mises_limit_pa = requirements.at("von_mises_limit_pa").get<double>();
    const auto replayEvaluation =
        replay_legacy_v1_findings(replayRequest, parsedMetrics);
    if (replayEvaluation.declared_obligations !=
            root.at("coverage").at("declared_obligations").get<int>() ||
        replayEvaluation.evaluated_obligations !=
            root.at("coverage").at("evaluated_obligations").get<int>() ||
        replayEvaluation.findings.size() != root.at("findings").size())
      return failure("replay_finding_mismatch", "replayed findings differ from archive");
    require_findings_replay(root.at("findings"),
                            legacy_v1_findings_json(replayEvaluation),
                            "findings");
    return {true, "verified", "exact artifact identities and DAT replay verified",
            parsedMetrics, coverage.at("declared_obligations").get<int>(),
            coverage.at("evaluated_obligations").get<int>(), "1.0.0", {},
            parsed, std::nullopt, std::nullopt, std::nullopt};
  } catch (const ArchiveVerificationError &error) {
    return failure(error.code(), error.what());
  } catch (const integrity::CanonicalJsonError &error) {
    return failure(error.code(), error.what());
  } catch (const std::exception &error) {
    return failure("archive_verification_failed", error.what());
  } catch (...) {
    return failure("archive_verification_failed", "unknown archive verification failure");
  }
}

StructuralArchive export_structural_archive(
    const std::filesystem::path &manifestPath,
    const std::filesystem::path &destinationDirectory) {
  const auto sourceVerification = verify_structural_archive(manifestPath);
  if (!sourceVerification.valid)
    throw std::runtime_error(sourceVerification.code + ": " +
                             sourceVerification.detail);
  if (destinationDirectory.empty() ||
      std::filesystem::exists(destinationDirectory))
    throw std::invalid_argument("archive export destination must not exist");
  const auto parent = destinationDirectory.parent_path();
  if (parent.empty() || !std::filesystem::is_directory(parent))
    throw std::invalid_argument("archive export parent must exist");

  std::mt19937_64 random{std::random_device{}()};
  auto temporary = destinationDirectory;
  temporary += ".partial-" + std::to_string(random());
  if (std::filesystem::exists(temporary))
    throw std::runtime_error("archive export temporary path already exists");
  try {
    std::filesystem::create_directory(temporary);
    const auto manifestBytes = read(manifestPath);
    const auto manifest = Json::parse(manifestBytes);
    std::vector<std::string> artifactNames;
    const bool twoSampleArchive =
        (manifest.value("$schema", "") == archiveSchemaV3 &&
         manifest.value("schema_version", "") == "3.0.0") ||
        (manifest.value("$schema", "") == archiveSchemaV4 &&
         manifest.value("schema_version", "") == "4.0.0");
    if (twoSampleArchive) {
      for (const auto role : {"coarse", "fine"})
        for (const auto key : {"setup", "deck", "dat", "frd", "sta",
                               "stdout", "stderr"})
          artifactNames.push_back(
              manifest.at("samples")
                  .at(role)
                  .at("artifacts")
                  .at(key)
                  .at("file")
                  .get<std::string>());
    } else {
      for (const auto key : {"setup", "deck", "dat", "frd", "sta",
                             "stdout", "stderr"})
        artifactNames.push_back(
            manifest.at("artifacts")
                .at(key)
                .at("file")
                .get<std::string>());
    }
    std::set<std::string> uniqueNames;
    for (const auto &name : artifactNames) {
      if (!safe_file(name))
        throw std::runtime_error("archive contains an unsafe artifact filename");
      if (!uniqueNames.insert(name).second)
        throw std::runtime_error(
            "archive contains a duplicate artifact filename");
      const auto bytes = read(manifestPath.parent_path() / name);
      write(temporary / name, bytes);
    }
    write(temporary / archiveName, manifestBytes);
    const auto copiedManifest = temporary / archiveName;
    const auto copyVerification = verify_structural_archive(copiedManifest);
    if (!copyVerification.valid)
      throw std::runtime_error(copyVerification.code + ": " +
                               copyVerification.detail);
    std::filesystem::rename(temporary, destinationDirectory);
    return {destinationDirectory / archiveName,
            integrity::sha256_bytes(manifestBytes),
            copyVerification.schema_version,
            copyVerification.validated_result_identity,
            copyVerification.refinement
                ? copyVerification.refinement->coarse()
                      .run()
                      .validated_result->identity
                : std::string{}};
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(temporary, ignored);
    throw;
  }
}

} // namespace prometheus::structural
