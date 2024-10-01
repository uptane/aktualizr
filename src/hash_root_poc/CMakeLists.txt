set(HASH_ROOT_POC_SRC main.cc)
set(HASH_ROOT_POC_HEADERS hash_root_poc.h)

add_executable(hash-root-poc ${HASH_ROOT_POC_SRC})

# See https://github.com/Kistler-Group/sdbus-cpp/blob/master/docs/using-sdbus-c++.md#integrating-sdbus-c-into-your-project
target_link_libraries(hash-root-poc PUBLIC aktualizr_lib)

install(TARGETS hash-root-poc
        COMPONENT aktualizr
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_aktualizr_test(NAME hash_root_poc SOURCES hash_root_poc_test.cc)
set_target_properties(t_hash_root_poc PROPERTIES
                      CXX_STANDARD 17
                      CXX_EXTENSIONS off)

aktualizr_source_file_checks(${HASH_ROOT_POC_SRC} ${HASH_ROOT_POC_HEADERS} ${TEST_SOURCES})
