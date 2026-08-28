const RUNTIME_URL = "/runtime/katex/katex";
const STYLESHEET_URL = "/runtime/katex/stylesheet";
const MAX_SOURCE_BYTES = 64 * 1024;
const MAX_TOKENS = 8192;
const MAX_BRACE_DEPTH = 64;
const MAX_CANDIDATE_BYTES = 2 * 1024 * 1024;
const MAX_OUTPUT_NODES = 32768;
const MAX_OUTPUT_DEPTH = 64;
const MAX_ATTRIBUTES = 16;
const RENDER_DEADLINE_MS = 30 * 1000;

const DENIED_COMMANDS = new Set([
  "href", "url", "includegraphics", "htmlclass", "htmlid", "htmlstyle",
  "htmldata", "def", "gdef", "edef", "xdef", "let", "futurelet",
  "newcommand", "renewcommand", "providecommand", "global", "csname",
  "endcsname", "expandafter", "noexpand"
]);

const ALLOWED_TAGS = new Set([
  "annotation", "line", "math", "menclose", "mfrac", "mi", "mn", "mo",
  "mover", "mpadded", "mphantom", "mroot", "mrow", "mspace", "msqrt",
  "mstyle", "msub", "msubsup", "msup", "mtable", "mtd", "mtext", "mtr",
  "munder", "munderover", "path", "semantics", "span", "svg"
]);

const ALLOWED_CLASSES = new Set((
  "accent-body accent-full amsrm angl anglpad arraycolsep boldsymbol boxpad " +
  "brace-center brace-left brace-right cancel-lap cancel-pad cd-arrow-pad " +
  "cd-label-left cd-label-right cd-vert-arrow cjk_fallback clap col-align-c " +
  "col-align-l col-align-r delim-size1 delim-size4 delimcenter delimsizing " +
  "eqn-num fbox fcolorbox fleqn fontsize-ensurer frac-line halfarrow-left " +
  "halfarrow-right hide-tail katex katex-accent katex-base katex-display " +
  "katex-fix katex-hdashline katex-hline katex-html katex-inner katex-mathml " +
  "katex-newline katex-overlay katex-overline katex-root katex-rule " +
  "katex-sizing katex-smash katex-sout katex-stretchy katex-strut katex-tag " +
  "katex-thinbox katex-underline katex-vbox katex-version large-op leqno llap " +
  "mainrm mathbb mathbf mathboldfrak mathboldsf mathcal mathfrak mathit " +
  "mathitsf mathnormal mathrm mathscr mathsf mathsfit mathtt mbin mclose " +
  "mfrac minner mml-eqn-num mn mop mopen mord mover mrel mspace msupsub " +
  "mtable mtight mtr-glue mult munder nulldelimiter op-limits op-symbol " +
  "overline-line pstrut reset-size1 reset-size2 reset-size3 reset-size4 " +
  "reset-size5 reset-size6 reset-size7 reset-size8 reset-size9 reset-size10 " +
  "reset-size11 rlap size1 size2 size3 size4 size5 size6 size7 size8 size9 " +
  "size10 size11 small-op sqrt svg-align text textbb textbf textboldfrak " +
  "textboldsf textfrak textit textitsf textrm textscr textsf texttt " +
  "underline-line vertical-separator vlist vlist-r vlist-s vlist-t vlist-t2 " +
  "x-arrow x-arrow-pad"
).split(" "));

const SAFE_ATTRIBUTES = new Set([
  "accent", "accentunder", "aria-hidden", "bevelled", "class", "close",
  "columnalign", "columnlines", "columnspacing", "d", "display",
  "displaystyle", "encoding", "equalcolumns", "equalrows", "fence", "form",
  "frame", "framespacing", "height", "largeop", "linethickness", "lspace",
  "mathbackground", "mathcolor", "mathvariant", "maxsize", "minsize",
  "movablelimits", "notation", "open", "preserveaspectratio", "rowalign",
  "rowlines", "rowspacing", "rspace", "scriptlevel", "separator", "stretchy",
  "stroke-width", "style", "symmetric", "viewbox", "voffset", "width", "xmlns"
]);

const STYLE_PROPERTIES = new Set([
  "border-bottom-width", "border-right-width", "border-style",
  "border-top-width", "border-width", "bottom", "color", "height", "left",
  "margin-left", "margin-right", "min-width", "padding-left", "position",
  "top", "vertical-align", "width"
]);

let runtimePromise;
let stylesheetPromise;

export function preflightMathSource(source) {
  if (typeof source !== "string" || !source ||
      new TextEncoder().encode(source).byteLength > MAX_SOURCE_BYTES ||
      /[\0\x01-\x08\x0b\x0c\x0e-\x1f\x7f]/.test(source)) return false;
  const tokens = source.match(/\\[A-Za-z@]+|\\.|[^\x09-\x0d\x20]/gu) || [];
  if (tokens.length > MAX_TOKENS) return false;
  let depth = 0;
  for (let index = 0; index < source.length; index++) {
    if (source[index] === "\\") { index++; continue; }
    if (source[index] === "{" && ++depth > MAX_BRACE_DEPTH) return false;
    if (source[index] === "}" && depth > 0) depth--;
  }
  for (const match of source.matchAll(/\\([A-Za-z@]+|.)/g)) {
    const command = match[1].toLowerCase();
    if (DENIED_COMMANDS.has(command) || command.startsWith("html")) return false;
  }
  return true;
}

export function classListAllowed(value) {
  const tokens = value.split(/\s+/).filter(Boolean);
  return tokens.length > 0 && tokens.every((token) => ALLOWED_CLASSES.has(token));
}

function numericCssValue(value) {
  return /^[-+]?(?:\d+(?:\.\d+)?|\.\d+)(?:em|ex|mu|px|%)?$/.test(value) ||
    value === "0";
}

function colorValue(value) {
  return /^(?:#[0-9a-fA-F]{3,8}|[a-zA-Z]{1,24}|transparent)$/.test(value);
}

export function styleAllowed(value) {
  if (typeof value !== "string" || value.length > 1024 || /[()@\\]/.test(value)) {
    return false;
  }
  const declarations = value.split(";").filter(Boolean);
  if (declarations.length > 16) return false;
  return declarations.every((declaration) => {
    const separator = declaration.indexOf(":");
    if (separator <= 0 || declaration.indexOf(":", separator + 1) >= 0) return false;
    const property = declaration.slice(0, separator).trim().toLowerCase();
    const propertyValue = declaration.slice(separator + 1).trim();
    if (!STYLE_PROPERTIES.has(property)) return false;
    if (property === "position") return propertyValue === "relative";
    if (property === "border-style") return propertyValue === "solid";
    if (property === "color") return colorValue(propertyValue);
    return numericCssValue(propertyValue);
  });
}

function attributeAllowed(name, value) {
  const lower = name.toLowerCase();
  if (!SAFE_ATTRIBUTES.has(lower) || name.startsWith("on") || value.length > 8192) {
    return false;
  }
  if (lower === "class") return classListAllowed(value);
  if (lower === "style") return styleAllowed(value);
  if (lower === "encoding") return value === "application/x-tex";
  if (lower === "xmlns") {
    return value === "http://www.w3.org/1998/Math/MathML" ||
      value === "http://www.w3.org/2000/svg";
  }
  if (lower === "d") return value.length <= 8192 && /^[0-9A-Za-z,.\s+-]+$/.test(value);
  if (lower === "viewbox") return /^[-+0-9. ]{1,96}$/.test(value);
  if (lower === "preserveaspectratio") {
    return /^(?:none|x(?:Min|Mid|Max)Y(?:Min|Mid|Max) (?:meet|slice))$/.test(value);
  }
  if (lower === "mathcolor" || lower === "mathbackground") return colorValue(value);
  return /^[A-Za-z0-9#.,+\- %]{1,128}$/.test(value);
}

function rebuildNode(node, depth, budget) {
  if (depth > MAX_OUTPUT_DEPTH || ++budget.nodes > MAX_OUTPUT_NODES) return null;
  if (node.nodeType === Node.TEXT_NODE) return document.createTextNode(node.data);
  if (node.nodeType !== Node.ELEMENT_NODE) return null;
  const tag = node.localName.toLowerCase();
  if (!ALLOWED_TAGS.has(tag) || node.attributes.length > MAX_ATTRIBUTES) return null;
  const namespace = tag === "svg" || node.namespaceURI === "http://www.w3.org/2000/svg"
    ? "http://www.w3.org/2000/svg"
    : node.namespaceURI === "http://www.w3.org/1998/Math/MathML"
      ? "http://www.w3.org/1998/Math/MathML" : "http://www.w3.org/1999/xhtml";
  const output = document.createElementNS(namespace, tag);
  for (const attribute of node.attributes) {
    if (!attributeAllowed(attribute.name, attribute.value)) return null;
    output.setAttribute(attribute.name, attribute.value);
  }
  for (const child of node.childNodes) {
    const rebuilt = rebuildNode(child, depth + 1, budget);
    if (!rebuilt) return null;
    output.appendChild(rebuilt);
  }
  return output;
}

export function rebuildKatexCandidate(candidate) {
  if (typeof candidate !== "string" ||
      new TextEncoder().encode(candidate).byteLength > MAX_CANDIDATE_BYTES ||
      typeof DOMParser !== "function") return null;
  const parsed = new DOMParser().parseFromString(candidate, "text/html");
  if (!parsed || parsed.body.childNodes.length !== 1) return null;
  return rebuildNode(parsed.body.firstChild, 0, {nodes: 0});
}

function ensureStylesheet() {
  if (!stylesheetPromise) {
    stylesheetPromise = new Promise((resolve, reject) => {
      const existing = document.querySelector('link[data-mdv-katex="true"]');
      if (existing) { resolve(true); return; }
      const link = document.createElement("link");
      link.rel = "stylesheet";
      link.href = STYLESHEET_URL;
      link.setAttribute("data-mdv-katex", "true");
      link.onload = () => resolve(true);
      link.onerror = () => reject(new Error("stylesheet unavailable"));
      document.head.appendChild(link);
    });
  }
  return stylesheetPromise;
}

function loadRuntime() {
  if (!runtimePromise) runtimePromise = import(RUNTIME_URL);
  return runtimePromise;
}

function withDeadline(promise) {
  let timer;
  const deadline = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error("render deadline")),
      RENDER_DEADLINE_MS);
  });
  return Promise.race([promise, deadline]).finally(() => clearTimeout(timer));
}

export async function renderMath(container, nodeId, displayMode) {
  if (!container || container.getAttribute("data-mdv-node") !== nodeId ||
      container.getAttribute("data-mdv-math-rendered") === "true") return false;
  const sourceNode = container.querySelector(".md-math-input");
  const fallbackNode = container.querySelector(".md-math-source");
  if (!sourceNode || !fallbackNode) return false;
  const source = sourceNode.textContent;
  if (!preflightMathSource(source)) return false;
  const fallback = fallbackNode.textContent;
  try {
    const [, katex] = await withDeadline(
      Promise.all([ensureStylesheet(), loadRuntime()]));
    const candidate = katex.renderToString(source, {
      output: "htmlAndMathml", throwOnError: true, strict: "error",
      trust: false, globalGroup: false, maxSize: 16, maxExpand: 256,
      displayMode: displayMode === true, macros: Object.create(null)
    });
    const rebuilt = rebuildKatexCandidate(candidate);
    if (!rebuilt || !container.isConnected || sourceNode.textContent !== source ||
        fallbackNode.textContent !== fallback ||
        container.getAttribute("data-mdv-node") !== nodeId) return false;
    container.replaceChildren(rebuilt);
    container.setAttribute("data-mdv-math-rendered", "true");
    return true;
  } catch (_) {
    return false;
  }
}
