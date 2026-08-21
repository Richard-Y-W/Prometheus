#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<int> ids_in_section(const std::string &deck,
                                const std::string &sectionPrefix) {
  std::vector<int> ids;
  std::istringstream input(deck);
  std::string line;
  bool inSection = false;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty() && line.front() == '*') {
      inSection = line.rfind(sectionPrefix, 0U) == 0U;
      continue;
    }
    if (!inSection)
      continue;
    std::istringstream row(line);
    int id{};
    char comma{};
    if (row >> id >> comma && comma == ',')
      ids.push_back(id);
  }
  return ids;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void write_valid_data(const std::string &job, const std::vector<int> &nodes,
                      const std::vector<int> &elements,
                      const double maximumDisplacementX = 0.0,
                      const double maximumDisplacementZ = -2.0e-5,
                      const double stress = 1.0e6,
                      const int peakStressElement = 0) {
  std::ofstream output(job + ".dat");
  output << " displacements (vx,vy,vz) for set NALL and time  "
            "0.1000000E+01\n\n";
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    output << nodes[index] << "  "
           << (index == 0U ? maximumDisplacementX : 0.0)
           << "  0.000000E+00  "
           << (index == 0U ? maximumDisplacementZ : 0.0) << '\n';
  }
  output << "\n stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) "
            "for set COMPONENT and time  0.1000000E+01\n\n";
  for (const int element : elements)
    output << element << "  1  "
           << (element == peakStressElement ? stress * 1.25 : stress)
           << "  0.000000E+00  0.000000E+00  "
              "0.000000E+00  0.000000E+00  0.000000E+00\n";
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  const std::string job = argv[1];
  std::cout << "CalculiX Version 2.23\nJob finished\n";
  std::cerr << "fixture stderr\n";
  if (job == "nonzero")
    return 7;
  if (job == "timeout") {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
  }
  if (job == "missing")
    return 0;

  const auto deck = read_file(job + ".inp");
  const auto nodes = ids_in_section(deck, "*NODE");
  const auto elements = ids_in_section(deck, "*ELEMENT, TYPE=C3D4");
  if (nodes.empty() || elements.empty())
    return 8;
  if (job == "invalid") {
    std::ofstream(job + ".dat")
        << " displacements (vx,vy,vz) for set NALL and time  "
           "0.1000000E+01\n\n"
        << nodes.front() << "  malformed\n";
  } else {
    const bool axialBenchmark = job.starts_with("prometheus_axial_");
    const bool cantileverCoarse = job.ends_with("cantilever_coarse");
    const bool cantileverFine = job.ends_with("cantilever_fine");
    const bool reviewedPairIndeterminateCoarse =
        job.ends_with("reviewed_pair_indeterminate_coarse");
    const bool reviewedPairIndeterminateFine =
        job.ends_with("reviewed_pair_indeterminate_fine");
    const bool yubiFine = job.ends_with("yubi_bracket_fine");
    write_valid_data(
        job, nodes, elements, axialBenchmark ? 5.0e-7 : 0.0,
        axialBenchmark ? 0.0
                       : reviewedPairIndeterminateCoarse ? -2.0e-5
                         : reviewedPairIndeterminateFine ? -2.5e-5
                       : cantileverCoarse ? -1.8e-4
                         : cantileverFine ? -1.9e-4
                                           : -2.0e-5,
        axialBenchmark ? 1.0e5
                       : reviewedPairIndeterminateCoarse ? 1.0e6
                         : reviewedPairIndeterminateFine ? 1.25e6
                       : cantileverCoarse ? 5.5e6
                         : cantileverFine ? 5.8e6
                                           : 1.0e6,
        yubiFine ? 40946 : 0);
  }
  std::ofstream(job + ".frd") << "fixture frd\n";
  std::ofstream(job + ".sta") << "1 1 1 1 1.0 1.0 1.0\n";
  return 0;
}
