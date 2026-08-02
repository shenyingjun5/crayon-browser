#!/usr/bin/env python3
"""L2 demo 验证用静态服务：托管 JS 动态加载视频的测试页（127.0.0.1:8890）。

页面：
  /a.html — 加载 2s 后 JS 动态创建 <video src=公网mp4>（验证 media src / MutationObserver 抓取）
  /b.html — hls.js 动态加载公网 m3u8（验证 XHR/fetch hook 抓取）
"""
import http.server
import functools

MP4 = "https://www.w3schools.com/html/mov_bbb.mp4"
M3U8 = "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8"

PAGE_A = f"""<!DOCTYPE html><html><head><meta charset="utf-8"><title>dynamic mp4</title></head>
<body><p>2s 后动态插入 video</p><script>
setTimeout(() => {{
  const v = document.createElement('video');
  v.controls = true;
  v.src = '{MP4}';
  document.body.appendChild(v);
  v.play().catch(() => {{}});
}}, 2000);
</script></body></html>"""

PAGE_B = f"""<!DOCTYPE html><html><head><meta charset="utf-8"><title>dynamic hls</title>
<script src="https://cdn.jsdelivr.net/npm/hls.js@1"></script></head>
<body><p>hls.js 动态加载</p><video id="v" controls muted style="width:480px"></video><script>
setTimeout(() => {{
  if (Hls.isSupported()) {{
    const h = new Hls();
    h.loadSource('{M3U8}');
    h.attachMedia(document.getElementById('v'));
    document.getElementById('v').play().catch(() => {{}});
  }}
}}, 1500);
</script></body></html>"""

# /c.html — 媒体请求发生在 Web Worker 内（验证 Worker hook 抓取，
# 模拟央视频 cmg.worker.js 这类在 worker 里拉流的站点）
PAGE_C = """<!DOCTYPE html><html><head><meta charset="utf-8"><title>worker hls</title></head>
<body><p>Worker 内拉流</p><script>
setTimeout(() => { new Worker('/worker.js'); }, 1000);
</script></body></html>"""

WORKER_JS = f"""// worker 内 fetch m3u8：主线程五路 hook 均不可见，只有 Worker hook 能抓到
fetch('{M3U8}').then(r => r.text()).then(() => {{}}).catch(() => {{}});
"""


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = None
        content_type = "text/html; charset=utf-8"
        if self.path.startswith("/a.html"):
            body = PAGE_A.encode()
        elif self.path.startswith("/b.html"):
            body = PAGE_B.encode()
        elif self.path.startswith("/c.html"):
            body = PAGE_C.encode()
        elif self.path.startswith("/worker.js"):
            body = WORKER_JS.encode()
            content_type = "application/javascript; charset=utf-8"
        if body is None:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    print("test pages: http://127.0.0.1:8890/a.html  http://127.0.0.1:8890/b.html")
    http.server.HTTPServer(("127.0.0.1", 8890), Handler).serve_forever()
