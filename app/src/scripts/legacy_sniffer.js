
(() => {
  if (window.__getVideoSniff) return;
  window.__getVideoSniff = true;
  const RE = /\.(m3u8|mp4|mpd)(\?|#|$)/i;
  const seen = new Set();
  function abs(u) {
    try { return new URL(u, location.href).href; } catch (e) { return null; }
  }
  // force=true：凭内容判定为 m3u8（响应体以 #EXTM3U 开头），URL 无扩展名也收
  function report(u, force) {
    try {
      if (!u || typeof u !== 'string') return;
      u = abs(u);
      if (!u || !/^https?:\/\//.test(u) || seen.has(u)) return;
      if (!force && !RE.test(u)) return;
      // DASH/HLS 的 init 段（_init.mp4）不是独立可播流，过滤（1905 实测）
      if (/_init\.mp4(\?|#|$)/i.test(u)) return;
      seen.add(u);
      const proto = force ? 'hls' : undefined;
      const payload = JSON.stringify({ url: u, page: location.href, proto });
      // 通道 0：iframe 内 → postMessage 给顶层框架转发（Tauri IPC 只注主框架，
      // beacon 走 http 在 https 页里是混合内容可能被拦，iframe 内两条都靠不住）
      try {
        if (window !== window.top) {
          window.top.postMessage({ __gvSniff: { url: u, page: location.href, proto } }, '*');
        }
      } catch (e) {}
      // 通道 1：Tauri IPC event
      try {
        if (window.__TAURI__ && window.__TAURI__.event) {
          window.__TAURI__.event.emit('sniff-found', { url: u, page: location.href, proto });
        }
      } catch (e) {}
      // 通道 2：Image beacon 兜底（无 CORS 预检）
      try { new Image().src = 'http://127.0.0.1:8377/sniff?data=' + encodeURIComponent(payload); } catch (e) {}
    } catch (e) {}
  }
  // 顶层框架：接收 iframe postMessage 上来的命中并代为上报
  try {
    if (window === window.top) {
      window.addEventListener('message', (ev) => {
        try {
          const d = ev.data && ev.data.__gvSniff;
          if (d && d.url) report(d.url, d.proto === 'hls');
        } catch (e) {}
      });
    }
  } catch (e) {}
  // 响应体以 #EXTM3U 开头 → 按内容判定为 HLS（kazumi 思路：hook fetch/XHR 看响应体，
  // 抓不走 video 标签、URL 无 .m3u8 扩展名的清单接口）。只读首块即断流，避免大文件。
  function checkM3u8Body(u, getReader) {
    try {
      const rd = getReader();
      if (!rd) return;
      rd.read().then(({ value }) => {
        try { rd.cancel(); } catch (e) {}
        try {
          if (!value || !value.length) return;
          const head = new TextDecoder().decode(value.slice(0, 4096)).trimStart();
          if (head.startsWith('#EXTM3U')) report(u, true);
        } catch (e) {}
      }).catch(() => {});
    } catch (e) {}
  }
  // 嵌套解析页：iframe src 的 query 里藏着真实流地址（url=xxx.m3u8 模式，
  // 可能再 percent-encode 一层），正则抠出明文与编码两种形态。
  const NEST_RE = /https?:\/\/[^\s"'<>]+?\.(?:m3u8|mp4|mpd)(?:\?[^\s"'<>]*)?/gi;
  const NEST_ENC_RE = /https?%3A%2F%2F[^\s"'<>]+?\.(?:m3u8|mp4|mpd)(?:%3F[^\s"'<>]*)?/gi;
  function digIframe(u) {
    try {
      if (!u || typeof u !== 'string') return;
      for (const m of u.match(NEST_RE) || []) report(m);
      for (const m of u.match(NEST_ENC_RE) || []) {
        try { report(decodeURIComponent(m)); } catch (e) {}
      }
    } catch (e) {}
  }
  // hook fetch：URL 匹配即报；同时克隆响应读首块，响应体是 m3u8 的也报
  const origFetch = window.fetch;
  if (origFetch) {
    window.fetch = function (input, init) {
      const reqUrl = typeof input === 'string' ? input : (input && input.url);
      try { report(reqUrl); } catch (e) {}
      const p = origFetch.apply(this, arguments);
      try {
        p.then((resp) => {
          try {
            if (!resp || !resp.clone) return;
            const c = resp.clone();
            if (!c.body || !c.body.getReader) return;
            checkM3u8Body(resp.url || reqUrl, () => c.body.getReader());
          } catch (e) {}
        }).catch(() => {});
      } catch (e) {}
      return p;
    };
  }
  // hook XHR：open 记 URL，load 时查 responseText 是否 m3u8 清单
  const origOpen = XMLHttpRequest.prototype.open;
  XMLHttpRequest.prototype.open = function (method, url) {
    try { report(url); } catch (e) {}
    try { this.__gvUrl = url; } catch (e) {}
    return origOpen.apply(this, arguments);
  };
  try {
    const origSend = XMLHttpRequest.prototype.send;
    XMLHttpRequest.prototype.send = function () {
      try {
        this.addEventListener('load', function () {
          try {
            // responseType 非文本时取 responseText 会抛，直接跳过
            const t = this.responseText;
            if (typeof t === 'string' && t.slice(0, 65536).trimStart().startsWith('#EXTM3U')) {
              report(this.__gvUrl || this.responseURL, true);
            }
          } catch (e) {}
        });
      } catch (e) {}
      return origSend.apply(this, arguments);
    };
  } catch (e) {}
  // hook HTMLMediaElement.src setter
  try {
    const desc = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, 'src');
    if (desc && desc.set) {
      Object.defineProperty(HTMLMediaElement.prototype, 'src', {
        configurable: true,
        get: desc.get,
        set(v) { try { report(v); } catch (e) {} return desc.set.call(this, v); }
      });
    }
  } catch (e) {}
  // MutationObserver：<video>/<source> 的 src/data-src 变化与新增节点；
  // <iframe> src 变化时抠 query 里的嵌套流地址
  try {
    const scanEl = (el) => {
      if (!el || !el.getAttribute) return;
      if (el.tagName !== 'IFRAME') {
        for (const a of ['src', 'data-src']) { const v = el.getAttribute(a); if (v) report(v); }
      }
      if (el.tagName === 'IFRAME') digIframe(el.getAttribute('src'));
      if (el.querySelectorAll) {
        for (const n of el.querySelectorAll('video,source')) {
          for (const a of ['src', 'data-src']) { const v = n.getAttribute(a); if (v) report(v); }
        }
        for (const f of el.querySelectorAll('iframe')) digIframe(f.getAttribute('src'));
      }
    };
    new MutationObserver((muts) => {
      for (const m of muts) {
        if (m.type === 'attributes') scanEl(m.target);
        for (const n of m.addedNodes) scanEl(n);
      }
    }).observe(document.documentElement || document, {
      subtree: true, childList: true, attributes: true, attributeFilter: ['src', 'data-src']
    });
  } catch (e) {}
  // PerformanceObserver（resource timing）兜底
  try {
    new PerformanceObserver((list) => {
      for (const e of list.getEntries()) report(e.name);
    }).observe({ type: 'resource', buffered: true });
  } catch (e) {}
  // Worker hook：包装 Worker 构造器，往 classic worker 脚本前注入嗅探 shim——
  // 覆盖央视频这类在 Worker 内 fetch/XHR 拉流的站点（主线程 hook 与
  // PerformanceObserver 都看不到 dedicated worker 内的请求）。
  // module worker 与 blob: 脚本不包装（importScripts 方案不适用），异常回退原构造器。
  try {
    const OrigWorker = window.Worker;
    if (OrigWorker) {
      // worker 内 shim：hook fetch/XHR，命中 postMessage 回主线程（复用主线程双通道上报）。
      // __BASE__ 占位符替换为原始脚本地址，保持 worker 内相对 URL 解析基准不变。
      const SHIM = `
var __sniffBase='__BASE__';
(function(){
  const RE=/\\.(m3u8|mp4|mpd)(\\?|#|$)/i;
  const seen=new Set();
  function abs(u){try{return new URL(u,__sniffBase).href;}catch(e){return null;}}
  function report(u){try{
    if(!u||typeof u!=='string')return;
    u=abs(u);
    if(!u||!/^https?:\\/\\//.test(u)||!RE.test(u)||seen.has(u))return;
    seen.add(u);
    postMessage({__getVideoSniff:u});
  }catch(e){}}
  const of=self.fetch;
  if(of){self.fetch=function(input,init){try{report(typeof input==='string'?input:(input&&input.url));}catch(e){}return of.apply(this,arguments);};}
  if(self.XMLHttpRequest){
    const oo=self.XMLHttpRequest.prototype.open;
    self.XMLHttpRequest.prototype.open=function(m,u){try{report(u);}catch(e){}return oo.apply(this,arguments);};
  }
})();
`;
      window.Worker = function (scriptURL, options) {
        try {
          if (options && options.type === 'module') throw 0;
          const abs = new URL(scriptURL, location.href).href;
          if (!/^https?:\/\//.test(abs)) throw 0;
          const src = SHIM.replace('__BASE__', abs.replace(/\\/g, '\\\\').replace(/'/g, "\\'"))
            + '\ntry{importScripts(' + JSON.stringify(abs) + ');}catch(e){}\n';
          const w = new OrigWorker(
            URL.createObjectURL(new Blob([src], { type: 'application/javascript' })),
            options
          );
          w.addEventListener('message', (ev) => {
            try {
              const u = ev.data && ev.data.__getVideoSniff;
              if (u) report(u);
            } catch (e) {}
          });
          return w;
        } catch (e) {
          return new OrigWorker(scriptURL, options);
        }
      };
      window.Worker.prototype = OrigWorker.prototype;
    }
  } catch (e) {}
  // 已有的 video/source/iframe
  try {
    for (const n of document.querySelectorAll('video,source')) {
      report(n.src || n.getAttribute('data-src'));
    }
    for (const f of document.querySelectorAll('iframe')) digIframe(f.getAttribute('src'));
  } catch (e) {}
})();
