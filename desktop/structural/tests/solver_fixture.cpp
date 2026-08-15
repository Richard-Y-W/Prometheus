#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  const std::string job = argv[1];
  std::cout << "fixture stdout\n";
  std::cerr << "fixture stderr\n";
  if (job == "nonzero") return 7;
  if (job == "timeout") {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
  }
  if (job == "missing") return 0;
  if (job != "success" && job != "prometheus_structural_run") return 8;
  std::ofstream(job + ".dat") << R"(
 displacements (vx,vy,vz) for set NALL and time  0.1000000E+01

         1  0.000000E+00  0.000000E+00 -2.000000E-05

 stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set COMPONENT and time  0.1000000E+01

         1   1  1.000000E+06  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00
)";
  std::ofstream(job + ".frd") << "fixture frd\n";
  std::ofstream(job + ".sta") << "fixture sta\n";
  return 0;
}
