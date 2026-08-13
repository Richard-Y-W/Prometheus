add_library(prometheus_replay_support STATIC
  src/replay.cpp
)
target_compile_features(prometheus_replay_support PUBLIC cxx_std_20)
target_include_directories(prometheus_replay_support
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
)
target_link_libraries(prometheus_replay_support
  PUBLIC
    prometheus_execution
    prometheus_run_store
)

add_executable(prometheus_replay main.cpp)
target_compile_features(prometheus_replay PRIVATE cxx_std_20)
target_include_directories(prometheus_replay PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
)
target_link_libraries(prometheus_replay PRIVATE prometheus_replay_support)

if(BUILD_TESTING)
  add_executable(prometheus_create_replay_fixture
    tests/create_replay_fixture.cpp
  )
  target_compile_features(prometheus_create_replay_fixture PRIVATE cxx_std_20)
  target_include_directories(prometheus_create_replay_fixture PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_compile_definitions(prometheus_create_replay_fixture PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(prometheus_create_replay_fixture PRIVATE
    prometheus_execution
    prometheus_run_store
  )

  add_executable(prometheus_replay_cli_tests
    tests/replay_cli_tests.cpp
  )
  target_compile_features(prometheus_replay_cli_tests PRIVATE cxx_std_20)
  target_include_directories(prometheus_replay_cli_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_compile_definitions(prometheus_replay_cli_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    PROMETHEUS_REPLAY_EXECUTABLE="$<TARGET_FILE:prometheus_replay>"
    PROMETHEUS_REPLAY_FIXTURE_CREATOR="$<TARGET_FILE:prometheus_create_replay_fixture>"
    PROMETHEUS_RUN_STORE_CONTENTION_HELPER="$<TARGET_FILE:prometheus_run_store_contention_helper>"
    PROMETHEUS_REPLAY_LINK_LIBRARIES="$<JOIN:$<TARGET_PROPERTY:prometheus_replay,LINK_LIBRARIES>,|>"
    PROMETHEUS_REPLAY_SUPPORT_LINK_LIBRARIES="$<JOIN:$<TARGET_PROPERTY:prometheus_replay_support,LINK_LIBRARIES>,|>"
    PROMETHEUS_EXECUTION_LINK_LIBRARIES="$<JOIN:$<TARGET_PROPERTY:prometheus_execution,LINK_LIBRARIES>,|>"
    PROMETHEUS_RUN_STORE_LINK_LIBRARIES="$<JOIN:$<TARGET_PROPERTY:prometheus_run_store,LINK_LIBRARIES>,|>"
    PROMETHEUS_CORE_LINK_LIBRARIES="$<JOIN:$<TARGET_PROPERTY:prometheus_core,LINK_LIBRARIES>,|>"
    PROMETHEUS_INTEGRITY_LINK_LIBRARIES="$<JOIN:$<TARGET_PROPERTY:prometheus_integrity,LINK_LIBRARIES>,|>"
  )
  target_link_libraries(prometheus_replay_cli_tests PRIVATE
    prometheus_replay_support
  )
  add_dependencies(prometheus_replay_cli_tests
    prometheus_replay
    prometheus_create_replay_fixture
    prometheus_run_store_contention_helper
  )
  add_test(NAME prometheus_replay COMMAND prometheus_replay_cli_tests)
endif()
