#!/usr/bin/env node
// MDV-19/20: deterministic real-CEF Mermaid performance/lifecycle harness.
// The caller launches a local Debug/Release bundle with a loopback DevTools
// port. This script never opens a public URL and emits counts/metrics only.

const portArgument = process.argv.find((argument) =>
  argument.startsWith("--port="));
const port = Number(portArgument?.slice("--port=".length) ?? "9333");
if (!Number.isSafeInteger(port) || port < 1024 || port > 65535) {
  throw new Error("invalid --port");
}

const pages = await (await fetch(
  `http://127.0.0.1:${port}/json/list`)).json();
if (!Array.isArray(pages) || pages.length === 0) throw new Error("no CEF page");
const socket = new WebSocket(pages[0].webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  socket.onopen = resolve;
  socket.onerror = reject;
});

let sequence = 0;
const pending = new Map();
const networkEvents = [];
socket.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.method?.startsWith("Network.")) networkEvents.push(message);
  if (!message.id || !pending.has(message.id)) return;
  const callback = pending.get(message.id);
  pending.delete(message.id);
  if (message.error) callback.reject(new Error(JSON.stringify(message.error)));
  else callback.resolve(message.result);
};

function call(method, params = {}) {
  const id = ++sequence;
  socket.send(JSON.stringify({id, method, params}));
  return new Promise((resolve, reject) => pending.set(id, {resolve, reject}));
}

const delay = (milliseconds) => new Promise((resolve) =>
  setTimeout(resolve, milliseconds));
await call("Page.enable");
await call("Performance.enable");
await call("Network.enable");
await call("Page.navigate", {url: "crayon://newtab/"});
await delay(300);
const ordinaryRequestBoundary = networkEvents.length;
await call("Page.navigate", {url: "crayon://mdv/"});
await delay(800);

const diagrams = [
  "flowchart LR\n  A --> B",
  "sequenceDiagram\n  Alice->>Bob: hello",
  "classDiagram\n  Animal <|-- Duck",
  "stateDiagram-v2\n  [*] --> Ready",
  "erDiagram\n  USER ||--o{ ORDER : places"
];
const expression = `
(async function(){
  const diagrams=${JSON.stringify(diagrams)};
  const paints=performance.getEntriesByType('paint').map((entry)=>({
    name:entry.name,startTime:entry.startTime}));
  const escape=(value)=>value.replaceAll('&','&amp;').replaceAll('<','&lt;')
    .replaceAll('>','&gt;');
  const markup=(count,storm)=>Array.from({length:count},(_,index)=>{
    let source=diagrams[index%diagrams.length];
    if(index%17===16) source='not-a-diagram';
    if(index===49) source='flowchart LR\\n  A --> '+('B'.repeat(70*1024));
    const id=(storm?'a':'b')+index.toString(16).padStart(31,'0');
    return '<pre><code data-mdv-mermaid="true" data-mdv-node="'+id+'">'+
      escape(source)+'</code></pre>';
  }).join('');
  mdvPush({preview:markup(20,true)});
  document.querySelector('code[data-mdv-mermaid]')?.scrollIntoView();
  await new Promise((resolve)=>setTimeout(resolve,30));
  const start=performance.now();
  let maxDelay=0,lastTick=performance.now();
  const timer=setInterval(()=>{const now=performance.now();maxDelay=Math.max(
    maxDelay,now-lastTick-20);lastTick=now;},20);
  mdvPush({preview:markup(50,false)});
  const nodes=[...document.querySelectorAll('code[data-mdv-mermaid]')];
  let firstDiagramMs=null;
  for(const node of nodes){
    node.scrollIntoView({block:'center'});
    const deadline=performance.now()+10000;
    while(node.getAttribute('data-mdv-mermaid-rendered')!=='true'&&
          node.getAttribute('data-mdv-mermaid-error')!=='true'&&
          performance.now()<deadline){
      await new Promise((resolve)=>setTimeout(resolve,16));
    }
    if(firstDiagramMs===null&&node.getAttribute('data-mdv-mermaid-rendered')===
       'true') firstDiagramMs=performance.now()-start;
  }
  clearInterval(timer);
  const adapter=await import('/runtime/mermaid/adapter');
  const stats=adapter.mermaidSessionStats();
  const rendered=nodes.filter((node)=>node.getAttribute(
    'data-mdv-mermaid-rendered')==='true').length;
  const errors=nodes.filter((node)=>node.getAttribute(
    'data-mdv-mermaid-error')==='true').length;
  const unresolved=nodes.map((node,index)=>({index,loading:node.getAttribute(
    'data-mdv-mermaid-loading'),rendered:node.getAttribute(
    'data-mdv-mermaid-rendered'),error:node.getAttribute(
    'data-mdv-mermaid-error')})).filter((item)=>
      item.rendered!=='true'&&item.error!=='true');
  window.dispatchEvent(new Event('memorypressure'));
  await new Promise((resolve)=>setTimeout(resolve,0));
  const memoryPressureStats=adapter.mermaidSessionStats();
  return {blockCount:nodes.length,rendered,errors,unresolved,
    firstDiagramMs,allVisitedMs:performance.now()-start,
    maxUiDelayMs:maxDelay,paints,stats,memoryPressureStats};
})()`;
const evaluation = await call("Runtime.evaluate", {
  expression, awaitPromise: true, returnByValue: true
});
if (evaluation.exceptionDetails) {
  throw new Error(JSON.stringify(evaluation.exceptionDetails));
}
const metrics = await call("Performance.getMetrics");

const requestById = new Map();
for (const event of networkEvents) {
  if (event.method === "Network.requestWillBeSent") {
    requestById.set(event.params.requestId, event.params.request.url);
  }
}
const requestUrls = [...requestById.values()];
const ordinaryUrls = networkEvents.slice(0, ordinaryRequestBoundary)
  .filter((event) => event.method === "Network.requestWillBeSent")
  .map((event) => event.params.request.url);
const mermaidUrls = requestUrls.filter((url) =>
  url.includes("/runtime/mermaid/"));
const mermaidEncodedBytes = networkEvents.filter((event) =>
  event.method === "Network.loadingFinished" &&
  (requestById.get(event.params.requestId) ?? "").includes(
    "/runtime/mermaid/")).reduce((total, event) =>
      total + event.params.encodedDataLength, 0);
const report = {
  page: evaluation.result.value,
  network: {
    ordinaryMermaidRequests: ordinaryUrls.filter((url) =>
      url.includes("/runtime/mermaid/")).length,
    mermaidRequestCount: mermaidUrls.length,
    mermaidEncodedBytes,
    publicRequests: requestUrls.filter((url) => /^https?:/i.test(url))
  },
  metrics: Object.fromEntries(metrics.metrics.map((entry) =>
    [entry.name, entry.value]))
};

const failures = [];
if (report.page.blockCount !== 50 || report.page.rendered !== 47 ||
    report.page.errors !== 3 || report.page.unresolved.length !== 0) {
  failures.push("50-block terminal counts mismatch");
}
if (report.page.stats.active !== 0 || report.page.stats.pending !== 0 ||
    report.page.stats.cacheEntries > 128 ||
    report.page.stats.cacheBytes > 16 * 1024 * 1024 ||
    report.page.stats.cacheHits === 0 || report.page.stats.stale === 0) {
  failures.push("scheduler/cache/generation bounds mismatch");
}
if (report.page.memoryPressureStats.cacheEntries !== 0 ||
    report.page.memoryPressureStats.cacheBytes !== 0) {
  failures.push("memory pressure did not clear cache");
}
if (report.network.ordinaryMermaidRequests !== 0 ||
    report.network.mermaidRequestCount === 0 ||
    report.network.publicRequests.length !== 0) {
  failures.push("offline/lazy resource contract mismatch");
}
await call("Page.navigate", {url: "crayon://newtab/"});
await delay(300);
socket.close();
console.log(JSON.stringify({...report, failures}, null, 2));
if (failures.length !== 0) process.exitCode = 1;
