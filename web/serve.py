#!/usr/bin/env python3
"""Serves a directory for development, like `python3 -m http.server`, but
tells the browser to ask before reusing any file, so that a rebuilt site
shows on a plain reload.  Usage: serve.py <directory> [port]"""
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


directory = sys.argv[1] if len(sys.argv) > 1 else "."
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8800
print(f"Serving {directory} on http://localhost:{port}")
http.server.ThreadingHTTPServer(("", port), functools.partial(Handler, directory=directory)).serve_forever()
