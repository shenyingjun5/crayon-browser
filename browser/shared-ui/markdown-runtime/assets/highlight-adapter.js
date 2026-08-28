const RESOURCE_PREFIX = "/runtime/highlight/";
const MAX_CANDIDATE_BYTES = 2 * 1024 * 1024;
const MAX_TOKEN_DEPTH = 64;
const MAX_TOKEN_NODES = 32768;

const LOAD_ORDERS = Object.freeze({
  bash: ["bash"], c: ["c"], cpp: ["cpp"], csharp: ["csharp"],
  css: ["css"], diff: ["diff"], dockerfile: ["bash", "dockerfile"],
  go: ["go"], graphql: ["graphql"], java: ["java"],
  javascript: ["css", "graphql", "xml", "javascript"], json: ["json"],
  kotlin: ["kotlin"], markdown: ["css", "graphql", "javascript", "xml", "markdown"],
  objectivec: ["objectivec"], php: ["php"], powershell: ["powershell"],
  python: ["python"], ruby: ["ruby"], rust: ["rust"], sql: ["sql"],
  swift: ["swift"], typescript: ["css", "graphql", "javascript", "xml", "typescript"],
  xml: ["css", "graphql", "javascript", "xml"], yaml: ["ruby", "yaml"]
});

let corePromise;
const grammarPromises = new Map();

function decodeText(value) {
  let output = "";
  for (let index = 0; index < value.length;) {
    if (value[index] !== "&") {
      output += value[index++];
      continue;
    }
    const entities = [
      ["&amp;", "&"], ["&lt;", "<"], ["&gt;", ">"],
      ["&quot;", "\""], ["&#x27;", "'"], ["&#39;", "'"]
    ];
    const entity = entities.find(([encoded]) => value.startsWith(encoded, index));
    if (!entity) return null;
    output += entity[1];
    index += entity[0].length;
  }
  return output;
}

export function parseHighlightCandidate(candidate) {
  if (typeof candidate !== "string" ||
      new TextEncoder().encode(candidate).byteLength > MAX_CANDIDATE_BYTES) {
    return null;
  }
  const root = {children: []};
  const stack = [root];
  let index = 0;
  let nodes = 0;
  while (index < candidate.length) {
    if (candidate.startsWith("</span>", index)) {
      if (stack.length === 1) return null;
      stack.pop();
      index += 7;
      continue;
    }
    if (candidate.startsWith("<span class=\"", index)) {
      const valueStart = index + 13;
      const valueEnd = candidate.indexOf("\">", valueStart);
      if (valueEnd < 0 || stack.length > MAX_TOKEN_DEPTH) return null;
      const rawClasses = candidate.slice(valueStart, valueEnd).split(" ");
      const classes = rawClasses.filter((token) => /^hljs-[a-z0-9_-]+$/.test(token));
      const token = {classes, children: []};
      stack[stack.length - 1].children.push(token);
      stack.push(token);
      index = valueEnd + 2;
      if (++nodes > MAX_TOKEN_NODES) return null;
      continue;
    }
    if (candidate[index] === "<") return null;
    const nextTag = candidate.indexOf("<", index);
    const end = nextTag < 0 ? candidate.length : nextTag;
    const text = decodeText(candidate.slice(index, end));
    if (text === null) return null;
    if (text) stack[stack.length - 1].children.push({text});
    index = end;
    if (++nodes > MAX_TOKEN_NODES) return null;
  }
  return stack.length === 1 ? root.children : null;
}

export function loadOrderForLanguage(canonical) {
  const order = LOAD_ORDERS[canonical];
  return order ? order.slice() : null;
}

async function loadHighlighter(canonical) {
  const order = loadOrderForLanguage(canonical);
  if (!order) return null;
  if (!corePromise) {
    corePromise = import(RESOURCE_PREFIX + "core").then((module) => module.default);
  }
  const highlighter = await corePromise;
  for (const grammarId of order) {
    if (!grammarPromises.has(grammarId)) {
      grammarPromises.set(grammarId,
        import(RESOURCE_PREFIX + grammarId).then((module) => {
          highlighter.registerLanguage(grammarId, module.default);
          return true;
        }));
    }
    await grammarPromises.get(grammarId);
  }
  return highlighter;
}

function appendTokenTree(parent, tokens) {
  for (const token of tokens) {
    if (Object.hasOwn(token, "text")) {
      parent.appendChild(document.createTextNode(token.text));
      continue;
    }
    const container = token.classes.length ? document.createElement("span") : parent;
    if (token.classes.length) {
      container.className = token.classes.join(" ");
      parent.appendChild(container);
    }
    appendTokenTree(container, token.children);
  }
}

export async function highlightCode(code, canonical, nodeId) {
  const order = loadOrderForLanguage(canonical);
  if (!code || !order || code.getAttribute("data-mdv-node") !== nodeId) return false;
  const source = code.textContent;
  try {
    const highlighter = await loadHighlighter(canonical);
    const candidate = highlighter.highlight(source, {
      language: canonical,
      ignoreIllegals: true
    }).value;
    const tokens = parseHighlightCandidate(candidate);
    if (!tokens || !code.isConnected || code.textContent !== source ||
        code.getAttribute("data-mdv-node") !== nodeId) return false;
    const fragment = document.createDocumentFragment();
    appendTokenTree(fragment, tokens);
    code.replaceChildren(fragment);
    code.setAttribute("data-mdv-highlighted", "true");
    return true;
  } catch (_) {
    return false;
  }
}
