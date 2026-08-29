import assert from "node:assert/strict";
import test from "node:test";
import {
  attributeAllowed,
  MermaidRenderScheduler,
  parseMermaidSvgCandidate,
  rebuildCssRules
} from "../assets/mermaid-adapter.js";

const RENDER_ID = "mdv-mermaid-0123456789abcdef0123456789abcdef";
const SVG_NS = "http://www.w3.org/2000/svg";

/// Minimal XML-subset parser plus DOM stubs so the fail-closed gate can be
/// exercised end to end without a browser. Test inputs stay well-formed.
function matchSimple(simple, node) {
  const id = simple.match(/#[A-Za-z0-9_.-]+/)?.[0].slice(1);
  if (id && node.getAttribute("id") !== id) return false;
  for (const wanted of simple.match(/\.[A-Za-z0-9_-]+/g) ?? []) {
    const value = node.getAttribute("class") ?? "";
    if (!value.split(/\s+/).includes(wanted.slice(1))) return false;
  }
  const tag = simple.replace(/[#.][A-Za-z0-9_.-]+/g, "");
  return !tag || node.localName === tag;
}

function matchesChain(selector, element) {
  const chain = selector.trim().split(/\s+/);
  let current = element;
  for (let index = chain.length - 1; index >= 0; index--) {
    while (current && current.nodeType === 1 &&
           !matchSimple(chain[index], current)) {
      current = current.parentNode;
    }
    if (!current || current.nodeType !== 1) return false;
    current = current.parentNode;
  }
  return true;
}

function makeElement(tag, namespace) {
  return {
    nodeType: 1, localName: tag, namespaceURI: namespace,
    attributes: [], childNodes: [], parentNode: null,
    style: {
      rules: [],
      setProperty(property, value, priority) {
        this.rules.push({property, value, priority});
      }
    },
    matches(selector) { return matchesChain(selector, this); },
    querySelectorAll(selector) {
      const found = [];
      const walk = (node) => {
        for (const child of node.childNodes) {
          if (child.nodeType === 1) {
            if (matchesChain(selector, child)) found.push(child);
            walk(child);
          }
        }
      };
      walk(this);
      return found;
    },
    setAttribute(name, value) {
      const existing = this.attributes.find((a) => a.name === name);
      if (existing) { existing.value = value; return; }
      this.attributes.push({name, value});
    },
    getAttribute(name) {
      const found = this.attributes.find((a) => a.name === name);
      return found === undefined ? null : found.value;
    },
    get textContent() {
      return this.childNodes.map((child) =>
        child.nodeType === 3 ? child.data : child.textContent).join("");
    },
    appendChild(child) {
      child.parentNode = this;
      this.childNodes.push(child);
    }
  };
}

globalThis.Node = {ELEMENT_NODE: 1, TEXT_NODE: 3};
globalThis.document = {
  createElementNS: (namespace, tag) => makeElement(tag, namespace),
  createTextNode: (data) => ({nodeType: 3, data, childNodes: []})
};

function parseXml(source) {
  let cursor = 0;
  function parseNode() {
    if (source[cursor] !== "<") {
      const end = source.indexOf("<", cursor);
      const stop = end < 0 ? source.length : end;
      const text = source.slice(cursor, stop);
      cursor = stop;
      return globalThis.document.createTextNode(text);
    }
    const openEnd = source.indexOf(">", cursor);
    let raw = source.slice(cursor + 1, openEnd);
    cursor = openEnd + 1;
    const selfClosing = raw.endsWith("/");
    if (selfClosing) raw = raw.slice(0, -1);
    const nameEnd = raw.search(/\s/);
    const tag = nameEnd < 0 ? raw : raw.slice(0, nameEnd);
    const element = makeElement(tag, SVG_NS);
    const attributePattern = /([A-Za-z_:][-A-Za-z0-9_.:]*)="([^"]*)"/g;
    let match;
    while ((match = attributePattern.exec(raw)) !== null) {
      element.setAttribute(match[1], match[2]);
    }
    if (!selfClosing) {
      while (cursor < source.length) {
        if (source[cursor] === "<" && source[cursor + 1] === "/") {
          cursor = source.indexOf(">", cursor) + 1;
          break;
        }
        const child = parseNode();
        if (child) element.appendChild(child);
      }
    }
    return element;
  }
  const root = parseNode();
  return {documentElement: root, querySelector: () => null};
}

globalThis.DOMParser = class {
  parseFromString(source) { return parseXml(source); }
};

test("attribute policy denies active content and external references", () => {
  assert.equal(attributeAllowed("id", RENDER_ID), true);
  assert.equal(attributeAllowed("id", "bad id"), false);
  assert.equal(attributeAllowed("onclick", "alert(1)"), false);
  assert.equal(attributeAllowed("onload", ""), false);
  assert.equal(attributeAllowed("href", "javascript:alert(1)"), false);
  assert.equal(attributeAllowed("href", "data:text/html,x"), false);
  assert.equal(attributeAllowed("href", "https://example.test/x"), false);
  assert.equal(attributeAllowed("href", "#owned"), true);
  assert.equal(attributeAllowed("fill", "url(#" + RENDER_ID + "-m)"), true);
  assert.equal(attributeAllowed("fill", "url(https://example.test/x)"), false);
  assert.equal(attributeAllowed("marker-end", "url(#x_pt)"), true);
  assert.equal(attributeAllowed("stroke", "#eceaeb"), true);
  assert.equal(attributeAllowed("style", "fill:#eceaeb;stroke:#333;"), true);
  assert.equal(attributeAllowed("style", "background:url(#x)"), false);
  assert.equal(attributeAllowed("style", "position:fixed"), false);
  assert.equal(attributeAllowed("d", "M0,0 L10,10"), true);
  assert.equal(attributeAllowed("transform", "translate(10,20)"), true);
  assert.equal(attributeAllowed("xmlns", "http://www.w3.org/2000/svg"), true);
  assert.equal(attributeAllowed("class", "node default"), true);
  assert.equal(attributeAllowed("class", "node{x:1}"), false);
  assert.equal(attributeAllowed("unknown-attr", "x"), false);
});

test("embedded style rules are closed and block-scoped", () => {
  const idMap = new Map([[RENDER_ID, RENDER_ID],
    ["flowchart-pointEnd", RENDER_ID + "-flowchart-pointEnd"]]);
  const css = "#" + RENDER_ID + "{font-family:\"trebuchet ms\",verdana;}" +
    "#" + RENDER_ID + " .edgeLabel{fill:#333;}" +
    "#" + RENDER_ID + " text#flowchart-pointEnd{stroke-width:2px;}";
  const rules = rebuildCssRules(css, idMap);
  assert.ok(rules);
  assert.equal(rules.length, 3);
  assert.deepEqual(rules[0].selectors, ["#" + RENDER_ID]);
  assert.deepEqual(rules[2].selectors,
    ["#" + RENDER_ID + " text#" + RENDER_ID + "-flowchart-pointEnd"]);
  assert.deepEqual(rules[1].declarations,
    [{property: "fill", value: "#333", priority: ""}]);
  // Unstyleable or escaping rules are dropped, never applied; only the
  // flat-rule structure itself fails the whole candidate.
  assert.deepEqual(rebuildCssRules("@import url(evil)", idMap), null);
  for (const dropped of [
    "#" + RENDER_ID + "{background:url(https://x)}",
    "a{color:red}",
    ".label{fill:red}",
    "#unknown{fill:red}",
    "#x>*{fill:red}",
    "#" + RENDER_ID + " [hidden]{fill:red}",
    "#" + RENDER_ID + "{behavior:url(x)}",
    "#" + RENDER_ID + " :root{fill:red}"
  ]) {
    assert.deepEqual(rebuildCssRules(dropped, idMap), [], dropped);
  }
  // Nested or trailing structure fails the whole candidate.
  assert.deepEqual(
    rebuildCssRules("#" + RENDER_ID + "{fill:red}}x{", idMap), null);
  assert.deepEqual(
    rebuildCssRules("#" + RENDER_ID + " @media{a{b:c}}", idMap), null);
});

test("gate rebuilds benign mermaid output and scopes every reference", () => {
  const svg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"" + RENDER_ID + "\" " +
    "role=\"graphics-document document\" viewBox=\"0 0 100 50\">" +
    "<style>#" + RENDER_ID + "{font-family:verdana;}#" + RENDER_ID +
    " .label{fill:#333;}</style>" +
    "<defs><marker id=\"flowchart-pointEnd\" refX=\"9\" orient=\"auto\">" +
    "<path d=\"M0,0 L10,5\"/></marker></defs>" +
    "<g><rect class=\"node\" fill=\"#eceaeb\"></rect>" +
    "<path marker-end=\"url(#flowchart-pointEnd)\"/></g>" +
    "<text x=\"10\" y=\"10\">A to B</text></svg>";
  const rebuilt = parseMermaidSvgCandidate(svg, RENDER_ID);
  assert.ok(rebuilt);
  assert.equal(rebuilt.localName, "svg");
  assert.equal(rebuilt.getAttribute("id"), RENDER_ID);
  // No style element survives; its rules land as CSSOM inline styles.
  assert.equal(rebuilt.querySelectorAll("style").length, 0);
  assert.deepEqual(rebuilt.style.rules,
    [{property: "font-family", value: "verdana", priority: ""}]);
  const marker = rebuilt.childNodes[0].childNodes[0];
  assert.equal(marker.localName, "marker");
  assert.equal(marker.getAttribute("id"), RENDER_ID + "-flowchart-pointEnd");
  const edge = rebuilt.childNodes[1].childNodes[1];
  assert.equal(edge.getAttribute("marker-end"),
    "url(#" + RENDER_ID + "-flowchart-pointEnd)");
});

test("gate rejects active content and escaping references", () => {
  const benign = "<svg id=\"" + RENDER_ID + "\"><g/></svg>";
  for (const hostile of [
    "<svg><script>alert(1)</script></svg>",
    "<svg><foreignObject><body>x</body></foreignObject></svg>",
    "<svg><a href=\"https://example.test\"><text>x</text></a></svg>",
    "<svg><image href=\"https://example.test/x.png\"/></svg>",
    "<svg><g onclick=\"alert(1)\"/></svg>",
    "<svg><text style=\"color:expression(alert(1))\">x</text></svg>",
    "<svg><use href=\"#other-block-id\"/></svg>",
    "<svg><path fill=\"url(#other-block-id)\"/></svg>",
    "not svg at all",
    ""
  ]) {
    assert.equal(parseMermaidSvgCandidate(hostile, RENDER_ID), null, hostile);
  }
  assert.ok(parseMermaidSvgCandidate(benign, RENDER_ID));
  // A style rule referencing another block is dropped, never applied.
  const crossBlock = parseMermaidSvgCandidate(
    "<svg id=\"" + RENDER_ID +
    "\"><style>#other-block-id{fill:red}</style></svg>", RENDER_ID);
  assert.ok(crossBlock);
  assert.deepEqual(crossBlock.style.rules, []);
  // An invalid render id fails closed regardless of content.
  assert.equal(parseMermaidSvgCandidate(benign, "bad id"), null);
  // Oversized candidates fail regardless of content.
  const oversized = "<svg>" + "<g/>".repeat(1200000) + "</svg>";
  assert.equal(parseMermaidSvgCandidate(oversized, RENDER_ID), null);
});

test("scheduler bounds concurrency and queue, coalesces and caches", async () => {
  const scheduler = new MermaidRenderScheduler({
    maxConcurrent: 2, maxPending: 3, maxCacheEntries: 2,
    maxCachedResultBytes: 32, maxCacheBytes: 48
  });
  const releases = [];
  let running = 0;
  let peak = 0;
  let produced = 0;
  const producer = (value) => () => new Promise((resolve) => {
    produced += 1;
    running += 1;
    peak = Math.max(peak, running);
    releases.push(() => {
      running -= 1;
      resolve({value, bytes: value.length});
    });
  });

  const first = scheduler.schedule("same", producer("cached"));
  const duplicate = scheduler.schedule("same", producer("never"));
  const second = scheduler.schedule("second", producer("two"));
  const queued = scheduler.schedule("queued", producer("three"));
  const queued2 = scheduler.schedule("queued2", producer("four"));
  const queued3 = scheduler.schedule("queued3", producer("five"));
  const overflow = await scheduler.schedule("overflow", producer("six"));
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(peak, 2);
  assert.equal(produced, 2);
  assert.equal(overflow.status, "capacity_exceeded");
  assert.equal(scheduler.stats().dropped, 1);

  while (releases.length > 0) {
    releases.shift()();
    await new Promise((resolve) => setImmediate(resolve));
  }
  const results = await Promise.all(
    [first, duplicate, second, queued, queued2, queued3]);
  assert.ok(results.every((result) => result.status === "ready"));
  assert.equal(produced, 5);
  assert.equal(results[0].value, "cached");
  assert.equal(results[1].value, "cached");
  const hit = await scheduler.schedule("queued3", producer("miss"));
  assert.equal(hit.status, "ready");
  assert.equal(hit.cacheHit, true);
  assert.equal(hit.value, "five");
  assert.equal(produced, 5);
  assert.ok(scheduler.stats().cacheEntries <= 2);
  assert.ok(scheduler.stats().cacheBytes <= 48);
});

test("scheduler fences generations and clears lifecycle resources", async () => {
  const scheduler = new MermaidRenderScheduler({
    maxConcurrent: 1, maxPending: 2, maxCacheEntries: 2,
    maxCachedResultBytes: 32, maxCacheBytes: 32
  });
  let releaseActive;
  const active = scheduler.schedule("active", () => new Promise((resolve) => {
    releaseActive = () => resolve({value: "old", bytes: 3});
  }));
  const pending = scheduler.schedule("pending", async () =>
    ({value: "pending", bytes: 7}));
  await new Promise((resolve) => setImmediate(resolve));
  scheduler.advanceGeneration();
  assert.equal((await pending).status, "stale");
  releaseActive();
  assert.equal((await active).status, "stale");
  await new Promise((resolve) => setImmediate(resolve));
  const fresh = await scheduler.schedule("fresh", async () =>
    ({value: "fresh", bytes: 5}));
  assert.equal(fresh.status, "ready");
  assert.equal(scheduler.stats().cacheEntries, 1);
  scheduler.clearCache();
  assert.equal(scheduler.stats().cacheEntries, 0);
  assert.equal(scheduler.stats().cacheBytes, 0);
  scheduler.shutdown();
  assert.equal((await scheduler.schedule("closed", async () =>
    ({value: "x", bytes: 1}))).status, "cancelled");
});

test("50-block repeated and failing burst stays bounded", async () => {
  const scheduler = new MermaidRenderScheduler({
    maxConcurrent: 4, maxPending: 16, maxCacheEntries: 8,
    maxCachedResultBytes: 64, maxCacheBytes: 256
  });
  let running = 0;
  let peak = 0;
  let produced = 0;
  const producers = new Map();
  for (let group = 0; group < 5; group += 1) {
    producers.set("group-" + group, async () => {
      produced += 1;
      running += 1;
      peak = Math.max(peak, running);
      await new Promise((resolve) => setImmediate(resolve));
      running -= 1;
      if (group === 4) throw new Error("fixture render failure");
      const value = "svg-" + group;
      return {value, bytes: value.length};
    });
  }
  const burst = Array.from({length: 50}, (_, index) => {
    const key = "group-" + (index % 5);
    return scheduler.schedule(key, producers.get(key));
  });
  const results = await Promise.all(burst);
  assert.equal(results.filter((result) => result.status === "ready").length, 40);
  assert.equal(results.filter((result) => result.status === "failed").length, 10);
  assert.equal(produced, 5);
  assert.ok(peak <= 4);
  assert.equal(scheduler.stats().pending, 0);
  assert.equal(scheduler.stats().active, 0);
  assert.equal(scheduler.stats().cacheEntries, 4);
});
