#!/usr/bin/env python3
"""Run the macOS CEF snapshot bridge against a loopback-only HTML fixture."""

from __future__ import annotations

import http.server
import pathlib
import subprocess
import sys
import tempfile
import threading


PARAGRAPHS = "".join(f"<p>Visible fixture paragraph {index}.</p>" for index in range(70))
FIXTURE = f"""<!doctype html><meta charset=utf-8><title>Snapshot fixture</title>
<main><h1>Visible fixture heading</h1>{PARAGRAPHS}
<section hidden><p>hidden fixture secret</p></section>
<ol start=3><li>ordered fixture item</li></ol>
<table><tr><th>Name</th><th>Value</th></tr><tr><td>alpha</td><td>1</td></tr></table>
<a href=/details>details</a><hr></main>"""


def main() -> int:
    if len(sys.argv) != 2:
        return 2
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="crayon-snapshot-fixture-") as root:
        pathlib.Path(root, "index.html").write_text(FIXTURE, encoding="utf-8")
        handler = lambda *args, **kwargs: http.server.SimpleHTTPRequestHandler(
            *args, directory=root, **kwargs
        )
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}/index.html"
            process = subprocess.Popen(
                [str(executable), url],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            try:
                stdout, stderr = process.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    stdout, stderr = process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    stdout, stderr = process.communicate(timeout=5)
                sys.stdout.write(stdout)
                sys.stderr.write(stderr)
                return 124
        finally:
            server.shutdown()
            thread.join(timeout=5)
    sys.stdout.write(stdout)
    sys.stderr.write(stderr)
    return process.returncode


if __name__ == "__main__":
    raise SystemExit(main())
