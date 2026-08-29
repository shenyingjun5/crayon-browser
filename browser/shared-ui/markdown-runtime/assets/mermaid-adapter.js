// MDV-17: page-side Mermaid runtime adapter. Loads the vendored ESM closure
// on demand, initializes one strict instance and renders decorated fence
// blocks through the fail-closed SVG policy gate (markdown-viewer.md §7).
// Served only as crayon://mdv /runtime/mermaid/adapter; no network surface.
const RUNTIME_URL = "/runtime/mermaid/mermaid.esm.min.mjs";
const MAX_SOURCE_BYTES = 64 * 1024;
const MAX_CANDIDATE_BYTES = 4 * 1024 * 1024;
const MAX_CSS_BYTES = 128 * 1024;
const MAX_OUTPUT_NODES = 65536;
const MAX_OUTPUT_DEPTH = 64;
const MAX_ATTRIBUTES = 24;
const MAX_IDS = 4096;
const MAX_TEXT_BYTES = 64 * 1024;
const RENDER_DEADLINE_MS = 30 * 1000;
const MAX_CONCURRENT_RENDERS = 4;
const MAX_PENDING_RENDERS = 16;
const MAX_CACHE_ENTRIES = 128;
const MAX_CACHED_RESULT_BYTES = 2 * 1024 * 1024;
const MAX_CACHE_BYTES = 16 * 1024 * 1024;
const MERMAID_VERSION = "11.17.2";
const SVG_POLICY_VERSION = "mdv-svg-policy-v1";
// SHA-256 of the canonical fixed initialize() options below. Documents cannot
// override it; including it keeps cache identity closed if defaults change.
const NORMALIZED_OPTIONS_DIGEST =
  "0191c446a6a83aa0f22ae68524f5ad2098148fbd29412b270a75a1d76bb19253";

const SVG_NAMESPACE = "http://www.w3.org/2000/svg";
const ID_PATTERN = /^[A-Za-z0-9_][A-Za-z0-9_.-]*$/;
const FRAGMENT_REFERENCE_PATTERN = /^#([A-Za-z0-9_][A-Za-z0-9_.-]*)$/;
const FRAGMENT_URL_PATTERN = /^url\(#([A-Za-z0-9_][A-Za-z0-9_.-]*)\)$/;
const SCHEME_PATTERN = /^[a-zA-Z][a-zA-Z0-9+.-]*:/;
const CONTROL_PATTERN = /[\0-\x08\x0b\x0c\x0e-\x1f\x7f]/;

// Closed element allow-list. Everything Mermaid may legally draw is here;
// script, foreignObject, embeds, links, images, media and SMIL animation
// are absent, so their tags fail the block. Filter primitives are limited
// to local, geometry-only operations (feImage can fetch, so it stays out).
const ALLOWED_TAGS = new Set([
  "svg", "g", "defs", "marker", "symbol", "pattern", "clipPath", "mask",
  "linearGradient", "radialGradient", "stop", "rect", "circle", "ellipse",
  "line", "path", "polygon", "polyline", "text", "tspan", "title", "desc",
  "style", "use", "filter", "feDropShadow", "feGaussianBlur", "feOffset",
  "feFlood", "feComposite", "feMerge", "feMergeNode", "feBlend",
  "feColorMatrix", "feMorphology"
]);

/// True when the tag is part of the closed SVG output allow-list.
export function tagAllowed(tag) {
  return ALLOWED_TAGS.has(tag);
}

// Closed attribute allow-list (matched case-insensitively). URL-bearing
// attributes additionally go through the fragment-only reference rules.
const ALLOWED_ATTRIBUTES = new Set([
  "xmlns", "version", "id", "class", "style", "transform", "viewbox",
  "preserveaspectratio", "width", "height", "x", "y", "x1", "x2", "y1", "y2",
  "cx", "cy", "r", "rx", "ry", "d", "points", "dx", "dy", "offset",
  "fill", "fill-opacity", "fill-rule", "stroke", "stroke-opacity",
  "stroke-width", "stroke-dasharray", "stroke-dashoffset", "stroke-linecap",
  "stroke-linejoin", "stroke-miterlimit", "opacity", "color",
  "font-family", "font-size", "font-style", "font-weight", "letter-spacing",
  "word-spacing", "text-anchor", "text-decoration", "dominant-baseline",
  "alignment-baseline", "marker-end", "marker-mid", "marker-start",
  "clip-path", "clip-rule", "mask", "filter", "refx", "refy",
  "markerwidth", "markerheight", "markerunits", "orient", "overflow",
  "patternunits", "patterncontentunits", "patterntransform",
  "gradientunits", "gradienttransform", "spreadmethod", "fx", "fy",
  "stop-color", "stop-opacity", "clippathunits", "maskunits", "role",
  "aria-roledescription", "href",
  // Filter primitives (no feImage: it can fetch external references).
  "in", "in2", "result", "values", "mode", "type", "operator", "k1", "k2",
  "k3", "k4", "dx", "dy", "radius", "slope", "intercept", "tablevalues",
  "stddeviation", "flood-color", "flood-opacity", "lighting-color", "name"
]);

// Attributes whose values may be fragment references (url(#id) or #id).
const REFERENCE_ATTRIBUTES = new Set([
  "href", "fill", "stroke", "marker-end", "marker-mid", "marker-start",
  "clip-path", "mask", "filter"
]);

// Closed CSS property allow-list for inline style attributes (strict);
// the embedded <style> text additionally accepts any charset-safe property
// via embeddedDeclarationAllowed. URL and shape functions stay impossible.
const STYLE_PROPERTIES = new Set([
  "fill", "fill-opacity", "fill-rule", "stroke", "stroke-opacity",
  "stroke-width", "stroke-dasharray", "stroke-dashoffset", "stroke-linecap",
  "stroke-linejoin", "stroke-miterlimit", "opacity", "color", "font",
  "font-family", "font-size", "font-style", "font-weight", "letter-spacing",
  "word-spacing", "line-height", "text-anchor", "text-align",
  "text-decoration", "text-transform", "dominant-baseline",
  "alignment-baseline", "white-space", "display", "visibility", "overflow",
  "marker-end", "marker-mid", "marker-start", "max-width", "width", "height",
  "padding", "padding-left", "padding-right", "padding-top", "padding-bottom"
]);

// Dangerous CSS value shapes: fetch-capable or script-capable functions,
// at-rules, escapes, HTML and scheme literals. Color and shape functions
// (rgb/hsl/drop-shadow/translate, ...) stay allowed.
const CSS_DENY_PATTERN = new RegExp(
  "url\\s*\\(|expression\\s*\\(|image-set?\\s*\\(|element\\s*\\(|" +
    "cross-fade\\s*\\(|paint\\s*\\(|@|\\\\|<|>|\\{|\\}|javascript:|" +
    "data:|vbscript:|behavior|binding", "i");

let mermaidPromise;

// Page-session-only scheduler. It owns all queue/cache/generation state and
// never persists source or diagnostics. A generation change invalidates queued
// work immediately; Mermaid has no cancellation API, so already-running work
// is allowed to finish but its result is reported stale and cannot reach DOM.
export class MermaidRenderScheduler {
  constructor(limits = {}) {
    this.maxConcurrent = limits.maxConcurrent ?? MAX_CONCURRENT_RENDERS;
    this.maxPending = limits.maxPending ?? MAX_PENDING_RENDERS;
    this.maxCacheEntries = limits.maxCacheEntries ?? MAX_CACHE_ENTRIES;
    this.maxCachedResultBytes = limits.maxCachedResultBytes ??
      MAX_CACHED_RESULT_BYTES;
    this.maxCacheBytes = limits.maxCacheBytes ?? MAX_CACHE_BYTES;
    this.generation = 1;
    this.active = 0;
    this.activeGeneration = 0;
    this.pending = [];
    this.cache = new Map();
    this.cacheBytes = 0;
    this.inflight = new Map();
    this.dropped = 0;
    this.evicted = 0;
    this.stale = 0;
    this.cacheHits = 0;
    this.closed = false;
  }

  schedule(key, producer) {
    if (this.closed) return Promise.resolve({status: "cancelled"});
    const cached = this.takeCached(key);
    if (cached !== undefined) {
      return Promise.resolve({status: "ready", value: cached, cacheHit: true});
    }
    const shared = this.inflight.get(key);
    if (shared && shared.generation === this.generation) return shared.promise;
    if (this.pending.length >= this.maxPending) {
      this.dropped += 1;
      return Promise.resolve({status: "capacity_exceeded"});
    }
    const generation = this.generation;
    let resolveJob;
    const promise = new Promise((resolve) => { resolveJob = resolve; });
    this.pending.push({key, producer, generation, resolve: resolveJob});
    this.inflight.set(key, {generation, promise});
    this.pump();
    return promise;
  }

  pump() {
    while (!this.closed && this.active < this.maxConcurrent &&
           this.pending.length > 0) {
      if (this.active > 0 &&
          this.pending[0].generation !== this.activeGeneration) return;
      const job = this.pending.shift();
      if (job.generation !== this.generation) {
        this.finishStale(job);
        continue;
      }
      if (this.active === 0) this.activeGeneration = job.generation;
      this.active += 1;
      Promise.resolve().then(job.producer).then((result) => {
        if (job.generation !== this.generation || this.closed) {
          this.finishStale(job);
          return;
        }
        if (!result || typeof result.value !== "string") {
          job.resolve({status: "failed"});
          return;
        }
        this.insertCached(job.key, result.value, result.bytes);
        job.resolve({status: "ready", value: result.value, cacheHit: false});
      }, () => job.resolve({status: "failed"})).finally(() => {
        this.active -= 1;
        if (this.active === 0) this.activeGeneration = 0;
        const current = this.inflight.get(job.key);
        if (current && current.generation === job.generation) {
          this.inflight.delete(job.key);
        }
        this.pump();
      });
    }
  }

  finishStale(job) {
    this.stale += 1;
    job.resolve({status: "stale"});
  }

  takeCached(key) {
    const entry = this.cache.get(key);
    if (!entry) return undefined;
    this.cacheHits += 1;
    this.cache.delete(key);
    this.cache.set(key, entry);
    return entry.value;
  }

  insertCached(key, value, bytes) {
    if (!Number.isSafeInteger(bytes) || bytes <= 0 ||
        bytes > this.maxCachedResultBytes || bytes > this.maxCacheBytes) return;
    const existing = this.cache.get(key);
    if (existing) {
      this.cacheBytes -= existing.bytes;
      this.cache.delete(key);
    }
    while (this.cache.size >= this.maxCacheEntries ||
           this.cacheBytes + bytes > this.maxCacheBytes) {
      const oldest = this.cache.keys().next().value;
      if (oldest === undefined) break;
      const removed = this.cache.get(oldest);
      this.cache.delete(oldest);
      this.cacheBytes -= removed.bytes;
      this.evicted += 1;
    }
    this.cache.set(key, {value, bytes});
    this.cacheBytes += bytes;
  }

  advanceGeneration() {
    if (this.closed) return;
    this.generation += 1;
    for (const job of this.pending.splice(0)) this.finishStale(job);
    this.inflight.clear();
    this.clearCache();
  }

  clearCache() {
    this.cache.clear();
    this.cacheBytes = 0;
  }

  shutdown() {
    if (this.closed) return;
    this.advanceGeneration();
    this.closed = true;
    this.clearCache();
  }

  stats() {
    return Object.freeze({
      active: this.active, pending: this.pending.length,
      cacheEntries: this.cache.size, cacheBytes: this.cacheBytes,
      cacheHits: this.cacheHits, dropped: this.dropped,
      evicted: this.evicted, stale: this.stale
    });
  }
}

function withDeadline(promise) {
  let timer;
  const deadline = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error("render deadline")),
      RENDER_DEADLINE_MS);
  });
  return Promise.race([promise, deadline]).finally(() => clearTimeout(timer));
}

function declarationAllowed(declaration) {
  const separator = declaration.indexOf(":");
  if (separator <= 0 ||
      declaration.indexOf(":", separator + 1) >= 0) return false;
  const property = declaration.slice(0, separator).trim().toLowerCase();
  const value = declaration.slice(separator + 1).trim();
  if (!STYLE_PROPERTIES.has(property)) return false;
  return value.length > 0 && value.length <= 512 &&
    !CSS_DENY_PATTERN.test(value);
}

/// Inline style attributes keep only their valid declarations: Mermaid
/// emits junk like "undefined;;;undefined" for unset style slots (no colon
/// — dropped, removal is inert). Any real declaration (has a colon) is
/// enforced; failing it fails the whole candidate.
export function sanitizeStyleValue(value) {
  if (typeof value !== "string" || value.length > 65536 ||
      CONTROL_PATTERN.test(value)) {
    return null;
  }
  const kept = [];
  for (const raw of value.split(";").filter(Boolean)) {
    if (!raw.includes(":")) continue;
    if (!declarationAllowed(raw)) return null;
    kept.push(raw.trim() + ";");
  }
  return kept.join("");
}

/// Embedded <style> rules cover Mermaid's whole stylesheet, so properties
/// are deny-based: closed charset, no behavior/binding, and the shared
/// value checks (no url(), at-rules, HTML, schemes or control bytes).
function embeddedDeclarationAllowed(declaration) {
  const separator = declaration.indexOf(":");
  if (separator <= 0 ||
      declaration.indexOf(":", separator + 1) >= 0) return false;
  const property = declaration.slice(0, separator).trim().toLowerCase();
  if (!/^[a-zA-Z-]{1,64}$/.test(property) ||
      property === "behavior" || property === "-moz-binding") {
    return false;
  }
  const value = declaration.slice(separator + 1).trim();
  return value.length > 0 && value.length <= 512 &&
    !CSS_DENY_PATTERN.test(value);
}

function fragmentValueAllowed(value) {
  if (typeof value !== "string" || value.length > 4096 ||
      CONTROL_PATTERN.test(value)) return false;
  if (FRAGMENT_URL_PATTERN.test(value)) return true;
  if (FRAGMENT_REFERENCE_PATTERN.test(value)) return true;
  return !SCHEME_PATTERN.test(value) && !CSS_DENY_PATTERN.test(value);
}

export function attributeAllowed(name, value) {
  if (typeof value !== "string" || value.length > 65536 ||
      CONTROL_PATTERN.test(value)) return false;
  const lower = name.toLowerCase();
  if (lower === "xmlns") return value === "http://www.w3.org/2000/svg";
  if (lower === "xmlns:xlink") return value === "http://www.w3.org/1999/xlink";
  if (lower.startsWith("on") || lower.startsWith("xmlns")) return false;
  // Mermaid tags elements with inert data attributes (e.g. data-look).
  const isData = /^data-[a-z0-9_.-]+$/.test(lower);
  if (!isData && !ALLOWED_ATTRIBUTES.has(lower)) return false;
  if (lower === "id") return ID_PATTERN.test(value);
  if (lower === "class") {
    const tokens = value.split(/\s+/).filter(Boolean);
    return tokens.length > 0 && tokens.length <= 16 && tokens.every((token) =>
      /^[A-Za-z0-9_][A-Za-z0-9_.-]*$/.test(token));
  }
  if (lower === "style") {
    // Mermaid can emit junk or empty style slots; keep valid declarations.
    const sanitized = sanitizeStyleValue(value);
    return sanitized !== null;
  }
  if (lower === "version") return value === "1.1";
  if (lower === "role") {
    return value.split(/\s+/).filter(Boolean).every((token) =>
      /^[a-z-]+$/.test(token));
  }
  if (lower === "d" || lower === "points") {
    return /^[A-Za-z0-9_,.\s()+-]*$/.test(value);
  }
  if (lower === "transform" || lower === "patterntransform" ||
      lower === "gradienttransform") {
    return /^[A-Za-z0-9_,.\s()+-]*$/.test(value) &&
      !/url\s*\(/i.test(value);
  }
  if (REFERENCE_ATTRIBUTES.has(lower)) return fragmentValueAllowed(value);
  return !SCHEME_PATTERN.test(value) && !CSS_DENY_PATTERN.test(value);
}

/// Rewrites every fragment reference in a reference-attribute value to its
/// block-scoped id; unknown or external targets fail closed.
function rewriteReferenceValue(value, idMap) {
  if (FRAGMENT_URL_PATTERN.test(value)) {
    const id = value.slice(5, -1);
    return idMap.has(id) ? "url(#" + idMap.get(id) + ")" : null;
  }
  if (FRAGMENT_REFERENCE_PATTERN.test(value)) {
    const id = value.slice(1);
    return idMap.has(id) ? "#" + idMap.get(id) : null;
  }
  return value;
}

/// Maps every #id token of a style selector to its block-scoped id; any
/// unknown target fails closed, keeping references inside this block.
function rewriteCssSelector(selector, idMap) {
  const pattern = /#([A-Za-z0-9_][A-Za-z0-9_.-]*)/g;
  let rebuilt = "";
  let cursor = 0;
  let match;
  while ((match = pattern.exec(selector)) !== null) {
    if (!idMap.has(match[1])) return null;
    rebuilt += selector.slice(cursor, match.index) + "#" + idMap.get(match[1]);
    cursor = match.index + match[0].length;
  }
  return rebuilt + selector.slice(cursor);
}

function mappedId(id, renderId) {
  if (id === renderId || id.startsWith(renderId + "-") ||
      id.startsWith(renderId + "_")) {
    return id;
  }
  return renderId + "-" + id;
}

/// Validates Mermaid's embedded <style> text and returns block-scoped rules.
/// Rules must be flat and every kept selector starts with a block-local #id.
/// Rules or declarations that fail validation are dropped, never applied:
/// unstyleable cosmetics (foreign look variants, var() references, dead
/// selectors) cannot be turned into anything dangerous, so only the flat
/// structure itself is fail-closed. Rules are applied through CSSOM property
/// assignment (never a <style> element), which cannot introduce at-rules.
export function rebuildCssRules(css, idMap) {
  if (typeof css !== "string" || !css.trim() ||
      new TextEncoder().encode(css).byteLength > MAX_CSS_BYTES ||
      CONTROL_PATTERN.test(css)) {
    return null;
  }
  // @keyframes blocks (nested) are stripped together with the animation
  // they would drive; any other at-rule or nesting fails the flat parse.
  const staticCss = css.replace(
    /@keyframes\s+[^{}]*\{(?:[^{}]*\{[^{}]*\})*[^{}]*\}/gi, "");
  const rules = staticCss.match(/[^{}]+\{[^{}]*\}/g);
  if (!rules) return null;
  const stripped = staticCss.replace(/[^{}]+\{[^{}]*\}/g, "");
  if (stripped.trim() !== "") return null;
  const parsed = [];
  for (const rule of rules) {
    const separator = rule.indexOf("{");
    const body = rule.slice(separator + 1, rule.length - 1);
    const selectors = [];
    let selectorOk = true;
    for (const part of rule.slice(0, separator).split(",")) {
      const selector = part.trim();
      if (selector.length < 2 || selector[0] !== "#" ||
          !/^[#.\w\s>~+,.-]+$/.test(selector)) {
        selectorOk = false;
        break;
      }
      const scoped = rewriteCssSelector(selector, idMap);
      if (!scoped) {
        selectorOk = false;
        break;
      }
      selectors.push(scoped);
    }
    if (!selectorOk) continue;
    const declarations = [];
    for (const declaration of body.split(";").filter(Boolean)) {
      if (!embeddedDeclarationAllowed(declaration)) continue;
      const colon = declaration.indexOf(":");
      let value = declaration.slice(colon + 1).trim();
      let priority = "";
      const important = value.match(/!\s*important$/i);
      if (important) {
        priority = "important";
        value = value.slice(0, important.index).trim();
      }
      declarations.push({
        property: declaration.slice(0, colon).trim().toLowerCase(),
        value, priority
      });
    }
    if (declarations.length > 0) {
      parsed.push({selectors, declarations});
    }
  }
  return parsed;
}

function collectIds(node, renderId, idMap) {
  if (node.nodeType === Node.ELEMENT_NODE) {
    const id = node.getAttribute("id");
    if (id !== null) {
      if (!ID_PATTERN.test(id) || idMap.size >= MAX_IDS) return false;
      idMap.set(id, mappedId(id, renderId));
    }
    for (const child of node.childNodes) {
      if (!collectIds(child, renderId, idMap)) return false;
    }
  }
  return true;
}

function rebuildNode(node, depth, budget, renderId, idMap) {
  if (depth > MAX_OUTPUT_DEPTH || ++budget.nodes > MAX_OUTPUT_NODES) {
    return null;
  }
  if (node.nodeType === Node.TEXT_NODE) {
    if (new TextEncoder().encode(node.data).byteLength > MAX_TEXT_BYTES) {
      return null;
    }
    return document.createTextNode(node.data);
  }
  if (node.nodeType !== Node.ELEMENT_NODE) return null;
  const tag = node.localName;
  if (!ALLOWED_TAGS.has(tag) || node.attributes.length > MAX_ATTRIBUTES) {
    return null;
  }
  if (tag === "style") {
    // Style elements are consumed, never emitted: their rules are validated
    // and re-applied through CSSOM property assignment below.
    return null;
  }
  const output = document.createElementNS(SVG_NAMESPACE, tag);
  for (const attribute of node.attributes) {
    const lower = attribute.name.toLowerCase();
    if (!attributeAllowed(attribute.name, attribute.value)) return null;
    if (lower === "id") {
      output.setAttribute("id", idMap.get(attribute.value));
      continue;
    }
    if (lower === "style") {
      const sanitized = sanitizeStyleValue(attribute.value);
      if (sanitized !== "") {
        output.setAttribute("style", sanitized);
      }
      continue;
    }
    if (lower === "href") {
      const rewritten = rewriteReferenceValue(attribute.value, idMap);
      if (rewritten === null) return null;
      output.setAttribute("href", rewritten);
      continue;
    }
    if (REFERENCE_ATTRIBUTES.has(lower) &&
        FRAGMENT_URL_PATTERN.test(attribute.value)) {
      const rewritten = rewriteReferenceValue(attribute.value, idMap);
      if (rewritten === null) return null;
      output.setAttribute(attribute.name, rewritten);
      continue;
    }
    output.setAttribute(attribute.name, attribute.value);
  }
  for (const child of node.childNodes) {
    if (child.nodeType === Node.ELEMENT_NODE &&
        child.localName === "style") {
      const rules = rebuildCssRules(child.textContent, idMap);
      if (rules === null) return null;
      budget.rules.push(...rules);
      continue;
    }
    const rebuilt = rebuildNode(child, depth + 1, budget, renderId, idMap);
    if (!rebuilt) return null;
    output.appendChild(rebuilt);
  }
  return output;
}

/// Applies validated, block-scoped CSS rules through CSSOM property
/// assignment, matching selectors only inside this SVG subtree. A selector
/// that fails to compile or escapes the subtree fails the whole candidate.
function applyCssRules(svg, rules) {
  for (const rule of rules) {
    for (const selector of rule.selectors) {
      let matched;
      try {
        matched = svg.matches(selector)
          ? [svg, ...svg.querySelectorAll(selector)]
          : [...svg.querySelectorAll(selector)];
      } catch (_) {
        return false;
      }
      if (matched.length === 0) continue;
      for (const element of matched) {
        for (const declaration of rule.declarations) {
          element.style.setProperty(declaration.property, declaration.value,
            declaration.priority);
        }
      }
    }
  }
  return true;
}

/// Parses a mermaid.render() SVG candidate and rebuilds it inside the closed
/// SVG policy: unknown tags/attributes, active content, external references
/// and cross-block fragment targets make the whole candidate fail.
export function parseMermaidSvgCandidate(candidate, renderId) {
  if (typeof candidate !== "string" || !candidate ||
      new TextEncoder().encode(candidate).byteLength > MAX_CANDIDATE_BYTES ||
      typeof DOMParser !== "function" || !ID_PATTERN.test(renderId)) {
    return null;
  }
  // Style elements are pulled out before parsing: parsing a style element
  // would trip the page CSP for no benefit, and the CSS is applied through
  // CSSOM property assignment instead.
  const styleTexts = [];
  const stripped = candidate.replace(
    /<style\b[^>]*>([\s\S]*?)<\/style\s*>/gi, (_, css) => {
      styleTexts.push(css);
      return "";
    });
  if (/<style\b/i.test(stripped)) return null;
  const parsed = new DOMParser().parseFromString(stripped, "image/svg+xml");
  if (!parsed || parsed.querySelector("parsererror")) return null;
  const root = parsed.documentElement;
  if (!root || root.localName !== "svg" ||
      root.namespaceURI !== SVG_NAMESPACE) {
    return null;
  }
  const idMap = new Map();
  if (!collectIds(root, renderId, idMap)) return null;
  const rules = styleTexts.length === 0
    ? []
    : rebuildCssRules(styleTexts.join(""), idMap);
  if (rules === null) return null;
  const budget = {nodes: 0, rules};
  const rebuilt = rebuildNode(root, 0, budget, renderId, idMap);
  if (!rebuilt || !applyCssRules(rebuilt, budget.rules)) return null;
  return rebuilt;
}

/// Applies the requested color scheme. initialize() may be called again
/// between renders; the theme only changes when it actually differs.
let currentTheme = null;

function ensureTheme(mermaid, theme) {
  if (currentTheme === theme) return;
  mermaid.initialize({
    startOnLoad: false, securityLevel: "strict", htmlLabels: false,
    flowchart: {htmlLabels: false}, class: {htmlLabels: false},
    theme: theme === "dark" ? "dark" : "default"
  });
  currentTheme = theme;
}

function loadMermaid() {
  if (!mermaidPromise) {
    mermaidPromise = import(RUNTIME_URL).then((module) => {
      const mermaid = module.default;
      if (!mermaid || typeof mermaid.render !== "function" ||
          typeof mermaid.initialize !== "function") {
        throw new Error("runtime unavailable");
      }
      return mermaid;
    });
  }
  return mermaidPromise;
}

function markFailure(container) {
  if (container && container.getAttribute("data-mdv-mermaid") === "true") {
    container.setAttribute("data-mdv-mermaid-error", "true");
  }
}

function staleResult(container, nodeId) {
  return !container.isConnected ||
    container.getAttribute("data-mdv-mermaid") !== "true" ||
    container.getAttribute("data-mdv-node") !== nodeId ||
    container.getAttribute("data-mdv-mermaid-rendered") === "true";
}

let renderSequence = 0;
const renderScheduler = new MermaidRenderScheduler();
const encoder = new TextEncoder();
let isolationNonce;

function sessionIsolationNonce() {
  if (isolationNonce) return isolationNonce;
  const bytes = new Uint8Array(32);
  if (globalThis.crypto && typeof globalThis.crypto.getRandomValues ===
      "function") {
    globalThis.crypto.getRandomValues(bytes);
    isolationNonce = Array.from(bytes, (value) =>
      value.toString(16).padStart(2, "0")).join("");
  } else {
    // No weak fallback: without a cryptographic page isolation nonce the
    // renderer remains functional but every request deliberately misses cache.
    isolationNonce = "cache-disabled";
  }
  return isolationNonce;
}

async function sourceCacheKey(source, scheme) {
  if (!globalThis.crypto || !globalThis.crypto.subtle ||
      typeof globalThis.crypto.subtle.digest !== "function" ||
      sessionIsolationNonce() === "cache-disabled") return null;
  const digest = await globalThis.crypto.subtle.digest(
    "SHA-256", encoder.encode(source));
  const hex = Array.from(new Uint8Array(digest), (value) =>
    value.toString(16).padStart(2, "0")).join("");
  return sessionIsolationNonce() + "\0mermaid\0" + MERMAID_VERSION + "\0" +
    hex + "\0" + scheme + "\0" + NORMALIZED_OPTIONS_DIGEST + "\0" +
    SVG_POLICY_VERSION;
}

export function advanceMermaidGeneration() {
  renderScheduler.advanceGeneration();
}

export function clearMermaidCache() {
  renderScheduler.clearCache();
}

export function shutdownMermaidSession() {
  renderScheduler.shutdown();
  mermaidPromise = undefined;
  currentTheme = null;
}

// Counts only; no source, digest, path or third-party error reaches diagnostics.
export function mermaidSessionStats() {
  return renderScheduler.stats();
}

export async function renderMermaid(container, nodeId, theme) {
  const scheme = theme === "dark" ? "dark" : "light";
  if (!container || typeof nodeId !== "string" || !nodeId ||
      staleResult(container, nodeId)) {
    return false;
  }
  const source = container.textContent;
  if (typeof source !== "string" || !source.trim() ||
      encoder.encode(source).byteLength > MAX_SOURCE_BYTES) {
    markFailure(container);
    return false;
  }
  try {
    const cacheKey = await sourceCacheKey(source, scheme);
    if (staleResult(container, nodeId)) return false;
    renderSequence += 1;
    const renderId = "mdv-mermaid-" + nodeId + "-r" + renderSequence;
    const requestKey = cacheKey ?? renderId;
    const scheduled = await renderScheduler.schedule(requestKey, async () => {
      const mermaid = await withDeadline(loadMermaid());
      ensureTheme(mermaid, scheme);
      const rendered = await withDeadline(mermaid.render(renderId, source));
      const candidate = rendered && typeof rendered.svg === "string"
        ? rendered.svg : "";
      // Cache only output that has already passed the Browser-owned gate.
      // Every consumer rebuilds it again with a new block-scoped render id.
      if (!parseMermaidSvgCandidate(candidate, renderId)) return null;
      const utf8Bytes = encoder.encode(candidate).byteLength;
      // V8 may retain a one-byte or two-byte string. Account the conservative
      // representation so the 16 MiB cap is an upper bound, not wishful math.
      const retainedBytes = Math.max(utf8Bytes, candidate.length * 2);
      return {value: candidate, bytes: retainedBytes};
    });
    if (scheduled.status === "capacity_exceeded" ||
        scheduled.status === "failed") {
      // Capacity/render failure is a local, source-preserving degradation.
      markFailure(container);
      return false;
    }
    if (scheduled.status !== "ready") return false;
    const candidate = scheduled.value;
    if (staleResult(container, nodeId)) return false;
    const rebuilt = parseMermaidSvgCandidate(candidate, renderId);
    if (!rebuilt) {
      markFailure(container);
      return false;
    }
    if (staleResult(container, nodeId)) return false;
    // The escaped DSL is preserved as hidden text so "view source" works
    // without re-reading anything outside this block.
    const sourceText = document.createElement("span");
    sourceText.className = "mdv-mermaid-source";
    sourceText.hidden = true;
    sourceText.textContent = source;
    container.replaceChildren(sourceText, rebuilt);
    container.setAttribute("data-mdv-mermaid-rendered", "true");
    return true;
  } catch (_) {
    markFailure(container);
    return false;
  }
}
