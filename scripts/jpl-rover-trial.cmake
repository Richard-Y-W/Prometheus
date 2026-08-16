cmake_minimum_required(VERSION 3.25)

get_filename_component(script_directory "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(repository_root "${script_directory}/.." ABSOLUTE)

set(production_revision "0c4a0d97ba09d028a9ca380ae8e6729ac4b8bef7")
set(production_license_sha256
  "74227c34e68957a55d4d16091aeca5bcd240ec15883e5dee71f4b25139064413")
set(production_source_repository
  "https://github.com/nasa-jpl/open-source-rover.git")
set(license_relative_path "LICENSE.txt")

if(NOT DEFINED PROMETHEUS_JPL_MODE)
  set(PROMETHEUS_JPL_MODE verify)
endif()
if(NOT PROMETHEUS_JPL_MODE STREQUAL "prepare" AND
   NOT PROMETHEUS_JPL_MODE STREQUAL "verify")
  message(FATAL_ERROR
    "PROMETHEUS_JPL_MODE must be 'prepare' or 'verify', not '${PROMETHEUS_JPL_MODE}'")
endif()

if(PROMETHEUS_JPL_TESTING)
  foreach(required IN ITEMS PROMETHEUS_JPL_TEST_SOURCE_REPOSITORY
                            PROMETHEUS_JPL_TEST_REVISION
                            PROMETHEUS_JPL_TEST_LICENSE_SHA256
                            PROMETHEUS_JPL_TEST_EXTERNAL_ROOT
                            PROMETHEUS_JPL_TEST_TRIAL_ROOT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
      message(FATAL_ERROR "${required} is required in test mode")
    endif()
  endforeach()
  if(NOT PROMETHEUS_JPL_MODE STREQUAL "prepare")
    message(FATAL_ERROR "Test overrides are supported only in prepare mode")
  endif()
  set(source_repository "${PROMETHEUS_JPL_TEST_SOURCE_REPOSITORY}")
  set(source_revision "${PROMETHEUS_JPL_TEST_REVISION}")
  set(expected_license_sha256 "${PROMETHEUS_JPL_TEST_LICENSE_SHA256}")
  set(external_root "${PROMETHEUS_JPL_TEST_EXTERNAL_ROOT}")
  set(trials_root "${PROMETHEUS_JPL_TEST_TRIAL_ROOT}")
else()
  foreach(test_override IN ITEMS PROMETHEUS_JPL_TEST_SOURCE_REPOSITORY
                                 PROMETHEUS_JPL_TEST_REVISION
                                 PROMETHEUS_JPL_TEST_LICENSE_SHA256
                                 PROMETHEUS_JPL_TEST_EXTERNAL_ROOT
                                 PROMETHEUS_JPL_TEST_TRIAL_ROOT)
    if(DEFINED ${test_override})
      message(FATAL_ERROR
        "${test_override} requires explicit PROMETHEUS_JPL_TESTING=ON")
    endif()
  endforeach()
  set(source_repository "${production_source_repository}")
  set(source_revision "${production_revision}")
  set(expected_license_sha256 "${production_license_sha256}")
  set(external_root "${repository_root}/out/external-demo")
  set(trials_root "${repository_root}/out/trials")
endif()

string(LENGTH "${source_revision}" revision_length)
string(LENGTH "${expected_license_sha256}" license_hash_length)
if(NOT revision_length EQUAL 40 OR NOT source_revision MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "The pinned revision must be 40 lowercase hexadecimal characters")
endif()
if(NOT license_hash_length EQUAL 64 OR
   NOT expected_license_sha256 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR
    "The expected license SHA-256 must be 64 lowercase hexadecimal characters")
endif()

string(SUBSTRING "${source_revision}" 0 7 revision_short)
set(cache_repository "${external_root}/open-source-rover")
set(archive_path "${external_root}/jpl-open-source-rover-${revision_short}.tar")
set(archive_staging "${archive_path}.staging")
set(trial_path "${trials_root}/jpl-open-source-rover-${revision_short}")
set(sidecar_path "${trial_path}.source.json")
set(trial_staging "${trial_path}.staging")
set(sidecar_staging "${sidecar_path}.staging")
set(trial_backup "${trial_path}.backup")
set(sidecar_backup "${sidecar_path}.backup")

function(normalized_absolute output path)
  get_filename_component(absolute "${path}" ABSOLUTE)
  cmake_path(NORMAL_PATH absolute OUTPUT_VARIABLE normalized)
  set(${output} "${normalized}" PARENT_SCOPE)
endfunction()

function(assert_safe_child target parent)
  normalized_absolute(target_absolute "${target}")
  normalized_absolute(parent_absolute "${parent}")
  set(parent_for_prefix "${parent_absolute}")
  cmake_path(IS_PREFIX parent_for_prefix "${target_absolute}" NORMALIZE is_child)
  if(NOT is_child OR target_absolute STREQUAL parent_absolute)
    message(FATAL_ERROR
      "Refusing cleanup outside the exact bounded root '${parent_absolute}': '${target_absolute}'")
  endif()
  if(EXISTS "${target_absolute}" AND IS_SYMLINK "${target_absolute}")
    message(FATAL_ERROR "Refusing recursive cleanup of symbolic link '${target_absolute}'")
  endif()
endfunction()

function(safe_remove target parent)
  assert_safe_child("${target}" "${parent}")
  if(EXISTS "${target}" OR IS_SYMLINK "${target}")
    file(REMOVE_RECURSE "${target}")
  endif()
endfunction()

function(compute_tree_identity root output_digest output_count)
  if(NOT IS_DIRECTORY "${root}" OR IS_SYMLINK "${root}")
    message(FATAL_ERROR "Cannot hash non-regular trial directory '${root}'")
  endif()
  file(GLOB_RECURSE entries
    LIST_DIRECTORIES FALSE
    RELATIVE "${root}"
    "${root}/*"
    "${root}/.*")
  list(REMOVE_DUPLICATES entries)
  list(SORT entries)
  set(manifest_bytes "")
  set(count 0)
  foreach(relative_path IN LISTS entries)
    set(absolute_path "${root}/${relative_path}")
    if(IS_SYMLINK "${absolute_path}")
      message(FATAL_ERROR
        "Pinned trial archives may not contain symbolic links: '${relative_path}'")
    endif()
    if(NOT EXISTS "${absolute_path}" OR IS_DIRECTORY "${absolute_path}")
      message(FATAL_ERROR "Trial entry is not a regular file: '${relative_path}'")
    endif()
    file(SIZE "${absolute_path}" byte_size)
    file(SHA256 "${absolute_path}" content_sha256)
    string(LENGTH "${relative_path}" path_length)
    string(LENGTH "${byte_size}" size_length)
    string(APPEND manifest_bytes
      "${path_length}:${relative_path}${size_length}:${byte_size}${content_sha256}\n")
    math(EXPR count "${count} + 1")
  endforeach()
  string(SHA256 tree_sha256 "${manifest_bytes}")
  set(${output_digest} "sha256:${tree_sha256}" PARENT_SCOPE)
  set(${output_count} "${count}" PARENT_SCOPE)
endfunction()

function(write_source_sidecar path tree_sha256 file_count archive_sha256)
  file(WRITE "${path}" "{\n")
  file(APPEND "${path}"
    "  \"schema\": \"urn:prometheus:jpl-rover-source:1\",\n"
    "  \"revision\": \"${source_revision}\",\n"
    "  \"license_path\": \"${license_relative_path}\",\n"
    "  \"license_sha256\": \"${expected_license_sha256}\",\n"
    "  \"tree_manifest_sha256\": \"${tree_sha256}\",\n"
    "  \"total_files\": ${file_count},\n"
    "  \"archive_sha256\": \"sha256:${archive_sha256}\"\n"
    "}\n")
endfunction()

function(validate_trial root sidecar output_ok output_reason)
  set(valid FALSE)
  set(reason "")
  if(NOT IS_DIRECTORY "${root}" OR IS_SYMLINK "${root}")
    set(reason "trial directory is missing or is not regular")
  elseif(NOT EXISTS "${sidecar}" OR IS_DIRECTORY "${sidecar}" OR
         IS_SYMLINK "${sidecar}")
    set(reason "source sidecar is missing or is not regular")
  elseif(NOT EXISTS "${root}/${license_relative_path}" OR
         IS_DIRECTORY "${root}/${license_relative_path}" OR
         IS_SYMLINK "${root}/${license_relative_path}")
    set(reason "${license_relative_path} is missing or is not a regular file")
  else()
    file(READ "${sidecar}" sidecar_json)
    string(JSON sidecar_type ERROR_VARIABLE json_error TYPE "${sidecar_json}")
    if(NOT json_error STREQUAL "NOTFOUND" OR
       NOT sidecar_type STREQUAL "OBJECT")
      set(reason "source sidecar is not one valid JSON object")
    else()
      foreach(field IN ITEMS schema revision license_path license_sha256
                             tree_manifest_sha256 total_files archive_sha256)
        string(JSON field_type ERROR_VARIABLE field_error
          TYPE "${sidecar_json}" "${field}")
        if(NOT field_error STREQUAL "NOTFOUND")
          set(reason "source sidecar field '${field}' is missing")
          break()
        endif()
        string(JSON field_value GET "${sidecar_json}" "${field}")
        set(sidecar_${field} "${field_value}")
      endforeach()
      if(reason STREQUAL "")
        if(NOT sidecar_schema STREQUAL "urn:prometheus:jpl-rover-source:1")
          set(reason "source sidecar schema is unsupported")
        elseif(NOT sidecar_revision STREQUAL source_revision)
          set(reason "source sidecar revision does not match the pinned revision")
        elseif(NOT sidecar_license_path STREQUAL license_relative_path)
          set(reason "source sidecar license path does not match")
        elseif(NOT sidecar_license_sha256 STREQUAL expected_license_sha256)
          set(reason "source sidecar license identity does not match")
        else()
          file(SHA256 "${root}/${license_relative_path}" actual_license_sha256)
          if(NOT actual_license_sha256 STREQUAL expected_license_sha256)
            set(reason "LICENSE bytes do not match the pinned SHA-256")
          else()
            compute_tree_identity("${root}" actual_tree_sha256 actual_file_count)
            if(NOT actual_tree_sha256 STREQUAL sidecar_tree_manifest_sha256)
              set(reason "trial tree bytes do not match the source sidecar")
            elseif(NOT actual_file_count STREQUAL sidecar_total_files)
              set(reason "trial file count does not match the source sidecar")
            else()
              set(valid TRUE)
            endif()
          endif()
        endif()
      endif()
    endif()
  endif()
  set(${output_ok} "${valid}" PARENT_SCOPE)
  set(${output_reason} "${reason}" PARENT_SCOPE)
endfunction()

function(run_required label)
  execute_process(
    COMMAND ${ARGN}
    WORKING_DIRECTORY "${repository_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "${label} failed with exit ${result}\nstdout:\n${output}\nstderr:\n${error}")
  endif()
endfunction()

find_program(GIT_EXECUTABLE NAMES git REQUIRED)
file(MAKE_DIRECTORY "${external_root}" "${trials_root}")

# Recover the previous directory names if an earlier process stopped between
# the backup and promotion renames.
if(NOT EXISTS "${trial_path}" AND EXISTS "${trial_backup}")
  file(RENAME "${trial_backup}" "${trial_path}" RESULT recovery_result)
  if(NOT recovery_result STREQUAL "0")
    message(FATAL_ERROR "Could not recover the previous trial: ${recovery_result}")
  endif()
endif()
if(NOT EXISTS "${sidecar_path}" AND EXISTS "${sidecar_backup}")
  file(RENAME "${sidecar_backup}" "${sidecar_path}" RESULT recovery_result)
  if(NOT recovery_result STREQUAL "0")
    message(FATAL_ERROR "Could not recover the previous sidecar: ${recovery_result}")
  endif()
endif()

set(existing_trial FALSE)
if(EXISTS "${trial_path}" OR EXISTS "${sidecar_path}")
  validate_trial("${trial_path}" "${sidecar_path}"
    existing_trial existing_trial_reason)
  if(existing_trial AND NOT PROMETHEUS_JPL_REFRESH)
    message(STATUS "Reusing validated pinned Rover trial: ${trial_path}")
  elseif(NOT existing_trial AND NOT PROMETHEUS_JPL_REFRESH)
    message(FATAL_ERROR
      "Cached Rover trial failed validation (${existing_trial_reason}). "
      "Run again with -DPROMETHEUS_JPL_REFRESH=ON to rebuild it from the pinned Git object.")
  endif()
endif()

if(NOT existing_trial OR PROMETHEUS_JPL_REFRESH)
  if(EXISTS "${cache_repository}" AND
     (NOT IS_DIRECTORY "${cache_repository}" OR IS_SYMLINK "${cache_repository}"))
    message(FATAL_ERROR "Git cache path is not a regular directory: ${cache_repository}")
  endif()
  if(NOT EXISTS "${cache_repository}/.git")
    if(EXISTS "${cache_repository}")
      safe_remove("${cache_repository}" "${external_root}")
    endif()
    file(MAKE_DIRECTORY "${cache_repository}")
    run_required("Git cache initialization"
      "${GIT_EXECUTABLE}" -C "${cache_repository}" init --quiet)
    run_required("Git cache remote configuration"
      "${GIT_EXECUTABLE}" -C "${cache_repository}" remote add origin
      "${source_repository}")
  else()
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${cache_repository}"
        remote get-url origin
      RESULT_VARIABLE remote_result
      OUTPUT_VARIABLE cached_remote
      ERROR_VARIABLE remote_error
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT remote_result EQUAL 0 OR NOT cached_remote STREQUAL source_repository)
      message(FATAL_ERROR
        "Git cache remote is not the pinned Rover source. Expected '${source_repository}', got '${cached_remote}'.")
    endif()
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${cache_repository}"
      cat-file -e "${source_revision}^{commit}"
    RESULT_VARIABLE object_result
    OUTPUT_QUIET ERROR_QUIET
  )
  if(NOT object_result EQUAL 0)
    run_required("Pinned Rover revision fetch"
      "${GIT_EXECUTABLE}" -C "${cache_repository}" fetch --depth 1 origin
      "${source_revision}")
  endif()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${cache_repository}"
      rev-parse "${source_revision}^{commit}"
    RESULT_VARIABLE revision_result
    OUTPUT_VARIABLE resolved_revision
    ERROR_VARIABLE revision_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT revision_result EQUAL 0 OR NOT resolved_revision STREQUAL source_revision)
    message(FATAL_ERROR
      "Pinned Rover object validation failed: ${revision_error}")
  endif()

  safe_remove("${archive_staging}" "${external_root}")
  safe_remove("${trial_staging}" "${trials_root}")
  safe_remove("${sidecar_staging}" "${trials_root}")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${cache_repository}" archive
      --format=tar "--output=${archive_staging}" "${source_revision}"
    RESULT_VARIABLE archive_result
    OUTPUT_VARIABLE archive_output
    ERROR_VARIABLE archive_error
  )
  if(NOT archive_result EQUAL 0)
    safe_remove("${archive_staging}" "${external_root}")
    message(FATAL_ERROR
      "Pinned Rover archive failed with exit ${archive_result}: ${archive_error}")
  endif()
  file(SHA256 "${archive_staging}" archive_sha256)
  file(MAKE_DIRECTORY "${trial_staging}")
  file(ARCHIVE_EXTRACT INPUT "${archive_staging}"
    DESTINATION "${trial_staging}")

  if(NOT EXISTS "${trial_staging}/${license_relative_path}" OR
     IS_DIRECTORY "${trial_staging}/${license_relative_path}" OR
     IS_SYMLINK "${trial_staging}/${license_relative_path}")
    safe_remove("${trial_staging}" "${trials_root}")
    safe_remove("${archive_staging}" "${external_root}")
    message(FATAL_ERROR
      "Pinned Rover archive does not contain a regular ${license_relative_path} file")
  endif()
  file(SHA256 "${trial_staging}/${license_relative_path}" staged_license_sha256)
  if(NOT staged_license_sha256 STREQUAL expected_license_sha256)
    safe_remove("${trial_staging}" "${trials_root}")
    safe_remove("${archive_staging}" "${external_root}")
    message(FATAL_ERROR
      "Pinned Rover LICENSE SHA-256 mismatch: expected '${expected_license_sha256}', actual '${staged_license_sha256}'")
  endif()
  compute_tree_identity("${trial_staging}" staged_tree_sha256 staged_file_count)
  write_source_sidecar("${sidecar_staging}" "${staged_tree_sha256}"
    "${staged_file_count}" "${archive_sha256}")
  validate_trial("${trial_staging}" "${sidecar_staging}"
    staged_trial_valid staged_trial_reason)
  if(NOT staged_trial_valid)
    safe_remove("${trial_staging}" "${trials_root}")
    safe_remove("${sidecar_staging}" "${trials_root}")
    safe_remove("${archive_staging}" "${external_root}")
    message(FATAL_ERROR
      "Staged Rover trial failed validation: ${staged_trial_reason}")
  endif()

  safe_remove("${archive_path}" "${external_root}")
  file(RENAME "${archive_staging}" "${archive_path}" RESULT archive_promote)
  if(NOT archive_promote STREQUAL "0")
    safe_remove("${trial_staging}" "${trials_root}")
    safe_remove("${sidecar_staging}" "${trials_root}")
    message(FATAL_ERROR "Could not promote the pinned Git archive: ${archive_promote}")
  endif()

  safe_remove("${trial_backup}" "${trials_root}")
  safe_remove("${sidecar_backup}" "${trials_root}")
  set(had_trial FALSE)
  set(had_sidecar FALSE)
  if(EXISTS "${trial_path}")
    file(RENAME "${trial_path}" "${trial_backup}" RESULT backup_result)
    if(NOT backup_result STREQUAL "0")
      message(FATAL_ERROR "Could not preserve the previous trial: ${backup_result}")
    endif()
    set(had_trial TRUE)
  endif()
  if(EXISTS "${sidecar_path}")
    file(RENAME "${sidecar_path}" "${sidecar_backup}" RESULT backup_result)
    if(NOT backup_result STREQUAL "0")
      if(had_trial)
        file(RENAME "${trial_backup}" "${trial_path}")
      endif()
      message(FATAL_ERROR "Could not preserve the previous sidecar: ${backup_result}")
    endif()
    set(had_sidecar TRUE)
  endif()

  file(RENAME "${trial_staging}" "${trial_path}" RESULT trial_promote)
  if(NOT trial_promote STREQUAL "0")
    if(had_trial)
      file(RENAME "${trial_backup}" "${trial_path}")
    endif()
    if(had_sidecar)
      file(RENAME "${sidecar_backup}" "${sidecar_path}")
    endif()
    message(FATAL_ERROR "Could not promote the staged Rover trial: ${trial_promote}")
  endif()
  file(RENAME "${sidecar_staging}" "${sidecar_path}" RESULT sidecar_promote)
  if(NOT sidecar_promote STREQUAL "0")
    safe_remove("${trial_path}" "${trials_root}")
    if(had_trial)
      file(RENAME "${trial_backup}" "${trial_path}")
    endif()
    if(had_sidecar)
      file(RENAME "${sidecar_backup}" "${sidecar_path}")
    endif()
    message(FATAL_ERROR "Could not promote the Rover source sidecar: ${sidecar_promote}")
  endif()
  safe_remove("${trial_backup}" "${trials_root}")
  safe_remove("${sidecar_backup}" "${trials_root}")
  message(STATUS
    "Prepared pinned Rover revision ${source_revision} (${staged_file_count} files)")
endif()

validate_trial("${trial_path}" "${sidecar_path}" final_valid final_reason)
if(NOT final_valid)
  message(FATAL_ERROR "Final Rover trial validation failed: ${final_reason}")
endif()

if(PROMETHEUS_JPL_MODE STREQUAL "verify")
  if(DEFINED PROMETHEUS_JPL_PRESET AND
     NOT PROMETHEUS_JPL_PRESET STREQUAL "")
    set(scanner_preset "${PROMETHEUS_JPL_PRESET}")
  elseif(WIN32)
    set(scanner_preset windows-release)
  else()
    set(scanner_preset desktop-no-occt-debug)
  endif()
  if(NOT scanner_preset MATCHES "^[A-Za-z0-9._-]+$")
    message(FATAL_ERROR "Unsafe scanner preset name: '${scanner_preset}'")
  endif()

  run_required("Project intake scanner configuration"
    "${CMAKE_COMMAND}" --preset "${scanner_preset}")
  run_required("Project intake scanner build"
    "${CMAKE_COMMAND}" --build --preset "${scanner_preset}"
    --target prometheus_project_intake_tests)
  set(scanner
    "${repository_root}/out/build/${scanner_preset}/desktop/app/prometheus_project_intake_tests")
  if(WIN32)
    string(APPEND scanner ".exe")
  endif()
  if(NOT EXISTS "${scanner}" OR IS_DIRECTORY "${scanner}")
    message(FATAL_ERROR "Built project intake scanner is missing: ${scanner}")
  endif()
  execute_process(
    COMMAND "${scanner}" --scan-only "${trial_path}"
    RESULT_VARIABLE scan_result
    OUTPUT_VARIABLE scan_json
    ERROR_VARIABLE scan_error
  )
  if(NOT scan_result EQUAL 0)
    message(FATAL_ERROR
      "Production Rover scan failed with exit ${scan_result}: ${scan_error}")
  endif()
  string(JSON scan_type ERROR_VARIABLE scan_json_error TYPE "${scan_json}")
  if(NOT scan_json_error STREQUAL "NOTFOUND" OR
     NOT scan_type STREQUAL "OBJECT")
    message(FATAL_ERROR
      "Production scanner stdout was not exactly one JSON object: ${scan_json_error}")
  endif()
  set(scan_summary "${trial_path}.scan.json")
  file(WRITE "${scan_summary}" "${scan_json}")
  set(expectations
    "${repository_root}/docs/trials/jpl-open-source-rover-expectations.json")
  run_required("Pinned Rover scan assertion"
    "${CMAKE_COMMAND}" "-DEXPECTED_JSON=${expectations}"
    "-DACTUAL_JSON=${scan_summary}"
    -P "${repository_root}/cmake/AssertProjectIntakeSummary.cmake")
  message(STATUS "Pinned Rover scan matches ${expectations}")
endif()

message(STATUS "PROMETHEUS_JPL_TRIAL_PATH=${trial_path}")
