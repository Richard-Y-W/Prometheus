add_library(prometheus_integrity STATIC
  src/canonical_json.cpp
  src/ecmascript_number.cpp
  ${PROJECT_SOURCE_DIR}/third_party/ryu/ryu/d2s.c
)

target_compile_features(prometheus_integrity PUBLIC cxx_std_20)
target_include_directories(prometheus_integrity
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
    ${PROJECT_SOURCE_DIR}/third_party/picosha2
    ${PROJECT_SOURCE_DIR}/third_party/ryu
)

if(BUILD_TESTING)
  add_executable(prometheus_integrity_tests
    tests/canonical_json_tests.cpp
  )
  target_compile_features(prometheus_integrity_tests PRIVATE cxx_std_20)
  target_include_directories(prometheus_integrity_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json/include
  )
  target_compile_definitions(prometheus_integrity_tests PRIVATE
    PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    PROMETHEUS_INTEGRITY_LINK_LIBRARIES="$<JOIN:$<TARGET_PROPERTY:prometheus_integrity,LINK_LIBRARIES>,|>"
  )
  target_link_libraries(prometheus_integrity_tests PRIVATE prometheus_integrity)
  add_test(NAME prometheus_integrity COMMAND prometheus_integrity_tests)
endif()
