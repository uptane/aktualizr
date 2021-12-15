#!/usr/bin/env python3

import logging
import argparse
from functools import wraps

from os import getcwd, chdir

from test_fixtures import KeyStore, with_aktualizr, with_uptane_backend, with_director, with_imagerepo, \
    with_sysroot, TestRunner, Treehub


logger = logging.getLogger(__file__)


class CustomUriTreehub(Treehub):
    prefix = '/mycustomuri'

    def __init__(self, ifc, port, client_handler_map={}):
        self.hits_with_prefix = 0
        super(CustomUriTreehub, self).__init__(ifc=ifc, port=port, client_handler_map=client_handler_map)

    class Handler(Treehub.Handler):
        def default_get(self):
            if self.path.startswith(CustomUriTreehub.prefix):
                self.server.hits_with_prefix += 1
                self.path = self.path[len(CustomUriTreehub.prefix):]

            super(CustomUriTreehub.Handler, self).default_get()


def with_custom_treehub_uri(handlers=[], port=0):
    def decorator(test):
        @wraps(test)
        def wrapper(*args, **kwargs):
            def func(handler_map={}):
                with CustomUriTreehub('localhost', port=port, client_handler_map=handler_map) as treehub:
                    return test(*args, **kwargs, treehub=treehub)

            if handlers and len(handlers) > 0:
                for handler in handlers:
                    result = func(handler.map(kwargs.get('test_path', '')))
                    if not result:
                        break
            else:
                result = func()
            return result
        return wrapper
    return decorator


"""
Test setting a custom URI in OSTree
"""
@with_uptane_backend(start_generic_server=True)
@with_director()
@with_custom_treehub_uri()
@with_sysroot()
@with_aktualizr(start=False, run_mode='once', output_logs=True)
def test_ostree_custom_uri(uptane_repo, aktualizr, director, sysroot,
                                               treehub, **kwargs):
    target_rev = treehub.revision
    # add an OSTree update with a custom URI
    uptane_repo.add_ostree_target(aktualizr.id, target_rev, target_uri=treehub.base_url + '/mycustomuri')

    with aktualizr:
        aktualizr.wait_for_completion()

    if treehub.hits_with_prefix <= 5:
        logger.error("Didn't fetch from custom URI. Got %d hits with %s prefix",
                     treehub.hits_with_prefix, CustomUriTreehub.prefix)
        return False
    return True


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)

    parser = argparse.ArgumentParser(description='Test backend failure')
    parser.add_argument('-b', '--build-dir', help='build directory', default='build')
    parser.add_argument('-s', '--src-dir', help='source directory', default='.')

    input_params = parser.parse_args()

    KeyStore.base_dir = input_params.src_dir
    initial_cwd = getcwd()
    chdir(input_params.build_dir)

    test_suite = [
        test_ostree_custom_uri,
    ]

    with TestRunner(test_suite) as runner:
        test_suite_run_result = runner.run()

    chdir(initial_cwd)
    exit(0 if test_suite_run_result else 1)
