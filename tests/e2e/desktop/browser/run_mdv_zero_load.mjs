#!/usr/bin/env node
// MRT-09W / MR-003: deterministic real-CEF on-demand zero-load harness.
// A document without extension nodes must produce zero runtime asset
// requests; a document with Highlight/KaTeX/Mermaid nodes may only load
// same-origin /runtime/ assets.  The caller launches a local Debug/Release
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
const requestUrls = [];
socket.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.method === "Network.requestWillBeSent") {
    requestUrls.push(message.params?.request?.url ?? "");
  }
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
await call("Network.enable");
await call("Page.navigate", {url: "crayon://mdv/"});
await delay(800);

async function evaluate(expression) {
  const result = await call("Runtime.evaluate", {
    expression, awaitPromise: true, returnByValue: true});
  if (result.exceptionDetails) {
    throw new Error(JSON.stringify(result.exceptionDetails));
  }
  return result.result.value;
}

// Phase 1: plain document without any extension node.
const baseline = requestUrls.length;
await evaluate(
  `(async function(){
    mdvPush({preview:'<h1>plain</h1><p>text only</p>'+
      '<pre><code>unknown fence no lang</code></pre>'});
    await new Promise((resolve)=>setTimeout(resolve,600));
    return true;})()`);
const plainRequests = requestUrls.slice(baseline);
const plainRuntime = plainRequests.filter((url) => url.includes("/runtime/"));
const plainPublic = plainRequests.filter((url) =>
  /^https?:\/\//.test(url) && !url.startsWith("http://127.0.0.1"));
expect(plainRuntime.length === 0,
  `plain doc triggered runtime requests: ${plainRuntime.join(",")}`);
expect(plainPublic.length === 0,
  `plain doc triggered public requests: ${plainPublic.join(",")}`);

// Phase 2: synthetic extension markup whose node IDs were never registered
// by the Browser-owned pipeline must be rejected fail-closed: the adapters
// verify node registration/structure, so no runtime asset may load and no
// render may land.
const extensionBaseline = requestUrls.length;
const phase2 = await evaluate(
  `(async function(){
    const fence='<pre><code data-mdv-highlight="js" data-mdv-node="'+
      'a'.repeat(32)+'">const x=1;</code></pre>';
    const math='<span data-mdv-math="inline" data-mdv-node="'+
      'b'.repeat(32)+'">x^2</span>';
    mdvPush({preview:fence+math});
    document.querySelector('code[data-mdv-highlight]')?.scrollIntoView();
    document.querySelector('[data-mdv-math]')?.scrollIntoView();
    await new Promise((resolve)=>setTimeout(resolve,3000));
    const code=document.querySelector('code[data-mdv-highlight]');
    const mathNode=document.querySelector('[data-mdv-math]');
    return {highlighted:code?code.getAttribute('data-mdv-highlighted'):null,
      mathRendered:mathNode?mathNode.getAttribute('data-mdv-math-rendered'):null};
  })()`);
expect(phase2.highlighted !== "true" && phase2.mathRendered !== "true",
  `unregistered synthetic nodes rendered: ${JSON.stringify(phase2)}`);
const extensionRequests = requestUrls.slice(extensionBaseline);
const runtimeRequests = extensionRequests.filter((url) =>
  url.includes("/runtime/"));
const heavyAssets = runtimeRequests.filter((url) =>
  !url.endsWith("/adapter"));
const publicRequests = extensionRequests.filter((url) =>
  /^https?:\/\//.test(url) && !url.startsWith("http://127.0.0.1"));
expect(heavyAssets.length === 0,
  `unregistered synthetic nodes loaded grammar/runtime assets: ${heavyAssets.join(",")}`);
expect(publicRequests.length === 0,
  `extension doc triggered public requests: ${publicRequests.join(",")}`);

console.log(JSON.stringify({plainRequests, runtimeRequests, failures},
  null, 2));
socket.close();
if (failures.length > 0) process.exit(1);
