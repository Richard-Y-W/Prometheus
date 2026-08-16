cmake_minimum_required(VERSION 3.25)

foreach(input_name IN ITEMS EXPECTED_JSON ACTUAL_JSON)
  if(NOT DEFINED ${input_name} OR "${${input_name}}" STREQUAL "")
    message(FATAL_ERROR "${input_name} is required")
  endif()
  if(NOT EXISTS "${${input_name}}" OR IS_DIRECTORY "${${input_name}}")
    message(FATAL_ERROR "${input_name} is not a readable JSON file: ${${input_name}}")
  endif()
endforeach()

file(READ "${EXPECTED_JSON}" expected_json)
file(READ "${ACTUAL_JSON}" actual_json)

function(require_json_value output json_text document_name field expected_type)
  string(JSON value_type ERROR_VARIABLE type_error TYPE "${json_text}" "${field}")
  if(NOT type_error STREQUAL "NOTFOUND")
    message(FATAL_ERROR "${document_name}.${field} is missing or malformed: ${type_error}")
  endif()
  if(NOT value_type STREQUAL expected_type)
    message(FATAL_ERROR
      "${document_name}.${field} has type ${value_type}; expected ${expected_type}")
  endif()
  string(JSON value ERROR_VARIABLE value_error GET "${json_text}" "${field}")
  if(NOT value_error STREQUAL "NOTFOUND")
    message(FATAL_ERROR "${document_name}.${field} cannot be read: ${value_error}")
  endif()
  set(${output} "${value}" PARENT_SCOPE)
endfunction()

foreach(document IN ITEMS expected actual)
  string(JSON root_type ERROR_VARIABLE root_error TYPE "${${document}_json}")
  if(NOT root_error STREQUAL "NOTFOUND" OR NOT root_type STREQUAL "OBJECT")
    message(FATAL_ERROR "${document} summary must be one valid JSON object")
  endif()

  require_json_value(ok_value "${${document}_json}" "${document}" ok BOOLEAN)
  if(NOT ok_value)
    message(FATAL_ERROR "${document}.ok must be true; got ${ok_value}")
  endif()
  require_json_value(error_value "${${document}_json}" "${document}" error STRING)
  if(NOT error_value STREQUAL "")
    message(FATAL_ERROR "${document}.error must be empty; got '${error_value}'")
  endif()

  require_json_value(inventory_value "${${document}_json}" "${document}"
    inventory_sha256 STRING)
  string(LENGTH "${inventory_value}" inventory_length)
  if(NOT inventory_length EQUAL 71 OR
     NOT inventory_value MATCHES "^sha256:[0-9a-f]+$")
    message(FATAL_ERROR
      "${document}.inventory_sha256 must be a strict lowercase SHA-256; got '${inventory_value}'")
  endif()

  foreach(field IN ITEMS total_files ready_files not_evaluated_files
                         unsupported_files unreadable_files duplicate_copies)
    require_json_value(count_value "${${document}_json}" "${document}"
      "${field}" NUMBER)
    if(NOT count_value MATCHES "^[0-9]+$")
      message(FATAL_ERROR
        "${document}.${field} must be a nonnegative integer; got '${count_value}'")
    endif()
    set(${document}_${field} "${count_value}")
  endforeach()
  require_json_value(primary_value "${${document}_json}" "${document}"
    primary_step_path STRING)

endforeach()

# Compare the asserted fields after validation. Root path and elapsed time are
# intentionally absent: they are observational metadata, not scan identity.
foreach(field IN ITEMS inventory_sha256 total_files ready_files
                       not_evaluated_files unsupported_files unreadable_files
                       duplicate_copies primary_step_path)
  string(JSON expected_value GET "${expected_json}" "${field}")
  string(JSON actual_value GET "${actual_json}" "${field}")
  if(NOT expected_value STREQUAL actual_value)
    message(FATAL_ERROR
      "Project intake field '${field}' mismatch: expected '${expected_value}', actual '${actual_value}'")
  endif()
endforeach()

foreach(document IN ITEMS expected actual)
  math(EXPR classified_total
    "${${document}_ready_files} + ${${document}_not_evaluated_files} + ${${document}_unsupported_files} + ${${document}_unreadable_files}")
  if(NOT classified_total EQUAL ${${document}_total_files})
    message(FATAL_ERROR
      "${document}.total_files is '${${document}_total_files}', but classification counts sum to '${classified_total}'")
  endif()
endforeach()

message(STATUS "Project intake summary matches ${EXPECTED_JSON}")
