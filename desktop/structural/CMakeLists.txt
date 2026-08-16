add_library(prometheus_structural STATIC
  src/calculix_deck.cpp
  src/calculix_runner.cpp
  src/calculix_result.cpp
  src/gmsh_mesh.cpp
  src/surface_groups.cpp
  src/surface_selection.cpp
  src/structural_request.cpp
  src/structural_findings.cpp
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
  target_link_libraries(prometheus_structural_tests PRIVATE
    prometheus_structural
  )
  target_compile_definitions(prometheus_structural_tests PRIVATE
    PROMETHEUS_SOLVER_FIXTURE_PATH="$<TARGET_FILE:prometheus_structural_solver_fixture>"
  )
  add_dependencies(prometheus_structural_tests prometheus_structural_solver_fixture)
  add_test(NAME prometheus_structural_tests COMMAND prometheus_structural_tests)
endif()
