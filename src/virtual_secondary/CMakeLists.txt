set(SOURCES managedsecondary.cc virtualsecondary.cc)

set(HEADERS managedsecondary.h virtualsecondary.h)

set(TARGET virtual_secondary)

add_library(virtual_secondary STATIC ${SOURCES})

target_include_directories(${TARGET} PUBLIC ${PROJECT_SOURCE_DIR}/src/virtual_secondary)

add_aktualizr_test(NAME virtual_secondary
                   SOURCES virtual_secondary_test.cc
                   PROJECT_WORKING_DIRECTORY 
                   LIBRARIES uptane_generator_lib virtual_secondary)

add_aktualizr_test(NAME bad_rotation
                   SOURCES bad_rotation_test.cc
                   PROJECT_WORKING_DIRECTORY
                   LIBRARIES uptane_generator_lib virtual_secondary)


aktualizr_source_file_checks(${HEADERS} ${SOURCES} ${TEST_SOURCES} ${FUZZ_SECONDARY_SOURCES})
