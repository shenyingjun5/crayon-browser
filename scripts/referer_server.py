#!/usr/bin/env python3
"""P5 播放闭环用 mock 防盗链源：仅当 Referer 为指定值时才给文件，否则 403。

用法: python3 scripts/referer_server.py <端口> <文件路径> <要求的Referer>
示例: python3 scripts/referer_server.py 8899 /tmp/p5.mp4 http://allowed.example/
"""
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8899
FILE = sys.argv[2] if len(sys.argv) > 2 else "/tmp/p5.mp4"
NEED_REFERER = sys.argv[3] if len(sys.argv) > 3 else "http://allowed.example/"


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.headers.get("Referer") != NEED_REFERER:
            self.send_response(403)
            self.end_headers()
            self.wfile.write(b"forbidden: bad referer")
            return
        try:
            with open(FILE, "rb") as f:
                data = f.read()
        except OSError:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "video/mp4")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    print(f"referer-guard server on 127.0.0.1:{PORT}, file={FILE}, need Referer={NEED_REFERER}")
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
