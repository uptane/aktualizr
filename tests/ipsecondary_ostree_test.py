#!/usr/bin/env python3

import argparse
import logging
import time

from os import getcwd, chdir, path

from test_fixtures import with_aktualizr, with_uptane_backend, KeyStore, with_secondary, with_treehub,\
    with_sysroot, with_director, TestRunner, IPSecondary

logger = logging.getLogger("IPSecondaryOstreeTest")


@with_treehub()
@with_uptane_backend()
@with_director()
@with_sysroot()
@with_secondary(start=False, output_logs=True)
@with_aktualizr(start=False, run_mode='once', output_logs=True)
def test_secondary_ostree_update(uptane_repo, secondary, aktualizr, treehub, sysroot, director, **kwargs):
    """Test Secondary OSTree update if a boot order of Secondary and Primary is undefined"""

    target_rev = treehub.revision
    expected_targetname = uptane_repo.add_ostree_target(secondary.id, target_rev, "GARAGE_TARGET_NAME")

    with secondary:
        with aktualizr:
            aktualizr.wait_for_completion()

    pending_rev = aktualizr.get_current_pending_image_info(secondary.id)

    if pending_rev != target_rev:
        logger.error("Pending version {} != the target one {}".format(pending_rev, target_rev))
        return False

    sysroot.update_revision(pending_rev)
    secondary.emulate_reboot()

    aktualizr.set_mode('full')
    with aktualizr:
        with secondary:
            director.wait_for_install()

    if not director.get_install_result():
        logger.error("Installation result is not successful")
        return False

    installed_rev = aktualizr.get_current_image_info(secondary.id)

    if installed_rev != target_rev:
        logger.error("Installed version {} != the target one {}".format(installed_rev, target_rev))
        return False

    if expected_targetname != director.get_ecu_manifest_filepath(secondary.id[1]):
        logger.error(
            "Target name doesn't match a filepath value of the reported manifest: expected: {}, actual: {}".
            format(expected_targetname, director.get_ecu_manifest_filepath(secondary.id[1])))
        return False

    return True


@with_treehub()
@with_uptane_backend()
@with_director()
@with_sysroot()
@with_secondary(start=False, output_logs=False, force_reboot=True)
@with_aktualizr(start=False, run_mode='once', output_logs=True)
def test_secondary_ostree_reboot(uptane_repo, secondary, aktualizr, treehub, sysroot, director, **kwargs):
    """Test Secondary OSTree update with automatic (forced) reboot"""
    target_rev = treehub.revision
    uptane_repo.add_ostree_target(secondary.id, target_rev, "GARAGE_TARGET_NAME")

    with secondary:
        with aktualizr:
            aktualizr.wait_for_completion()
        secondary.wait_for_completion()

    pending_rev = aktualizr.get_current_pending_image_info(secondary.id)

    if pending_rev != target_rev:
        logger.error("Pending version {} != the target one {}".format(pending_rev, target_rev))
        return False

    sysroot.update_revision(pending_rev)

    aktualizr.set_mode('full')
    with aktualizr:
        with secondary:
            director.wait_for_install()

    if not director.get_install_result():
        logger.error("Installation result is not successful")
        return False

    installed_rev = aktualizr.get_current_image_info(secondary.id)

    if installed_rev != target_rev:
        logger.error("Installed version {} != the target one {}".format(installed_rev, target_rev))
        return False

    return True


# test suit runner
if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)

    parser = argparse.ArgumentParser(description='Test IP Secondary with OSTree')
    parser.add_argument('-b', '--build-dir', help='build directory', default='build')
    parser.add_argument('-s', '--src-dir', help='source directory', default='.')

    input_params = parser.parse_args()

    KeyStore.base_dir = path.abspath(input_params.src_dir)
    initial_cwd = getcwd()
    chdir(input_params.build_dir)

    test_suite = [
        test_secondary_ostree_update,
        test_secondary_ostree_reboot,
    ]

    with TestRunner(test_suite) as runner:
        test_suite_run_result = runner.run()

    chdir(initial_cwd)
    exit(0 if test_suite_run_result else 1)
