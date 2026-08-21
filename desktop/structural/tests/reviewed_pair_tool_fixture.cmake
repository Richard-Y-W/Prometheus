if(NOT DEFINED PAIR_RUNNER OR NOT DEFINED REPLAY OR NOT DEFINED SOLVER OR
   NOT DEFINED FIXTURE_ROOT OR NOT DEFINED OUTPUT_ROOT)
  message(FATAL_ERROR "reviewed-pair tool fixture arguments are incomplete")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}/mutable-fixture")
file(COPY
  "${FIXTURE_ROOT}/reviewed-pair.json"
  "${FIXTURE_ROOT}/coarse.inp"
  "${FIXTURE_ROOT}/fine.inp"
  "${FIXTURE_ROOT}/material-evidence.json"
  DESTINATION "${OUTPUT_ROOT}/mutable-fixture")

set(valid_output "${OUTPUT_ROOT}/valid-run")
execute_process(
  COMMAND "${PAIR_RUNNER}"
          "${FIXTURE_ROOT}/reviewed-pair.json"
          "${SOLVER}"
          "${valid_output}"
          5
  RESULT_VARIABLE runner_result
  OUTPUT_VARIABLE runner_stdout
  ERROR_VARIABLE runner_stderr
)
if(NOT runner_result EQUAL 0 OR
   NOT runner_stdout MATCHES "status=completed" OR
   NOT runner_stdout MATCHES "refinement=accepted" OR
   NOT runner_stdout MATCHES
       "evaluation=no_violation_detected_within_scope" OR
   NOT runner_stdout MATCHES "archive_schema_version=4.0.0")
  message(FATAL_ERROR
    "reviewed-pair runner failed (${runner_result})\n${runner_stdout}\n${runner_stderr}")
endif()

foreach(role IN ITEMS coarse fine)
  set(job "reviewed_pair_${role}")
  foreach(extension IN ITEMS inp dat frd sta)
    if(NOT EXISTS "${valid_output}/${job}.${extension}")
      message(FATAL_ERROR
        "reviewed-pair runner omitted ${job}.${extension}")
    endif()
  endforeach()
endforeach()
file(GLOB solver_inputs "${valid_output}/*.inp")
list(LENGTH solver_inputs solver_input_count)
if(NOT solver_input_count EQUAL 2)
  message(FATAL_ERROR
    "reviewed-pair runner created ${solver_input_count} solver inputs instead of two")
endif()

set(archive "${valid_output}/prometheus-structural-run.json")
if(NOT EXISTS "${archive}")
  message(FATAL_ERROR "reviewed-pair runner omitted its structural archive")
endif()
execute_process(
  COMMAND "${REPLAY}" "${archive}"
  RESULT_VARIABLE replay_result
  OUTPUT_VARIABLE replay_stdout
  ERROR_VARIABLE replay_stderr
)
if(NOT replay_result EQUAL 0 OR
   NOT replay_stdout MATCHES "status=verified")
  message(FATAL_ERROR
    "reviewed-pair archive replay failed (${replay_result})\n${replay_stdout}\n${replay_stderr}")
endif()

set(mutated_manifest
  "${OUTPUT_ROOT}/mutable-fixture/reviewed-pair.json")
file(READ "${mutated_manifest}" mutated_bytes)
string(REPLACE
  "sha256:470736a3283085f70819fdc2652ce1e6195a94f6b6b3581f47e1a4785c8682ae"
  "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
  mutated_bytes "${mutated_bytes}")
file(WRITE "${mutated_manifest}" "${mutated_bytes}")
set(failed_output "${OUTPUT_ROOT}/preflight-failure-output")
execute_process(
  COMMAND "${PAIR_RUNNER}"
          "${mutated_manifest}"
          "${SOLVER}"
          "${failed_output}"
          5
  RESULT_VARIABLE failure_result
  OUTPUT_VARIABLE failure_stdout
  ERROR_VARIABLE failure_stderr
)
if(failure_result EQUAL 0 OR
   NOT failure_stderr MATCHES "reviewed_pair_artifact_hash_mismatch")
  message(FATAL_ERROR
    "changed reviewed-pair mesh did not fail preflight (${failure_result})\n${failure_stdout}\n${failure_stderr}")
endif()
if(EXISTS "${failed_output}")
  message(FATAL_ERROR
    "preflight failure created a solver output directory")
endif()
