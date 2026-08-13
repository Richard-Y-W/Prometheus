add_library(prometheus_core STATIC
  src/motor_arm_builtin_v1.cpp
  src/project_summary.cpp
)
target_include_directories(prometheus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(prometheus_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(prometheus_core PRIVATE /fp:strict)
else()
  target_compile_options(prometheus_core PRIVATE -ffp-contract=off -fno-fast-math)
endif()
if(BUILD_TESTING)
  add_executable(prometheus_core_tests tests/core_tests.cpp)
  target_link_libraries(prometheus_core_tests PRIVATE prometheus_core)
  add_test(NAME prometheus_core_tests COMMAND prometheus_core_tests)

  add_executable(prometheus_project_summary_tests tests/project_summary_tests.cpp)
  target_link_libraries(prometheus_project_summary_tests PRIVATE prometheus_core)
  add_test(
    NAME prometheus_project_summary_tests
    COMMAND prometheus_project_summary_tests
  )
endif()
