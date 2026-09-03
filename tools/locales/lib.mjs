import {createHash} from "node:crypto";
import {
  existsSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  statSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import {TextDecoder} from "node:util";

const SUPPORTED_LOCALES = ["en-US", "zh-CN", "zh-TW"];
const GENERATED_ROOT = "browser/shared-ui/localization/generated";
const KEY_PATTERN = /^[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+$/;
const RESOURCE_ID_PATTERN = /^IDS_CRAYON_[A-Z0-9_]+$/;
const PLACEHOLDER_PATTERN = /\{([a-z][a-z0-9_]*)\}/g;
const FORBIDDEN_CONTROL_PATTERN = /[\u0000-\u001f\u007f]/;

export function parseFlatStringCatalog(buffer, label) {
  let source;
  try {
    source = new TextDecoder("utf-8", {fatal: true}).decode(buffer);
  } catch {
    throw new Error(`${label}: invalid UTF-8`);
  }
  if (source.charCodeAt(0) === 0xfeff) {
    throw new Error(`${label}: UTF-8 BOM is not allowed`);
  }

  let offset = 0;
  const entries = [];
  const seen = new Set();
  const skipWhitespace = () => {
    while (offset < source.length && /[ \t\r\n]/.test(source[offset])) offset += 1;
  };
  const expect = (character) => {
    skipWhitespace();
    if (source[offset] !== character) {
      throw new Error(`${label}: expected ${JSON.stringify(character)} at byte-like offset ${offset}`);
    }
    offset += 1;
  };
  const parseString = () => {
    skipWhitespace();
    if (source[offset] !== '"') {
      throw new Error(`${label}: expected JSON string at byte-like offset ${offset}`);
    }
    const start = offset;
    offset += 1;
    let escaped = false;
    while (offset < source.length) {
      const character = source[offset];
      offset += 1;
      if (escaped) {
        escaped = false;
      } else if (character === "\\") {
        escaped = true;
      } else if (character === '"') {
        try {
          return JSON.parse(source.slice(start, offset));
        } catch (error) {
          throw new Error(`${label}: invalid JSON string: ${error.message}`);
        }
      } else if (character.charCodeAt(0) < 0x20) {
        throw new Error(`${label}: unescaped control character in JSON string`);
      }
    }
    throw new Error(`${label}: unterminated JSON string`);
  };

  expect("{");
  skipWhitespace();
  if (source[offset] === "}") {
    throw new Error(`${label}: catalog must not be empty`);
  }
  while (true) {
    const key = parseString();
    if (!KEY_PATTERN.test(key)) throw new Error(`${label}: invalid key ${JSON.stringify(key)}`);
    if (seen.has(key)) throw new Error(`${label}: duplicate key ${JSON.stringify(key)}`);
    seen.add(key);
    expect(":");
    const value = parseString();
    validateValue(label, key, value);
    entries.push([key, value]);
    skipWhitespace();
    if (source[offset] === "}") {
      offset += 1;
      break;
    }
    expect(",");
    skipWhitespace();
    if (source[offset] === "}") throw new Error(`${label}: trailing comma is not allowed`);
  }
  skipWhitespace();
  if (offset !== source.length) throw new Error(`${label}: trailing content after root object`);
  return entries;
}

export function validateCatalogs(catalogs, localeOrder = SUPPORTED_LOCALES) {
  if (localeOrder.join("\0") !== SUPPORTED_LOCALES.join("\0")) {
    throw new Error(`locale order must be ${SUPPORTED_LOCALES.join(",")}`);
  }
  for (const locale of localeOrder) {
    if (!catalogs.has(locale)) throw new Error(`missing locale catalog: ${locale}`);
  }
  const reference = catalogs.get(localeOrder[0]);
  const referenceKeys = reference.map(([key]) => key);
  for (const locale of localeOrder.slice(1)) {
    const entries = catalogs.get(locale);
    const keys = entries.map(([key]) => key);
    if (keys.length !== referenceKeys.length) {
      throw new Error(`${locale}: key count ${keys.length} differs from ${localeOrder[0]} ${referenceKeys.length}`);
    }
    for (let index = 0; index < referenceKeys.length; index += 1) {
      if (keys[index] !== referenceKeys[index]) {
        throw new Error(`${locale}: key order/parity mismatch at ${index}: expected ${referenceKeys[index]}, got ${keys[index]}`);
      }
      const referenceValue = reference[index][1];
      const value = entries[index][1];
      const expectedPlaceholders = extractPlaceholders(referenceValue);
      const actualPlaceholders = extractPlaceholders(value);
      if (expectedPlaceholders.join("\0") !== actualPlaceholders.join("\0")) {
        throw new Error(`${locale}:${keys[index]}: placeholder set differs from ${localeOrder[0]}`);
      }
      if (acceleratorCount(referenceValue) !== acceleratorCount(value)) {
        throw new Error(`${locale}:${keys[index]}: accelerator count differs from ${localeOrder[0]}`);
      }
    }
  }
  return referenceKeys;
}

export function buildOutputs(repoRoot) {
  const manifestRelative = "browser/shared-ui/locales/manifest.json";
  const manifestPath = resolveWithin(repoRoot, manifestRelative);
  const manifestBuffer = readFileSync(manifestPath);
  let manifest;
  try {
    manifest = JSON.parse(new TextDecoder("utf-8", {fatal: true}).decode(manifestBuffer));
  } catch (error) {
    throw new Error(`${manifestRelative}: invalid UTF-8 or JSON: ${error.message}`);
  }
  validateManifest(manifest);

  const catalogs = new Map();
  const sourceBuffers = new Map([[manifestRelative, normalizeLf(manifestBuffer)]]);
  for (const locale of manifest.locales) {
    const sourcePath = resolveWithin(repoRoot, locale.source);
    const sourceBuffer = readFileSync(sourcePath);
    catalogs.set(locale.tag, parseFlatStringCatalog(sourceBuffer, locale.source));
    sourceBuffers.set(locale.source, normalizeLf(sourceBuffer));
  }
  const keys = validateCatalogs(catalogs, manifest.locales.map(({tag}) => tag));
  const catalogMaps = new Map(
    [...catalogs].map(([locale, entries]) => [locale, new Map(entries)]),
  );
  validateWindowsResources(manifest.windows_resources, new Set(keys));

  const outputs = new Map();
  outputs.set(manifest.outputs.cpp_catalog, generateCppCatalog(manifest, keys, catalogMaps));
  outputs.set(manifest.outputs.windows_rc, generateWindowsRc(manifest, catalogMaps));
  for (const locale of manifest.locales) {
    outputs.set(
      `${manifest.outputs.macos_root}/${locale.macos_lproj}/Localizable.strings`,
      generateMacStrings(keys, catalogMaps.get(locale.tag)),
    );
    outputs.set(
      `${manifest.outputs.macos_root}/${locale.macos_lproj}/InfoPlist.strings`,
      generateMacInfoPlist(catalogMaps.get(locale.tag).get("app.title")),
    );
  }

  const lock = {
    schema_version: 1,
    catalog_version: manifest.catalog_version,
    locales: manifest.locales.map(({tag}) => tag),
    key_count: keys.length,
    source_files: inventory(sourceBuffers),
    generated_files: inventory(outputs),
  };
  outputs.set(manifest.outputs.lock, `${JSON.stringify(lock, null, 2)}\n`);
  for (const outputPath of outputs.keys()) {
    const normalized = outputPath.replaceAll("\\", "/");
    if (!normalized.startsWith(`${GENERATED_ROOT}/`)) {
      throw new Error(`generated output escaped managed root: ${outputPath}`);
    }
  }
  return {manifest, outputs, keyCount: keys.length};
}

export function writeOutputs(repoRoot, outputs) {
  for (const [relativePath, content] of outputs) {
    const target = resolveWithin(repoRoot, relativePath);
    mkdirSync(path.dirname(target), {recursive: true});
    writeFileSync(target, content, "utf8");
  }
}

export function verifyOutputs(repoRoot, outputs) {
  const expected = new Set();
  for (const [relativePath, content] of outputs) {
    expected.add(relativePath.replaceAll("\\", "/"));
    const target = resolveWithin(repoRoot, relativePath);
    if (!existsSync(target)) throw new Error(`generated output is missing: ${relativePath}`);
    const actual = readFileSync(target);
    const wanted = Buffer.from(content, "utf8");
    if (!actual.equals(wanted)) throw new Error(`generated output is stale: ${relativePath}`);
  }
  const generatedRoot = resolveWithin(repoRoot, GENERATED_ROOT);
  if (existsSync(generatedRoot)) {
    for (const filePath of walkFiles(generatedRoot)) {
      const relativePath = path.relative(repoRoot, filePath).replaceAll("\\", "/");
      if (!expected.has(relativePath)) throw new Error(`unexpected generated output: ${relativePath}`);
    }
  }
}

function validateValue(label, key, value) {
  if (typeof value !== "string") throw new Error(`${label}:${key}: value must be a string`);
  if (!value.trim()) throw new Error(`${label}:${key}: value must not be empty`);
  if (value.includes("\ufffd")) throw new Error(`${label}:${key}: replacement character is not allowed`);
  if (FORBIDDEN_CONTROL_PATTERN.test(value)) throw new Error(`${label}:${key}: control characters are not allowed`);
}

function validateManifest(manifest) {
  if (manifest?.schema_version !== 1) throw new Error("locale manifest schema_version must be 1");
  if (manifest.catalog_version !== "desktop-localization-v1") throw new Error("unexpected catalog_version");
  if (manifest.default_locale !== "en-US") throw new Error("default_locale must be en-US");
  if (!Array.isArray(manifest.locales)) throw new Error("manifest locales must be an array");
  const tags = manifest.locales.map(({tag}) => tag);
  if (tags.join("\0") !== SUPPORTED_LOCALES.join("\0")) {
    throw new Error(`manifest locales must be ${SUPPORTED_LOCALES.join(",")}`);
  }
  const sources = new Set();
  const lprojs = new Set();
  for (const locale of manifest.locales) {
    if (!locale.source || sources.has(locale.source)) throw new Error(`invalid/duplicate locale source for ${locale.tag}`);
    if (!locale.windows_language) throw new Error(`missing windows_language for ${locale.tag}`);
    if (!locale.macos_lproj || lprojs.has(locale.macos_lproj)) throw new Error(`invalid/duplicate macos_lproj for ${locale.tag}`);
    sources.add(locale.source);
    lprojs.add(locale.macos_lproj);
  }
  const outputs = manifest.outputs ?? {};
  for (const name of ["cpp_catalog", "windows_rc", "macos_root", "lock"]) {
    if (typeof outputs[name] !== "string" || !outputs[name]) throw new Error(`missing manifest output ${name}`);
  }
  if (!Array.isArray(manifest.windows_resources) || !manifest.windows_resources.length) {
    throw new Error("windows_resources must be a non-empty array");
  }
}

function validateWindowsResources(resources, keys) {
  const ids = new Set();
  const mappedKeys = new Set();
  for (const resource of resources) {
    if (!RESOURCE_ID_PATTERN.test(resource.id ?? "")) throw new Error(`invalid Windows resource id: ${resource.id}`);
    if (!keys.has(resource.key)) throw new Error(`Windows resource references unknown key: ${resource.key}`);
    if (ids.has(resource.id)) throw new Error(`duplicate Windows resource id: ${resource.id}`);
    if (mappedKeys.has(resource.key)) throw new Error(`duplicate Windows resource key: ${resource.key}`);
    ids.add(resource.id);
    mappedKeys.add(resource.key);
  }
}

function generateCppCatalog(manifest, keys, catalogMaps) {
  const lines = [
    "// Generated by tools/locales/generate.mjs. Do not edit.",
    "#ifndef CRAYON_BROWSER_SHARED_UI_LOCALIZATION_GENERATED_LOCALE_CATALOG_DATA_H_",
    "#define CRAYON_BROWSER_SHARED_UI_LOCALIZATION_GENERATED_LOCALE_CATALOG_DATA_H_",
    "",
    "#include <array>",
    "#include <string_view>",
    "",
    "namespace crayon::browser::localization::generated {",
    "",
    "struct LocaleCatalogEntry {",
    "  std::string_view key;",
    "  std::string_view en_us;",
    "  std::string_view zh_cn;",
    "  std::string_view zh_tw;",
    "};",
    "",
    `inline constexpr std::array<LocaleCatalogEntry, ${keys.length}> kLocaleCatalogEntries{{`,
  ];
  for (const key of keys) {
    lines.push(
      `    {${cppString(key)}, ${cppString(catalogMaps.get("en-US").get(key))}, ${cppString(catalogMaps.get("zh-CN").get(key))}, ${cppString(catalogMaps.get("zh-TW").get(key))}},`,
    );
  }
  lines.push(
    "}};",
    "",
    `inline constexpr std::string_view kCatalogVersion = ${cppString(manifest.catalog_version)};`,
    "",
    "}  // namespace crayon::browser::localization::generated",
    "",
    "#endif  // CRAYON_BROWSER_SHARED_UI_LOCALIZATION_GENERATED_LOCALE_CATALOG_DATA_H_",
    "",
  );
  return lines.join("\n");
}

function generateWindowsRc(manifest, catalogMaps) {
  const lines = ["// Generated by tools/locales/generate.mjs. Do not edit.", "#pragma code_page(65001)", ""];
  for (const locale of manifest.locales) {
    const catalog = catalogMaps.get(locale.tag);
    lines.push(`LANGUAGE ${locale.windows_language}`, "", "STRINGTABLE", "BEGIN");
    for (const resource of manifest.windows_resources) {
      lines.push(`  ${resource.id} ${rcString(catalog.get(resource.key))}`);
    }
    lines.push("END", "");
  }
  return lines.join("\n");
}

function generateMacStrings(keys, catalog) {
  return `${keys.map((key) => `${macString(key)} = ${macString(catalog.get(key))};`).join("\n")}\n`;
}

function generateMacInfoPlist(productName) {
  return `"CFBundleDisplayName" = ${macString(productName)};\n"CFBundleName" = ${macString(productName)};\n`;
}

function cppString(value) {
  return JSON.stringify(value);
}

function rcString(value) {
  return `"${value.replaceAll("\\", "\\\\").replaceAll('"', '""')}"`;
}

function macString(value) {
  return `"${value.replaceAll("\\", "\\\\").replaceAll('"', '\\"')}"`;
}

function extractPlaceholders(value) {
  return [...value.matchAll(PLACEHOLDER_PATTERN)].map((match) => match[1]).sort();
}

function acceleratorCount(value) {
  let count = 0;
  for (let index = 0; index < value.length; index += 1) {
    if (value[index] !== "&") continue;
    if (value[index + 1] === "&") {
      index += 1;
    } else {
      count += 1;
    }
  }
  return count;
}

function normalizeLf(buffer) {
  return Buffer.from(buffer.toString("utf8").replaceAll("\r\n", "\n"), "utf8");
}

function inventory(files) {
  return [...files]
    .sort(([left], [right]) => left.localeCompare(right, "en"))
    .map(([relativePath, content]) => ({
      path: relativePath.replaceAll("\\", "/"),
      sha256: createHash("sha256").update(content).digest("hex"),
      bytes: Buffer.byteLength(content),
    }));
}

function resolveWithin(repoRoot, relativePath) {
  const root = path.resolve(repoRoot);
  const resolved = path.resolve(root, relativePath);
  const relative = path.relative(root, resolved);
  if (!relative || relative.startsWith("..") || path.isAbsolute(relative)) {
    if (!relative) throw new Error(`path must name a file below repository root: ${relativePath}`);
    throw new Error(`path escaped repository root: ${relativePath}`);
  }
  return resolved;
}

function walkFiles(directory) {
  return readdirSync(directory, {withFileTypes: true})
    .sort((left, right) => left.name.localeCompare(right.name, "en"))
    .flatMap((entry) => {
      const filePath = path.join(directory, entry.name);
      if (entry.isSymbolicLink()) throw new Error(`symlink is not allowed in generated output: ${filePath}`);
      if (entry.isDirectory()) return walkFiles(filePath);
      if (!entry.isFile() || !statSync(filePath).isFile()) throw new Error(`unexpected generated entry: ${filePath}`);
      return [filePath];
    });
}
