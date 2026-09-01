#!/usr/bin/env node
// MDV-20W: deterministic real-CEF theme / device-scale / narrow-viewport /
// reload resource-fall harness.  The caller launches a local Debug/Release
// bundle with a loopback DevTools port.  This script never opens a public
// URL and emits assertions only.

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
socket.onmessage = (event) => {
  const message = JSON.parse(event.data);
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

const failures = [];
function expect(condition, label) {
  if (!condition) failures.push(label);
}

await call("Page.enable");
await call("Performance.enable");
await call("Page.navigate", {url: "crayon://mdv/"});
await delay(800);

const pushAndRender = `
(async function(){
  const markup='<pre><code data-mdv-mermaid="true" data-mdv-node="'+
    'f'.repeat(32)+'">flowchart LR\\n  A --> B</code></pre>';
  mdvPush({preview:markup});
  const node=document.querySelector('code[data-mdv-mermaid]');
  node.scrollIntoView({block:'center'});
  const deadline=performance.now()+10000;
  while(node.getAttribute('data-mdv-mermaid-rendered')!=='true'&&
        node.getAttribute('data-mdv-mermaid-error')!=='true'&&
        performance.now()<deadline){
    await new Promise((resolve)=>setTimeout(resolve,16));
  }
  return {rendered:node.getAttribute('data-mdv-mermaid-rendered'),
    error:node.getAttribute('data-mdv-mermaid-error'),
    hasSvg:!!node.querySelector('svg')};
})()`;

async function evaluate(expression) {
  const result = await call("Runtime.evaluate", {
    expression, awaitPromise: true, returnByValue: true});
  if (result.exceptionDetails) {
    throw new Error(JSON.stringify(result.exceptionDetails));
  }
  return result.result.value;
}

// 1. Baseline light theme render.
const light = await evaluate(pushAndRender);
expect(light.rendered === "true" && light.hasSvg, "light theme render");
const lightFill = await evaluate(
  `(function(){const svg=document.querySelector(
    'code[data-mdv-mermaid] svg');return svg?svg.outerHTML.slice(0,4096):'';})()`);

// 2. Dark theme: media emulation must trigger retheme + re-render.
await call("Emulation.setEmulatedMedia", {features: [
  {name: "prefers-color-scheme", value: "dark"}]});
const dark = await evaluate(
  `(async function(){
    const deadline=performance.now()+10000;
    for(;;){
      const node=document.querySelector('code[data-mdv-mermaid]');
      if(node&&node.getAttribute('data-mdv-mermaid-rendered')==='true'&&
         node.querySelector('svg')){
        return {rendered:'true',hasSvg:true,
          dark:window.matchMedia('(prefers-color-scheme: dark)').matches};
      }
      if(node&&node.getAttribute('data-mdv-mermaid-error')==='true'){
        return {rendered:null,hasSvg:false,error:true,
          dark:window.matchMedia('(prefers-color-scheme: dark)').matches};
      }
      if(performance.now()>=deadline){
        return {rendered:node?node.getAttribute('data-mdv-mermaid-rendered'):null,
          hasSvg:!!(node&&node.querySelector('svg')),
          dark:window.matchMedia('(prefers-color-scheme: dark)').matches};
      }
      await new Promise((resolve)=>setTimeout(resolve,100));
    }})()`);
expect(dark.dark === true, "dark media applied");
expect(dark.rendered === "true" && dark.hasSvg, "dark theme re-render");
const darkFill = await evaluate(
  `(function(){const svg=document.querySelector(
    'code[data-mdv-mermaid] svg');return svg?svg.outerHTML.slice(0,4096):'';})()`);
expect(lightFill !== darkFill, "theme switch changes svg output");

// 3. Narrow viewport at 200% device scale keeps rendering functional.
await call("Emulation.setDeviceMetricsOverride", {
  width: 520, height: 800, deviceScaleFactor: 2, mobile: false});
await delay(400);
const narrow = await evaluate(pushAndRender);
expect(narrow.rendered === "true" && narrow.hasSvg,
  "narrow/200% render");
const viewport = await evaluate(
  `({innerWidth:window.innerWidth,ratio:window.devicePixelRatio})`);
expect(viewport.innerWidth === 520, "narrow viewport width");
expect(viewport.ratio === 2, "200% device scale");
await call("Emulation.clearDeviceMetricsOverride");
await call("Emulation.setEmulatedMedia", {features: [
  {name: "prefers-color-scheme", value: "light"}]});

// 4. Reload resource fall: heap after full reload must not keep growing.
async function heapUsed() {
  const metrics = await call("Performance.getMetrics");
  const entry = metrics.metrics.find((item) => item.name === "JSHeapUsedSize");
  return entry ? entry.value : 0;
}
await evaluate(pushAndRender);
const heapLoaded = await heapUsed();
await call("Page.navigate", {url: "crayon://mdv/"});
await delay(1200);
await call("Runtime.evaluate", {expression:
  "new Promise((resolve)=>{if(window.gc){gc();}setTimeout(resolve,400);})",
  awaitPromise: true});
const heapAfterReload = await heapUsed();
expect(heapAfterReload < heapLoaded * 1.5,
  `heap fall after reload (${heapLoaded} -> ${heapAfterReload})`);

console.log(JSON.stringify({light, dark, narrow, viewport,
  heapLoaded, heapAfterReload, failures}, null, 2));
socket.close();
if (failures.length > 0) process.exit(1);
