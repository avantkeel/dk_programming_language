"""Tiny local HTTP server used by `make test-http`."""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, status, payload, content_type="application/json"):
        data = payload if isinstance(payload, bytes) else json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _handle(self):
        url = urlparse(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length).decode() if length else ""
        if url.path == "/json":
            self._send(200, {"name": "dk", "items": [1, 2, 3]})
        elif url.path == "/echo":
            self._send(200, {"method": self.command, "body": body,
                             "xtest": self.headers.get("X-Test", "")})
        elif url.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/json")
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif url.path == "/query":
            self._send(200, {"q": parse_qs(url.query).get("q", [""])[0]})
        elif url.path == "/slow":
            time.sleep(3)
            self._send(200, {"slow": True})
        else:
            self._send(404, {"error": "not found"})

    do_GET = do_POST = do_PUT = do_DELETE = _handle


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
