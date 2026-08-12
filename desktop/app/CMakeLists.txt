qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)
add_library(prometheus_desktop_support STATIC cad_controller.cpp cad_controller.hpp service_controller.cpp service_controller.hpp review_payload.cpp review_payload.hpp engineering_controller.cpp engineering_controller.hpp)
target_include_directories(prometheus_desktop_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(prometheus_desktop_support PUBLIC Qt6::Quick3D Qt6::Concurrent Qt6::Network prometheus_core)
qt_add_executable(prometheus_desktop main.cpp)
set_source_files_properties(../ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
qt_add_qml_module(prometheus_desktop URI Prometheus VERSION 1.0 NO_CACHEGEN QML_FILES ../ui/Main.qml)
target_link_libraries(prometheus_desktop PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Quick3D Qt6::Concurrent prometheus_core prometheus_desktop_support)
if(TARGET prometheus_cad)
  target_link_libraries(prometheus_desktop_support PUBLIC prometheus_cad)
  target_compile_definitions(prometheus_desktop_support PRIVATE PROMETHEUS_HAS_OCCT=1)
endif()
set_target_properties(prometheus_desktop PROPERTIES WIN32_EXECUTABLE TRUE)
if(BUILD_TESTING)
  add_executable(prometheus_review_payload_tests tests/review_payload_tests.cpp review_payload.cpp review_payload.hpp)
  target_include_directories(prometheus_review_payload_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(prometheus_review_payload_tests PRIVATE Qt6::Core)
  add_test(NAME prometheus_review_payload_tests COMMAND prometheus_review_payload_tests)
endif()
if(BUILD_TESTING AND NOT TARGET prometheus_cad)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
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
