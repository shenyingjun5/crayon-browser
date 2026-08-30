#!/usr/bin/env python3
"""Run the macOS CEF-to-Core Markdown path against loopback-only fixtures."""

from __future__ import annotations

import http.server
import pathlib
import subprocess
import sys
import tempfile
import threading


PARAGRAPHS = "".join(f"<p>Visible fixture paragraph {index}.</p>" for index in range(70))
BACKPRESSURE_PARAGRAPHS = "".join(
    f"<p>Backpressure fixture paragraph {index}.</p>" for index in range(1100)
)
FIXTURE = f"""<!doctype html><meta charset=utf-8><title>Snapshot fixture</title>
<main><h1>Visible fixture heading</h1>{PARAGRAPHS}
<section hidden><p>hidden fixture secret</p></section>
<ol start=3><li>ordered fixture item</li></ol>
<table><tr><th>Name</th><th>Value</th></tr><tr><td>alpha</td><td>1</td></tr></table>
<a href=/details>details</a><hr></main>"""
EMPTY_FIXTURE = "<!doctype html><meta charset=utf-8><title>Empty fixture</title><main></main>"
BACKPRESSURE_FIXTURE = f"""<!doctype html><meta charset=utf-8>
<title>Backpressure fixture</title><main>{BACKPRESSURE_PARAGRAPHS}</main>"""
RECOVERY_FIXTURE = """<!doctype html><meta charset=utf-8><title>Recovery fixture</title>
<main><h1>Recovery fixture heading</h1><p>Recovered after lifecycle fence.</p></main>"""
SCENARIOS = (
    "normal",
    "empty",
    "navigation",
    "cancel",
    "close",
    "backpressure",
    "crash",
)


class FixtureServer(http.server.ThreadingHTTPServer):
    """Suppress expected disconnect noise when lifecycle tests cancel loads."""

    def handle_error(self, request, client_address):
        _, error, _ = sys.exc_info()
        if isinstance(error, (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)


def main() -> int:
    if len(sys.argv) != 2:
        return 2
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="crayon-snapshot-fixture-") as root:
        root_path = pathlib.Path(root)
        root_path.joinpath("index.html").write_text(FIXTURE, encoding="utf-8")
        root_path.joinpath("empty.html").write_text(EMPTY_FIXTURE, encoding="utf-8")
        root_path.joinpath("backpressure.html").write_text(
            BACKPRESSURE_FIXTURE, encoding="utf-8"
        )
        root_path.joinpath("recovery.html").write_text(
            RECOVERY_FIXTURE, encoding="utf-8"
        )
        handler = lambda *args, **kwargs: http.server.SimpleHTTPRequestHandler(
            *args, directory=root, **kwargs
        )
        server = FixtureServer(("127.0.0.1", 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            for scenario in SCENARIOS:
                fixture = {
                    "empty": "empty.html",
                    "backpressure": "backpressure.html",
                }.get(scenario, "index.html")
                url = f"http://127.0.0.1:{server.server_port}/{fixture}"
                process = subprocess.Popen(
                    [str(executable), url, scenario],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                try:
                    stdout, stderr = process.communicate(timeout=35)
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
                sys.stdout.write(stdout)
                sys.stderr.write(stderr)
                if process.returncode != 0:
                    return process.returncode
        finally:
            server.shutdown()
            thread.join(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
