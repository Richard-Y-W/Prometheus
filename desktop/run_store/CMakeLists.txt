add_library(prometheus_run_store STATIC
  src/project_v2.cpp
  src/legacy_project_v1.cpp
  src/object_store.cpp
  src/run_store.cpp
  src/structural_archive_store.cpp
  src/project_bundle.cpp
)
if(WIN32)
  target_sources(prometheus_run_store PRIVATE src/platform_io_windows.cpp)
elseif(UNIX)
  target_sources(prometheus_run_store PRIVATE src/platform_io_posix.cpp)
else()
  message(FATAL_ERROR "Prometheus run store requires native POSIX or Windows I/O")
endif()
target_compile_features(prometheus_run_store PUBLIC cxx_std_20)
target_include_directories(prometheus_run_store
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
)
target_link_libraries(prometheus_run_store PUBLIC prometheus_integrity)

if(BUILD_TESTING)
  add_executable(prometheus_run_store_project_tests
    tests/project_v2_tests.cpp
  )
  target_compile_features(prometheus_run_store_project_tests PRIVATE cxx_std_20)
  target_include_directories(prometheus_run_store_project_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_compile_definitions(prometheus_run_store_project_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    PROMETHEUS_RUN_STORE_LINK_LIBRARIES="$<JOIN:$<TARGET_PROPERTY:prometheus_run_store,LINK_LIBRARIES>,|>"
  )
  target_link_libraries(prometheus_run_store_project_tests PRIVATE
    prometheus_run_store
    prometheus_integrity
  )
  add_test(
    NAME prometheus_run_store_project
    COMMAND prometheus_run_store_project_tests
  )

  add_executable(prometheus_legacy_project_v1_tests
    tests/legacy_project_v1_tests.cpp
  )
  target_compile_features(prometheus_legacy_project_v1_tests PRIVATE cxx_std_20)
  target_include_directories(prometheus_legacy_project_v1_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_link_libraries(prometheus_legacy_project_v1_tests PRIVATE
    prometheus_run_store
  )
  add_test(
    NAME prometheus_legacy_project
    COMMAND prometheus_legacy_project_v1_tests
  )

  add_executable(prometheus_run_store_object_tests
    tests/object_store_tests.cpp
  )
  target_compile_features(prometheus_run_store_object_tests PRIVATE cxx_std_20)
  target_compile_definitions(prometheus_run_store_object_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(prometheus_run_store_object_tests PRIVATE
    prometheus_run_store
    prometheus_integrity
  )
  add_test(
    NAME prometheus_run_store_object
    COMMAND prometheus_run_store_object_tests
  )

  add_executable(prometheus_run_store_contention_helper
    tests/run_store_contention_helper.cpp
  )
  target_compile_features(prometheus_run_store_contention_helper PRIVATE cxx_std_20)
  target_compile_definitions(prometheus_run_store_contention_helper PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(prometheus_run_store_contention_helper PRIVATE
    prometheus_run_store
    prometheus_integrity
  )

  add_executable(prometheus_run_store_transaction_tests
    tests/run_store_transaction_tests.cpp
  )
  target_compile_features(prometheus_run_store_transaction_tests PRIVATE cxx_std_20)
  target_compile_definitions(prometheus_run_store_transaction_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    PROMETHEUS_RUN_STORE_CONTENTION_HELPER="$<TARGET_FILE:prometheus_run_store_contention_helper>"
  )
  target_link_libraries(prometheus_run_store_transaction_tests PRIVATE
    prometheus_run_store
    prometheus_integrity
  )
  add_dependencies(prometheus_run_store_transaction_tests
    prometheus_run_store_contention_helper
  )
  add_test(
    NAME prometheus_run_store_transaction
    COMMAND prometheus_run_store_transaction_tests
  )
endif()
