set(SWUPDATE_POC_SRC main.cc)
set(SWUPDATE_POC_HEADERS swupdate_poc.h)

find_package(swupdate REQUIRED)

add_executable(swupdate-poc ${SWUPDATE_POC_SRC})

# See https://github.com/Kistler-Group/sdbus-cpp/blob/master/docs/using-sdbus-c++.md#integrating-sdbus-c-into-your-project
target_link_libraries(swupdate-poc PUBLIC aktualizr_lib PRIVATE swupdate)

install(TARGETS swupdate-poc
        COMPONENT aktualizr
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_aktualizr_test(NAME swupdate_poc SOURCES swupdate_poc_test.cc)
set_target_properties(t_swupdate_poc PROPERTIES
                      CXX_STANDARD 17
                      CXX_EXTENSIONS off)

aktualizr_source_file_checks(${SWUPDATE_POC_SRC} ${SWUPDATE_POC_HEADERS} ${TEST_SOURCES})
