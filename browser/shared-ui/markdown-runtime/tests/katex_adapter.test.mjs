import assert from "node:assert/strict";
import test from "node:test";
import katex from "../../../../third_party/katex/assets/katex.mjs";
import {
  classListAllowed,
  preflightMathSource,
  styleAllowed
} from "../assets/katex-adapter.js";

test("source preflight freezes command and budget policy", () => {
  assert.equal(preflightMathSource(String.raw`\frac{x^2+1}{2}`), true);
  for (const source of [
    String.raw`\href{https://example.test}{x}`,
    String.raw`\includegraphics{file:///tmp/x}`,
    String.raw`\htmlFuture{x}`,
    String.raw`\gdef\x{y}`,
    String.raw`\csname href\endcsname{x}`,
    "",
    "x".repeat(65537)
  ]) assert.equal(preflightMathSource(source), false, source);
  assert.equal(preflightMathSource(String.raw`\alpha`.repeat(8192)), true);
  assert.equal(preflightMathSource(String.raw`\alpha`.repeat(8193)), false);
});

test("class and style policies accept real KaTeX layout only", () => {
  assert.equal(classListAllowed("katex katex-html"), true);
  assert.equal(classListAllowed("katex owned"), false);
  for (const style of [
    "height:0.6667em;vertical-align:-0.0833em;",
    "margin-right:0.2222em;position:relative;top:-0.0011em;",
    "border-style:solid;border-width:0.04em;"
  ]) assert.equal(styleAllowed(style), true, style);
  for (const style of [
    "background:url(https://example.test/x)",
    "behavior:expression(alert(1))",
    "position:fixed",
    "color:var(--owned)"
  ]) assert.equal(styleAllowed(style), false, style);
});

test("real fixed-option output stays free of active content", () => {
  const options = {
    output: "htmlAndMathml", throwOnError: true, strict: "error",
    trust: false, globalGroup: false, maxSize: 16, maxExpand: 256,
    displayMode: true, macros: Object.create(null)
  };
  for (const source of [
    String.raw`E=mc^2`,
    String.raw`\frac{x^2+1}{\sqrt{2}}`,
    String.raw`\sum_{i=1}^{n}i`,
    String.raw`\begin{matrix}a&b\\c&d\end{matrix}`
  ]) {
    const output = katex.renderToString(source, options);
    assert.match(output, /class="katex/);
    assert.equal(/<script|<iframe|<object|<embed|\son[a-z]+=|\b(?:href|src)=/i
      .test(output), false);
  }
  const hostile = katex.renderToString(
    String.raw`\text{<img src=x onerror=alert(1)>}`, options);
  assert.equal(hostile.includes("<img"), false);
});
