#! /usr/bin/env python3

from mocktreehub import TreehubServer, TemporaryCredentials
from socketserver import ThreadingTCPServer
import subprocess
import threading

import sys

# This object was manually corrupted by overwriting the last 2 bytes with FFFF
bad_object = '41/45b1a9bade30efb28ff921f7a555ff82ba7d3b7b83b968084436167912fa83.filez'


class OstreeRepo(object):
    """
    Detect if garage-push uploads the known-corrupt object
    """

    def __init__(self):
        self.got_bad_object = False

    def upload(self, name):
        print("Uploaded", name)
        if name == bad_object:
            print("garage-push attempted to upload known corrupted object:%s" % name)
            self.got_bad_object = True
        return 204

    def query(self, name):
        return 404


def main():
    ostree_repo = OstreeRepo()

    def handler(*args):
        TreehubServer(ostree_repo, *args)

    httpd = ThreadingTCPServer(('localhost', 0), handler)
    address, port = httpd.socket.getsockname()
    print("Serving at port", port)
    t = threading.Thread(target=httpd.serve_forever)
    t.setDaemon(True)
    t.start()

    target = sys.argv[1]

    with TemporaryCredentials(port) as creds:
        # First try with integrity checks enabled (the default)
        dut = subprocess.Popen(args=[target, '--credentials', creds.path(), '--ref', 'master',
                                     '--repo', 'corrupt-repo'])
        try:
            exitcode = dut.wait(120)
            if exitcode == 0:
                print("garage-push should report an error result")
                sys.exit(1)
            if ostree_repo.got_bad_object:
                print("Bad object was uploaded")
                sys.exit(1)
        except subprocess.TimeoutExpired:
            print("garage-push hung")
            sys.exit(1)
        # With --disable-integrity-checks, it should succeed
        dut = subprocess.Popen(args=[target, '--credentials', creds.path(), '--ref', 'master',
                                     '--repo', 'corrupt-repo', '--disable-integrity-checks'])
        try:
            exitcode = dut.wait(120)
            if exitcode != 0:
                print("garage-push should succeed when integrity checks are not enabled")
                sys.exit(1)
            if not ostree_repo.got_bad_object:
                print("Bad object should have been uploaded anyway")
                sys.exit(1)
        except subprocess.TimeoutExpired:
            print("garage-push hung")
            sys.exit(1)


if __name__ == '__main__':
    main()
