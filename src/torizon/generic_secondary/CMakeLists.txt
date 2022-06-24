set(SOURCES torizongenericsecondary.cc)

set(HEADERS torizongenericsecondary.h)

set(TARGET torizon_generic_secondary)

add_library(${TARGET} STATIC
  ${SOURCES}
)

target_include_directories(${TARGET} PUBLIC ${PROJECT_SOURCE_DIR}/src/virtual_secondary ${CMAKE_CURRENT_SOURCE_DIR})

add_aktualizr_test(
  NAME torizon_generic_secondary
  SOURCES generic_secondary_test.cc
  PROJECT_WORKING_DIRECTORY
  LIBRARIES torizon_generic_secondary virtual_secondary
)

aktualizr_source_file_checks(${HEADERS} ${SOURCES} ${TEST_SOURCES})
