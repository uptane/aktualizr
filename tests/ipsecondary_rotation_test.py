#!/usr/bin/env python3

import argparse
import logging
import time

from os import getcwd, chdir, path

from test_fixtures import with_aktualizr, with_uptane_backend, KeyStore, with_secondary, with_treehub,\
    with_sysroot, with_director, TestRunner, IPSecondary

logger = logging.getLogger("IPSecondaryRotationTest")


@with_uptane_backend()
@with_director()
@with_secondary(start=False, output_logs=True)
@with_aktualizr(start=False, output_logs=True)
def test_secondary_director_root_rotation(uptane_repo, secondary, aktualizr, director, **kwargs):
    '''Test Secondary update after rotating the Director Root twice'''

    # add a new image to the repo in order to update the Secondary with it and
    # thus get initial metadata stored in the Secondary.
    secondary_image_filename = "secondary_image_filename.img"
    secondary_image_hash = uptane_repo.add_image(id=secondary.id, image_filename=secondary_image_filename)

    logger.debug("Trying to update ECU {} with the image {}".
                format(secondary.id, (secondary_image_hash, secondary_image_filename)))

    # start Secondary and aktualizr processes, aktualizr is started in 'once' mode
    with secondary, aktualizr:
        aktualizr.wait_for_completion()

    if not director.get_install_result():
        logger.error("Installation result is not successful")
        return False

    # check currently installed hash
    if secondary_image_hash != aktualizr.get_current_image_info(secondary.id):
        logger.error("Target image hash doesn't match the currently installed hash")
        return False

    if secondary_image_filename != director.get_ecu_manifest_filepath(secondary.id[1]):
        logger.error("Target name doesn't match a filepath value of the reported manifest: {}".format(director.get_manifest()))
        return False

    # Rotate Director Root twice to make sure putRoot will be used.
    uptane_repo.rotate_root(is_director=True)
    uptane_repo.rotate_root(is_director=True)

    # Add another image to the repo in order to update the Secondary with it
    # and thus send metadata again.
    uptane_repo.clear_targets()
    secondary_image_filename = "another_secondary_image.img"
    secondary_image_hash = uptane_repo.add_image(id=secondary.id, image_filename=secondary_image_filename)

    logger.debug("Trying to update ECU {} with the image {}".
                format(secondary.id, (secondary_image_hash, secondary_image_filename)))

    # start Secondary and aktualizr processes, aktualizr is started in 'once' mode
    with secondary, aktualizr:
        aktualizr.wait_for_completion()

    if not director.get_install_result():
        logger.error("Installation result is not successful")
        return False

    # check currently installed hash
    if secondary_image_hash != aktualizr.get_current_image_info(secondary.id):
        logger.error("Target image hash doesn't match the currently installed hash")
        return False

    if secondary_image_filename != director.get_ecu_manifest_filepath(secondary.id[1]):
        logger.error("Target name doesn't match a filepath value of the reported manifest: {}".format(director.get_manifest()))
        return False

    return True


@with_uptane_backend()
@with_director()
@with_secondary(start=False, output_logs=True)
@with_aktualizr(start=False, output_logs=True)
def test_secondary_image_root_rotation(uptane_repo, secondary, aktualizr, director, **kwargs):
    '''Test Secondary update after rotating the Image repo Root twice'''

    # add a new image to the repo in order to update the Secondary with it and
    # thus get initial metadata stored in the Secondary.
    secondary_image_filename = "secondary_image_filename.img"
    secondary_image_hash = uptane_repo.add_image(id=secondary.id, image_filename=secondary_image_filename)

    logger.debug("Trying to update ECU {} with the image {}".
                format(secondary.id, (secondary_image_hash, secondary_image_filename)))

    # start Secondary and aktualizr processes, aktualizr is started in 'once' mode
    with secondary, aktualizr:
        aktualizr.wait_for_completion()

    if not director.get_install_result():
        logger.error("Installation result is not successful")
        return False

    # check currently installed hash
    if secondary_image_hash != aktualizr.get_current_image_info(secondary.id):
        logger.error("Target image hash doesn't match the currently installed hash")
        return False

    if secondary_image_filename != director.get_ecu_manifest_filepath(secondary.id[1]):
        logger.error("Target name doesn't match a filepath value of the reported manifest: {}".format(director.get_manifest()))
        return False

    # Rotate Director Root twice to make sure putRoot will be used.
    uptane_repo.rotate_root(is_director=False)
    uptane_repo.rotate_root(is_director=False)

    # Add another image to the repo in order to update the Secondary with it
    # and thus send metadata again.
    uptane_repo.clear_targets()
    secondary_image_filename = "another_secondary_image.img"
    secondary_image_hash = uptane_repo.add_image(id=secondary.id, image_filename=secondary_image_filename)

    logger.debug("Trying to update ECU {} with the image {}".
                format(secondary.id, (secondary_image_hash, secondary_image_filename)))

    # start Secondary and aktualizr processes, aktualizr is started in 'once' mode
    with secondary, aktualizr:
        aktualizr.wait_for_completion()

    if not director.get_install_result():
        logger.error("Installation result is not successful")
        return False

    # check currently installed hash
    if secondary_image_hash != aktualizr.get_current_image_info(secondary.id):
        logger.error("Target image hash doesn't match the currently installed hash")
        return False

    if secondary_image_filename != director.get_ecu_manifest_filepath(secondary.id[1]):
        logger.error("Target name doesn't match a filepath value of the reported manifest: {}".format(director.get_manifest()))
        return False

    return True


@with_uptane_backend()
@with_director()
@with_secondary(start=False, output_logs=True, verification_type="Tuf")
@with_aktualizr(start=False, output_logs=True)
def test_secondary_tuf_director_root_rotation(uptane_repo, secondary, aktualizr, director, **kwargs):
    '''Test Secondary update with TUF verification after rotating the Director Root twice'''

    # add a new image to the repo in order to update the Secondary with it and
    # thus get initial metadata stored in the Secondary.
    secondary_image_filename = "secondary_image_filename.img"
    secondary_image_hash = uptane_repo.add_image(id=secondary.id, image_filename=secondary_image_filename)

    logger.debug("Trying to update ECU {} with the image {}".
                format(secondary.id, (secondary_image_hash, secondary_image_filename)))

    # start Secondary and aktualizr processes, aktualizr is started in 'once' mode
    with secondary, aktualizr:
        aktualizr.wait_for_completion()

    if not director.get_install_result():
        logger.error("Installation result is not successful")
        return False

    # check currently installed hash
    if secondary_image_hash != aktualizr.get_current_image_info(secondary.id):
        logger.error("Target image hash doesn't match the currently installed hash")
        return False

    if secondary_image_filename != director.get_ecu_manifest_filepath(secondary.id[1]):
        logger.error("Target name doesn't match a filepath value of the reported manifest: {}".format(director.get_manifest()))
        return False

    # Rotate Director Root twice. Note that the Secondary won't care.
    uptane_repo.rotate_root(is_director=True)
    uptane_repo.rotate_root(is_director=True)

    # Add another image to the repo in order to update the Secondary with it
    # and thus send metadata again.
    uptane_repo.clear_targets()
    secondary_image_filename = "another_secondary_image.img"
    secondary_image_hash = uptane_repo.add_image(id=secondary.id, image_filename=secondary_image_filename, custom_version='2')

    logger.debug("Trying to update ECU {} with the image {}".
                format(secondary.id, (secondary_image_hash, secondary_image_filename)))

    # start Secondary and aktualizr processes, aktualizr is started in 'once' mode
    with secondary, aktualizr:
        aktualizr.wait_for_completion()

    if not director.get_install_result():
        logger.error("Installation result is not successful")
        return False

    # check currently installed hash
    if secondary_image_hash != aktualizr.get_current_image_info(secondary.id):
        logger.error("Target image hash doesn't match the currently installed hash")
        return False

    if secondary_image_filename != director.get_ecu_manifest_filepath(secondary.id[1]):
        logger.error("Target name doesn't match a filepath value of the reported manifest: {}".format(director.get_manifest()))
        return False

    return True


@with_uptane_backend()
@with_director()
@with_secondary(start=False, output_logs=True, verification_type="Tuf")
@with_aktualizr(start=False, output_logs=True)
def test_secondary_tuf_image_root_rotation(uptane_repo, secondary, aktualizr, director, **kwargs):
    '''Test Secondary update with TUF verification after rotating the Image repo Root twice'''

    # add a new image to the repo in order to update the Secondary with it and
    # thus get initial metadata stored in the Secondary.
    secondary_image_filename = "secondary_image_filename.img"
    secondary_image_hash = uptane_repo.add_image(id=secondary.id, image_filename=secondary_image_filename)

    logger.debug("Trying to update ECU {} with the image {}".
                format(secondary.id, (secondary_image_hash, secondary_image_filename)))

    # start Secondary and aktualizr processes, aktualizr is started in 'once' mode
    with secondary, aktualizr:
        aktualizr.wait_for_completion()

    if not director.get_install_result():
        logger.error("Installation result is not successful")
        return False

    # check currently installed hash
    if secondary_image_hash != aktualizr.get_current_image_info(secondary.id):
        logger.error("Target image hash doesn't match the currently installed hash")
        return False

    if secondary_image_filename != director.get_ecu_manifest_filepath(secondary.id[1]):
        logger.error("Target name doesn't match a filepath value of the reported manifest: {}".format(director.get_manifest()))
        return False

    # Rotate Director Root twice to make sure putRoot will be used.
    uptane_repo.rotate_root(is_director=False)
    uptane_repo.rotate_root(is_director=False)

    # Add another image to the repo in order to update the Secondary with it
    # and thus send metadata again.
    uptane_repo.clear_targets()
    secondary_image_filename = "another_secondary_image.img"
    secondary_image_hash = uptane_repo.add_image(id=secondary.id, image_filename=secondary_image_filename, custom_version='2')

    logger.debug("Trying to update ECU {} with the image {}".
                format(secondary.id, (secondary_image_hash, secondary_image_filename)))

    # start Secondary and aktualizr processes, aktualizr is started in 'once' mode
    with secondary, aktualizr:
        aktualizr.wait_for_completion()

    if not director.get_install_result():
        logger.error("Installation result is not successful")
        return False

    # check currently installed hash
    if secondary_image_hash != aktualizr.get_current_image_info(secondary.id):
        logger.error("Target image hash doesn't match the currently installed hash")
        return False

    if secondary_image_filename != director.get_ecu_manifest_filepath(secondary.id[1]):
        logger.error("Target name doesn't match a filepath value of the reported manifest: {}".format(director.get_manifest()))
        return False

    return True


# test suit runner
if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)

    parser = argparse.ArgumentParser(description='Test IP Secondary Root rotation')
    parser.add_argument('-b', '--build-dir', help='build directory', default='build')
    parser.add_argument('-s', '--src-dir', help='source directory', default='.')

    input_params = parser.parse_args()

    KeyStore.base_dir = path.abspath(input_params.src_dir)
    initial_cwd = getcwd()
    chdir(input_params.build_dir)

    test_suite = [
                    test_secondary_director_root_rotation,
                    test_secondary_image_root_rotation,
                    test_secondary_tuf_director_root_rotation,
                    test_secondary_tuf_image_root_rotation,
    ]

    with TestRunner(test_suite) as runner:
        test_suite_run_result = runner.run()

    chdir(initial_cwd)
    exit(0 if test_suite_run_result else 1)
