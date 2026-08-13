set(PROMETHEUS_CONSUMER_JCS
  "${PROJECT_SOURCE_DIR}/fixtures/contracts/package-consumer.motor-arm-builtin-v1.jcs"
)
set(PROMETHEUS_CONSUMER_HASH_FILE
  "${PROJECT_SOURCE_DIR}/fixtures/contracts/package-consumer.motor-arm-builtin-v1.sha256"
)
file(READ "${PROMETHEUS_CONSUMER_HASH_FILE}" PROMETHEUS_SUPPORTED_CONSUMER_HASH)
string(STRIP "${PROMETHEUS_SUPPORTED_CONSUMER_HASH}" PROMETHEUS_SUPPORTED_CONSUMER_HASH)
string(LENGTH "${PROMETHEUS_SUPPORTED_CONSUMER_HASH}" PROMETHEUS_CONSUMER_HASH_LENGTH)
if(NOT PROMETHEUS_CONSUMER_HASH_LENGTH EQUAL 71 OR
   NOT PROMETHEUS_SUPPORTED_CONSUMER_HASH MATCHES "^sha256:[0-9a-f]+$")
  message(FATAL_ERROR "package consumer hash file has invalid spelling")
endif()
string(SUBSTRING "${PROMETHEUS_SUPPORTED_CONSUMER_HASH}" 7 64
  PROMETHEUS_SUPPORTED_CONSUMER_HASH_HEX
)
file(SHA256 "${PROMETHEUS_CONSUMER_JCS}" PROMETHEUS_ACTUAL_CONSUMER_HASH_HEX)
if(NOT PROMETHEUS_ACTUAL_CONSUMER_HASH_HEX STREQUAL
   PROMETHEUS_SUPPORTED_CONSUMER_HASH_HEX)
  message(FATAL_ERROR "package consumer canonical bytes disagree with hash file")
endif()
file(SIZE "${PROMETHEUS_CONSUMER_JCS}" PROMETHEUS_SUPPORTED_CONSUMER_BYTE_LENGTH)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/prometheus/execution")
configure_file(
  include/prometheus/execution/supported_consumer_contract.hpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/prometheus/execution/supported_consumer_contract.hpp"
  @ONLY
)

add_library(prometheus_execution STATIC
  src/contracts.cpp
  src/package_consumer.cpp
)
target_compile_features(prometheus_execution PUBLIC cxx_std_20)
target_include_directories(prometheus_execution
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
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

  add_executable(prometheus_package_consumer_tests
    tests/package_consumer_tests.cpp
  )
  target_compile_features(prometheus_package_consumer_tests PRIVATE cxx_std_20)
  target_include_directories(prometheus_package_consumer_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_compile_definitions(prometheus_package_consumer_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(prometheus_package_consumer_tests PRIVATE
    prometheus_execution
    prometheus_integrity
  )
  add_test(
    NAME prometheus_package_consumer
    COMMAND prometheus_package_consumer_tests
  )
endif()
