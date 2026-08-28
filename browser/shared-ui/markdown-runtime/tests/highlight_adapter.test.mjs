import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import hljs from "../../../../third_party/highlight/assets/core.min.js";
import javascript from "../../../../third_party/highlight/assets/languages/javascript.min.js";
import {
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
