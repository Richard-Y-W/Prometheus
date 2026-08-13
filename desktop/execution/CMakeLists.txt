add_library(prometheus_execution STATIC
  src/contracts.cpp
)
target_compile_features(prometheus_execution PUBLIC cxx_std_20)
target_include_directories(prometheus_execution
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
)
target_link_libraries(prometheus_execution PRIVATE
  prometheus_integrity
  prometheus_core
)

if(BUILD_TESTING)
  add_executable(prometheus_execution_contracts_tests
    tests/contracts_tests.cpp
  )
  target_compile_features(prometheus_execution_contracts_tests PRIVATE cxx_std_20)
  target_include_directories(prometheus_execution_contracts_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_compile_definitions(prometheus_execution_contracts_tests PRIVATE
    PROMETHEUS_EXECUTION_LINK_LIBRARIES="$<JOIN:$<TARGET_PROPERTY:prometheus_execution,LINK_LIBRARIES>,|>"
  )
  target_link_libraries(prometheus_execution_contracts_tests PRIVATE
    prometheus_execution
    prometheus_integrity
  )
  add_test(
    NAME prometheus_execution_contracts
    COMMAND prometheus_execution_contracts_tests
  )
endif()
