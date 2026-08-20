#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace prometheus::run_store {

inline constexpr std::string_view project_v2_schema_id =
    "urn:prometheus:schema:project:2.0.0";
inline constexpr std::string_view project_v2_schema_version = "2.0.0";
inline constexpr std::string_view project_inventory_schema_id =
    "urn:prometheus:schema:project-inventory-snapshot:1.0.0";
inline constexpr std::string_view project_inventory_media_type =
    "application/vnd.prometheus.project-inventory+json";
inline constexpr std::string_view execution_project_snapshot_schema_id =
    "urn:prometheus:schema:execution-project-snapshot:1.0.0";
inline constexpr std::string_view execution_project_snapshot_media_type =
    "application/vnd.prometheus.execution-project-snapshot+json";
inline constexpr std::string_view execution_store_version = "1.0.0";
inline constexpr std::size_t maximum_project_bytes = 8U * 1024U * 1024U;
inline constexpr std::size_t maximum_object_bytes = 8U * 1024U * 1024U;
inline constexpr std::size_t maximum_package_bindings = 10000U;
inline constexpr std::size_t maximum_joint_bindings = 10000U;
inline constexpr std::size_t maximum_requirement_bindings = 10000U;
inline constexpr std::size_t maximum_material_bindings = 10000U;
inline constexpr std::size_t maximum_load_bindings = 10000U;
inline constexpr std::size_t maximum_restraint_bindings = 10000U;
inline constexpr std::size_t maximum_selection_faces = 200000U;
inline constexpr std::size_t maximum_selection_nodes = 200000U;
inline constexpr std::size_t maximum_committed_runs = 10000U;
inline constexpr std::size_t maximum_events = 256U;
inline constexpr std::string_view structural_manifest_media_type =
    "application/vnd.prometheus.structural-run-archive+json";
inline constexpr std::string_view structural_manifest_schema_id =
    "urn:prometheus:schema:structural-run-archive:1.0.0";
inline constexpr std::string_view structural_project_run_media_type =
    "application/vnd.prometheus.structural-project-run+json";
inline constexpr std::string_view structural_project_run_schema_id =
    "urn:prometheus:schema:structural-project-run:1.0.0";
inline constexpr std::string_view structural_artifact_chunk_media_type =
    "application/vnd.prometheus.structural-artifact-chunk+json";
inline constexpr std::string_view structural_artifact_chunk_schema_id =
    "urn:prometheus:schema:structural-artifact-chunk:1.0.0";

struct Diagnostic final {
  std::string stage;
  std::string code;
  std::string message;
  std::optional<std::string> field;
  std::optional<std::string> path;
};

template <typename T> class Result final {
public:
  [[nodiscard]] static Result success(T value) {
    return Result(std::move(value));
  }

  [[nodiscard]] static Result failure(Diagnostic diagnostic) {
    return Result(std::move(diagnostic));
  }

  [[nodiscard]] bool has_value() const noexcept {
    return std::holds_alternative<T>(payload_);
  }

  [[nodiscard]] const T &value() const { return std::get<T>(payload_); }
  [[nodiscard]] T &value() { return std::get<T>(payload_); }

  [[nodiscard]] const Diagnostic &diagnostic() const {
    return std::get<Diagnostic>(payload_);
  }

private:
  explicit Result(T value) : payload_(std::move(value)) {}
  explicit Result(Diagnostic diagnostic)
      : payload_(std::move(diagnostic)) {}

  std::variant<T, Diagnostic> payload_;
};

struct StoredObjectReference final {
  std::string object_hash;
  std::uint64_t byte_length;
  std::string media_type;
  std::string schema_id;
  std::string schema_version;

  bool operator==(const StoredObjectReference &) const = default;
};

struct ComponentBinding final {
  std::string cad_entity_id;
  std::string revision_id;
  std::string label;
};

struct PlacementOverride final {
  std::string cad_entity_id;
  double translation_x_m;
  double translation_y_m;
  double translation_z_m;
  double rotation_x_deg;
  double rotation_y_deg;
  double rotation_z_deg;
  std::string rotation_convention;
};

struct Connection final {
  std::string id;
  std::string source_part;
  std::string source_name;
  std::string source_anchor;
  std::string target_part;
  std::string target_name;
  std::string target_anchor;
  std::string connection_type;
  bool confirmed_by_user;
  std::string anchor_origin;
  std::string semantic_status;
};

struct InterferenceClassification final {
  std::string first_id;
  std::string second_id;
  std::string classification;
};

struct RevoluteJoint final {
  std::string type;
  std::uint64_t source_index;
  std::uint64_t target_index;
  std::string axis;
  double minimum_deg;
  double maximum_deg;
  double pivot_x;
  double pivot_y;
  double pivot_z;
  bool confirmed_by_user;
};

struct GeometryFinding final {
  std::string finding_kind;
  std::string status;
  std::string severity;
  std::string title;
  std::string mechanism;
  double calculated;
  std::string unit;
  double available;
  double margin_fraction;
  std::string evidence;
  std::string assumption;
  std::string estimated_range;
  std::optional<std::string> first_id;
  std::optional<std::string> second_id;
};

struct EngineeringState final {
  std::optional<RevoluteJoint> joint;
  std::vector<GeometryFinding> geometry_findings;
  std::string geometry_status;
};

struct PackageBinding final {
  std::uint64_t binding_revision;
  std::optional<std::uint64_t> supersedes_binding_revision;
  std::string cad_entity_id;
  StoredObjectReference package;
};

// Phase 6 checkpoint 2: the append-only, content-free counterpart of
// PackageBinding for a confirmed CAD-to-CAD joint. Unlike a package binding,
// a joint has no associated content-addressed byte blob to embed -- its
// parameters are inline and the edge is self-contained.
struct JointBinding final {
  std::uint64_t binding_revision;
  std::optional<std::uint64_t> supersedes_binding_revision;
  std::string source_cad_entity_id;
  std::string target_cad_entity_id;
  std::string type;
  std::string axis;
  double minimum_deg;
  double maximum_deg;
  double pivot_x;
  double pivot_y;
  double pivot_z;
};

// Phase 6 checkpoint 3: the append-only, content-free counterpart of
// JointBinding for a confirmed reviewed structural requirement. Like a
// joint, a requirement has no content-addressed byte blob to embed -- its
// fields are inline and the edge is self-contained.
struct RequirementBinding final {
  std::uint64_t binding_revision;
  std::optional<std::uint64_t> supersedes_binding_revision;
  std::string geometry_sha256;
  std::string analysis_id;
  std::string quantity;
  std::string other_quantity_description;
  std::string comparator;
  double limit_value;
  std::string unit;
  std::string applicability;
  std::string criticality;
  std::string source_or_exploratory_rationale;
};

// Phase 6 checkpoint 4: the append-only, content-free counterpart of
// PackageBinding for a reviewed structural material. Like a package
// binding, exactly one material is active for a given geometry at a time --
// but unlike a package binding, there is no content-addressed byte blob to
// embed, so the fields are inline and the edge is self-contained, matching
// JointBinding's and RequirementBinding's shape instead.
struct MaterialBinding final {
  std::uint64_t binding_revision;
  std::optional<std::uint64_t> supersedes_binding_revision;
  std::string geometry_sha256;
  std::string analysis_id;
  std::string designation;
  std::string source_sha256;
  std::string applicability;
  double youngs_modulus_pa;
  double poisson_ratio;
};

// Phase 6 checkpoint 5: the append-only, content-free counterpart of
// MaterialBinding for a reviewed structural surface selection. Exactly one
// load selection and one restraint selection are active for a given
// geometry at a time, the same single-key shape as MaterialBinding.
// face_node_ids/node_ids carry the exact durable boundary topology a
// visual patch selection resolved to (see
// prometheus::structural::BoundarySelection), not just a visual patch id,
// so the reviewed selection is provenanced independent of the patches a
// later mesh regeneration might renumber.
struct LoadBinding final {
  std::uint64_t binding_revision;
  std::optional<std::uint64_t> supersedes_binding_revision;
  std::string geometry_sha256;
  std::string analysis_id;
  std::string selection_label;
  std::vector<std::array<int, 3>> face_node_ids;
  std::vector<int> node_ids;
  double area_m2;
  double force_x_n;
  double force_y_n;
  double force_z_n;
};

struct RestraintBinding final {
  std::uint64_t binding_revision;
  std::optional<std::uint64_t> supersedes_binding_revision;
  std::string geometry_sha256;
  std::string analysis_id;
  std::string selection_label;
  std::vector<std::array<int, 3>> face_node_ids;
  std::vector<int> node_ids;
  double area_m2;
};

struct Event final {
  std::uint64_t sequence;
  std::string event_kind;
  std::string status;
  std::optional<std::string> related_hash;
  std::string occurred_at_utc;
  std::string diagnostic_code;
};

struct ExecutionIndex final {
  std::vector<PackageBinding> package_bindings;
  std::optional<StoredObjectReference> current_scenario;
  std::vector<StoredObjectReference> committed_runs;
  std::vector<Event> events;
  std::vector<JointBinding> joint_bindings;
  std::vector<RequirementBinding> requirement_bindings;
  std::vector<MaterialBinding> material_bindings;
  std::vector<LoadBinding> load_bindings;
  std::vector<RestraintBinding> restraint_bindings;
};

struct ProjectV2 final {
  std::string name;
  std::string cad_source;
  std::string assembly_artifact_hash;
  std::string coordinate_system;
  std::string length_unit;
  std::vector<ComponentBinding> component_bindings;
  std::vector<PlacementOverride> placement_overrides;
  std::vector<Connection> connections;
  std::vector<InterferenceClassification> interference_classifications;
  EngineeringState engineering;
  // Canonical JSON for the exact legacy v1 engineering object. It is opaque,
  // display-only preservation and is never consulted by execution code.
  std::optional<std::string> legacy_v1_engineering_state;
  ExecutionIndex execution;
};

[[nodiscard]] bool is_valid_object_hash(std::string_view value) noexcept;
[[nodiscard]] bool
is_supported_object_reference(const StoredObjectReference &reference) noexcept;
[[nodiscard]] Result<ProjectV2> parse_project_v2(std::string_view bytes) noexcept;
[[nodiscard]] Result<std::string>
serialize_project_v2(const ProjectV2 &project) noexcept;

} // namespace prometheus::run_store
