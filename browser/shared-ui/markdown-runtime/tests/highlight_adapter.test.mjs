import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import hljs from "../../../../third_party/highlight/assets/core.min.js";
import javascript from "../../../../third_party/highlight/assets/languages/javascript.min.js";
import {
  applyHighlightedCode,
  loadOrderForLanguage,
  parseHighlightCandidate
} from "../assets/highlight-adapter.js";

test("candidate policy keeps only closed highlight spans", () => {
  const candidate = '<span class="hljs-title function_">x</span>&lt;b&gt;';
  assert.deepEqual(parseHighlightCandidate(candidate), [
    {classes: ["hljs-title"], children: [{text: "x"}]},
    {text: "<b>"}
  ]);
  for (const invalid of [
    '<script>alert(1)</script>',
    '<span onclick="x">bad</span>',
    '<span class="hljs-keyword">open',
    '</span>',
    '&unknown;',
    '你'.repeat(700000)
  ]) {
    assert.equal(parseHighlightCandidate(invalid), null);
  }
});

test("real explicit-language output passes while hostile source stays text", () => {
  hljs.registerLanguage("javascript", javascript);
  const source = 'const value = "<img src=x onerror=alert(1)>";';
  const candidate = hljs.highlight(source, {
    language: "javascript",
    ignoreIllegals: true
  }).value;
  const tokens = parseHighlightCandidate(candidate);
  assert.ok(tokens);
  assert.equal(JSON.stringify(tokens).includes("<img src=x onerror=alert(1)>"), true);
  assert.equal(candidate.includes("<img src=x onerror"), false);
});

test("load plans are closed and dependency-aware", () => {
  assert.deepEqual(loadOrderForLanguage("dockerfile"), ["bash", "dockerfile"]);
  assert.deepEqual(loadOrderForLanguage("javascript"),
    ["css", "graphql", "xml", "javascript"]);
  assert.equal(loadOrderForLanguage("unknown"), null);
});

test("all adapter load plans match the frozen manifest closure", () => {
  const manifest = JSON.parse(fs.readFileSync(
    new URL("../../../../third_party/highlight/manifest.json", import.meta.url),
    "utf8"));
  const byId = new Map(manifest.languages.map((language) => [language.id, language]));
  const closure = (root) => {
    const visiting = new Set();
    const visited = new Set();
    const order = [];
    const visit = (id) => {
      if (visited.has(id) || visiting.has(id)) return;
      const language = byId.get(id);
      assert.ok(language, `missing dependency ${id}`);
      visiting.add(id);
      for (const dependency of language.dependencies) visit(dependency);
      visiting.delete(id);
      visited.add(id);
      order.push(id);
    };
    visit(root);
    return order;
  };
  for (const language of manifest.languages) {
    assert.deepEqual(loadOrderForLanguage(language.id), closure(language.id),
      `load order for ${language.id}`);
  }
});

function fakeCodeElement(className, nodeId) {
  const attrs = new Map([["data-mdv-node", nodeId]]);
  const element = {
    attrs,
    className,
    replaced: false,
    connected: true,
    get isConnected() { return element.connected; },
    get textContent() { return "const value = 1;"; },
    getAttribute(name) { return attrs.has(name) ? attrs.get(name) : null; },
    setAttribute(name, value) { attrs.set(name, String(value)); },
    classList: {
      add(token) {
        const classes = new Set(element.className.split(" ").filter(Boolean));
        classes.add(token);
        element.className = [...classes].sort().join(" ");
      },
      contains(token) { return element.className.split(" ").includes(token); }
    },
    replaceChildren(...children) {
      element.replaced = true;
      element.children = children;
    }
  };
  return element;
}

function fakeDocument() {
  return {
    createTextNode: (text) => ({text}),
    createElement: () => ({className: "", children: [], appendChild() {}}),
    createDocumentFragment: () => ({
      children: [],
      appendChild(child) { this.children.push(child); }
    })
  };
}

test("applyHighlightedCode marks state and guarantees the hljs class", () => {
  globalThis.document = fakeDocument();
  try {
    const code = fakeCodeElement("language-javascript", "n1");
    const candidate = hljs.highlight(code.textContent, {
      language: "javascript",
      ignoreIllegals: true
    }).value;
    assert.equal(
      applyHighlightedCode(code, candidate, code.textContent, "n1"), true);
    assert.equal(code.getAttribute("data-mdv-highlighted"), "true");
    assert.equal(code.classList.contains("hljs"), true);
    assert.ok(code.replaced);

    const bare = fakeCodeElement("", "n2");
    assert.equal(
      applyHighlightedCode(bare, candidate, bare.textContent, "n2"), true);
    assert.equal(bare.classList.contains("hljs"), true);
  } finally {
    delete globalThis.document;
  }
});

test("stale, detached or hostile candidates are refused without success marks", () => {
  globalThis.document = fakeDocument();
  try {
    const candidate = hljs.highlight("const value = 1;", {
      language: "javascript",
      ignoreIllegals: true
    }).value;

    const stale = fakeCodeElement("language-javascript", "n1");
    assert.equal(
      applyHighlightedCode(stale, candidate, stale.textContent, "other"),
      false);
    assert.ok(!stale.replaced);
    assert.equal(stale.getAttribute("data-mdv-highlighted"), null);

    const detached = fakeCodeElement("language-javascript", "n1");
    detached.connected = false;
    assert.equal(
      applyHighlightedCode(detached, candidate, detached.textContent, "n1"),
      false);

    const hostile = fakeCodeElement("language-javascript", "n1");
    assert.equal(
      applyHighlightedCode(hostile, "<script>alert(1)</script>",
                           hostile.textContent, "n1"),
      false);
    assert.ok(!hostile.replaced);
  } finally {
    delete globalThis.document;
  }
});
