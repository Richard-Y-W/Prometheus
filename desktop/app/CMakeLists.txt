qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)
add_library(prometheus_desktop_support STATIC cad_controller.cpp cad_controller.hpp project_controller.cpp project_controller.hpp project_intake.cpp project_intake.hpp execution_controller.cpp execution_controller.hpp service_controller.cpp service_controller.hpp exact_package_download.cpp exact_package_download.hpp review_payload.cpp review_payload.hpp engineering_controller.cpp engineering_controller.hpp structural_controller.cpp structural_controller.hpp)
target_include_directories(prometheus_desktop_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
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
  )
  target_link_libraries(prometheus_project_intake_tests PRIVATE
    Qt6::Concurrent
    Qt6::Test
  )
  add_test(
    NAME prometheus_project_intake
    COMMAND prometheus_project_intake_tests
  )

  add_executable(prometheus_engineering_controller_tests
    tests/engineering_controller_tests.cpp
    engineering_controller.cpp
    engineering_controller.hpp
  )
  target_include_directories(prometheus_engineering_controller_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(prometheus_engineering_controller_tests PRIVATE
    Qt6::Core
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
