#!/usr/bin/env python3
"""Capture language signals from the real desktop browser over loopback only."""

from __future__ import annotations

import argparse
import html
import http.server
import http.client
import json
import pathlib
import secrets
import threading
import time
from typing import Any


MAX_REPORT_BYTES = 4096
MAX_LANGUAGE_ITEMS = 8
MAX_LANGUAGE_LENGTH = 64


class ProbeState:
    def __init__(self, route_token: str | None = None) -> None:
        self.lock = threading.Lock()
        self.route_token = route_token or secrets.token_urlsafe(24)
        self.accept_language: str | None = None
        self.report: dict[str, Any] | None = None
        self.completed = threading.Event()


def valid_report(value: Any) -> bool:
    if not isinstance(value, dict):
        return False
    language = value.get("navigator_language")
    languages = value.get("navigator_languages")
    document_language = value.get("document_language")
    if not isinstance(language, str) or len(language) > MAX_LANGUAGE_LENGTH:
        return False
    if not isinstance(document_language, str) or len(document_language) > MAX_LANGUAGE_LENGTH:
        return False
    if not isinstance(languages, list) or len(languages) > MAX_LANGUAGE_ITEMS:
        return False
    return all(
        isinstance(item, str) and len(item) <= MAX_LANGUAGE_LENGTH
        for item in languages
    )


class ProbeHandler(http.server.BaseHTTPRequestHandler):
    server_version = "CrayonLocalizationProbe/1"

    @property
    def probe_state(self) -> ProbeState:
        return self.server.probe_state  # type: ignore[attr-defined]

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path != f"/{self.probe_state.route_token}/":
            self.send_error(404)
            return
        accept_language = self.headers.get("Accept-Language", "")
        if len(accept_language) > 512:
            self.send_error(431)
            return
        with self.probe_state.lock:
            self.probe_state.accept_language = accept_language
        body = f"""<!doctype html>
<html lang="probe"><head><meta charset="utf-8"><title>Crayon localization probe</title>
<meta name="viewport" content="width=device-width,initial-scale=1"></head>
<body><main><h1>Crayon localization probe</h1>
<dl><dt>Accept-Language</dt><dd id="accept">{html.escape(accept_language)}</dd>
<dt>navigator.language</dt><dd id="language"></dd>
<dt>navigator.languages</dt><dd id="languages"></dd>
<dt>document language</dt><dd id="document-language"></dd></dl>
<p id="status">report pending</p></main>
<script nonce="crayon-localization-probe">
const report={{navigator_language:navigator.language,
  navigator_languages:Array.from(navigator.languages),
  document_language:document.documentElement.lang}};
document.querySelector('#language').textContent=report.navigator_language;
document.querySelector('#languages').textContent=JSON.stringify(report.navigator_languages);
document.querySelector('#document-language').textContent=report.document_language;
fetch('report',{{method:'POST',headers:{{'Content-Type':'application/json'}},
  body:JSON.stringify(report)}}).then(response=>{{
    document.querySelector('#status').textContent=response.ok?'report captured':'report rejected';
  }}).catch(()=>{{document.querySelector('#status').textContent='report failed';}});
</script></body></html>""".encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'none'; script-src 'nonce-crayon-localization-probe'; "
            "connect-src 'self'; base-uri 'none'; frame-ancestors 'none'",
        )
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path != f"/{self.probe_state.route_token}/report":
            self.send_error(404)
            return
        raw_length = self.headers.get("Content-Length", "")
        if not raw_length.isdigit():
            self.send_error(411)
            return
        length = int(raw_length)
        if length <= 0 or length > MAX_REPORT_BYTES:
            self.send_error(413)
            return
        try:
            report = json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self.send_error(400)
            return
        if not valid_report(report):
            self.send_error(422)
            return
        with self.probe_state.lock:
            self.probe_state.report = report
        self.send_response(204)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.probe_state.completed.set()

    def log_message(self, _format: str, *_args: object) -> None:
        return


class ProbeServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, address: tuple[str, int], state: ProbeState) -> None:
        super().__init__(address, ProbeHandler)
        self.probe_state = state


def write_json(path: pathlib.Path, value: dict[str, Any]) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--expected-language")
    parser.add_argument("--expected-languages")
    parser.add_argument("--expected-accept-language")
    return parser.parse_args()


def self_check() -> int:
    failures: list[str] = []
    state = ProbeState("self-check-route")
    server = ProbeServer(("127.0.0.1", 0), state)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        connection = http.client.HTTPConnection(
            "127.0.0.1", server.server_port, timeout=5
        )
        connection.request("GET", "/wrong-route/")
        wrong_route = connection.getresponse()
        wrong_route.read()
        if wrong_route.status != 404:
            failures.append("unscoped probe route was not rejected")
        connection.request(
            "GET",
            "/self-check-route/",
            headers={"Accept-Language": "zh-TW,zh;q=0.9"},
        )
        response = connection.getresponse()
        body = response.read().decode("utf-8")
        if response.status != 200:
            failures.append(f"GET returned {response.status}")
        if response.getheader("Cache-Control") != "no-store":
            failures.append("GET did not disable caching")
        csp = response.getheader("Content-Security-Policy", "")
        if "default-src 'none'" not in csp or "connect-src 'self'" not in csp:
            failures.append("GET CSP is not closed to loopback self")
        if "zh-TW,zh;q=0.9" not in body or "fetch('report'" not in body:
            failures.append("GET body omitted probe evidence fields")

        invalid = json.dumps(
            {
                "navigator_language": "zh-TW",
                "navigator_languages": "zh-TW",
                "document_language": "probe",
            }
        ).encode("utf-8")
        connection.request(
            "POST",
            "/self-check-route/report",
            body=invalid,
            headers={"Content-Type": "application/json", "Content-Length": str(len(invalid))},
        )
        invalid_response = connection.getresponse()
        invalid_response.read()
        if invalid_response.status != 422 or state.completed.is_set():
            failures.append("invalid report was not rejected")

        report = {
            "navigator_language": "zh-TW",
            "navigator_languages": ["zh-TW", "zh", "en-US", "en"],
            "document_language": "probe",
        }
        encoded = json.dumps(report).encode("utf-8")
        connection.request(
            "POST",
            "/self-check-route/report",
            body=encoded,
            headers={"Content-Type": "application/json", "Content-Length": str(len(encoded))},
        )
        valid_response = connection.getresponse()
        valid_response.read()
        if valid_response.status != 204 or not state.completed.wait(1):
            failures.append("valid report was not captured")
        with state.lock:
            if state.accept_language != "zh-TW,zh;q=0.9" or state.report != report:
                failures.append("captured language evidence changed")
        connection.close()
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=5)

    if failures:
        for failure in failures:
            print(f"SELFCHK {failure}")
        return 1
    print("selfcheck passed: loopback, CSP, header capture and bounded report contract")
    return 0


def main() -> int:
    args = parse_args()
    if args.self_check:
        return self_check()
    if args.output is None:
        raise SystemExit("--output is required unless --self-check is used")
    if not 1.0 <= args.timeout <= 900.0:
        raise SystemExit("timeout must be between 1 and 900 seconds")
    expected_languages = None
    if args.expected_languages is not None:
        expected_languages = args.expected_languages.split(",")
        if not expected_languages or any(
            not item or len(item) > MAX_LANGUAGE_LENGTH
            for item in expected_languages
        ):
            raise SystemExit("expected languages must be a bounded comma-separated list")
    output = args.output.resolve()
    if output.exists() and (output.is_dir() or output.is_symlink()):
        raise SystemExit("output must be a regular file path")
    output.parent.mkdir(parents=True, exist_ok=True)

    state = ProbeState()
    server = ProbeServer(("127.0.0.1", 0), state)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    write_json(
        output,
        {
            "schema_version": 1,
            "status": "ready",
            "url": (
                f"http://127.0.0.1:{server.server_port}/"
                f"{state.route_token}/"
            ),
        },
    )

    completed = state.completed.wait(args.timeout)
    server.shutdown()
    server.server_close()
    worker.join(timeout=5)
    if not completed:
        write_json(output, {"schema_version": 1, "status": "timeout"})
        return 1

    with state.lock:
        accept_language = state.accept_language
        report = dict(state.report or {})
    checks = {
        "navigator_language": args.expected_language is None
        or report.get("navigator_language") == args.expected_language,
        "navigator_languages": expected_languages is None
        or report.get("navigator_languages") == expected_languages,
        "accept_language": args.expected_accept_language is None
        or accept_language == args.expected_accept_language,
        "document_language": report.get("document_language") == "probe",
    }
    passed = all(checks.values())
    write_json(
        output,
        {
            "schema_version": 1,
            "status": "passed" if passed else "failed",
            "accept_language": accept_language,
            **report,
            "checks": checks,
            "captured_at_unix_ms": int(time.time() * 1000),
        },
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
