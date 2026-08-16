if(NOT DEFINED EXPORTER OR NOT DEFINED VERIFIER OR
   NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "structural case tool fixture arguments are incomplete")
endif()

file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(
  COMMAND "${EXPORTER}" --tension-bar
    "${FIXTURE}/expectations.json"
    "${FIXTURE}/package-smoke/source-mesh.inp"
    "${OUTPUT}" coarse known_pass
  RESULT_VARIABLE export_result
  OUTPUT_VARIABLE export_output
  ERROR_VARIABLE export_error
)
if(NOT export_result EQUAL 0)
  message(FATAL_ERROR
    "structural package export failed: ${export_output}${export_error}")
endif()

file(READ "${OUTPUT}/prometheus-structural-execution.json" execution_json)
string(JSON result_profile GET "${execution_json}" result_profile)
if(NOT result_profile STREQUAL "analytic_tension_bar_v1")
  message(FATAL_ERROR "tension-bar package omitted its result profile")
endif()

file(READ "${FIXTURE}/package-smoke/source-mesh.inp" source_mesh)
string(REPLACE
  "6, 1.0, 0.0, 0.1"
  "6, 1.1, 0.0, 0.1"
  wrong_geometry_mesh "${source_mesh}")
file(WRITE "${OUTPUT}/wrong-geometry.inp" "${wrong_geometry_mesh}")
execute_process(
  COMMAND "${EXPORTER}" --tension-bar
    "${FIXTURE}/expectations.json"
    "${OUTPUT}/wrong-geometry.inp"
    "${OUTPUT}/wrong-geometry-package" coarse known_pass
  RESULT_VARIABLE wrong_geometry_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(wrong_geometry_result EQUAL 0)
  message(FATAL_ERROR
    "a mesh outside the predeclared benchmark geometry was accepted")
endif()
file(READ "${FIXTURE}/expectations.json" expectations_json)
string(REPLACE
  "\"target_size_m\": 0.1"
  "\"target_size_m\": 0.01"
  unordered_expectations "${expectations_json}")
file(WRITE "${OUTPUT}/unordered-expectations.json"
  "${unordered_expectations}")
file(COPY_FILE
  "${FIXTURE}/tension-bar.geo"
  "${OUTPUT}/tension-bar.geo"
)
execute_process(
  COMMAND "${EXPORTER}" --tension-bar
    "${OUTPUT}/unordered-expectations.json"
    "${FIXTURE}/package-smoke/source-mesh.inp"
    "${OUTPUT}/unordered-package" coarse known_pass
  RESULT_VARIABLE unordered_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(unordered_result EQUAL 0)
  message(FATAL_ERROR "unordered benchmark mesh targets were accepted")
endif()

file(COPY_FILE
  "${FIXTURE}/package-smoke/prometheus_structural_case.sta"
  "${OUTPUT}/prometheus_structural_case.sta"
)
file(COPY_FILE
  "${FIXTURE}/package-smoke/prometheus_structural_case.dat"
  "${OUTPUT}/prometheus_structural_case.dat"
)
file(COPY_FILE
  "${FIXTURE}/package-smoke/prometheus_structural_case.frd"
  "${OUTPUT}/prometheus_structural_case.frd"
)
execute_process(
  COMMAND "${VERIFIER}" "${OUTPUT}" 0
    sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    "CalculiX Version 2.23 synthetic package seam fixture"
    "${FIXTURE}/package-smoke/prometheus_structural_case.stdout.txt"
    "${FIXTURE}/package-smoke/prometheus_structural_case.stderr.txt"
    "${FIXTURE}/package-smoke/refinement.json"
    1.0 "${OUTPUT}/result.json"
  RESULT_VARIABLE verify_result
  OUTPUT_VARIABLE verify_output
  ERROR_VARIABLE verify_error
)
if(NOT verify_result EQUAL 0)
  message(FATAL_ERROR
    "structural package verification failed: ${verify_output}${verify_error}")
endif()

file(READ "${OUTPUT}/result.json" result_json)
string(JSON complete GET "${result_json}" complete)
string(JSON status GET "${result_json}" status)
string(JSON displacement_status GET "${result_json}" findings 0 status)
string(JSON stress_status GET "${result_json}" findings 1 status)
string(JSON loaded_count GET "${result_json}"
  benchmark_metrics loaded_face_node_count)
string(JSON central_count GET "${result_json}"
  benchmark_metrics central_band_element_count)
string(JSON frd_sha256 GET "${result_json}" raw_artifacts frd_sha256)
if(NOT complete OR NOT status STREQUAL "complete" OR
   NOT displacement_status STREQUAL "pass" OR
   NOT stress_status STREQUAL "indeterminate" OR
   NOT loaded_count EQUAL 4 OR NOT central_count EQUAL 2 OR
   NOT frd_sha256 MATCHES "^sha256:[0-9a-f]+$")
  message(FATAL_ERROR "structural package result has unexpected semantics")
endif()

set(generic_output "${OUTPUT}/generic")
execute_process(
  COMMAND "${EXPORTER}"
    "${OUTPUT}/reviewed-structural-case.json"
    "${FIXTURE}/package-smoke/source-mesh.inp"
    "${generic_output}"
  RESULT_VARIABLE generic_export_result
  OUTPUT_VARIABLE generic_export_output
  ERROR_VARIABLE generic_export_error
)
if(NOT generic_export_result EQUAL 0)
  message(FATAL_ERROR
    "generic package export failed: ${generic_export_output}${generic_export_error}")
endif()
file(READ
  "${generic_output}/prometheus-structural-execution.json"
  generic_execution_json)
string(JSON generic_profile GET "${generic_execution_json}" result_profile)
if(NOT generic_profile STREQUAL "structural_findings_v1")
  message(FATAL_ERROR "generic package has the wrong result profile")
endif()
file(COPY_FILE
  "${FIXTURE}/package-smoke/prometheus_structural_case.sta"
  "${generic_output}/prometheus_structural_case.sta"
)
file(COPY_FILE
  "${FIXTURE}/package-smoke/prometheus_structural_case.dat"
  "${generic_output}/prometheus_structural_case.dat"
)
file(COPY_FILE
  "${FIXTURE}/package-smoke/prometheus_structural_case.frd"
  "${generic_output}/prometheus_structural_case.frd"
)
execute_process(
  COMMAND "${VERIFIER}" "${generic_output}" 0
    sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    "CalculiX Version 2.23 synthetic package seam fixture"
    "${FIXTURE}/package-smoke/prometheus_structural_case.stdout.txt"
    "${FIXTURE}/package-smoke/prometheus_structural_case.stderr.txt"
    "${FIXTURE}/package-smoke/refinement.json"
    1.0 "${generic_output}/result.json"
  RESULT_VARIABLE generic_verify_result
  OUTPUT_VARIABLE generic_verify_output
  ERROR_VARIABLE generic_verify_error
)
if(NOT generic_verify_result EQUAL 0)
  message(FATAL_ERROR
    "generic package verification failed: ${generic_verify_output}${generic_verify_error}")
endif()
file(READ "${generic_output}/result.json" generic_result_json)
string(JSON generic_complete GET "${generic_result_json}" complete)
string(JSON generic_benchmark_type TYPE
  "${generic_result_json}" benchmark_metrics)
if(NOT generic_complete OR NOT generic_benchmark_type STREQUAL "NULL")
  message(FATAL_ERROR
    "generic structural verification invoked benchmark-specific math")
endif()

file(APPEND "${OUTPUT}/source-mesh.inp" "\n")
execute_process(
  COMMAND "${VERIFIER}" "${OUTPUT}" 0
    sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    "CalculiX Version 2.23 synthetic package seam fixture"
    "${FIXTURE}/package-smoke/prometheus_structural_case.stdout.txt"
    "${FIXTURE}/package-smoke/prometheus_structural_case.stderr.txt"
    "${FIXTURE}/package-smoke/refinement.json"
    1.0 "${OUTPUT}/tampered-result.json"
  RESULT_VARIABLE tampered_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(NOT tampered_result EQUAL 9)
  message(FATAL_ERROR "modified package mesh did not fail closed")
endif()
