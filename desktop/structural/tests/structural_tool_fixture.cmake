if(NOT DEFINED EXPORTER OR NOT DEFINED RUNNER OR NOT DEFINED BENCHMARK OR
   NOT DEFINED REFINEMENT OR NOT DEFINED REPLAY OR NOT DEFINED SOLVER OR
   NOT DEFINED OUTPUT_ROOT)
  message(FATAL_ERROR "structural tool fixture arguments are incomplete")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}/smoke")

execute_process(
  COMMAND "${EXPORTER}" "${OUTPUT_ROOT}/package"
  RESULT_VARIABLE exporter_result
  OUTPUT_VARIABLE exporter_stdout
  ERROR_VARIABLE exporter_stderr
)
if(NOT exporter_result EQUAL 0 OR
   NOT exporter_stdout MATCHES "status=exported compiled_setup_identity=" OR
   NOT EXISTS "${OUTPUT_ROOT}/package/reviewed-structural-setup.json" OR
   NOT EXISTS "${OUTPUT_ROOT}/package/prometheus_axial_smoke.inp")
  message(FATAL_ERROR
    "compiled smoke export failed (${exporter_result})\n${exporter_stdout}\n${exporter_stderr}")
endif()

execute_process(
  COMMAND "${RUNNER}" --axial-smoke "${SOLVER}"
          "${OUTPUT_ROOT}/smoke" compiled_smoke 5
  RESULT_VARIABLE runner_result
  OUTPUT_VARIABLE runner_stdout
  ERROR_VARIABLE runner_stderr
)
if(NOT runner_result EQUAL 0 OR
   NOT runner_stdout MATCHES "status=completed evidence=validated")
  message(FATAL_ERROR
    "compiled smoke runner failed (${runner_result})\n${runner_stdout}\n${runner_stderr}")
endif()

execute_process(
  COMMAND "${BENCHMARK}" "${SOLVER}" "${OUTPUT_ROOT}/benchmark" axial
  RESULT_VARIABLE benchmark_result
  OUTPUT_VARIABLE benchmark_stdout
  ERROR_VARIABLE benchmark_stderr
)
if(NOT benchmark_result EQUAL 0 OR
   NOT benchmark_stdout MATCHES "benchmark=passed")
  message(FATAL_ERROR
    "structural benchmark failed (${benchmark_result})\n${benchmark_stdout}\n${benchmark_stderr}")
endif()

execute_process(
  COMMAND "${REFINEMENT}" "${SOLVER}" "${OUTPUT_ROOT}/refinement" --smoke
  RESULT_VARIABLE refinement_result
  OUTPUT_VARIABLE refinement_stdout
  ERROR_VARIABLE refinement_stderr
)
if(NOT refinement_result EQUAL 0 OR
   NOT refinement_stdout MATCHES "validation_profile=smoke_non_authoritative" OR
   NOT refinement_stdout MATCHES "refinement=smoke_passed" OR
   NOT refinement_stdout MATCHES "observable.cantilever.maximum_displacement.change_fraction=" OR
   NOT refinement_stdout MATCHES "observable.cantilever.section_von_mises.change_fraction=" OR
   NOT refinement_stdout MATCHES "global.maximum_von_mises_stress.participated_in_acceptance=false" OR
   NOT refinement_stdout MATCHES "archive_schema_version=4.0.0")
  message(FATAL_ERROR
    "structural refinement failed (${refinement_result})\n${refinement_stdout}\n${refinement_stderr}")
endif()

set(manifest
  "${OUTPUT_ROOT}/benchmark/prometheus-structural-run.json")
if(NOT EXISTS "${manifest}")
  message(FATAL_ERROR "structural benchmark did not write its archive")
endif()
execute_process(
  COMMAND "${REPLAY}" "${manifest}"
  RESULT_VARIABLE replay_result
  OUTPUT_VARIABLE replay_stdout
  ERROR_VARIABLE replay_stderr
)
if(NOT replay_result EQUAL 0 OR
   NOT replay_stdout MATCHES "status=verified")
  message(FATAL_ERROR
    "structural archive replay failed (${replay_result})\n${replay_stdout}\n${replay_stderr}")
endif()
