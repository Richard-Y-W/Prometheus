#include <iostream>

int main() {
  std::cerr
      << "Raw-deck execution is disabled: a CalculiX run must receive an "
         "immutable CompiledStructuralSetup. The package-backed command is "
         "enabled with the reconciled structural workflow.\n";
  return 2;
}
