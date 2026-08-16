#include "prometheus/structural/calculix_result.hpp"

#include "calculix_deck_internal.hpp"
#include "prometheus/structural/structural_request.hpp"
#include "prometheus/structural/structural_setup.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>

namespace prometheus::structural {
namespace {

using Json = nlohmann::json;
constexpr double finalStepTime = 1.0;
constexpr double timeTolerance = 1.0e-9;

bool strict_sha256(const std::string_view value) {
  return value.size() == 71U && value.starts_with("sha256:") &&
         std::ranges::all_of(value.substr(7), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool safe_text(const std::string_view value,
               const std::size_t maximumBytes = 512U) {
  return !value.empty() && value.size() <= maximumBytes &&
         std::ranges::all_of(value, [](const unsigned char character) {
           return character >= 0x20U && character != 0x7fU;
         });
}

std::optional<int> integer_token(const std::string_view token) {
  int value{};
  const auto [end, error] =
      std::from_chars(token.data(), token.data() + token.size(), value);
  if (error != std::errc{} || end != token.data() + token.size())
    return std::nullopt;
  return value;
}

double real_token(std::string token) {
  std::ranges::replace(token, 'D', 'E');
  std::ranges::replace(token, 'd', 'e');
  std::istringstream input(token);
  input.imbue(std::locale::classic());
  double value{};
  char trailing{};
  if (!(input >> value) || (input >> trailing) || !std::isfinite(value))
    throw std::runtime_error(
        "CalculiX numeric output is malformed or non-finite");
  return value;
}

double section_time(const std::string &line) {
  const auto position = line.find("and time");
  if (position == std::string::npos)
    throw std::runtime_error("CalculiX result section has no time identity");
  std::istringstream tail(line.substr(position + 8U));
  std::string token;
  if (!(tail >> token))
    throw std::runtime_error("CalculiX result section time is missing");
  return real_token(std::move(token));
}

void add_issue(CompiledCalculixResult &result, std::string code,
               std::string message) {
  result.issues.push_back({std::move(code), std::move(message)});
}

bool near_final_time(const double value) {
  return std::abs(value - finalStepTime) <= timeTolerance;
}

bool exact_line(const std::string_view text, const std::string_view expected) {
  std::istringstream input{std::string(text)};
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos)
      continue;
    const auto last = line.find_last_not_of(" \t");
    if (std::string_view(line).substr(first, last - first + 1U) == expected)
      return true;
  }
  return false;
}

bool solver_reports_error(const std::string &standardOutput,
                          const std::string &standardError) {
  auto combined = standardOutput + '\n' + standardError;
  std::ranges::transform(combined, combined.begin(),
                         [](const unsigned char character) {
                           return static_cast<char>(std::toupper(character));
                         });
  constexpr std::array<std::string_view, 5> markers{
      "*ERROR", "JOB FAILED", "SOLUTION CONTAINS NAN",
      "SEGMENTATION FAULT", "FLOATING POINT EXCEPTION"};
  return std::ranges::any_of(markers, [&](const std::string_view marker) {
    return combined.find(marker) != std::string::npos;
  });
}

std::vector<CalculixConvergenceEvidence>
parse_status(const std::string_view statusBytes) {
  std::vector<CalculixConvergenceEvidence> rows;
  std::istringstream input{std::string(statusBytes)};
  input.imbue(std::locale::classic());
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    row.imbue(std::locale::classic());
    std::array<std::string, 7> tokens;
    if (!(row >> tokens[0]))
      continue;
    const auto step = integer_token(tokens[0]);
    if (!step.has_value())
      continue;
    for (std::size_t index = 1; index < tokens.size(); ++index)
      if (!(row >> tokens[index]))
        throw std::runtime_error("CalculiX status row is incomplete");
    std::string trailing;
    if (row >> trailing)
      throw std::runtime_error(
          "CalculiX status row has unexpected fields");
    const auto increment = integer_token(tokens[1]);
    const auto attempt = integer_token(tokens[2]);
    const auto iterations = integer_token(tokens[3]);
    if (!increment.has_value() || !attempt.has_value() ||
        !iterations.has_value())
      throw std::runtime_error("CalculiX status integer field is malformed");
    CalculixConvergenceEvidence parsed{
        .step = *step,
        .increment = *increment,
        .attempt = *attempt,
        .iterations = *iterations,
        .total_time = real_token(tokens[4]),
        .step_time = real_token(tokens[5]),
        .increment_time = real_token(tokens[6])};
    if (parsed.step <= 0 || parsed.increment <= 0 || parsed.attempt <= 0 ||
        parsed.iterations <= 0 || parsed.total_time <= 0.0 ||
        parsed.step_time <= 0.0 || parsed.increment_time <= 0.0)
      throw std::runtime_error(
          "CalculiX status row contains invalid values");
    if (!rows.empty() &&
        (parsed.total_time + timeTolerance < rows.back().total_time ||
         (parsed.step == rows.back().step &&
          parsed.step_time + timeTolerance < rows.back().step_time)))
      throw std::runtime_error("CalculiX status time is not monotonic");
    rows.push_back(parsed);
  }
  if (rows.empty())
    throw std::runtime_error(
        "CalculiX status has no converged increment rows");
  return rows;
}

double von_mises(const double xx, const double yy, const double zz,
                 const double xy, const double xz, const double yz) {
  const double value = std::sqrt(
      0.5 * ((xx - yy) * (xx - yy) + (yy - zz) * (yy - zz) +
             (zz - xx) * (zz - xx)) +
      3.0 * (xy * xy + xz * xz + yz * yz));
  if (!std::isfinite(value))
    throw std::runtime_error("CalculiX von Mises stress is non-finite");
  return value;
}

CalculixArtifactIdentity identity(const std::string_view bytes) {
  return {integrity::sha256_bytes(bytes), bytes.size()};
}

CompiledCalculixResult compile_result(
    const StructuralRequest &request, const std::string_view expectedDeck,
    const bool requestAlreadyValidated,
    const std::string_view compiledSetupIdentity,
    const CalculixRunEvidence &evidence) {
  CompiledCalculixResult result;
  result.compiled_setup_identity = compiledSetupIdentity;
  result.artifacts = {
      .deck = identity(evidence.deck_bytes),
      .sta = identity(evidence.status_bytes),
      .dat = identity(evidence.data_bytes),
      .frd = {evidence.frd_sha256, evidence.frd_byte_length},
      .standard_output = identity(evidence.standard_output),
      .standard_error = identity(evidence.standard_error)};
  result.backend = {.executable_sha256 = evidence.solver_executable_sha256,
                    .version = evidence.solver_version};

  if (!requestAlreadyValidated && !validate_request(request).empty())
    add_issue(result, "invalid_structural_request",
              "Solver evidence cannot compile against an invalid request");
  if (!strict_sha256(evidence.solver_executable_sha256))
    add_issue(result, "invalid_solver_identity",
              "Exact solver executable SHA-256 is required");
  if (!safe_text(evidence.solver_version))
    add_issue(result, "invalid_solver_version",
              "A safe nonempty solver version is required");
  else if (!exact_line(evidence.standard_output, evidence.solver_version))
    add_issue(result, "solver_version_mismatch",
              "Declared solver version is absent from standard output");
  if (!strict_sha256(evidence.frd_sha256) ||
      evidence.frd_byte_length == 0U)
    add_issue(result, "invalid_frd_identity",
              "Exact nonempty FRD identity is required");
  if (evidence.process_exit_code != 0)
    add_issue(result, "solver_process_failed",
              "CalculiX process returned a nonzero exit status");
  if (!exact_line(evidence.standard_output, "Job finished"))
    add_issue(result, "solver_completion_marker_missing",
              "CalculiX completion marker is absent from standard output");
  if (solver_reports_error(evidence.standard_output,
                           evidence.standard_error))
    add_issue(result, "solver_reported_error",
              "CalculiX reported an error despite process completion");
  if (evidence.deck_bytes != expectedDeck)
    add_issue(result, "deck_request_mismatch",
              "Executed deck bytes do not match the reviewed request");
  if (!result.issues.empty())
    return result;

  std::vector<CalculixConvergenceEvidence> statusRows;
  try {
    statusRows = parse_status(evidence.status_bytes);
  } catch (const std::exception &error) {
    add_issue(result, "invalid_solver_status", error.what());
  }
  if (!statusRows.empty()) {
    const auto &final = statusRows.back();
    if (final.step != 1 || !near_final_time(final.total_time) ||
        !near_final_time(final.step_time))
      add_issue(result, "solver_step_incomplete",
                "CalculiX did not reach the requested static step time");
    else
      result.convergence = final;
  }

  CalculixDat parsed;
  try {
    parsed = parse_calculix_dat(evidence.data_bytes);
  } catch (const std::exception &error) {
    add_issue(result, "invalid_result_data", error.what());
  }
  if (!result.issues.empty())
    return result;

  double finalDisplacementTime = 0.0;
  for (const auto &row : parsed.displacements)
    finalDisplacementTime = std::max(finalDisplacementTime, row.time);
  double finalStressTime = 0.0;
  for (const auto &row : parsed.stresses)
    finalStressTime = std::max(finalStressTime, row.time);
  if ((!parsed.displacements.empty() &&
       !near_final_time(finalDisplacementTime)) ||
      (!parsed.stresses.empty() && !near_final_time(finalStressTime)))
    add_issue(result, "result_time_incomplete",
              "CalculiX result sections do not reach the requested step time");

  std::set<int> expectedNodes;
  for (const auto &node : request.nodes)
    expectedNodes.insert(node.id);
  std::map<int, NodalDisplacement> displacementByNode;
  for (const auto &row : parsed.displacements) {
    if (!near_final_time(row.time))
      continue;
    if (!expectedNodes.contains(row.node_id)) {
      add_issue(result, "unexpected_displacement_row",
                "CalculiX returned an unexpected displacement node");
      continue;
    }
    if (!displacementByNode.emplace(row.node_id, row).second)
      add_issue(result, "duplicate_displacement_row",
                "CalculiX returned a duplicate displacement node");
  }
  for (const int nodeId : expectedNodes)
    if (!displacementByNode.contains(nodeId))
      add_issue(result, "missing_displacement_row",
                "CalculiX omitted an expected displacement node");

  std::set<std::pair<int, int>> expectedStressRows;
  for (const auto &element : request.elements)
    expectedStressRows.emplace(element.id, 1);
  std::map<std::pair<int, int>, ElementStress> stressByIdentity;
  for (const auto &row : parsed.stresses) {
    if (!near_final_time(row.time))
      continue;
    const std::pair identityValue{row.element_id, row.integration_point};
    if (!expectedStressRows.contains(identityValue)) {
      add_issue(result, "unexpected_stress_row",
                "CalculiX returned an unexpected stress identity");
      continue;
    }
    if (!stressByIdentity.emplace(identityValue, row).second)
      add_issue(result, "duplicate_stress_row",
                "CalculiX returned a duplicate stress identity");
  }
  for (const auto &identityValue : expectedStressRows)
    if (!stressByIdentity.contains(identityValue))
      add_issue(result, "missing_stress_row",
                "CalculiX omitted an expected stress identity");
  if (!result.issues.empty())
    return result;

  for (const auto &[nodeId, row] : displacementByNode) {
    (void)nodeId;
    result.normalized.displacements.push_back(row);
  }
  for (const auto &[identityValue, row] : stressByIdentity) {
    (void)identityValue;
    result.normalized.stresses.push_back(row);
  }
  result.metrics = summarize_calculix_dat(result.normalized);
  const auto identityDocument = integrity::canonicalize_json_bytes(
      Json{{"$schema",
            "urn:prometheus:schema:compiled-calculix-result:2.0.0"},
           {"schema_version", "2.0.0"},
           {"compiler_version", "calculix-evidence-compiler-v2"},
           {"compiled_setup_identity", result.compiled_setup_identity},
           {"request_geometry_sha256", request.geometry_sha256},
           {"backend",
            {{"executable_sha256", result.backend.executable_sha256},
             {"version", result.backend.version}}},
           {"artifacts",
            {{"deck", result.artifacts.deck.sha256},
             {"sta", result.artifacts.sta.sha256},
             {"dat", result.artifacts.dat.sha256},
             {"frd", result.artifacts.frd.sha256},
             {"stdout", result.artifacts.standard_output.sha256},
             {"stderr", result.artifacts.standard_error.sha256}}},
           {"convergence",
            {{"step", result.convergence->step},
             {"increment", result.convergence->increment},
             {"attempt", result.convergence->attempt},
             {"iterations", result.convergence->iterations},
             {"total_time", result.convergence->total_time},
             {"step_time", result.convergence->step_time},
             {"increment_time", result.convergence->increment_time}}},
           {"metrics",
            {{"maximum_displacement_m",
              result.metrics->maximum_displacement_m},
             {"maximum_von_mises_pa",
              result.metrics->maximum_von_mises_pa},
             {"displacement_rows", result.metrics->displacement_rows},
             {"stress_rows", result.metrics->stress_rows}}}}
          .dump());
  result.identity = integrity::sha256_bytes(identityDocument);
  return result;
}

} // namespace

CalculixDat parse_calculix_dat(const std::string_view rawDat) {
  enum class Section { none, displacement, stress };
  Section section = Section::none;
  double currentTime{};
  bool sawDisplacementSection = false;
  bool sawStressSection = false;
  CalculixDat result;
  std::istringstream input{std::string(rawDat)};
  input.imbue(std::locale::classic());
  std::string line;
  while (std::getline(input, line)) {
    if (line.find("displacements (vx,vy,vz)") != std::string::npos) {
      section = Section::displacement;
      currentTime = section_time(line);
      sawDisplacementSection = true;
      continue;
    }
    if (line.find(
            "stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz)") !=
        std::string::npos) {
      section = Section::stress;
      currentTime = section_time(line);
      sawStressSection = true;
      continue;
    }
    if (section == Section::none)
      continue;

    std::istringstream row(line);
    row.imbue(std::locale::classic());
    std::string identityToken;
    if (!(row >> identityToken))
      continue;
    const auto rowIdentity = integer_token(identityToken);
    if (!rowIdentity.has_value())
      continue;
    if (*rowIdentity <= 0)
      throw std::runtime_error("CalculiX result identity is invalid");

    if (section == Section::displacement) {
      std::array<std::string, 3> values;
      if (!(row >> values[0] >> values[1] >> values[2]))
        throw std::runtime_error("CalculiX displacement row is incomplete");
      std::string trailing;
      if (row >> trailing)
        throw std::runtime_error(
            "CalculiX displacement row has unexpected fields");
      NodalDisplacement parsed{.node_id = *rowIdentity,
                               .x_m = real_token(values[0]),
                               .y_m = real_token(values[1]),
                               .z_m = real_token(values[2]),
                               .time = currentTime};
      parsed.magnitude_m = std::hypot(parsed.x_m, parsed.y_m, parsed.z_m);
      if (!std::isfinite(parsed.magnitude_m))
        throw std::runtime_error(
            "CalculiX displacement magnitude is non-finite");
      result.displacements.push_back(parsed);
    } else {
      std::string integrationPointToken;
      std::array<std::string, 6> values;
      if (!(row >> integrationPointToken >> values[0] >> values[1] >>
            values[2] >> values[3] >> values[4] >> values[5]))
        throw std::runtime_error("CalculiX stress row is incomplete");
      std::string trailing;
      if (row >> trailing)
        throw std::runtime_error(
            "CalculiX stress row has unexpected fields");
      const auto integrationPoint = integer_token(integrationPointToken);
      if (!integrationPoint.has_value() || *integrationPoint <= 0)
        throw std::runtime_error("CalculiX stress identity is invalid");
      ElementStress parsed{
          .element_id = *rowIdentity,
          .integration_point = *integrationPoint,
          .xx_pa = real_token(values[0]),
          .yy_pa = real_token(values[1]),
          .zz_pa = real_token(values[2]),
          .xy_pa = real_token(values[3]),
          .xz_pa = real_token(values[4]),
          .yz_pa = real_token(values[5]),
          .time = currentTime};
      parsed.von_mises_pa =
          von_mises(parsed.xx_pa, parsed.yy_pa, parsed.zz_pa, parsed.xy_pa,
                    parsed.xz_pa, parsed.yz_pa);
      result.stresses.push_back(parsed);
    }
  }
  if (!sawDisplacementSection || !sawStressSection)
    throw std::runtime_error(
        "CalculiX output is missing required displacement or stress sections");
  return result;
}

CalculixMetrics summarize_calculix_dat(const CalculixDat &normalized) {
  CalculixMetrics result;
  result.displacement_rows = normalized.displacements.size();
  result.stress_rows = normalized.stresses.size();
  for (const auto &row : normalized.displacements)
    result.maximum_displacement_m =
        std::max(result.maximum_displacement_m, row.magnitude_m);
  for (const auto &row : normalized.stresses)
    result.maximum_von_mises_pa =
        std::max(result.maximum_von_mises_pa, row.von_mises_pa);
  return result;
}

CompiledCalculixResult compile_calculix_result(
    const StructuralRequest &request, const CalculixRunEvidence &evidence) {
  const auto requestIssues = validate_request(request);
  std::string expectedDeck;
  if (requestIssues.empty())
    expectedDeck = detail::generate_validated_calculix_deck(request);
  return compile_result(request, expectedDeck, requestIssues.empty(), {},
                        evidence);
}

CompiledCalculixResult compile_calculix_result(
    const CompiledStructuralSetup &setup,
    const CalculixRunEvidence &evidence) {
  return compile_result(setup.request, setup.calculix_deck, true,
                        setup.identity, evidence);
}

} // namespace prometheus::structural
