set(SOURCES command_runner.cc compose_manager.cc
    dockercomposesecondary.cc dockerofflineloader.cc dockertarballloader.cc)

set(HEADERS command_runner.h compose_manager.h
    dockercomposesecondary.h dockerofflineloader.h dockertarballloader.h)

set(TARGET torizon_dockercompose_secondary)

add_library(${TARGET} STATIC
  ${SOURCES}
)

target_include_directories(
  ${TARGET} PUBLIC
  ${PROJECT_SOURCE_DIR}/src/virtual_secondary
  ${PROJECT_SOURCE_DIR}/third_party/jsoncpp/include
  ${CMAKE_CURRENT_SOURCE_DIR})

add_aktualizr_test(
  NAME torizon_dockercompose_secondary
  SOURCES dockercompose_secondary_test.cc
  PROJECT_WORKING_DIRECTORY
  LIBRARIES torizon_dockercompose_secondary virtual_secondary
)

aktualizr_source_file_checks(${HEADERS} ${SOURCES} ${TEST_SOURCES})

add_subdirectory("tests")
