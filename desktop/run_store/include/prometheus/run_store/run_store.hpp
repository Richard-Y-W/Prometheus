#pragma once

#include <prometheus/run_store/project_v2.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>

namespace prometheus::run_store {

struct StructuralArchiveObjects;
struct ProjectEvidenceArchiveObjects;

inline constexpr std::chrono::milliseconds maximum_lock_wait{5000};

enum class TransactionBoundary {
  before_object_temporary_create,
  after_object_temporary_create,
  before_object_write,
  after_object_write,
  before_object_flush,
  after_object_flush,
  before_object_verification,
  after_object_verification,
  before_object_rename,
  after_object_rename,
  before_project_temporary_create,
  after_project_temporary_create,
  before_project_write,
  after_project_write,
  before_project_flush,
  after_project_flush,
  before_project_verification,
  after_project_verification,
  before_project_replacement,
  after_project_replacement,
};

struct TransactionOptions final {
  std::chrono::milliseconds lock_timeout{maximum_lock_wait};
  std::stop_token stop_token{};
  // A test/fault-injection hook. Returning true aborts at that exact durable
  // boundary. It can only introduce failure; it cannot bypass verification,
  // locking, flushing, or replacement.
  std::function<bool(TransactionBoundary)> boundary_hook{};
};

struct ObjectToStore final {
  StoredObjectReference reference;
  std::string bytes;
};

struct CompletedRunObjects final {
  ObjectToStore package;
  ObjectToStore scenario;
  ObjectToStore request;
  ObjectToStore result;
  ObjectToStore manifest;
};

struct Publication final {
  ProjectV2 project;
  bool already_committed;
};

[[nodiscard]] Result<ProjectV2>
create_project_v2(const std::filesystem::path &project_path,
                  const ProjectV2 &initial_project,
                  TransactionOptions options = {}) noexcept;

// Reads only the atomically replaced project index. This intentionally does
// not claim that its execution sidecar exists and is used by the desktop only
// for degraded metadata/CAD recovery when the sidecar is missing.
[[nodiscard]] Result<ProjectV2> open_project_index_read_only(
    const std::filesystem::path &project_path) noexcept;

// Replaces a damaged current index with the last validated index retained by a
// successful prior write. A valid current index is never rolled back.
[[nodiscard]] Result<ProjectV2> recover_previous_project_index(
    const std::filesystem::path &project_path,
    TransactionOptions options = {}) noexcept;

// Updates only mutable CAD and geometry snapshot fields. The execution index
// and legacy preservation are reloaded and retained while holding the writer
// lock so a stale desktop snapshot cannot erase newer committed runs.
[[nodiscard]] Result<ProjectV2>
save_project_snapshot(const std::filesystem::path &project_path,
                      const ProjectV2 &snapshot,
                      TransactionOptions options = {}) noexcept;

[[nodiscard]] Result<ProjectV2> install_package_binding(
    const std::filesystem::path &project_path, std::string cad_entity_id,
    const StoredObjectReference &package_reference,
    std::string_view package_bytes, TransactionOptions options = {}) noexcept;

// Appends a new, append-only JointBinding graph edge for a confirmed
// revolute joint between two CAD entities, superseding the prior active
// binding for the same (unordered) entity pair, if any. Unlike
// install_package_binding, there is no associated content-addressed object
// to install -- the joint's parameters are inline in the project index.
[[nodiscard]] Result<ProjectV2> install_joint_binding(
    const std::filesystem::path &project_path,
    std::string source_cad_entity_id, std::string target_cad_entity_id,
    std::string joint_type, std::string axis, double minimum_deg,
    double maximum_deg, double pivot_x, double pivot_y, double pivot_z,
    TransactionOptions options = {}) noexcept;

// Input for install_requirement_binding. A plain struct rather than
// positional parameters: RequirementBinding carries ten caller-supplied
// fields (unlike install_joint_binding's nine), past the point where
// positional arguments stay safe to read or order correctly at a call site.
struct RequirementBindingInput final {
  std::string geometry_sha256;
  std::string analysis_id;
  std::string quantity;
  std::string other_quantity_description;
  std::string comparator;
  double limit_value{};
  std::string unit;
  std::string applicability;
  std::string criticality;
  std::string source_or_exploratory_rationale;
};

// Appends a new, append-only RequirementBinding graph edge for a confirmed
// reviewed structural requirement, superseding the prior active binding for
// the same (geometry, quantity[, other_quantity_description]) key, if any.
// Like install_joint_binding, there is no associated content-addressed
// object to install -- the requirement's fields are inline in the project
// index.
[[nodiscard]] Result<ProjectV2> install_requirement_binding(
    const std::filesystem::path &project_path,
    RequirementBindingInput input, TransactionOptions options = {}) noexcept;

// Input for install_material_binding -- a plain struct for the same
// readability reason as RequirementBindingInput.
struct MaterialBindingInput final {
  std::string geometry_sha256;
  std::string analysis_id;
  std::string designation;
  std::string source_sha256;
  std::string applicability;
  double youngs_modulus_pa{};
  double poisson_ratio{};
};

// Appends a new, append-only MaterialBinding graph edge for a reviewed
// structural material, superseding the prior active binding for the same
// geometry, if any. Like install_package_binding, exactly one material is
// active per geometry at a time -- but like install_joint_binding and
// install_requirement_binding, there is no content-addressed object to
// install, since the material's fields are inline in the project index.
[[nodiscard]] Result<ProjectV2> install_material_binding(
    const std::filesystem::path &project_path, MaterialBindingInput input,
    TransactionOptions options = {}) noexcept;

// Input for install_load_binding / install_restraint_binding. face_node_ids
// and node_ids are the exact durable boundary topology a visual patch
// selection resolved to (prometheus::structural::BoundarySelection), not a
// transient patch id.
struct SurfaceSelectionBindingInput final {
  std::string geometry_sha256;
  std::string analysis_id;
  std::string selection_label;
  std::vector<std::array<int, 3>> face_node_ids;
  std::vector<int> node_ids;
  double area_m2{};
};

// Appends a new, append-only LoadBinding graph edge for a reviewed surface
// load selection, superseding the prior active binding for the same
// geometry, if any -- the same single-key shape as install_material_binding.
[[nodiscard]] Result<ProjectV2> install_load_binding(
    const std::filesystem::path &project_path,
    SurfaceSelectionBindingInput selection, double force_x_n, double force_y_n,
    double force_z_n, TransactionOptions options = {}) noexcept;

// Appends a new, append-only RestraintBinding graph edge for a reviewed
// fixed surface selection, superseding the prior active binding for the
// same geometry, if any.
[[nodiscard]] Result<ProjectV2> install_restraint_binding(
    const std::filesystem::path &project_path,
    SurfaceSelectionBindingInput selection,
    TransactionOptions options = {}) noexcept;

[[nodiscard]] Result<ProjectV2>
set_current_scenario(const std::filesystem::path &project_path,
                     const StoredObjectReference &scenario_reference,
                     std::string_view scenario_bytes,
                     TransactionOptions options = {}) noexcept;

[[nodiscard]] Result<Publication>
publish_completed_run(const std::filesystem::path &project_path,
                      const CompletedRunObjects &objects,
                      TransactionOptions options = {}) noexcept;

// Anchors a canonical, immutable accounting snapshot of a scanned project
// folder. The snapshot records file identities and classification only; it
// does not claim semantic understanding of any artifact.
[[nodiscard]] Result<Publication> commit_project_inventory_snapshot(
    const std::filesystem::path &project_path,
    const ObjectToStore &snapshot,
    TransactionOptions options = {}) noexcept;

[[nodiscard]] Result<Publication> publish_project_inventory_archive(
    const std::filesystem::path &project_path,
    const ObjectToStore &inventory_snapshot,
    const ProjectEvidenceArchiveObjects &archive,
    TransactionOptions options = {}) noexcept;

// Anchors a verified structural archive manifest in the project history. Raw
// solver artifacts remain external until the portable-bundle checkpoint; their
// exact hashes and lengths are closed over by this immutable manifest.
[[nodiscard]] Result<Publication> commit_structural_archive_manifest(
    const std::filesystem::path &project_path,
    const ObjectToStore &manifest,
    TransactionOptions options = {}) noexcept;

[[nodiscard]] Result<Publication> publish_structural_archive(
    const std::filesystem::path &project_path,
    const StructuralArchiveObjects &objects,
    TransactionOptions options = {}) noexcept;

[[nodiscard]] Result<ProjectV2>
open_read_only(const std::filesystem::path &project_path,
               TransactionOptions options = {}) noexcept;

} // namespace prometheus::run_store
