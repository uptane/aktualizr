set(SWUPDATE_POC_SRC main.cc)
set(SWUPDATE_POC_HEADERS swupdate_poc.h)

find_package(swupdate REQUIRED)

add_executable(swupdate-poc ${SWUPDATE_POC_SRC})
target_link_libraries(swupdate-poc PUBLIC aktualizr_lib PRIVATE swupdate)

install(TARGETS swupdate-poc
        COMPONENT aktualizr
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_aktualizr_test(NAME swupdate_poc SOURCES swupdate_poc_test.cc)

aktualizr_source_file_checks(${SWUPDATE_POC_SRC} ${SWUPDATE_POC_HEADERS} ${TEST_SOURCES})
