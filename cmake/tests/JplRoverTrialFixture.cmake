cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS CASE REPOSITORY_ROOT TEST_BINARY_ROOT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
if(NOT CASE STREQUAL "correct" AND NOT CASE STREQUAL "wrong_license")
  message(FATAL_ERROR "Unknown fixture CASE: ${CASE}")
endif()

find_program(GIT_EXECUTABLE NAMES git REQUIRED)
set(driver "${REPOSITORY_ROOT}/scripts/jpl-rover-trial.cmake")
if(NOT EXISTS "${driver}")
  message(FATAL_ERROR "Rover trial driver is missing: ${driver}")
endif()

file(REMOVE_RECURSE "${TEST_BINARY_ROOT}")
file(MAKE_DIRECTORY "${TEST_BINARY_ROOT}")
set(upstream "${TEST_BINARY_ROOT}/upstream")
set(external_root "${TEST_BINARY_ROOT}/external")
set(trial_root "${TEST_BINARY_ROOT}/trials")
file(MAKE_DIRECTORY "${upstream}")

function(run_required label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "${label} failed with exit ${result}\nstdout:\n${output}\nstderr:\n${error}")
  endif()
endfunction()

run_required("git init" "${GIT_EXECUTABLE}" -C "${upstream}" init --quiet)
file(WRITE "${upstream}/LICENSE.txt" "fixture license\n")
file(WRITE "${upstream}/assembly.step" "ISO-10303-21;\nEND-ISO-10303-21;\n")
file(WRITE "${upstream}/ignored.tmp" "must not enter the archive\n")
file(APPEND "${upstream}/.git/info/exclude" "ignored.tmp\n")
run_required("git add" "${GIT_EXECUTABLE}" -C "${upstream}" add LICENSE.txt assembly.step)
run_required(
  "git commit" "${GIT_EXECUTABLE}" -C "${upstream}"
  -c user.name=PrometheusFixture -c user.email=fixture@example.invalid
  commit --quiet -m fixture)
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${upstream}" rev-parse HEAD
  RESULT_VARIABLE revision_result
  OUTPUT_VARIABLE revision
  ERROR_VARIABLE revision_error
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(LENGTH "${revision}" revision_length)
if(NOT revision_result EQUAL 0 OR NOT revision_length EQUAL 40 OR
   NOT revision MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Could not resolve fixture revision: ${revision_error}")
endif()
file(SHA256 "${upstream}/LICENSE.txt" license_sha256)
string(SUBSTRING "${revision}" 0 7 revision_short)
set(trial "${trial_root}/jpl-open-source-rover-${revision_short}")
set(sidecar "${trial}.source.json")

set(common_arguments
  "-DPROMETHEUS_JPL_MODE=prepare"
  "-DPROMETHEUS_JPL_REFRESH=ON"
  "-DPROMETHEUS_JPL_TESTING=ON"
  "-DPROMETHEUS_JPL_TEST_SOURCE_REPOSITORY=${upstream}"
  "-DPROMETHEUS_JPL_TEST_REVISION=${revision}"
  "-DPROMETHEUS_JPL_TEST_EXTERNAL_ROOT=${external_root}"
  "-DPROMETHEUS_JPL_TEST_TRIAL_ROOT=${trial_root}"
)

execute_process(
  COMMAND "${CMAKE_COMMAND}" ${common_arguments}
    "-DPROMETHEUS_JPL_TEST_LICENSE_SHA256=${license_sha256}"
    -P "${driver}"
  RESULT_VARIABLE prepare_result
  OUTPUT_VARIABLE prepare_output
  ERROR_VARIABLE prepare_error
)
if(NOT prepare_result EQUAL 0)
  message(FATAL_ERROR
    "Fixture preparation failed with exit ${prepare_result}\nstdout:\n${prepare_output}\nstderr:\n${prepare_error}")
endif()

file(GLOB_RECURSE prepared_files
  LIST_DIRECTORIES FALSE RELATIVE "${trial}" "${trial}/*")
list(SORT prepared_files)
if(NOT prepared_files STREQUAL "LICENSE.txt;assembly.step")
  message(FATAL_ERROR
    "Pinned archive contained unexpected files: '${prepared_files}'")
endif()
if(NOT EXISTS "${sidecar}")
  message(FATAL_ERROR "Prepared trial sidecar is missing")
endif()

if(CASE STREQUAL "correct")
  file(APPEND "${trial}/assembly.step" "tampered\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -DPROMETHEUS_JPL_MODE=prepare
      -DPROMETHEUS_JPL_TESTING=ON
      "-DPROMETHEUS_JPL_TEST_SOURCE_REPOSITORY=${upstream}"
      "-DPROMETHEUS_JPL_TEST_REVISION=${revision}"
      "-DPROMETHEUS_JPL_TEST_LICENSE_SHA256=${license_sha256}"
      "-DPROMETHEUS_JPL_TEST_EXTERNAL_ROOT=${external_root}"
      "-DPROMETHEUS_JPL_TEST_TRIAL_ROOT=${trial_root}"
      -P "${driver}"
    RESULT_VARIABLE corrupt_result
    OUTPUT_VARIABLE corrupt_output
    ERROR_VARIABLE corrupt_error
  )
  if(corrupt_result EQUAL 0)
    message(FATAL_ERROR "Normal mode trusted a corrupted cached trial")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${common_arguments}
      "-DPROMETHEUS_JPL_TEST_LICENSE_SHA256=${license_sha256}"
      -P "${driver}"
    RESULT_VARIABLE refresh_result
    OUTPUT_VARIABLE refresh_output
    ERROR_VARIABLE refresh_error
  )
  if(NOT refresh_result EQUAL 0)
    message(FATAL_ERROR
      "Refresh did not restore from the pinned object: ${refresh_error}")
  endif()
  file(SHA256 "${trial}/assembly.step" restored_sha256)
  file(SHA256 "${upstream}/assembly.step" expected_restored_sha256)
  if(NOT restored_sha256 STREQUAL expected_restored_sha256)
    message(FATAL_ERROR "Refresh did not restore exact pinned bytes")
  endif()
elseif(CASE STREQUAL "wrong_license")
  file(SHA256 "${trial}/LICENSE.txt" license_before)
  file(SHA256 "${trial}/assembly.step" assembly_before)
  file(SHA256 "${sidecar}" sidecar_before)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${common_arguments}
      -DPROMETHEUS_JPL_TEST_LICENSE_SHA256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
      -P "${driver}"
    RESULT_VARIABLE rejected_result
    OUTPUT_VARIABLE rejected_output
    ERROR_VARIABLE rejected_error
  )
  if(rejected_result EQUAL 0)
    message(FATAL_ERROR "Preparation accepted the wrong license SHA-256")
  endif()
  file(SHA256 "${trial}/LICENSE.txt" license_after)
  file(SHA256 "${trial}/assembly.step" assembly_after)
  file(SHA256 "${sidecar}" sidecar_after)
  if(NOT license_before STREQUAL license_after OR
     NOT assembly_before STREQUAL assembly_after OR
     NOT sidecar_before STREQUAL sidecar_after)
    message(FATAL_ERROR
      "Failed preparation changed the last valid trial or sidecar")
  endif()
endif()
