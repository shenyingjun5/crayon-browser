#!/usr/bin/env python3
"""Run the macOS CEF-to-Core Markdown path against loopback-only fixtures."""

from __future__ import annotations

import http.server
import base64
import pathlib
import subprocess
import sys
import tempfile
import threading
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
RECOVERY_FIXTURE = """<!doctype html><meta charset=utf-8><title>Recovery fixture</title>
<main><h1>Recovery fixture heading</h1><p>Recovered after lifecycle fence.</p></main>"""
MEDIA_FIXTURE = """<!doctype html><meta charset=utf-8><title>Media fixture</title>
<main><h1>Media fixture</h1><audio controls src=/tone.wav></audio></main>"""
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
    "media",
    "media-clear-mp4",
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
MANUAL_SCENARIOS = ("media-manual",)


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
    scenarios = AUTOMATED_SCENARIOS if len(sys.argv) == 2 else (sys.argv[2],)
    if any(scenario not in AUTOMATED_SCENARIOS + MANUAL_SCENARIOS for scenario in scenarios):
        return 2
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
        root_path.joinpath("media.html").write_text(MEDIA_FIXTURE, encoding="utf-8")
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
            for scenario in scenarios:
                fixture = {
                    "empty": "empty.html",
                    "backpressure": "backpressure.html",
                    "media": "media.html",
                    "media-manual": "media.html",
                    "media-clear-mp4": "media-mp4.html",
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
