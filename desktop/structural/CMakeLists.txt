add_library(prometheus_structural STATIC
  src/calculix_deck.cpp
  src/calculix_result.cpp
  src/gmsh_mesh.cpp
  src/mesh_validation.cpp
  src/smoke_case.cpp
  src/structural_case.cpp
  src/structural_finding.cpp
  src/structural_request.cpp
  src/surface_setup.cpp
)
target_compile_features(prometheus_structural PUBLIC cxx_std_20)
target_include_directories(prometheus_structural PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(prometheus_structural PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
)
target_link_libraries(prometheus_structural PRIVATE prometheus_integrity)
if(MSVC)
  target_compile_options(prometheus_structural PRIVATE /fp:strict)
else()
  target_compile_options(prometheus_structural PRIVATE
    -ffp-contract=off
    -fno-fast-math
  )
endif()

add_executable(prometheus_export_structural_smoke
  tools/export_structural_smoke.cpp
)
target_link_libraries(prometheus_export_structural_smoke PRIVATE
  prometheus_structural
)

add_executable(prometheus_export_structural_case
  tools/export_structural_case.cpp
)
target_include_directories(prometheus_export_structural_case PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
)
target_link_libraries(prometheus_export_structural_case PRIVATE
  prometheus_structural
  prometheus_integrity
)

add_executable(prometheus_structural_mesh_probe
  tools/structural_mesh_probe.cpp
)
target_link_libraries(prometheus_structural_mesh_probe PRIVATE
  prometheus_structural
)

add_executable(prometheus_verify_structural_smoke
  tools/verify_structural_smoke.cpp
)
target_link_libraries(prometheus_verify_structural_smoke PRIVATE
  prometheus_structural
)

add_executable(prometheus_verify_structural_case
  tools/verify_structural_case.cpp
)
target_include_directories(prometheus_verify_structural_case PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
)
target_link_libraries(prometheus_verify_structural_case PRIVATE
  prometheus_structural
  prometheus_integrity
)

if(BUILD_TESTING)
  add_executable(prometheus_structural_tests
    tests/structural_tests.cpp
  )
  target_link_libraries(prometheus_structural_tests PRIVATE
    prometheus_structural
  )
  add_test(NAME prometheus_structural_tests COMMAND prometheus_structural_tests)
  set(structural_smoke_fixture
    ${PROJECT_SOURCE_DIR}/fixtures/structural/calculix-smoke/complete
  )
  add_test(
    NAME prometheus_structural_smoke_verifier_fixture
    COMMAND prometheus_verify_structural_smoke
      ${structural_smoke_fixture}
      0
      sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
      "CalculiX Version 2.23"
      ${structural_smoke_fixture}/prometheus_tetra_smoke.stdout.txt
      ${structural_smoke_fixture}/prometheus_tetra_smoke.stderr.txt
  )
  add_test(
    NAME prometheus_structural_case_tools_fixture
    COMMAND ${CMAKE_COMMAND}
      -DEXPORTER=$<TARGET_FILE:prometheus_export_structural_case>
      -DVERIFIER=$<TARGET_FILE:prometheus_verify_structural_case>
      -DFIXTURE=${PROJECT_SOURCE_DIR}/fixtures/structural/tension-bar
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/structural-case-tools-fixture
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/structural_case_tools_fixture.cmake
  )
endif()
