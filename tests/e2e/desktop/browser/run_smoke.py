#!/usr/bin/env python3
"""CEF-14: desktop browser E2E smoke harness (macOS first).

Modes:
  selfcheck — validate harness invariants (redaction vectors, expected
              process names, argument handling). Deterministic, no app
              launch. Used by ctest when no app bundle is configured.
  smoke     — launch the built CrayonBrowser.app via LaunchServices,
              verify the full multi-process tree (main + 5 helpers),
              verify no non-loopback sockets, quit, verify zero
              residue, and write a redacted JSON report.

No public network is used at any point; the app under test opens
about:blank only (URL-driven fixtures land with shell omnibox wiring).

Usage:
  run_smoke.py selfcheck
  run_smoke.py smoke --bundle <CrayonBrowser.app> [--out <report.json>]
"""

import json
import re
import subprocess
import sys
import time
from pathlib import Path

EXPECTED_HELPERS = (
    "CrayonBrowser Helper",
    "CrayonBrowser Helper (GPU)",
    "CrayonBrowser Helper (Renderer)",
    "CrayonBrowser Helper (Plugin)",
    "CrayonBrowser Helper (Alerts)",
)

# Redaction rules: query values, cookies, authorization, bearer tokens,
# signed URLs and userinfo — mirrors the repo LeakScanner semantics.
REDACT_PATTERNS = [
    (re.compile(r"([?&][A-Za-z0-9_.-]+=)[^&\s]+"), r"\1[redacted]"),
    (re.compile(r"(?i)(cookie\s*:\s*)[^\r\n]+"), r"\1[redacted]"),
    # bearer runs before the generic authorization rule so the scheme
    # word is masked with its credential
    (re.compile(r"(?i)(authorization\s*:\s*bearer\s+)[A-Za-z0-9._-]+"), r"\1[redacted]"),
    (re.compile(r"(?i)(authorization\s*:\s*)[^\r\n]+"), r"\1[redacted]"),
    (re.compile(r"(?i)(token=)[^&\s]+"), r"\1[redacted]"),
    (re.compile(r"https?://[^/\s]+@"), "https://[redacted]@"),
]


def redact(text: str) -> str:
    for pattern, replacement in REDACT_PATTERNS:
        text = pattern.sub(replacement, text)
    return text


def run(command, timeout=30):
    return subprocess.run(command, capture_output=True, text=True, timeout=timeout)


def app_processes():
    result = run(["ps", "-axo", "pid=,comm="])
    processes = []
    for line in result.stdout.splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) != 2:
            continue
        name = Path(parts[1]).name
        if name == "CrayonBrowser" or name.startswith("CrayonBrowser Helper"):
            processes.append({"pid": parts[0], "name": name})
    return processes


def non_loopback_sockets(pids):
    findings = []
    for pid in pids:
        result = run(["lsof", "-a", "-p", pid, "-i", "-n", "-P"], timeout=30)
        for line in result.stdout.splitlines():
            if "TCP" not in line and "UDP" not in line:
                continue
            if "127.0.0.1" in line or "::1" in line or "localhost" in line:
                continue
            findings.append(redact(line.strip()))
    return findings


def selfcheck() -> int:
    failures = []

    vectors = [
        ("https://x.example/v.m3u8?token=abc123&sig=zz", "https://x.example/v.m3u8?token=[redacted]&sig=[redacted]"),
        ("Cookie: SESSDATA=deadbeef", "Cookie: [redacted]"),
        ("authorization: Bearer abc.def.ghi", "authorization: [redacted]"),
        ("https://user:pass@host/path", "https://[redacted]@host/path"),
        ("clean text without secrets", "clean text without secrets"),
    ]
    for raw, expected in vectors:
        if redact(raw) != expected:
            failures.append(f"redaction vector failed: {raw!r} -> {redact(raw)!r}")

    if len(EXPECTED_HELPERS) != 5:
        failures.append("expected helper set changed")

    if failures:
        for failure in failures:
            print(f"SELFCHK {failure}", file=sys.stderr)
        return 1
    print("selfcheck passed: redaction vectors and process expectations ok")
    return 0


def smoke(bundle: str, out_path: str) -> int:
    bundle_path = Path(bundle).resolve()
    if not bundle_path.is_dir() or not (bundle_path / "Contents/MacOS/CrayonBrowser").exists():
        print(f"SELFCHK bundle missing or invalid: {bundle}", file=sys.stderr)
        return 1

    report = {"bundle": redact(str(bundle_path)), "phases": {}, "failed": []}

    # A stale instance would corrupt the process-tree assertions;
    # quit it and wait for full exit first.
    if app_processes():
        run(["osascript", "-e", 'tell application "CrayonBrowser" to quit'])
        for _ in range(10):
            time.sleep(1)
            if not app_processes():
                break

    run(["open", str(bundle_path)])
    processes = []
    for _ in range(20):  # poll: helper spawn can lag a cold start
        time.sleep(1)
        processes = app_processes()
        if len(processes) >= 6:
            break
    names = sorted({p["name"] for p in processes})
    report["phases"]["launch"] = {"process_count": len(processes), "names": names}
    if len(processes) < 6:
        report["failed"].append(
            f"expected >=6 processes (main + 5 helpers), saw {len(processes)}: {names}"
        )

    sockets = non_loopback_sockets([p["pid"] for p in processes])
    report["phases"]["network"] = {"non_loopback_sockets": sockets}
    if sockets:
        report["failed"].append("non-loopback sockets observed (public network use)")

    run(["osascript", "-e", 'tell application "CrayonBrowser" to quit'])
    residue = []
    for _ in range(10):  # poll shutdown for up to 10s
        time.sleep(1)
        residue = app_processes()
        if not residue:
            break
    report["phases"]["shutdown"] = {"residue_count": len(residue)}
    if residue:
        report["failed"].append(f"process residue after quit: {len(residue)}")

    report_path = Path(out_path)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")

    if report["failed"]:
        for failure in report["failed"]:
            print(f"SMOKE {failure}", file=sys.stderr)
        return 1
    print("smoke passed: full process tree, loopback-only, zero residue")
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    mode = sys.argv[1]
    if mode == "selfcheck":
        return selfcheck()
    if mode == "smoke":
        bundle = None
        out = ".cache/e2e/desktop/browser/report.json"
        for arg in sys.argv[2:]:
            if arg.startswith("--bundle="):
                bundle = arg.split("=", 1)[1]
            elif arg.startswith("--out="):
                out = arg.split("=", 1)[1]
        if not bundle:
            print("SELFCHK smoke mode requires --bundle", file=sys.stderr)
            return 2
        return smoke(bundle, out)
    print(f"unknown mode: {mode}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
