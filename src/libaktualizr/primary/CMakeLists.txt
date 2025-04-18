set(SOURCES aktualizr.cc
            aktualizr_helpers.cc
            provisioner.cc
            reportqueue.cc
            secondary_provider.cc
            sotauptaneclient.cc)

set(HEADERS aktualizr_helpers.h
            provisioner.h
            reportqueue.h
            secondary_config.h
            secondary_provider_builder.h
            sotauptaneclient.h)

add_library(primary OBJECT ${SOURCES})

add_library(provisioner_test_utils STATIC provisioner_test_utils.cc)
aktualizr_source_file_checks(provisioner_test_utils.cc provisioner_test_utils.h)


add_aktualizr_test(NAME aktualizr
                   SOURCES aktualizr_test.cc
                   PROJECT_WORKING_DIRECTORY
                   ARGS ${PROJECT_BINARY_DIR}/uptane_repos
                   LIBRARIES uptane_generator_lib virtual_secondary)
add_dependencies(t_aktualizr uptane_repo_full_no_correlation_id)

add_aktualizr_test(NAME reregistration
                   SOURCES reregistration_test.cc
                   PROJECT_WORKING_DIRECTORY
                   LIBRARIES uptane_generator_lib virtual_secondary)

if (BUILD_OSTREE)
    add_aktualizr_test(NAME aktualizr_fullostree
                       SOURCES aktualizr_fullostree_test.cc
                       PROJECT_WORKING_DIRECTORY
                       ARGS ${PROJECT_BINARY_DIR}/ostree_repo
                       LIBRARIES uptane_generator_lib virtual_secondary)
    set_target_properties(t_aktualizr_fullostree PROPERTIES LINK_FLAGS -Wl,--export-dynamic)
    add_dependencies(t_aktualizr_fullostree uptane-generator make_ostree_sysroot)

    add_aktualizr_test(NAME download_nonostree
                       SOURCES download_nonostree_test.cc
                       PROJECT_WORKING_DIRECTORY
                       ARGS $<TARGET_FILE:uptane-generator> ${PROJECT_BINARY_DIR}/ostree_repo
                       LIBRARIES virtual_secondary)
    add_dependencies(t_download_nonostree uptane-generator make_ostree_sysroot)
    set_tests_properties(test_download_nonostree PROPERTIES LABELS "noptest")

    add_aktualizr_test(NAME aktualizr_lite
                       SOURCES aktualizr_lite_test.cc
                       LIBRARIES uptane_generator_lib
                       PROJECT_WORKING_DIRECTORY
                       ARGS ${PROJECT_BINARY_DIR}/ostree_repo)
    set_target_properties(t_aktualizr_lite PROPERTIES LINK_FLAGS -Wl,--export-dynamic)
    # Recent versions of Boost (e.g. v1.87) deprecated boost::process::v1::child::wait_for since it is
    # unreliable. However the new v2 API isn;t available in the 1.74 version we support in Debian
    # oldstable. For now disable the warning
    # https://www.boost.org/doc/libs/1_74_0/doc/html/process.html
    #
    set_source_files_properties(aktualizr_lite_test.cc PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations")
    add_dependencies(t_aktualizr_lite make_ostree_sysroot)
else (BUILD_OSTREE)
    aktualizr_source_file_checks(aktualizr_fullostree_test.cc download_nonostree_test.cc aktualizr_lite_test.cc)
endif (BUILD_OSTREE)

add_aktualizr_test(NAME provisioner
                   SOURCES provisioner_test.cc
                   PROJECT_WORKING_DIRECTORY
                   LIBRARIES PUBLIC uptane_generator_lib provisioner_test_utils)

add_aktualizr_test(NAME reportqueue
                   SOURCES reportqueue_test.cc
                   PROJECT_WORKING_DIRECTORY
                   LIBRARIES PUBLIC uptane_generator_lib)

add_aktualizr_test(NAME empty_targets
                   SOURCES empty_targets_test.cc
                   PROJECT_WORKING_DIRECTORY
                   ARGS "$<TARGET_FILE:uptane-generator>"
                   LIBRARIES uptane_generator_lib virtual_secondary)

add_aktualizr_test(NAME custom_url SOURCES custom_url_test.cc
                   PROJECT_WORKING_DIRECTORY
                   ARGS "$<TARGET_FILE:uptane-generator>"
                   LIBRARIES uptane_generator_lib virtual_secondary)

add_aktualizr_test(NAME target_mismatch
                   SOURCES target_mismatch_test.cc
                   PROJECT_WORKING_DIRECTORY
                   ARGS "$<TARGET_FILE:uptane-generator>"
                   LIBRARIES uptane_generator_lib virtual_secondary)

add_aktualizr_test(NAME metadata_fetch
                   SOURCES metadata_fetch_test.cc
                   PROJECT_WORKING_DIRECTORY
                   LIBRARIES uptane_generator_lib virtual_secondary)

add_aktualizr_test(NAME metadata_expiration
                   SOURCES metadata_expiration_test.cc
                   PROJECT_WORKING_DIRECTORY
                   ARGS "$<TARGET_FILE:uptane-generator>"
                   LIBRARIES uptane_generator_lib virtual_secondary)

add_aktualizr_test(NAME device_cred_prov
                   SOURCES device_cred_prov_test.cc
                   PROJECT_WORKING_DIRECTORY
                   LIBRARIES uptane_generator_lib provisioner_test_utils)
set_tests_properties(test_device_cred_prov PROPERTIES LABELS "crypto")


add_aktualizr_test(NAME uptane_key
                   SOURCES uptane_key_test.cc
                   PROJECT_WORKING_DIRECTORY
                   LIBRARIES uptane_generator_lib virtual_secondary)
set_tests_properties(test_uptane_key PROPERTIES LABELS "crypto")


aktualizr_source_file_checks(${SOURCES} ${HEADERS} ${TEST_SOURCES})
