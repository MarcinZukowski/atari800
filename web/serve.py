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

    # POST /__save?name=FILE stores the body as saves/FILE (a test saving a state or a picture)
    def do_POST(self):
        import os, urllib.parse
        q = urllib.parse.urlparse(self.path)
        if q.path != "/__save":
            self.send_error(404); return
        name = os.path.basename(urllib.parse.parse_qs(q.query).get("name", ["save"])[0])
        os.makedirs(os.path.join(directory, "saves"), exist_ok=True)
        with open(os.path.join(directory, "saves", name), "wb") as f:
            f.write(self.rfile.read(int(self.headers.get("Content-Length", 0))))
        self.send_response(200); self.end_headers()


directory = sys.argv[1] if len(sys.argv) > 1 else "."
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8800
print(f"Serving {directory} on http://localhost:{port}")
http.server.ThreadingHTTPServer(("", port), functools.partial(Handler, directory=directory)).serve_forever()
