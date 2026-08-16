#include "prometheus/structural/calculix_result.hpp"

#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/structural_request.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <algorithm>
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

constexpr double finalStepTime = 1.0;
constexpr double timeTolerance = 1.0e-9;

bool strict_sha256(const std::string_view value) {
  return value.size() == 71U && value.starts_with("sha256:") &&
         std::ranges::all_of(value.substr(7), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool safe_text(const std::string_view value) {
  return !value.empty() &&
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
    throw std::runtime_error("CalculiX numeric output is malformed or non-finite");
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

bool solver_completed(const std::string_view standardOutput) {
  std::istringstream input{std::string(standardOutput)};
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos)
      continue;
    const auto last = line.find_last_not_of(" \t");
    if (line.substr(first, last - first + 1U) == "Job finished")
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
      "*ERROR", "JOB FAILED", "SOLUTION CONTAINS NAN", "SEGMENTATION FAULT",
      "FLOATING POINT EXCEPTION"};
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
      throw std::runtime_error("CalculiX status row has unexpected fields");
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
        .increment_time = real_token(tokens[6]),
    };
    if (parsed.step <= 0 || parsed.increment <= 0 || parsed.attempt <= 0 ||
        parsed.iterations <= 0 || parsed.total_time <= 0.0 ||
        parsed.step_time <= 0.0 || parsed.increment_time <= 0.0)
      throw std::runtime_error("CalculiX status row contains invalid values");
    if (!rows.empty() &&
        (parsed.total_time + timeTolerance < rows.back().total_time ||
         (parsed.step == rows.back().step &&
          parsed.step_time + timeTolerance < rows.back().step_time)))
      throw std::runtime_error("CalculiX status time is not monotonic");
    rows.push_back(parsed);
  }
  if (rows.empty())
    throw std::runtime_error("CalculiX status has no converged increment rows");
  return rows;
}

double von_mises(const std::array<double, 6> &stress) {
  const auto [xx, yy, zz, xy, xz, yz] = stress;
  const double value = std::sqrt(
      0.5 * ((xx - yy) * (xx - yy) + (yy - zz) * (yy - zz) +
             (zz - xx) * (zz - xx)) +
      3.0 * (xy * xy + xz * xz + yz * yz));
  if (!std::isfinite(value))
    throw std::runtime_error("CalculiX von Mises stress is non-finite");
  return value;
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
    const auto identity = integer_token(identityToken);
    if (!identity.has_value())
      continue;

    if (section == Section::displacement) {
      std::array<std::string, 3> values;
      if (!(row >> values[0] >> values[1] >> values[2]))
        throw std::runtime_error("CalculiX displacement row is incomplete");
      std::string trailing;
      if (row >> trailing)
        throw std::runtime_error(
            "CalculiX displacement row has unexpected fields");
      DisplacementRow parsed{.node_id = *identity, .time = currentTime};
      for (std::size_t axis = 0; axis < 3U; ++axis)
        parsed.displacement_m[axis] = real_token(values[axis]);
      if (parsed.node_id <= 0)
        throw std::runtime_error(
            "CalculiX displacement node identity is invalid");
      result.displacements.push_back(parsed);
    } else {
      std::string integrationPointToken;
      std::array<std::string, 6> values;
      if (!(row >> integrationPointToken >> values[0] >> values[1] >>
            values[2] >> values[3] >> values[4] >> values[5]))
        throw std::runtime_error("CalculiX stress row is incomplete");
      std::string trailing;
      if (row >> trailing)
        throw std::runtime_error("CalculiX stress row has unexpected fields");
      const auto integrationPoint = integer_token(integrationPointToken);
      if (!integrationPoint.has_value() || *identity <= 0 ||
          *integrationPoint <= 0)
        throw std::runtime_error("CalculiX stress identity is invalid");
      StressRow parsed{.element_id = *identity,
                       .integration_point = *integrationPoint,
                       .time = currentTime};
      for (std::size_t component = 0; component < 6U; ++component)
        parsed.stress_pa[component] = real_token(values[component]);
      result.stresses.push_back(parsed);
    }
  }
  if (!sawDisplacementSection || !sawStressSection)
    throw std::runtime_error(
        "CalculiX output is missing required displacement or stress sections");
  return result;
}

CompiledCalculixResult compile_calculix_result(
    const StructuralRequest &request, const CalculixRunEvidence &evidence) {
  namespace integrity = prometheus::integrity;
  CompiledCalculixResult result;
  result.artifacts = {
      .deck_sha256 = integrity::sha256_bytes(evidence.deck_bytes),
      .status_sha256 = integrity::sha256_bytes(evidence.status_bytes),
      .data_sha256 = integrity::sha256_bytes(evidence.data_bytes),
      .standard_output_sha256 =
          integrity::sha256_bytes(evidence.standard_output),
      .standard_error_sha256 =
          integrity::sha256_bytes(evidence.standard_error),
  };
  result.backend = {.executable_sha256 = evidence.solver_executable_sha256,
                    .version = evidence.solver_version};

  if (!validate_request(request).empty())
    add_issue(result, "invalid_structural_request",
              "Solver evidence cannot compile against an invalid request");
  if (!strict_sha256(evidence.solver_executable_sha256))
    add_issue(result, "invalid_solver_identity",
              "Exact solver executable SHA-256 is required");
  if (!safe_text(evidence.solver_version))
    add_issue(result, "invalid_solver_version",
              "A safe nonempty solver version is required");
  if (evidence.process_exit_code != 0)
    add_issue(result, "solver_process_failed",
              "CalculiX process returned a nonzero exit status");
  if (!solver_completed(evidence.standard_output))
    add_issue(result, "solver_completion_marker_missing",
              "CalculiX completion marker is absent from standard output");
  if (solver_reports_error(evidence.standard_output, evidence.standard_error))
    add_issue(result, "solver_reported_error",
              "CalculiX reported an error despite process completion");

  try {
    if (generate_calculix_deck(request) != evidence.deck_bytes)
      add_issue(result, "deck_request_mismatch",
                "Executed deck bytes do not match the reviewed request");
  } catch (const std::exception &error) {
    add_issue(result, "invalid_structural_request", error.what());
  }
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
  std::map<int, DisplacementRow> displacementByNode;
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
  std::map<std::pair<int, int>, StressRow> stressByIdentity;
  for (const auto &row : parsed.stresses) {
    if (!near_final_time(row.time))
      continue;
    const std::pair identity{row.element_id, row.integration_point};
    if (!expectedStressRows.contains(identity)) {
      add_issue(result, "unexpected_stress_row",
                "CalculiX returned an unexpected stress identity");
      continue;
    }
    if (!stressByIdentity.emplace(identity, row).second)
      add_issue(result, "duplicate_stress_row",
                "CalculiX returned a duplicate stress identity");
  }
  for (const auto &identity : expectedStressRows)
    if (!stressByIdentity.contains(identity))
      add_issue(result, "missing_stress_row",
                "CalculiX omitted an expected stress identity");
  if (!result.issues.empty())
    return result;

  CalculixMetrics metrics;
  for (const auto &[nodeId, row] : displacementByNode) {
    (void)nodeId;
    result.normalized.displacements.push_back(row);
    metrics.maximum_displacement_m =
        std::max(metrics.maximum_displacement_m,
                 std::hypot(row.displacement_m[0], row.displacement_m[1],
                            row.displacement_m[2]));
  }
  for (const auto &[identity, row] : stressByIdentity) {
    (void)identity;
    result.normalized.stresses.push_back(row);
    try {
      metrics.maximum_von_mises_pa =
          std::max(metrics.maximum_von_mises_pa, von_mises(row.stress_pa));
    } catch (const std::exception &error) {
      add_issue(result, "invalid_result_data", error.what());
    }
  }
  if (!result.issues.empty()) {
    result.normalized = {};
    return result;
  }
  metrics.displacement_rows = result.normalized.displacements.size();
  metrics.stress_rows = result.normalized.stresses.size();
  result.metrics = metrics;
  return result;
}

} // namespace prometheus::structural
