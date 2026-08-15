add_library(prometheus_structural STATIC
  src/calculix_deck.cpp
  src/calculix_result.cpp
  src/gmsh_mesh.cpp
  src/structural_request.cpp
)
target_compile_features(prometheus_structural PUBLIC cxx_std_20)
target_include_directories(prometheus_structural PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
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

if(BUILD_TESTING)
  add_executable(prometheus_structural_tests
    tests/structural_tests.cpp
  )
  target_link_libraries(prometheus_structural_tests PRIVATE
    prometheus_structural
  )
  add_test(NAME prometheus_structural_tests COMMAND prometheus_structural_tests)
endif()
