set(SOURCES main.cc secondary_config.cc secondary.cc)
set(HEADERS secondary_config.h secondary.h)

add_executable(aktualizr ${SOURCES})
target_link_libraries(aktualizr aktualizr_lib virtual_secondary aktualizr-posix)
target_include_directories(aktualizr PUBLIC ${PROJECT_SOURCE_DIR}/third_party)

install(TARGETS aktualizr RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT aktualizr)

add_aktualizr_test(NAME primary_secondary_registration
                   SOURCES primary_secondary_registration_test.cc secondary.cc secondary_config.cc
                   PROJECT_WORKING_DIRECTORY LIBRARIES PUBLIC aktualizr-posix virtual_secondary uptane_generator_lib)
target_include_directories(t_primary_secondary_registration PUBLIC ${PROJECT_SOURCE_DIR}/src/libaktualizr-posix)

# Check the --help option works.
add_test(NAME aktualizr-option-help
         COMMAND aktualizr --help)

# Report version.
add_test(NAME aktualizr-option-version
         COMMAND aktualizr --version)
set_tests_properties(aktualizr-option-version PROPERTIES PASS_REGULAR_EXPRESSION "Current aktualizr version is: ${AKTUALIZR_VERSION}")

aktualizr_source_file_checks(${SOURCES} ${HEADERS} ${TEST_SOURCES})

# vim: set tabstop=4 shiftwidth=4 expandtab:
