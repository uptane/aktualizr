set(SOURCES packagemanagerfactory.cc
            packagemanagerfake.cc
            packagemanagerinterface.cc)

set(HEADERS packagemanagerfake.h)

add_library(package_manager OBJECT ${SOURCES})
aktualizr_source_file_checks(${SOURCES} packagemanagerconfig.cc ${HEADERS})

target_sources(config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/packagemanagerconfig.cc)

add_aktualizr_test(NAME packagemanagerfake SOURCES packagemanagerfake_test.cc LIBRARIES PUBLIC uptane_generator_lib)

# OSTree backend
if(BUILD_OSTREE)
    target_sources(package_manager PRIVATE ostreemanager.cc)
    target_include_directories(package_manager PUBLIC ${LIBOSTREE_INCLUDE_DIRS})

    add_custom_target(make_ostree_sysroot
                      COMMAND ${PROJECT_SOURCE_DIR}/scripts/make_ostree_sysroot.sh
                      ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    add_dependencies(build_tests make_ostree_sysroot)

    add_aktualizr_test(NAME ostreemanager SOURCES ostreemanager_test.cc PROJECT_WORKING_DIRECTORY
                       ARGS ${PROJECT_BINARY_DIR}/ostree_repo)
endif(BUILD_OSTREE)

# RAUC backend
if(BUILD_RAUC)
    target_sources(package_manager PRIVATE raucmanager.cc)
    target_include_directories(package_manager PUBLIC ${LIBRAUC_INCLUDE_DIRS})
    set_target_properties(package_manager PROPERTIES
                      CXX_STANDARD 17
                      CXX_EXTENSIONS off)
    target_link_libraries(package_manager PUBLIC SDBusCpp::sdbus-c++)

    add_aktualizr_test(NAME raucmanager SOURCES raucmanager_test.cc PROJECT_WORKING_DIRECTORY)
    set_target_properties(t_raucmanager PROPERTIES
                      CXX_STANDARD 17
                      CXX_EXTENSIONS off)
endif(BUILD_RAUC)

add_aktualizr_test(NAME packagemanagerconfig SOURCES packagemanagerconfig_test.cc NO_VALGRIND)
add_aktualizr_test(NAME packagemanager_factory SOURCES packagemanagerfactory_test.cc
                   ARGS ${PROJECT_BINARY_DIR}/ostree_repo)
add_aktualizr_test(NAME fetcher SOURCES fetcher_test.cc ARGS PROJECT_WORKING_DIRECTORY LIBRARIES PUBLIC uptane_generator_lib)
add_aktualizr_test(NAME fetcher_death SOURCES fetcher_death_test.cc NO_VALGRIND ARGS PROJECT_WORKING_DIRECTORY)

aktualizr_source_file_checks(fetcher_death_test.cc
                             fetcher_test.cc
                             packagemanagerconfig_test.cc
                             packagemanagerfake_test.cc
                             packagemanagerfactory_test.cc
                             ostreemanager_test.cc
                             ostreemanager.cc
                             ostreemanager.h
                             raucmanager_test.cc
                             raucmanager.cc
                             raucmanager.h)
