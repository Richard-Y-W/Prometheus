qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)
add_library(prometheus_desktop_support STATIC
  cad_controller.cpp cad_controller.hpp
  project_controller.cpp project_controller.hpp
  project_intake.cpp project_intake.hpp
  execution_controller.cpp execution_controller.hpp
  execution_component_variant.cpp execution_component_variant.hpp
  component_binding_controller.cpp component_binding_controller.hpp
  service_controller.cpp service_controller.hpp
  exact_package_download.cpp exact_package_download.hpp
  review_payload.cpp review_payload.hpp
  engineering_controller.cpp engineering_controller.hpp
  structural_backend.cpp structural_backend.hpp
  structural_controller.cpp structural_controller.hpp)
target_include_directories(prometheus_desktop_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include)
target_link_libraries(prometheus_desktop_support PUBLIC Qt6::Quick3D Qt6::Concurrent Qt6::Network prometheus_core prometheus_execution prometheus_integrity prometheus_replay_support prometheus_run_store prometheus_structural)
qt_add_executable(prometheus_desktop main.cpp)
set_source_files_properties(../ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(../ui/ComponentPackagePanel.qml PROPERTIES QT_RESOURCE_ALIAS ComponentPackagePanel.qml)
set_source_files_properties(../ui/MotorScenarioDialog.qml PROPERTIES QT_RESOURCE_ALIAS MotorScenarioDialog.qml)
set_source_files_properties(../ui/MotorRunPanel.qml PROPERTIES QT_RESOURCE_ALIAS MotorRunPanel.qml)
set_source_files_properties(../ui/RunHistoryPanel.qml PROPERTIES QT_RESOURCE_ALIAS RunHistoryPanel.qml)
set_source_files_properties(../ui/ProjectInventoryPanel.qml PROPERTIES QT_RESOURCE_ALIAS ProjectInventoryPanel.qml)
set_source_files_properties(../ui/StructuralSetupPanel.qml PROPERTIES QT_RESOURCE_ALIAS StructuralSetupPanel.qml)
qt_add_qml_module(prometheus_desktop URI Prometheus VERSION 1.0 NO_CACHEGEN QML_FILES
  ../ui/Main.qml
  ../ui/ComponentPackagePanel.qml
  ../ui/MotorScenarioDialog.qml
  ../ui/MotorRunPanel.qml
  ../ui/RunHistoryPanel.qml
  ../ui/ProjectInventoryPanel.qml
  ../ui/StructuralSetupPanel.qml
)
target_link_libraries(prometheus_desktop PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Quick3D Qt6::Concurrent prometheus_core prometheus_desktop_support)
if(TARGET prometheus_cad)
  target_link_libraries(prometheus_desktop_support PUBLIC prometheus_cad)
  target_compile_definitions(prometheus_desktop_support PRIVATE PROMETHEUS_HAS_OCCT=1)
endif()
set_target_properties(prometheus_desktop PROPERTIES WIN32_EXECUTABLE TRUE)
if(BUILD_TESTING)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
  add_executable(prometheus_project_intake_tests
    tests/project_intake_tests.cpp
    project_intake.cpp
    project_intake.hpp
  )
  target_include_directories(prometheus_project_intake_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_link_libraries(prometheus_project_intake_tests PRIVATE
    Qt6::Concurrent
    Qt6::Test
    prometheus_integrity
    prometheus_run_store
  )
  add_test(
    NAME prometheus_project_intake
    COMMAND prometheus_project_intake_tests
  )
  add_test(
    NAME prometheus_project_intake_summary_good
    COMMAND ${CMAKE_COMMAND}
      -DEXPECTED_JSON=${PROJECT_SOURCE_DIR}/fixtures/trials/project-intake-summary/expected.json
      -DACTUAL_JSON=${PROJECT_SOURCE_DIR}/fixtures/trials/project-intake-summary/actual-good.json
      -P ${PROJECT_SOURCE_DIR}/cmake/AssertProjectIntakeSummary.cmake
  )
  add_test(
    NAME prometheus_project_intake_summary_bad
    COMMAND ${CMAKE_COMMAND}
      -DEXPECTED_JSON=${PROJECT_SOURCE_DIR}/fixtures/trials/project-intake-summary/expected.json
      -DACTUAL_JSON=${PROJECT_SOURCE_DIR}/fixtures/trials/project-intake-summary/actual-bad.json
      -P ${PROJECT_SOURCE_DIR}/cmake/AssertProjectIntakeSummary.cmake
  )
  set_tests_properties(prometheus_project_intake_summary_bad PROPERTIES
    WILL_FAIL TRUE
  )
  add_test(
    NAME prometheus_jpl_rover_prepare_fixture
    COMMAND ${CMAKE_COMMAND}
      -DCASE=correct
      -DREPOSITORY_ROOT=${PROJECT_SOURCE_DIR}
      -DTEST_BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/jpl-rover-fixture-correct
      -P ${PROJECT_SOURCE_DIR}/cmake/tests/JplRoverTrialFixture.cmake
  )
  add_test(
    NAME prometheus_jpl_rover_failed_promotion_fixture
    COMMAND ${CMAKE_COMMAND}
      -DCASE=wrong_license
      -DREPOSITORY_ROOT=${PROJECT_SOURCE_DIR}
      -DTEST_BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/jpl-rover-fixture-wrong-license
      -P ${PROJECT_SOURCE_DIR}/cmake/tests/JplRoverTrialFixture.cmake
  )
  set_tests_properties(
    prometheus_jpl_rover_prepare_fixture
    prometheus_jpl_rover_failed_promotion_fixture
    PROPERTIES TIMEOUT 60
  )
  if(WIN32)
    add_test(
      NAME prometheus_outside_user_bundle_fixture
      COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${PROJECT_SOURCE_DIR}/scripts/tests/outside-user-bundle-fixture.ps1
    )
  endif()

  add_executable(prometheus_engineering_controller_tests
    tests/engineering_controller_tests.cpp
  )
  target_include_directories(prometheus_engineering_controller_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(prometheus_engineering_controller_tests PRIVATE
    Qt6::Core
    prometheus_desktop_support
  )
  add_test(
    NAME prometheus_engineering_controller
    COMMAND prometheus_engineering_controller_tests
  )

  add_executable(prometheus_structural_controller_tests
    tests/structural_controller_tests.cpp
  )
  target_link_libraries(prometheus_structural_controller_tests PRIVATE
    Qt6::Core
    prometheus_desktop_support
  )
  target_compile_definitions(prometheus_structural_controller_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    PROMETHEUS_SOLVER_FIXTURE_PATH="$<TARGET_FILE:prometheus_structural_solver_fixture>"
  )
  add_dependencies(prometheus_structural_controller_tests
    prometheus_structural_solver_fixture
  )
  add_test(
    NAME prometheus_structural_controller
    COMMAND prometheus_structural_controller_tests
  )
  set_tests_properties(prometheus_structural_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )

  add_executable(prometheus_review_payload_tests tests/review_payload_tests.cpp review_payload.cpp review_payload.hpp)
  target_include_directories(prometheus_review_payload_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(prometheus_review_payload_tests PRIVATE Qt6::Core)
  add_test(NAME prometheus_review_payload_tests COMMAND prometheus_review_payload_tests)

  add_executable(prometheus_exact_package_download_tests
    tests/exact_package_download_tests.cpp
    exact_package_download.cpp
    exact_package_download.hpp
    review_payload.cpp
    review_payload.hpp
    service_controller.cpp
    service_controller.hpp
  )
  target_include_directories(prometheus_exact_package_download_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_definitions(prometheus_exact_package_download_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(prometheus_exact_package_download_tests PRIVATE
    Qt6::Network
    Qt6::Test
    prometheus_integrity
  )
  add_test(
    NAME prometheus_exact_package_download
    COMMAND prometheus_exact_package_download_tests
  )

  add_executable(prometheus_component_binding_controller_tests
    tests/component_binding_controller_tests.cpp
  )
  target_compile_definitions(prometheus_component_binding_controller_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(prometheus_component_binding_controller_tests PRIVATE
    Qt6::Test
    prometheus_desktop_support
  )
  add_test(
    NAME prometheus_component_binding_controller
    COMMAND prometheus_component_binding_controller_tests
  )

  add_executable(prometheus_project_controller_tests
    tests/project_controller_tests.cpp
  )
  target_link_libraries(prometheus_project_controller_tests PRIVATE
    Qt6::Gui
    prometheus_desktop_support
    prometheus_run_store
  )
  add_test(
    NAME prometheus_project_controller
    COMMAND prometheus_project_controller_tests
  )
  set_tests_properties(prometheus_project_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )

  add_executable(prometheus_execution_controller_tests
    tests/execution_controller_tests.cpp
  )
  target_compile_definitions(prometheus_execution_controller_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    PROMETHEUS_RUN_STORE_CONTENTION_HELPER="$<TARGET_FILE:prometheus_run_store_contention_helper>"
  )
  target_link_libraries(prometheus_execution_controller_tests PRIVATE
    Qt6::Gui
    prometheus_desktop_support
  )
  add_test(
    NAME prometheus_execution_controller
    COMMAND prometheus_execution_controller_tests
  )
  add_dependencies(prometheus_execution_controller_tests
    prometheus_run_store_contention_helper
  )
  set_tests_properties(prometheus_execution_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )

  add_executable(prometheus_qml_authority_tests
    tests/qml_authority_tests.cpp
  )
  target_compile_definitions(prometheus_qml_authority_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    PROMETHEUS_UI_DIR="${PROJECT_SOURCE_DIR}/desktop/ui"
  )
  target_link_libraries(prometheus_qml_authority_tests PRIVATE
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Quick3D
    Qt6::Test
    prometheus_desktop_support
  )
  add_test(
    NAME prometheus_qml_authority
    COMMAND prometheus_qml_authority_tests
  )
  set_tests_properties(prometheus_qml_authority PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )
endif()
if(BUILD_TESTING AND NOT TARGET prometheus_cad)
  add_executable(prometheus_cad_controller_no_occt_tests
    tests/cad_controller_no_occt_tests.cpp
  )
  target_link_libraries(prometheus_cad_controller_no_occt_tests PRIVATE
    Qt6::Gui
    Qt6::Test
    prometheus_desktop_support
  )
  add_test(
    NAME prometheus_cad_controller_no_occt_tests
    COMMAND prometheus_cad_controller_no_occt_tests
  )
  set_tests_properties(prometheus_cad_controller_no_occt_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )
endif()
if(BUILD_TESTING AND TARGET prometheus_cad)
  add_executable(prometheus_project_tests tests/project_tests.cpp)
  target_link_libraries(prometheus_project_tests PRIVATE prometheus_desktop_support Qt6::Gui)
  target_compile_definitions(prometheus_project_tests PRIVATE PROMETHEUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME prometheus_project_tests COMMAND prometheus_project_tests)
  set_tests_properties(prometheus_project_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
