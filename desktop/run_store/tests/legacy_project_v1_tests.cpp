#include <prometheus/run_store/legacy_project_v1.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

namespace run_store = prometheus::run_store;
using Json = nlohmann::json;

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

Json legacy_project()
{
    return Json{
        {"schema_version", "1.0.0"},
        {"name", "Motor arm"},
        {"cad_source", "motor-arm.step"},
        {"coordinate_system", "right-handed Z-up"},
        {"length_unit", "m"},
        {"component_bindings",
            Json::array({Json{{"cad_entity_id", "motor"},
                {"revision_id", "revision-123"},
                {"label", "Fixture Works PM-36"}}})},
        {"placement_overrides",
            Json::array({Json{{"cad_entity_id", "arm"},
                {"translation_x_m", 0.01},
                {"translation_y_m", 0.0},
                {"translation_z_m", 0.0},
                {"rotation_x_deg", 0.0},
                {"rotation_y_deg", 0.0},
                {"rotation_z_deg", 30.0},
                {"rotation_convention",
                    "extrinsic_X_then_Y_then_Z_about_imported_bounds_center"}}})},
        {"connections",
            Json::array({Json{{"id", "arm:x_min->base:x_max"},
                {"source_part", "arm"},
                {"source_name", "Arm"},
                {"source_anchor", "x_min"},
                {"target_part", "base"},
                {"target_name", "Base"},
                {"target_anchor", "x_max"},
                {"connection_type", "fixed"},
                {"confirmed_by_user", true},
                {"anchor_origin", "derived_bounds"},
                {"semantic_status", "provisional_geometry_anchor"}}})},
        {"interference_classifications",
            Json::array({Json{{"first_id", "motor"},
                {"second_id", "base"},
                {"classification", "intended_engagement"}}})},
        {"engineering",
            Json{
                {"joint", Json{{"type", "revolute"},
                              {"source_index", 1},
                              {"target_index", 2},
                              {"axis", "Z"},
                              {"minimum_deg", 0.0},
                              {"maximum_deg", 90.0},
                              {"pivot_x", 0.0},
                              {"pivot_y", 0.0},
                              {"pivot_z", 0.0},
                              {"confirmed_by_user", true}}},
                {"scenario", Json{{"payload_kg", 8.0},
                                 {"arm_m", 0.2},
                                 {"rotation_deg", 90.0}}},
                {"findings",
                    Json::array({
                        Json{{"status", "information"},
                            {"severity", "information"},
                            {"title", "Intentional solid engagement"},
                            {"mechanism", "Motor and base share volume."},
                            {"calculated", 0.000001},
                            {"unit", "m³"},
                            {"available", 0.0},
                            {"margin_fraction", -1.0},
                            {"evidence", "Imported STEP B-Rep geometry"},
                            {"assumption", "Reviewed intended fit"},
                            {"estimated_range", ""},
                            {"first_id", "motor"},
                            {"second_id", "base"}},
                        Json{{"status", "fail"},
                            {"severity", "critical"},
                            {"title", "Continuous horizontal holding torque"},
                            {"mechanism", "Historical fixed motor finding"},
                            {"calculated", 0.224152},
                            {"unit", "N·m"},
                            {"available", 0.208},
                            {"margin_fraction", -0.07765},
                            {"evidence", "Historical fixture"},
                            {"assumption", "Historical fixed component"},
                            {"estimated_range", ""}}})},
                {"run_status", "Completed — 2 findings"}}},
    };
}

void valid_conversion_is_bounded_and_non_authoritative()
{
    const auto parsed = run_store::parse_legacy_project_v1(
        legacy_project().dump());
    check(parsed.has_value(), "valid legacy project parses");
    if (!parsed.has_value()) {
        return;
    }
    const auto& project = parsed.value().project;
    check(project.name == "Motor arm", "legacy name survives");
    check(project.component_bindings.size() == 1U,
        "legacy component binding survives");
    check(project.placement_overrides.size() == 1U,
        "legacy placement survives");
    check(project.connections.size() == 1U,
        "legacy connection survives");
    check(project.interference_classifications.size() == 1U,
        "legacy interference classification survives");
    check(project.engineering.joint.has_value(),
        "reviewed legacy joint survives as geometry state");
    check(project.engineering.geometry_findings.size() == 1U,
        "only the geometry finding enters authoritative geometry state");
    check(project.engineering.geometry_findings.front().finding_kind
            == "static_interference",
        "legacy geometry finding receives the typed geometry kind");
    check(project.legacy_v1_engineering_state.has_value(),
        "complete legacy engineering state is preserved");
    check(project.legacy_v1_engineering_state->find("payload_kg")
            != std::string::npos
            && project.legacy_v1_engineering_state->find(
                   "Continuous horizontal holding torque")
                != std::string::npos,
        "legacy motor scenario and finding remain display-only preservation");
    check(project.execution.package_bindings.empty()
            && !project.execution.current_scenario.has_value()
            && project.execution.committed_runs.empty()
            && project.execution.events.empty(),
        "legacy history never becomes a recorded execution run");
}

void unit_alone_cannot_promote_a_legacy_finding()
{
    auto spoofed = legacy_project();
    spoofed["engineering"]["findings"].push_back(
        Json{{"status", "information"},
            {"severity", "information"},
            {"title", "Historical motor volume proxy"},
            {"mechanism", "Not an old Prometheus geometry finding family."},
            {"calculated", 0.000001},
            {"unit", "m³"},
            {"available", 0.000002},
            {"margin_fraction", 0.5},
            {"evidence", "Hand-edited legacy state"},
            {"assumption", ""},
            {"estimated_range", ""},
            {"first_id", "motor"},
            {"second_id", "base"}});
    const auto parsed = run_store::parse_legacy_project_v1(spoofed.dump());
    check(parsed.has_value(), "bounded spoofed legacy state still parses");
    if (parsed.has_value()) {
        check(parsed.value().project.engineering.geometry_findings.size() == 1U,
            "unit spelling alone cannot promote a legacy finding");
        check(parsed.value().project.legacy_v1_engineering_state->find(
                  "Historical motor volume proxy")
                != std::string::npos,
            "unrecognized legacy finding remains display-only");
    }
}

void malformed_legacy_projects_fail_closed()
{
    auto unknown = legacy_project();
    unknown["unexpected"] = true;
    auto parsed = run_store::parse_legacy_project_v1(unknown.dump());
    check(!parsed.has_value()
            && parsed.diagnostic().code == "unknown_field",
        "unknown legacy field rejects");

    auto version = legacy_project();
    version["schema_version"] = "2.0.0";
    parsed = run_store::parse_legacy_project_v1(version.dump());
    check(!parsed.has_value()
            && parsed.diagnostic().code == "unsupported_schema_version",
        "non-v1 legacy schema rejects");

    const auto bytes = legacy_project().dump();
    const auto insertion = bytes.find('{') + 1U;
    const auto duplicate = bytes.substr(0U, insertion)
        + "\"name\":\"duplicate\"," + bytes.substr(insertion);
    parsed = run_store::parse_legacy_project_v1(duplicate);
    check(!parsed.has_value() && parsed.diagnostic().code == "duplicate_key",
        "duplicate legacy key rejects before mapping");

    auto unsafe = legacy_project();
    unsafe["engineering"]["joint"]["source_index"] =
        9007199254740992ULL;
    parsed = run_store::parse_legacy_project_v1(unsafe.dump());
    check(!parsed.has_value() && parsed.diagnostic().code == "unsafe_integer",
        "unsafe legacy integer rejects");

    parsed = run_store::parse_legacy_project_v1(
        std::string(run_store::maximum_project_bytes + 1U, ' '));
    check(!parsed.has_value()
            && parsed.diagnostic().code == "max_raw_bytes_exceeded",
        "oversized legacy bytes reject");
}

void filesystem_reader_rejects_symlinks()
{
    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path()
        / ("prometheus-legacy-v1-" + suffix);
    std::filesystem::create_directories(root);
    const auto real = root / "legacy.prometheus";
    {
        std::ofstream output(real, std::ios::binary);
        output << legacy_project().dump();
    }
    const auto opened = run_store::open_legacy_project_v1(real);
    check(opened.has_value(), "regular legacy project opens");

    const auto link = root / "legacy-link.prometheus";
    std::error_code error;
    std::filesystem::create_symlink(real, link, error);
    if (!error) {
        const auto rejected = run_store::open_legacy_project_v1(link);
        check(!rejected.has_value()
                && rejected.diagnostic().code == "unsafe_project_path",
            "legacy project symlink rejects");
    }
    std::filesystem::remove_all(root, error);
}

} // namespace

int main()
{
    valid_conversion_is_bounded_and_non_authoritative();
    unit_alone_cannot_promote_a_legacy_finding();
    malformed_legacy_projects_fail_closed();
    filesystem_reader_rejects_symlinks();
    return failures == 0 ? 0 : 1;
}
