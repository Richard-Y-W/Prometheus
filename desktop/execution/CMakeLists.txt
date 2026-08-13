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

set(PROMETHEUS_BACKEND_IDENTITY_FILES
  "${PROJECT_SOURCE_DIR}/desktop/core/include/prometheus/simulation/motor_arm_builtin_v1.hpp"
  "${PROJECT_SOURCE_DIR}/desktop/core/src/motor_arm_builtin_v1.cpp"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${PROMETHEUS_BACKEND_IDENTITY_FILES}
)
set(PROMETHEUS_BACKEND_IDENTITY_MATERIAL
  "motor_arm_builtin_v1|1.0.0|fp_contract=disabled|fast_math=false|rounding=to_nearest"
)
foreach(PROMETHEUS_BACKEND_IDENTITY_FILE IN LISTS PROMETHEUS_BACKEND_IDENTITY_FILES)
  file(SHA256 "${PROMETHEUS_BACKEND_IDENTITY_FILE}" PROMETHEUS_BACKEND_FILE_HASH)
  string(APPEND PROMETHEUS_BACKEND_IDENTITY_MATERIAL
    "|${PROMETHEUS_BACKEND_FILE_HASH}"
  )
endforeach()
string(SHA256 PROMETHEUS_BACKEND_BUILD_HASH
  "${PROMETHEUS_BACKEND_IDENTITY_MATERIAL}"
)
set(PROMETHEUS_BACKEND_BUILD_FINGERPRINT
  "sha256:${PROMETHEUS_BACKEND_BUILD_HASH}"
)

add_library(prometheus_execution STATIC
  src/contracts.cpp
  src/execute.cpp
  src/finding_compiler.cpp
  src/numeric_profile.cpp
  src/numeric_profile_platform.cpp
  src/package_consumer.cpp
)
target_compile_features(prometheus_execution PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(prometheus_execution PRIVATE /fp:strict)
else()
  target_compile_options(prometheus_execution PRIVATE
    -ffp-contract=off
    -fno-fast-math
  )
endif()
target_include_directories(prometheus_execution
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
)
target_link_libraries(prometheus_execution
  PRIVATE prometheus_integrity
  PUBLIC prometheus_core
)
target_compile_definitions(prometheus_execution PRIVATE
  PROMETHEUS_BACKEND_BUILD_FINGERPRINT="${PROMETHEUS_BACKEND_BUILD_FINGERPRINT}"
  PROMETHEUS_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
  PROMETHEUS_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
)

add_executable(prometheus_export_program_01b_vectors
  tools/export_program_01b_vectors.cpp
)
target_compile_features(prometheus_export_program_01b_vectors PRIVATE cxx_std_20)
target_include_directories(prometheus_export_program_01b_vectors PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
)
target_compile_definitions(prometheus_export_program_01b_vectors PRIVATE
  PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(prometheus_export_program_01b_vectors PRIVATE
  prometheus_execution
  prometheus_integrity
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

  add_executable(prometheus_motor_backend_tests
    tests/motor_backend_tests.cpp
  )
  target_compile_features(prometheus_motor_backend_tests PRIVATE cxx_std_20)
  target_link_libraries(prometheus_motor_backend_tests PRIVATE prometheus_core)
  add_test(NAME prometheus_motor_backend COMMAND prometheus_motor_backend_tests)

  add_executable(prometheus_finding_compiler_tests
    tests/finding_compiler_tests.cpp
  )
  target_compile_features(prometheus_finding_compiler_tests PRIVATE cxx_std_20)
  target_link_libraries(prometheus_finding_compiler_tests PRIVATE
    prometheus_execution
    prometheus_integrity
  )
  add_test(
    NAME prometheus_finding_compiler
    COMMAND prometheus_finding_compiler_tests
  )

  add_executable(prometheus_execution_process_probe
    tests/execution_process_probe.cpp
  )
  target_compile_features(prometheus_execution_process_probe PRIVATE cxx_std_20)
  target_link_libraries(prometheus_execution_process_probe PRIVATE
    prometheus_execution
  )

  add_executable(prometheus_execution_tests
    tests/execution_tests.cpp
  )
  target_compile_features(prometheus_execution_tests PRIVATE cxx_std_20)
  target_include_directories(prometheus_execution_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_compile_definitions(prometheus_execution_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    PROMETHEUS_EXECUTION_PROCESS_PROBE="$<TARGET_FILE:prometheus_execution_process_probe>"
  )
  target_link_libraries(prometheus_execution_tests PRIVATE
    prometheus_execution
    prometheus_integrity
  )
  add_dependencies(prometheus_execution_tests prometheus_execution_process_probe)
  add_test(
    NAME prometheus_execution_end_to_end
    COMMAND prometheus_execution_tests
  )
endif()
