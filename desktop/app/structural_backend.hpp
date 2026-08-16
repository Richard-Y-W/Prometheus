#pragma once

#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/prepared_mesh.hpp"
#include "prometheus/structural/structural_archive.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/structural_setup.hpp"
#include "prometheus/structural/surface_groups.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct DesktopStructuralRun final {
  prometheus::structural::SolverRunResult run;
  prometheus::structural::StructuralEvaluation evaluation;
  std::optional<prometheus::structural::StructuralArchive> archive;
  std::string archive_error;
};

class StructuralBackend {
public:
  virtual ~StructuralBackend() = default;

  [[nodiscard]] virtual prometheus::structural::PreparedMesh prepareMesh(
      std::string_view bytes, double coordinate_scale_to_m) const = 0;

  [[nodiscard]] virtual std::vector<prometheus::structural::SurfacePatch>
  groupPatches(const prometheus::structural::PreparedMesh &mesh,
               double angle_degrees) const = 0;

  [[nodiscard]] virtual prometheus::structural::CompiledStructuralSetup
  compileSetup(const prometheus::structural::StructuralSetup &setup) const = 0;

  [[nodiscard]] virtual DesktopStructuralRun execute(
      const prometheus::structural::SolverRunOptions &options,
      const prometheus::structural::CompiledStructuralSetup &setup) const = 0;
};

[[nodiscard]] std::shared_ptr<const StructuralBackend>
makeLocalStructuralBackend();
