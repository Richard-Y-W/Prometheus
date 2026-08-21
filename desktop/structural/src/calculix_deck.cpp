#include "prometheus/structural/calculix_deck.hpp"

#include "calculix_deck_internal.hpp"
#include "prometheus/structural/structural_request.hpp"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace prometheus::structural {

std::string detail::generate_validated_calculix_deck(
    const StructuralRequest &request) {
  auto nodes = request.nodes;
  auto elements = request.elements;
  auto restraints = request.fully_fixed_node_ids;
  auto loads = request.nodal_forces;
  std::ranges::sort(nodes, {}, &Node::id);
  std::ranges::sort(elements, {}, &Tetrahedron::id);
  std::ranges::sort(restraints);
  restraints.erase(std::unique(restraints.begin(), restraints.end()), restraints.end());
  std::ranges::sort(loads, {}, &NodalForce::node_id);

  std::ostringstream out;
  out.imbue(std::locale::classic());
  // CalculiX token fields reject the longer 17-digit scientific spelling.
  // Ten digits remains deterministic and exceeds the intended input precision.
  out << std::scientific << std::setprecision(10);
  out << "*HEADING\nPrometheus " << request.analysis_id << " | "
      << request.component_name << " | " << request.geometry_sha256 << "\n";
  out << "** SI units: m, N, Pa\n*NODE, NSET=NALL\n";
  for (const auto &node : nodes)
    out << node.id << ", " << node.position_m[0] << ", "
        << node.position_m[1] << ", " << node.position_m[2] << "\n";
  out << "*ELEMENT, TYPE=C3D4, ELSET=COMPONENT\n";
  for (const auto &element : elements)
    out << element.id << ", " << element.node_ids[0] << ", "
        << element.node_ids[1] << ", " << element.node_ids[2] << ", "
        << element.node_ids[3] << "\n";
  out << "*MATERIAL, NAME=REVIEWED_MATERIAL\n*ELASTIC\n"
      << request.youngs_modulus_pa << ", " << request.poisson_ratio << "\n";
  if (request.capability == StructuralCapability::modal_frequency)
    out << "*DENSITY\n" << *request.density_kg_m3 << "\n";
  out << "*SOLID SECTION, ELSET=COMPONENT, MATERIAL=REVIEWED_MATERIAL\n"
      << "*BOUNDARY\n";
  for (const int nodeId : restraints)
    out << nodeId << ", 1, 3, " << 0.0 << "\n";
  if (request.capability == StructuralCapability::modal_frequency) {
    // No *CLOAD: a modal deck extracts eigenvalues of the unloaded
    // structure. SOLVER=SPOOLES matches the linear-static step below --
    // the default PaStiX solver segfaults on small/degenerate systems
    // (empirically verified).
    out << "*STEP\n*FREQUENCY, SOLVER=SPOOLES\n1,\n*END STEP\n";
    return out.str();
  }
  out << "*STEP\n*STATIC, SOLVER=SPOOLES\n*CLOAD\n";
  for (const auto &load : loads)
    for (int direction = 0; direction < 3; ++direction)
      if (load.force_n[direction] != 0.0)
        out << load.node_id << ", " << direction + 1 << ", "
            << load.force_n[direction] << "\n";
  out << "*NODE FILE\nU\n*EL FILE\nS\n"
      << "*NODE PRINT, NSET=NALL\nU\n"
      << "*EL PRINT, ELSET=COMPONENT\nS\n*END STEP\n";
  return out.str();
}

std::string generate_calculix_deck(const StructuralRequest &request) {
  const auto issues = validate_request(request);
  if (!issues.empty())
    throw std::invalid_argument(issues.front().code + ": " +
                                issues.front().message);
  return detail::generate_validated_calculix_deck(request);
}

} // namespace prometheus::structural
