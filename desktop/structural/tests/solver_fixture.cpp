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
                      const std::vector<int> &elements) {
  std::ofstream output(job + ".dat");
  output << " displacements (vx,vy,vz) for set NALL and time  "
            "0.1000000E+01\n\n";
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    output << nodes[index] << "  0.000000E+00  0.000000E+00  "
           << (index == 0U ? "-2.000000E-05" : "0.000000E+00") << '\n';
  }
  output << "\n stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) "
            "for set COMPONENT and time  0.1000000E+01\n\n";
  for (const int element : elements)
    output << element
           << "  1  1.000000E+06  0.000000E+00  0.000000E+00  "
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
    write_valid_data(job, nodes, elements);
  }
  std::ofstream(job + ".frd") << "fixture frd\n";
  std::ofstream(job + ".sta") << "1 1 1 1 1.0 1.0 1.0\n";
  return 0;
}
