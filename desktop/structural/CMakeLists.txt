add_library(prometheus_structural STATIC
  src/calculix_deck.cpp
  src/calculix_runner.cpp
  src/calculix_result.cpp
  src/gmsh_mesh.cpp
  src/mesh_validation.cpp
  src/prepared_mesh.cpp
  src/reviewed_pair.cpp
  src/surface_groups.cpp
  src/surface_selection.cpp
  src/structural_request.cpp
  src/structural_observables.cpp
  src/structural_findings.cpp
  src/structural_refinement.cpp
  src/structural_archive.cpp
  src/structural_benchmarks.cpp
  src/structural_setup.cpp
)
if(WIN32)
  target_sources(prometheus_structural PRIVATE src/solver_process_windows.cpp)
else()
  target_sources(prometheus_structural PRIVATE src/solver_process_posix.cpp)
endif()
target_compile_features(prometheus_structural PUBLIC cxx_std_20)
target_include_directories(prometheus_structural PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
)
target_link_libraries(prometheus_structural PUBLIC prometheus_integrity)
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

add_executable(prometheus_structural_mesh_probe
  tools/structural_mesh_probe.cpp
)
target_link_libraries(prometheus_structural_mesh_probe PRIVATE
  prometheus_structural
)

add_executable(prometheus_run_calculix_job
  tools/run_calculix_job.cpp
)
target_link_libraries(prometheus_run_calculix_job PRIVATE
  prometheus_structural
)

add_executable(prometheus_run_structural_benchmark
  tools/run_structural_benchmark.cpp
)
target_link_libraries(prometheus_run_structural_benchmark PRIVATE
  prometheus_structural
)

add_executable(prometheus_run_structural_refinement
  tools/run_structural_refinement.cpp
)
target_link_libraries(prometheus_run_structural_refinement PRIVATE
  prometheus_structural
)

add_executable(prometheus_run_reviewed_structural_pair
  tools/run_reviewed_structural_pair.cpp
)
target_link_libraries(prometheus_run_reviewed_structural_pair PRIVATE
  prometheus_structural
)

add_executable(prometheus_replay_structural_run
  tools/replay_structural_run.cpp
)
target_link_libraries(prometheus_replay_structural_run PRIVATE
  prometheus_structural
)

add_executable(prometheus_export_structural_archive
  tools/export_structural_archive.cpp
)
target_link_libraries(prometheus_export_structural_archive PRIVATE
  prometheus_structural
)

if(BUILD_TESTING)
  add_executable(prometheus_structural_solver_fixture
    tests/solver_fixture.cpp
  )
  add_executable(prometheus_structural_tests
    tests/structural_tests.cpp
  )
  add_executable(prometheus_structural_observable_tests
    tests/structural_observable_tests.cpp
  )
  add_executable(prometheus_reviewed_pair_tests
    tests/reviewed_pair_tests.cpp
  )
  target_link_libraries(prometheus_structural_tests PRIVATE
    prometheus_structural
  )
  target_include_directories(prometheus_structural_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_link_libraries(prometheus_structural_observable_tests PRIVATE
    prometheus_structural
  )
  target_link_libraries(prometheus_reviewed_pair_tests PRIVATE
    prometheus_structural
  )
  target_include_directories(prometheus_reviewed_pair_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_compile_definitions(prometheus_reviewed_pair_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
  )
  target_compile_definitions(prometheus_structural_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    PROMETHEUS_SOLVER_FIXTURE_PATH="$<TARGET_FILE:prometheus_structural_solver_fixture>"
  )
  add_dependencies(
    prometheus_structural_tests
    prometheus_export_structural_smoke
    prometheus_replay_structural_run
    prometheus_run_calculix_job
    prometheus_run_structural_benchmark
    prometheus_run_structural_refinement
    prometheus_structural_solver_fixture
  )
  add_dependencies(
    prometheus_reviewed_pair_tests
    prometheus_run_reviewed_structural_pair
    prometheus_replay_structural_run
    prometheus_structural_solver_fixture
  )
  add_test(NAME prometheus_structural_tests COMMAND prometheus_structural_tests)
  add_test(
    NAME prometheus_structural_observable_tests
    COMMAND prometheus_structural_observable_tests
  )
  add_test(
    NAME prometheus_reviewed_pair_tests
    COMMAND prometheus_reviewed_pair_tests
  )
  add_test(
    NAME prometheus_reviewed_pair_tool_fixture
    COMMAND ${CMAKE_COMMAND}
      -DPAIR_RUNNER=$<TARGET_FILE:prometheus_run_reviewed_structural_pair>
      -DREPLAY=$<TARGET_FILE:prometheus_replay_structural_run>
      -DSOLVER=$<TARGET_FILE:prometheus_structural_solver_fixture>
      -DFIXTURE_ROOT=${PROJECT_SOURCE_DIR}/fixtures/structural/reviewed-pair-smoke
      -DYUBI_FIXTURE_ROOT=${PROJECT_SOURCE_DIR}/fixtures/structural/yubi-bracket
      -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/reviewed-pair-tool-fixture
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/reviewed_pair_tool_fixture.cmake
  )
  add_test(
    NAME prometheus_structural_tool_fixture
    COMMAND ${CMAKE_COMMAND}
      -DEXPORTER=$<TARGET_FILE:prometheus_export_structural_smoke>
      -DRUNNER=$<TARGET_FILE:prometheus_run_calculix_job>
      -DBENCHMARK=$<TARGET_FILE:prometheus_run_structural_benchmark>
      -DREFINEMENT=$<TARGET_FILE:prometheus_run_structural_refinement>
      -DREPLAY=$<TARGET_FILE:prometheus_replay_structural_run>
      -DSOLVER=$<TARGET_FILE:prometheus_structural_solver_fixture>
      -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/structural-tool-fixture
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/structural_tool_fixture.cmake
  )
endif()
