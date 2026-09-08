#!/usr/bin/env python3
"""Build the existing macOS product for local acceptance; never publish it."""

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TIME_BUDGET_SECONDS = 1800
MINIMUM_FREE_BYTES = 8 * 1024**3


def git(*args, cwd=ROOT):
    return subprocess.check_output(["git", *args], cwd=cwd).decode().strip()


def fingerprint():
    paths = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
    ).decode().split("\0")
    digest = hashlib.sha256()
    for name in sorted(set(paths)):
        path = ROOT / name
        if (not name or name.startswith(("docs/", "output/", ".playwright-cli/"))
                or not path.is_file()):
            continue
        digest.update(name.encode() + b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    digest.update(git("rev-parse", "HEAD", cwd=ROOT / "third_party/cast-sdk").encode())
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cef-root", type=Path, required=True)
    parser.add_argument("--flavor", choices=("Debug", "Release"), default="Debug")
    args = parser.parse_args()
    started = time.monotonic()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise RuntimeError("This adapter requires native macOS arm64")
    for tool in ("cmake", "ninja", "cargo", "clang++", "codesign", "node"):
        if shutil.which(tool) is None:
            raise RuntimeError(f"Missing build tool: {tool}")
    if shutil.disk_usage(ROOT).free < MINIMUM_FREE_BYTES:
        raise RuntimeError("At least 8 GiB of free space is required")
    lock = ROOT / "config/cast-sdk-source.toml"
    revision = next(line.split('"')[1] for line in lock.read_text().splitlines()
                    if line.startswith("revision = "))
    sdk = ROOT / "third_party/cast-sdk"
    if git("rev-parse", "HEAD", cwd=sdk) != revision:
        raise RuntimeError("Cast-SDK does not match the source lock")
    if git("status", "--porcelain", cwd=sdk):
        raise RuntimeError("Cast-SDK source must be clean")
    cef_root = args.cef_root.resolve(strict=True)
    build_dir = ROOT / ".cache/build" / f"macos-arm64-cef-{args.flavor.lower()}-ninja"
    source_digest = fingerprint()
    receipt = {
        "schema": "crayon.macos-local-build.v1", "mode": "local-acceptance",
        "flavor": args.flavor, "platform": "macos-arm64",
        "sourceCommit": git("rev-parse", "HEAD"), "sourceDigest": source_digest,
        "sdkRevision": revision, "publicationForbidden": True, "steps": [],
        "state": "INPUT_FROZEN",
    }
    build_dir.mkdir(parents=True, exist_ok=True)
    attempt = build_dir / "local-builds" / str(time.time_ns())
    attempt.mkdir(parents=True, exist_ok=False)
    receipt_path = attempt / "receipt.json"

    def run(command):
        remaining = TIME_BUDGET_SECONDS - (time.monotonic() - started)
        if remaining <= 0:
            raise TimeoutError("Local build exceeded its 30 minute budget")
        if fingerprint() != source_digest:
            raise RuntimeError("Source changed after input freeze")
        print("Running:", " ".join(command), flush=True)
        before = time.monotonic()
        log_path = attempt / f"step-{len(receipt['steps']) + 1}.log"
        with log_path.open("w") as log:
            result = subprocess.run(command, cwd=ROOT, timeout=remaining,
                                    check=False, stdout=log, stderr=subprocess.STDOUT)
        receipt["steps"].append({"command": command, "exitCode": result.returncode,
                                 "seconds": round(time.monotonic() - before, 2),
                                 "log": str(log_path)})
        if result.returncode:
            print("\n".join(log_path.read_text(errors="replace").splitlines()[-25:]))
            raise RuntimeError(f"First failure: {command[0]} exited {result.returncode}")
        print(f"PASS ({time.monotonic() - before:.1f}s)", flush=True)

    try:
        run(["cmake", f"-DCRAYON_CEF_LOCAL_ROOT={cef_root}",
             "-P", "cmake/cef/DownloadCef.cmake"])
        run(["cmake", "-S", ".", "-B", str(build_dir), "-G", "Ninja",
             f"-DCMAKE_BUILD_TYPE={args.flavor}", "-DCRAYON_BUILD_TESTS=ON",
             "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DPROJECT_ARCH=arm64",
             "-DCRAYON_ENABLE_CEF=ON", f"-DCRAYON_CEF_ROOT={cef_root}",
             "-DUSE_SANDBOX=ON"])
        run(["cmake", "--build", str(build_dir), "--target", "crayon_browser",
             "--parallel", "2"])
        apps = list((build_dir / "browser/cef-shell").glob("**/CrayonBrowser.app"))
        if len(apps) != 1:
            raise RuntimeError("Expected exactly one product application")
        app = apps[0]
        run(["codesign", "--verify", "--deep", "--strict", str(app)])
        if fingerprint() != source_digest:
            raise RuntimeError("Source changed during the build")
        executable = app / "Contents/MacOS/CrayonBrowser"
        receipt.update(state="BUILD_VERIFIED_NOT_ACCEPTED", artifact=str(app),
                       executableSha256=hashlib.sha256(executable.read_bytes()).hexdigest())
        print(f"Local application: {app}", flush=True)
    except Exception as error:
        receipt.update(state="FAILED", error=str(error))
        raise
    finally:
        receipt["seconds"] = round(time.monotonic() - started, 2)
        receipt_path.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
