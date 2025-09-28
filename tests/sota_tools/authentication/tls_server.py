#!/usr/bin/python3
from http.server import HTTPServer,SimpleHTTPRequestHandler
import argparse
from os.path import join
import socket
import ssl

class ReUseHTTPServer(HTTPServer):
    def server_bind(self):
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        HTTPServer.server_bind(self)

parser = argparse.ArgumentParser()
parser.add_argument('port', type=int)
parser.add_argument('cert_path')
parser.add_argument('--noauth', action='store_true')
args = parser.parse_args()

httpd = ReUseHTTPServer(('localhost', args.port), SimpleHTTPRequestHandler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(certfile=join(args.cert_path, 'server.crt'),
                        keyfile=join(args.cert_path, 'server.key'))
context.load_verify_locations(cafile=join(args.cert_path, 'ca.crt'))
context.verify_mode = ssl.CERT_NONE if args.noauth else ssl.CERT_REQUIRED
httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
httpd.serve_forever()
