import { readFile, readdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const rootIndex = process.argv.indexOf("--root");
const designRoot = rootIndex === -1
  ? path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..")
  : path.resolve(process.argv[rootIndex + 1] ?? "");
if (rootIndex !== -1 && !process.argv[rootIndex + 1]) {
  throw new Error("--root requires a design root path");
}

const requiredIds = [
  "view.source", "view.preview", "view.split",
  "format.heading1", "format.heading2", "format.heading3",
  "format.bold", "format.italic", "format.strike", "format.inline-code",
  "block.bullet-list", "block.ordered-list", "block.task-list",
  "block.quote", "block.code", "block.table", "block.link", "block.divider",
  "structure.menu", "structure.outdent", "structure.indent",
  "structure.align-default", "structure.align-left",
  "structure.align-center", "structure.align-right",
];
const allowedGroups = new Set(["view", "heading", "inline", "block", "insert", "structure", "alignment"]);
const allowedSurfaces = new Set(["mdv-view-switch", "mdv-format-toolbar", "mdv-structure-menu"]);
const forbiddenSvgPatterns = [
  /<script\b/i, /<style\b/i, /<foreignObject\b/i, /<(?:image|use)\b/i,
  /<!DOCTYPE\b/i, /<!ENTITY\b/i, /\bon[a-z]+\s*=/i,
  /\b(?:xlink:)?href\s*=/i, /@import\b/i, /url\s*\(/i,
  /data:image/i, /https?:\/\//i, /assets[\\/]brand/i, /#[0-9a-f]{3,8}\b/i,
];

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function exactKeys(value, keys, label) {
  assert(value && typeof value === "object" && !Array.isArray(value), `${label} must be an object`);
  assert(
    JSON.stringify(Object.keys(value).sort()) === JSON.stringify([...keys].sort()),
    `${label} contains missing or unreviewed keys`,
  );
}

const manifest = JSON.parse(await readFile(path.join(designRoot, "icons/manifest.json"), "utf8"));
exactKeys(manifest, ["schemaVersion", "canvas", "source", "tokenSource", "metrics", "glyphs"], "manifest");
assert(manifest.schemaVersion === "mdv-toolbar-v1", "Unexpected toolbar schema");
assert(manifest.canvas === "0 0 24 24", "Toolbar glyph canvas must be 24x24");
assert(manifest.source === "crayon-original-glyphs", "Toolbar glyphs must remain Crayon-owned");
assert(manifest.tokenSource === "browser/shared-ui/design/tokens.json", "Unexpected token source");

const sharedTokens = JSON.parse(
  await readFile(path.resolve(designRoot, "../../design/tokens.json"), "utf8"),
);
exactKeys(
  manifest.metrics,
  ["glyphDip", "minimumHitTargetDip", "preferredHitTargetDip", "focusRingDip"],
  "metrics",
);
for (const key of Object.keys(manifest.metrics)) {
  assert(manifest.metrics[key] === sharedTokens.metrics[key], `metrics.${key} drifted from browser-design-v1`);
}

const ids = manifest.glyphs.map((glyph) => glyph.id);
assert(new Set(ids).size === ids.length, "Duplicate toolbar glyph id");
assert(
  JSON.stringify([...ids].sort()) === JSON.stringify([...requiredIds].sort()),
  "Toolbar glyph registry is incomplete or contains an unreviewed role",
);

const files = [];
for (const glyph of manifest.glyphs) {
  exactKeys(glyph, ["id", "file", "labelKey", "group", "surfaces", "mirrorInRtl"], `glyph ${glyph.id}`);
  assert(/^[a-z0-9-]+\.svg$/.test(glyph.file), `Invalid file for ${glyph.id}`);
  assert(/^mdv\.(?:view|tool)[_.]/.test(glyph.labelKey), `Invalid label key for ${glyph.id}`);
  assert(allowedGroups.has(glyph.group), `Invalid group for ${glyph.id}`);
  assert(Array.isArray(glyph.surfaces) && glyph.surfaces.length > 0, `Missing surface for ${glyph.id}`);
  assert(glyph.surfaces.every((surface) => allowedSurfaces.has(surface)), `Invalid surface for ${glyph.id}`);
  assert(typeof glyph.mirrorInRtl === "boolean", `Missing RTL policy for ${glyph.id}`);
  files.push(glyph.file);

  const svg = await readFile(path.join(designRoot, "icons", glyph.file), "utf8");
  assert(/<svg\b/.test(svg), `${glyph.file} is not SVG`);
  assert(/viewBox="0 0 24 24"/.test(svg), `${glyph.file} must use a 24x24 viewBox`);
  assert(/aria-hidden="true"/.test(svg), `${glyph.file} must be decorative`);
  assert(/focusable="false"/.test(svg), `${glyph.file} must not take focus`);
  assert(/currentColor/.test(svg), `${glyph.file} must inherit semantic color`);
  const scan = svg.replace('xmlns="http://www.w3.org/2000/svg"', "");
  for (const pattern of forbiddenSvgPatterns) {
    assert(!pattern.test(scan), `${glyph.file} contains forbidden content: ${pattern}`);
  }
}
assert(new Set(files).size === files.length, "Glyph files cannot be reused across semantic roles");
const actualFiles = (await readdir(path.join(designRoot, "icons")))
  .filter((name) => name.endsWith(".svg"))
  .sort();
assert(
  JSON.stringify(actualFiles) === JSON.stringify([...files].sort()),
  "Every SVG must be declared exactly once",
);

process.stdout.write(`Verified ${ids.length} MDV toolbar glyphs\n`);
