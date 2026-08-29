import assert from "node:assert/strict";
import test from "node:test";
import {
  attributeAllowed,
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
