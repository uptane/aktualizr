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

if(BUILD_SWUPDATE)
    target_sources(package_manager PRIVATE swupdatemanager.cc)
    target_include_directories(package_manager PUBLIC ${LIBSWUPDATE_INCLUDE_DIRS})

    add_aktualizr_test(NAME swupdatemanager SOURCES swupdatemanager_test.cc PROJECT_WORKING_DIRECTORY)
endif(BUILD_SWUPDATE)

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
                             swupdatemanager_test.cc
                             swupdatemanager.cc
                             swupdatemanager.h)
                             
