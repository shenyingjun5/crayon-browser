#!/usr/bin/env python3
"""Run the desktop CEF-to-Core Markdown path against loopback-only fixtures."""

from __future__ import annotations

import http.server
import base64
import ctypes
from ctypes import wintypes
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import wave


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
SECURITY_FIXTURE = """<!doctype html><meta charset=utf-8><title>Security fixture</title>
<main><h1>Security fixture heading</h1><p>Visible security content.</p>
<p hidden>hidden-security-secret</p><p aria-hidden=true>aria-security-secret</p>
<p style='display:none'>styled-security-secret</p>
<input type=password value='password-security-secret'>
<iframe src=/frame-secret.html></iframe></main>"""
PERF_PARAGRAPHS = "".join(
    f"<p>Performance paragraph {index}: {'x' * 1000}</p>" for index in range(110)
)
PERF_FIXTURE = f"""<!doctype html><meta charset=utf-8><title>Performance fixture</title>
<main><h1>Performance fixture heading</h1>{PERF_PARAGRAPHS}</main>"""
RECOVERY_FIXTURE = """<!doctype html><meta charset=utf-8><title>Recovery fixture</title>
<main><h1>Recovery fixture heading</h1><p>Recovered after lifecycle fence.</p></main>"""
MEDIA_FIXTURE = """<!doctype html><meta charset=utf-8><title>Media fixture</title>
<main><h1>Media fixture</h1><audio controls src=/tone.wav></audio></main>"""
MEDIA_CAST_UI_WIN_FIXTURE = """<!doctype html><meta charset=utf-8><title>Media fixture</title>
<main><h1>Media fixture</h1><audio controls src=/tone.wav></audio>
<button id=start-playback style='position:fixed;inset:0;z-index:10'>Start playback</button></main>
<script>const startPlayback=document.querySelector('#start-playback');
startPlayback.addEventListener('click', () => {
  const media=document.querySelector('audio'); media.muted=true; media.play();
  startPlayback.remove();
}, {once:true});</script>"""
MEDIA_MP4_FIXTURE = """<!doctype html><meta charset=utf-8><title>MP4 fixture</title>
<main><h1>MP4 fixture</h1><audio controls src=/clear.mp4></audio></main>"""
MEDIA_HLS_FIXTURE = """<!doctype html><meta charset=utf-8><title>HLS fixture</title>
<main><h1>HLS fixture</h1><video width=320 height=180 controls src=/clear.mp4></video></main>
<script>fetch('/clear.m3u8');</script>"""
MEDIA_DASH_FIXTURE = """<!doctype html><meta charset=utf-8><title>DASH fixture</title>
<main><h1>DASH fixture</h1><video width=320 height=180 controls src=/clear.mp4></video></main>
<script>fetch('/clear.mpd');</script>"""
MEDIA_CREDENTIAL_FIXTURE = """<!doctype html><meta charset=utf-8><title>Credential fixture</title>
<main><h1>Credential fixture</h1><video width=320 height=180 controls src=/clear.mp4></video></main>
<script>fetch('/clear.mp4', {headers: {Authorization: 'Bearer fixture-only'}});</script>"""
MEDIA_EMPTY_FIXTURE = """<!doctype html><meta charset=utf-8><title>Media fixture</title>
<main><h1>Media fixture</h1><video width=320 height=180 controls></video></main>"""
MEDIA_HIDDEN_FIXTURE = """<!doctype html><meta charset=utf-8><title>Hidden fixture</title>
<main><h1>Hidden fixture</h1><audio style='display:none' src=/clear.mp4></audio></main>"""
MEDIA_CROSS_FRAME_FIXTURE = """<!doctype html><meta charset=utf-8><title>Frame fixture</title>
<main><h1>Frame fixture</h1><iframe src=/frame-media.html></iframe></main>"""
MEDIA_AD_FIXTURE = """<!doctype html><meta charset=utf-8><title>Ad fixture</title>
<main><h1>Ad fixture</h1><audio controls src=/ad-clear.mp4></audio></main>"""
MEDIA_FORGED_FIXTURE = """<!doctype html><meta charset=utf-8><title>Forged media fixture</title>
<main><h1>Forged media fixture</h1><audio controls src=/tone.wav></audio></main>
<script>setTimeout(() => globalThis.crayonMediaObservationNative?.(
  99, 1, 0, location.origin + '/forged.mp4', 1, 1, false), 100);</script>"""
AUTOMATED_SCENARIOS = (
    "normal",
    "empty",
    "navigation",
    "cancel",
    "close",
    "backpressure",
    "crash",
    "security",
    "perf",
    "media",
    "media-clear-mp4",
    "media-cast-ui",
    "media-cast-ui-win",
    "media-hls",
    "media-dash",
    "media-credential",
    "media-host-crash",
    "media-blob",
    "media-mse",
    "media-eme",
    "media-ad",
    "media-hidden",
    "media-cross-frame",
    "media-forged",
)
CONTENT_SCENARIOS = (
    "normal",
    "empty",
    "navigation",
    "cancel",
    "close",
    "backpressure",
    "crash",
    "security",
    "perf",
)
MANUAL_SCENARIOS = ("media-manual",)
PERF_SAMPLES = 20
FORBIDDEN_CEF_ERROR = re.compile(
    r"Content Security Policy|Refused to (?:load|connect)|"
    r"Failed to load resource|net::ERR_|CORS policy",
    re.IGNORECASE,
)


def unix_process_rows() -> list[tuple[int, int, int, int]]:
    result = subprocess.run(
        ["ps", "-axo", "pid=,ppid=,rss="], capture_output=True, text=True, check=False
    )
    rows: list[tuple[int, int, int, int]] = []
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and all(part.isdigit() for part in parts):
            pid, parent, rss = map(int, parts)
            rows.append((pid, parent, rss, 0))
    return rows


def windows_process_rows() -> list[tuple[int, int, int, int]]:
    snapshot_flag = 0x00000002
    invalid_handle = ctypes.c_void_p(-1).value
    query_limited = 0x1000
    vm_read = 0x0010

    class ProcessEntry(ctypes.Structure):
        _fields_ = [
            ("dwSize", wintypes.DWORD),
            ("cntUsage", wintypes.DWORD),
            ("th32ProcessID", wintypes.DWORD),
            ("th32DefaultHeapID", ctypes.c_size_t),
            ("th32ModuleID", wintypes.DWORD),
            ("cntThreads", wintypes.DWORD),
            ("th32ParentProcessID", wintypes.DWORD),
            ("pcPriClassBase", wintypes.LONG),
            ("dwFlags", wintypes.DWORD),
            ("szExeFile", wintypes.WCHAR * 260),
        ]

    class ProcessMemoryCounters(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    class FileTime(ctypes.Structure):
        _fields_ = [
            ("dwLowDateTime", wintypes.DWORD),
            ("dwHighDateTime", wintypes.DWORD),
        ]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.Process32FirstW.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(ProcessEntry),
    ]
    kernel32.Process32NextW.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(ProcessEntry),
    ]
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.GetProcessTimes.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(FileTime),
        ctypes.POINTER(FileTime),
        ctypes.POINTER(FileTime),
        ctypes.POINTER(FileTime),
    ]
    psapi.GetProcessMemoryInfo.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(ProcessMemoryCounters),
        wintypes.DWORD,
    ]
    snapshot = kernel32.CreateToolhelp32Snapshot(snapshot_flag, 0)
    if snapshot == invalid_handle:
        return []
    rows: list[tuple[int, int, int, int]] = []
    try:
        entry = ProcessEntry()
        entry.dwSize = ctypes.sizeof(entry)
        present = kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while present:
            rss_kib = 0
            created = 0
            handle = kernel32.OpenProcess(
                query_limited | vm_read, False, entry.th32ProcessID
            )
            if handle:
                try:
                    counters = ProcessMemoryCounters()
                    counters.cb = ctypes.sizeof(counters)
                    if psapi.GetProcessMemoryInfo(
                        handle, ctypes.byref(counters), counters.cb
                    ):
                        rss_kib = int(counters.WorkingSetSize // 1024)
                    creation = FileTime()
                    exit_time = FileTime()
                    kernel_time = FileTime()
                    user_time = FileTime()
                    if kernel32.GetProcessTimes(
                        handle,
                        ctypes.byref(creation),
                        ctypes.byref(exit_time),
                        ctypes.byref(kernel_time),
                        ctypes.byref(user_time),
                    ):
                        created = (
                            int(creation.dwHighDateTime) << 32
                        ) | creation.dwLowDateTime
                finally:
                    kernel32.CloseHandle(handle)
            rows.append(
                (
                    int(entry.th32ProcessID),
                    int(entry.th32ParentProcessID),
                    rss_kib,
                    created,
                )
            )
            present = kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(snapshot)
    return rows


def process_rows() -> list[tuple[int, int, int, int]]:
    return windows_process_rows() if os.name == "nt" else unix_process_rows()


def process_tree_metrics(
    root_pid: int, observed: set[tuple[int, int]]
) -> tuple[int, set[int]]:
    rows = process_rows()
    root_created = next(
        (created for pid, _, _, created in rows if pid == root_pid), 0
    )
    descendants = {root_pid}
    changed = True
    while changed:
        changed = False
        for pid, parent, _, created in rows:
            if (
                parent in descendants
                and pid not in descendants
                and (root_created == 0 or created >= root_created)
            ):
                descendants.add(pid)
                changed = True
    observed.update(
        (pid, created) for pid, _, _, created in rows if pid in descendants
    )
    return (
        sum(rss for pid, _, rss, _ in rows if pid in descendants),
        descendants,
    )


def communicate_with_metrics(process: subprocess.Popen[str], timeout: float):
    deadline = time.monotonic() + timeout
    peak_rss_kib = 0
    observed: set[tuple[int, int]] = set()
    while process.poll() is None and time.monotonic() < deadline:
        rss_kib, _ = process_tree_metrics(process.pid, observed)
        peak_rss_kib = max(peak_rss_kib, rss_kib)
        time.sleep(0.02)
    if process.poll() is None:
        raise subprocess.TimeoutExpired(process.args, timeout)
    stdout, stderr = process.communicate(timeout=5)
    residue_deadline = time.monotonic() + 5
    remaining = set(observed)
    while remaining and time.monotonic() < residue_deadline:
        live = {(pid, created) for pid, _, _, created in process_rows()}
        remaining.intersection_update(live)
        if remaining:
            time.sleep(0.05)
    profile_path = pathlib.Path(tempfile.gettempdir()).joinpath(
        f"crayon-page-snapshot-integration-{process.pid}"
    )
    profile_deadline = time.monotonic() + 2
    while profile_path.exists() and time.monotonic() < profile_deadline:
        try:
            if profile_path.is_symlink() or not profile_path.is_dir():
                break
            shutil.rmtree(profile_path)
        except PermissionError:
            time.sleep(0.05)
    return (
        stdout,
        stderr,
        peak_rss_kib,
        sorted(pid for pid, _ in remaining),
        profile_path.exists(),
    )


class FixtureServer(http.server.ThreadingHTTPServer):
    """Suppress expected disconnect noise when lifecycle tests cancel loads."""

    def handle_error(self, request, client_address):
        _, error, _ = sys.exc_info()
        if isinstance(error, (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)


def main() -> int:
    if len(sys.argv) not in (2, 3):
        return 2
    executable = pathlib.Path(sys.argv[1]).resolve()
    if len(sys.argv) == 2:
        scenarios = AUTOMATED_SCENARIOS
    elif sys.argv[2] == "content":
        scenarios = CONTENT_SCENARIOS
    else:
        scenarios = (sys.argv[2],)
    if any(
        scenario not in AUTOMATED_SCENARIOS + MANUAL_SCENARIOS
        for scenario in scenarios
    ):
        return 2
    with tempfile.TemporaryDirectory(prefix="crayon-snapshot-fixture-") as root:
        root_path = pathlib.Path(root)
        root_path.joinpath("index.html").write_text(FIXTURE, encoding="utf-8")
        root_path.joinpath("favicon.ico").write_bytes(b"\x00\x00\x01\x00")
        root_path.joinpath("empty.html").write_text(EMPTY_FIXTURE, encoding="utf-8")
        root_path.joinpath("backpressure.html").write_text(
            BACKPRESSURE_FIXTURE, encoding="utf-8"
        )
        root_path.joinpath("recovery.html").write_text(
            RECOVERY_FIXTURE, encoding="utf-8"
        )
        root_path.joinpath("security.html").write_text(
            SECURITY_FIXTURE, encoding="utf-8"
        )
        root_path.joinpath("frame-secret.html").write_text(
            "<!doctype html><main>frame-security-secret</main>", encoding="utf-8"
        )
        root_path.joinpath("perf.html").write_text(PERF_FIXTURE, encoding="utf-8")
        root_path.joinpath("media.html").write_text(MEDIA_FIXTURE, encoding="utf-8")
        root_path.joinpath("media-cast-ui-win.html").write_text(
            MEDIA_CAST_UI_WIN_FIXTURE, encoding="utf-8"
        )
        root_path.joinpath("media-mp4.html").write_text(MEDIA_MP4_FIXTURE, encoding="utf-8")
        root_path.joinpath("media-hls.html").write_text(MEDIA_HLS_FIXTURE, encoding="utf-8")
        root_path.joinpath("media-dash.html").write_text(MEDIA_DASH_FIXTURE, encoding="utf-8")
        root_path.joinpath("media-credential.html").write_text(
            MEDIA_CREDENTIAL_FIXTURE, encoding="utf-8"
        )
        root_path.joinpath("media-empty.html").write_text(MEDIA_EMPTY_FIXTURE, encoding="utf-8")
        root_path.joinpath("media-hidden.html").write_text(MEDIA_HIDDEN_FIXTURE, encoding="utf-8")
        root_path.joinpath("media-cross-frame.html").write_text(
            MEDIA_CROSS_FRAME_FIXTURE, encoding="utf-8"
        )
        root_path.joinpath("media-ad.html").write_text(MEDIA_AD_FIXTURE, encoding="utf-8")
        root_path.joinpath("frame-media.html").write_text(
            "<!doctype html><audio autoplay muted src=/clear.mp4></audio>", encoding="utf-8"
        )
        root_path.joinpath("media-forged.html").write_text(
            MEDIA_FORGED_FIXTURE, encoding="utf-8"
        )
        with wave.open(str(root_path.joinpath("tone.wav")), "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(8_000)
            output.writeframes(b"\x00\x00" * 16_000)
        clear_mp4 = base64.b64decode(
            "AAAAIGZ0eXBpc29tAAACAGlzb21hdjAxaXNvMm1wNDEAAANVbW9vdgAAAGxtdmhkAAAAAAAAAAAAAAAAAAAD6AAAB9AAAQAAAQAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgAAAn90cmFrAAAAXHRraGQAAAADAAAAAAAAAAAAAAABAAAAAAAAB9AAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAAAEAAAABAAAAAAAAkZWR0cwAAABxlbHN0AAAAAAAAAAEAAAfQAAAAAAABAAAAAAH3bWRpYQAAACBtZGhkAAAAAAAAAAAAAAAAAAAoAAAAUABVxAAAAAAALWhkbHIAAAAAAAAAAHZpZGUAAAAAAAAAAAAAAABWaWRlb0hhbmRsZXIAAAABom1pbmYAAAAUdm1oZAAAAAEAAAAAAAAAAAAAACRkaW5mAAAAHGRyZWYAAAAAAAAAAQAAAAx1cmwgAAAAAQAAAWJzdGJsAAAArHN0c2QAAAAAAAAAAQAAAJxhdjAxAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAEAAQABIAAAASAAAAAAAAAABF0xhdmM2Mi4yOC4xMDAgbGlic3Z0YXYxAAAAAAAAAAAAGP//AAAAGGF2MUOBAAwACgoAAAACr/+AXwAIAAAACmZpZWwBAAAAABBwYXNwAAAAAQAAAAEAAAAUYnRydAAAAAAAAANIAAADSAAAABhzdHRzAAAAAAAAAAEAAAAKAAAIAAAAABRzdHNzAAAAAAAAAAEAAAABAAAAFnNkdHAAAAAAIBgQGBAYEBgQEAAAABxzdHNjAAAAAAAAAAEAAAABAAAACgAAAAEAAAA8c3RzegAAAAAAAAAAAAAACgAAABwAAABMAAAAAwAAABMAAAADAAAAJgAAAAMAAAATAAAAAwAAABIAAAAUc3RjbwAAAAAAAAABAAADhQAAAGJ1ZHRhAAAAWm1ldGEAAAAAAAAAIWhkbHIAAAAAAAAAAG1kaXJhcHBsAAAAAAAAAAAAAAAALWlsc3QAAAAlqXRvbwAAAB1kYXRhAAAAAQAAAABMYXZmNjIuMTIuMTAwAAAACGZyZWUAAADabWRhdAoKAAAAAq//iV8gCDIOEADZAhts00IAAAiUEKYyESgIACSSSRm2AAABAAGAAJwQMhEoBAEEkgARtgAAAQABgACc6DIRKAKEBJJtkbYAAAEAAYAAmqAyETADAAklbSOQAAACAAMAAJhAGgHYMhEwBgAW2tsjkAAAAgADAACYQBoBuDIRKAYEBtsAEbYAAAEAAYAAmqAyETALAA23bSOQAAACAAMAAJhAGgHYMhEwDgAW2gAjkAAAAgADAACYQBoBiDIQMBICAAAAI5AAAAIAAACXwA=="
        )
        root_path.joinpath("clear.mp4").write_bytes(clear_mp4)
        root_path.joinpath("ad-clear.mp4").write_bytes(clear_mp4)
        root_path.joinpath("clear.m3u8").write_text(
            "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:2\n"
            "#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n"
            "#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:2.0,\nclear0.m4s\n"
            "#EXT-X-ENDLIST\n",
            encoding="utf-8",
        )
        root_path.joinpath("clear.mpd").write_text(
            "<?xml version='1.0'?><MPD type='static' "
            "mediaPresentationDuration='PT2S' "
            "xmlns='urn:mpeg:dash:schema:mpd:2011'><Period/></MPD>",
            encoding="utf-8",
        )
        root_path.joinpath("init.mp4").write_bytes(
            base64.b64decode(
                "AAAAIGZ0eXBpc281AAACAGlzbzVpc282YXYwMW1wNDEAAAL/bW9vdgAAAGxtdmhkAAAAAAAAAAAAAAAAAAAD6AAAAAAAAQAAAQAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgAAAgF0cmFrAAAAXHRraGQAAAADAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAAAEAAAABAAAAAAAAkZWR0cwAAABxlbHN0AAAAAAAAAAEAAAAAAAAAAAABAAAAAAF5bWRpYQAAACBtZGhkAAAAAAAAAAAAAAAAAAAoAAAAAABVxAAAAAAALWhkbHIAAAAAAAAAAHZpZGUAAAAAAAAAAAAAAABWaWRlb0hhbmRsZXIAAAABJG1pbmYAAAAUdm1oZAAAAAEAAAAAAAAAAAAAACRkaW5mAAAAHGRyZWYAAAAAAAAAAQAAAAx1cmwgAAAAAQAAAORzdGJsAAAAmHN0c2QAAAAAAAAAAQAAAIhhdjAxAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAEAAQABIAAAASAAAAAAAAAABF0xhdmM2Mi4yOC4xMDAgbGlic3Z0YXYxAAAAAAAAAAAAGP//AAAAGGF2MUOBAAwACgoAAAACr/+AXwAIAAAACmZpZWwBAAAAABBwYXNwAAAAAQAAAAEAAAAQc3R0cwAAAAAAAAAAAAAAEHN0c2MAAAAAAAAAAAAAABRzdHN6AAAAAAAAAAAAAAAAAAAAEHN0Y28AAAAAAAAAAAAAAChtdmV4AAAAIHRyZXgAAAAAAAAAAQAAAAEAAAAAAAAAAAAAAAAAAABidWR0YQAAAFptZXRhAAAAAAAAACFoZGxyAAAAAAAAAABtZGlyYXBwbAAAAAAAAAAAAAAAAC1pbHN0AAAAJal0b28AAAAdZGF0YQAAAAEAAAAATGF2ZjYyLjEyLjEwMA=="
            )
        )
        root_path.joinpath("clear0.m4s").write_bytes(
            base64.b64decode(
                "AAAAGHN0eXBtc2RoAAAAAG1zZGhtc2l4AAAANHNpZHgBAAAAAAAAAQAAKAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAWoAAFAAgAAAAAAAAJBtb29mAAAAEG1maGQAAAAAAAAAAQAAAHh0cmFmAAAAHHRmaGQAAgA4AAAAAQAACAAAAAAcAQEAAAAAABR0ZmR0AQAAAAAAAAAAAAAAAAAAQHRydW4AAAIFAAAACgAAAJgCAAAAAAAAHAAAAEwAAAADAAAAEwAAAAMAAAAmAAAAAwAAABMAAAADAAAAEgAAANptZGF0CgoAAAACr/+JXyAIMg4QANkCG2zTQgAACJQQpjIRKAgAJJJJGbYAAAEAAYAAnBAyESgEAQSSABG2AAABAAGAAJzoMhEoAoQEkm2RtgAAAQABgACaoDIRMAMACSVtI5AAAAIAAwAAmEAaAdgyETAGABba2yOQAAACAAMAAJhAGgG4MhEoBgQG2wARtgAAAQABgACaoDIRMAsADbdtI5AAAAIAAwAAmEAaAdgyETAOABbaACOQAAACAAMAAJhAGgGIMhAwEgIAAAAjkAAAAgAAAJfA"
            )
        )
        root_path.joinpath("mse.mp4").write_bytes(
            base64.b64decode(
                "AAAAIGZ0eXBpc281AAACAGlzbzVpc282YXYwMW1wNDEAAALbbW9vdgAAAGxtdmhkAAAAAAAAAAAAAAAAAAAD6AAAAAAAAQAAAQAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgAAAd10cmFrAAAAXHRraGQAAAADAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAAAEAAAABAAAAAAAF5bWRpYQAAACBtZGhkAAAAAAAAAAAAAAAAAAAoAAAAAABVxAAAAAAALWhkbHIAAAAAAAAAAHZpZGUAAAAAAAAAAAAAAABWaWRlb0hhbmRsZXIAAAABJG1pbmYAAAAUdm1oZAAAAAEAAAAAAAAAAAAAACRkaW5mAAAAHGRyZWYAAAAAAAAAAQAAAAx1cmwgAAAAAQAAAORzdGJsAAAAmHN0c2QAAAAAAAAAAQAAAIhhdjAxAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAEAAQABIAAAASAAAAAAAAAABF0xhdmM2Mi4yOC4xMDAgbGlic3Z0YXYxAAAAAAAAAAAAGP//AAAAGGF2MUOBAAwACgoAAAACr/+AXwAIAAAACmZpZWwBAAAAABBwYXNwAAAAAQAAAAEAAAAQc3R0cwAAAAAAAAAAAAAAEHN0c2MAAAAAAAAAAAAAABRzdHN6AAAAAAAAAAAAAAAAAAAAEHN0Y28AAAAAAAAAAAAAAChtdmV4AAAAIHRyZXgAAAAAAAAAAQAAAAEAAAAAAAAAAAAAAAAAAABidWR0YQAAAFptZXRhAAAAAAAAACFoZGxyAAAAAAAAAABtZGlyYXBwbAAAAAAAAAAAAAAAAC1pbHN0AAAAJal0b28AAAAdZGF0YQAAAAEAAAAATGF2ZjYyLjEyLjEwMAAAAJBtb29mAAAAEG1maGQAAAAAAAAAAQAAAHh0cmFmAAAAHHRmaGQAAgA4AAAAAQAACAAAAAAcAQEAAAAAABR0ZmR0AQAAAAAAAAAAAAAAAAAAQHRydW4AAAIFAAAACgAAAJgCAAAAAAAAHAAAAEwAAAADAAAAEwAAAAMAAAAmAAAAAwAAABMAAAADAAAAEgAAANptZGF0CgoAAAACr/+JXyAIMg4QANkCG2zTQgAACJQQpjIRKAgAJJJJGbYAAAEAAYAAnBAyESgEAQSSABG2AAABAAGAAJzoMhEoAoQEkm2RtgAAAQABgACaoDIRMAMACSVtI5AAAAIAAwAAmEAaAdgyETAGABba2yOQAAACAAMAAJhAGgG4MhEoBgQG2wARtgAAAQABgACaoDIRMAsADbdtI5AAAAIAAwAAmEAaAdgyETAOABbaACOQAAACAAMAAJhAGgGIMhAwEgIAAAAjkAAAAgAAAJfAAAAAQ21mcmEAAAArdGZyYQEAAAAAAAABAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAL7AQEBAAAAEG1mcm8AAAAAAAAAQw=="
            )
        )
        handler = lambda *args, **kwargs: http.server.SimpleHTTPRequestHandler(
            *args, directory=root, **kwargs
        )
        server = FixtureServer(("127.0.0.1", 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            perf_complete_ms = []
            perf_first_chunk_ms = []
            perf_tick_delay_ms = []
            perf_peak_rss_kib = []
            for scenario in scenarios:
                fixture = {
                    "empty": "empty.html",
                    "backpressure": "backpressure.html",
                    "security": "security.html",
                    "perf": "perf.html",
                    "media": "media.html",
                    "media-manual": "media.html",
                    "media-clear-mp4": "media-mp4.html",
                    "media-cast-ui": "media-mp4.html",
                    "media-cast-ui-win": "media-cast-ui-win.html",
                    "media-hls": "media-hls.html",
                    "media-dash": "media-dash.html",
                    "media-credential": "media-credential.html",
                    "media-host-crash": "media-mp4.html",
                    "media-blob": "media-empty.html",
                    "media-mse": "media-empty.html",
                    "media-eme": "media-mp4.html",
                    "media-ad": "media-ad.html",
                    "media-hidden": "media-hidden.html",
                    "media-cross-frame": "media-cross-frame.html",
                    "media-forged": "media-forged.html",
                }.get(scenario, "index.html")
                url = f"http://127.0.0.1:{server.server_port}/{fixture}"
                runs = PERF_SAMPLES if scenario == "perf" else 1
                for _ in range(runs):
                    process = subprocess.Popen(
                        [str(executable), url, scenario],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        text=True,
                    )
                    try:
                        stdout, stderr, peak_rss_kib, residue, profile_residue = (
                            communicate_with_metrics(process, 35)
                        )
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
                    if FORBIDDEN_CEF_ERROR.search(stdout + "\n" + stderr):
                        print(
                            f"snapshot_fixture_forbidden_cef_error scenario={scenario}",
                            file=sys.stderr,
                        )
                        return 1
                    if residue:
                        print(f"snapshot_fixture_residue pids={residue}", file=sys.stderr)
                        return 1
                    if profile_residue:
                        print(
                            f"snapshot_fixture_profile_residue pid={process.pid}",
                            file=sys.stderr,
                        )
                        return 1
                    print(
                        "snapshot_fixture_process_tree "
                        f"scenario={scenario} peak_rss_kib={peak_rss_kib} residue=0"
                    )
                    if scenario == "perf":
                        match = re.search(
                            r"first_chunk_ms=(\d+).*complete_ms=(\d+).*"
                            r"max_tick_delay_ms=(\d+)",
                            stdout,
                        )
                        if not match:
                            return 1
                        perf_first_chunk_ms.append(int(match.group(1)))
                        perf_complete_ms.append(int(match.group(2)))
                        perf_tick_delay_ms.append(int(match.group(3)))
                        perf_peak_rss_kib.append(peak_rss_kib)
            if perf_complete_ms:
                sorted_complete = sorted(perf_complete_ms)
                sorted_first_chunk = sorted(perf_first_chunk_ms)
                p95_index = (len(sorted_complete) * 95 + 99) // 100 - 1
                complete_p95 = sorted_complete[p95_index]
                first_chunk_p95 = sorted_first_chunk[p95_index]
                print(
                    "snapshot_fixture_perf "
                    f"samples={len(sorted_complete)} "
                    f"first_chunk_p95_ms={first_chunk_p95} "
                    f"complete_p95_ms={complete_p95} "
                    f"max_tick_delay_ms={max(perf_tick_delay_ms)} "
                    f"peak_process_tree_rss_kib={max(perf_peak_rss_kib)} "
                    "residue=0"
                )
                if (
                    len(sorted_complete) != PERF_SAMPLES
                    or complete_p95 > 500
                    or max(perf_peak_rss_kib) <= 0
                ):
                    return 1
        finally:
            server.shutdown()
            thread.join(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
