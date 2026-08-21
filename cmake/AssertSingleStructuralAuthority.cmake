if(NOT DEFINED REPOSITORY_ROOT OR REPOSITORY_ROOT STREQUAL "")
  message(FATAL_ERROR "REPOSITORY_ROOT is required")
endif()

set(retired_files
  desktop/app/structural_setup_controller.cpp
  desktop/app/structural_setup_controller.hpp
  desktop/app/tests/structural_setup_controller_tests.cpp
  desktop/structural/include/prometheus/structural/structural_finding.hpp
  desktop/structural/src/structural_finding.cpp
  desktop/structural/include/prometheus/structural/structural_case.hpp
  desktop/structural/src/structural_case.cpp
  desktop/structural/include/prometheus/structural/surface_setup.hpp
  desktop/structural/src/surface_setup.cpp
  desktop/structural/include/prometheus/structural/smoke_case.hpp
  desktop/structural/src/smoke_case.cpp
  desktop/structural/tools/export_structural_case.cpp
  desktop/structural/tools/verify_structural_case.cpp
  desktop/structural/tools/verify_structural_smoke.cpp
  desktop/structural/tests/structural_case_tools_fixture.cmake
)

foreach(path IN LISTS retired_files)
  if(EXISTS "${REPOSITORY_ROOT}/${path}")
    message(FATAL_ERROR "retired structural authority remains: ${path}")
  endif()
endforeach()

file(READ "${REPOSITORY_ROOT}/desktop/app/CMakeLists.txt" app_cmake)
file(READ "${REPOSITORY_ROOT}/desktop/structural/CMakeLists.txt" structural_cmake)
file(READ "${REPOSITORY_ROOT}/desktop/ui/Main.qml" main_qml)
file(READ "${REPOSITORY_ROOT}/scripts/run-calculix-smoke.ps1" smoke_script)
file(READ "${REPOSITORY_ROOT}/scripts/run-structural-validation.ps1"
     validation_script)
foreach(symbol IN ITEMS
    StructuralSetupController
    prometheus_export_structural_case
    prometheus_verify_structural_case
    prometheus_verify_structural_smoke)
  string(FIND
    "${app_cmake}${structural_cmake}${main_qml}${smoke_script}${validation_script}"
    "${symbol}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "retired structural symbol remains active: ${symbol}")
  endif()
endforeach()
