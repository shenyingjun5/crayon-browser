import { readFile, readdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const rootArgumentIndex = process.argv.indexOf("--root");
const designRoot = rootArgumentIndex === -1
  ? path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..")
  : path.resolve(process.argv[rootArgumentIndex + 1] ?? "");
assertRootArgument();
const expectedSchemaVersion = "browser-design-v1";
const requiredGlyphIds = [
  "window.minimize",
  "window.maximize",
  "window.restore",
  "window.close",
  "tab.new",
  "tab.close",
  "tab.search",
  "nav.back",
  "nav.forward",
  "nav.reload",
  "nav.stop",
  "nav.home",
  "omnibox.site-info",
  "bookmark.outline",
  "bookmark.filled",
  "cast.device",
  "menu.more",
  "download.open",
  "history.open",
  "settings.open",
  "profile.open",
];
const forbiddenSvgPatterns = [
  /<script\b/i,
  /<style\b/i,
  /<foreignObject\b/i,
  /<(?:image|use)\b/i,
  /<!DOCTYPE\b/i,
  /<!ENTITY\b/i,
  /\bon[a-z]+\s*=/i,
  /\b(?:xlink:)?href\s*=/i,
  /@import\b/i,
  /url\s*\(/i,
  /data:image/i,
  /https?:\/\//i,
  /assets[\\/]brand/i,
  /#[0-9a-f]{3,8}\b/i,
];

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function assertRootArgument() {
  if (rootArgumentIndex !== -1 && !process.argv[rootArgumentIndex + 1]) {
    throw new Error("--root requires a design root path");
  }
}

async function readJson(relativePath) {
  const contents = await readFile(path.join(designRoot, relativePath), "utf8");
  return JSON.parse(contents);
}

function assertInteger(value, label) {
  assert(Number.isInteger(value) && value > 0, `${label} must be a positive integer`);
}

function assertExactKeys(value, expectedKeys, label) {
  assert(value && typeof value === "object" && !Array.isArray(value), `${label} must be an object`);
  const actualKeys = Object.keys(value).sort();
  assert(
    JSON.stringify(actualKeys) === JSON.stringify([...expectedKeys].sort()),
    `${label} contains missing or unreviewed keys`,
  );
}

function expectedGolden(tokens, themeName, viewportName, scale) {
  const metrics = tokens.metrics;
  const widthDip = tokens.viewports[viewportName].widthDip;
  return {
    schemaVersion: tokens.schemaVersion,
    theme: themeName,
    viewport: viewportName,
    deviceScaleFactor: scale,
    logical: {
      widthDip,
      tabStripHeightDip: metrics.tabStripHeightDip,
      navigationBarHeightDip: metrics.navigationBarHeightDip,
      shellHeightDip: metrics.shellHeightDip,
      iconCanvasDip: metrics.iconCanvasDip,
      glyphDip: metrics.glyphDip,
      minimumHitTargetDip: metrics.minimumHitTargetDip,
      focusRingDip: metrics.focusRingDip,
    },
    physical: {
      widthPx: widthDip * scale,
      shellHeightPx: metrics.shellHeightDip * scale,
      iconCanvasPx: metrics.iconCanvasDip * scale,
      glyphPx: metrics.glyphDip * scale,
      minimumHitTargetPx: metrics.minimumHitTargetDip * scale,
      focusRingPx: metrics.focusRingDip * scale,
    },
    colors: tokens.themes[themeName].colors,
    visibleControls: tokens.layouts[viewportName].visibleControls,
    overflowControls: tokens.layouts[viewportName].overflowControls,
  };
}

const tokens = await readJson("tokens.json");
assertExactKeys(
  tokens,
  [
    "schemaVersion",
    "units",
    "informationArchitecture",
    "viewports",
    "breakpoints",
    "scales",
    "metrics",
    "layouts",
    "states",
    "themes",
  ],
  "tokens",
);
assert(tokens.schemaVersion === expectedSchemaVersion, "Unexpected token schema version");
assert(tokens.units === "dip", "Design dimensions must use platform-neutral DIP units");
assertExactKeys(tokens.informationArchitecture, ["rows", "tabStripOrder", "navigationOrder"], "informationArchitecture");
assert(
  JSON.stringify(tokens.informationArchitecture.rows) ===
    JSON.stringify(["tab-strip", "navigation-bar"]),
  "Desktop chrome must retain the two-row information architecture",
);
assert(
  JSON.stringify(tokens.informationArchitecture.tabStripOrder) ===
    JSON.stringify(["app.window-identity", "tabs", "tab.new", "tab.search", "window.controls"]),
  "Tab-strip role order changed without a schema review",
);
assert(
  JSON.stringify(tokens.informationArchitecture.navigationOrder) ===
    JSON.stringify([
      "nav.back",
      "nav.forward",
      "nav.reload-stop",
      "nav.home",
      "omnibox",
      "bookmark.outline",
      "download.open",
      "cast.device",
      "profile.open",
      "menu.more",
    ]),
  "Navigation role order changed without a schema review",
);
for (const key of [
  "tabStripHeightDip",
  "navigationBarHeightDip",
  "shellHeightDip",
  "iconCanvasDip",
  "glyphDip",
  "minimumHitTargetDip",
  "focusRingDip",
]) {
  assertInteger(tokens.metrics[key], `metrics.${key}`);
}
assertExactKeys(
  tokens.metrics,
  [
    "tabStripHeightDip",
    "navigationBarHeightDip",
    "shellHeightDip",
    "iconCanvasDip",
    "glyphDip",
    "minimumHitTargetDip",
    "preferredHitTargetDip",
    "focusRingDip",
    "controlGapDip",
    "groupGapDip",
    "cornerRadiusDip",
    "pillRadiusDip",
    "tabMinWidthDip",
    "tabMaxWidthDip",
    "omniboxMinWidthDip",
  ],
  "metrics",
);
assert(
  tokens.metrics.tabStripHeightDip + tokens.metrics.navigationBarHeightDip ===
    tokens.metrics.shellHeightDip,
  "Shell height must equal the two visible row heights",
);
assert(tokens.metrics.iconCanvasDip === 24, "Glyph canvas must remain 24 DIP");
assert(tokens.metrics.glyphDip <= tokens.metrics.iconCanvasDip, "Glyph cannot exceed its canvas");
assert(tokens.metrics.minimumHitTargetDip >= 32, "Desktop hit target must be at least 32 DIP");
assert(
  JSON.stringify(tokens.scales) === JSON.stringify([1, 2]),
  "The contract must cover 100% and 200% scales",
);
assertExactKeys(tokens.themes, ["light", "dark"], "themes");
const requiredColorKeys = [
  "toolbarBackground",
  "tabStripBackground",
  "activeTabBackground",
  "inactiveTabForeground",
  "foreground",
  "mutedForeground",
  "hoverBackground",
  "pressedBackground",
  "focusRing",
  "separator",
  "brandAction",
  "success",
  "warning",
  "error",
];
for (const themeName of ["light", "dark"]) {
  assert(tokens.themes[themeName]?.colors, `Missing ${themeName} theme`);
  assertExactKeys(tokens.themes[themeName], ["colors"], `${themeName} theme`);
  assertExactKeys(tokens.themes[themeName].colors, requiredColorKeys, `${themeName} colors`);
}
assertExactKeys(tokens.viewports, ["narrow", "wide"], "viewports");
assertExactKeys(tokens.breakpoints, ["wideMinWidthDip"], "breakpoints");
assertInteger(tokens.breakpoints.wideMinWidthDip, "wide breakpoint");
assert(
  tokens.viewports.narrow.widthDip < tokens.breakpoints.wideMinWidthDip &&
    tokens.viewports.wide.widthDip >= tokens.breakpoints.wideMinWidthDip,
  "Viewport fixtures must straddle the wide breakpoint",
);
assertExactKeys(tokens.layouts, ["narrow", "wide"], "layouts");
const expectedLayouts = {
  narrow: {
    visibleControls: ["nav.back", "nav.forward", "nav.reload-stop", "omnibox", "cast.device", "menu.more"],
    overflowControls: ["nav.home", "bookmark.outline", "download.open", "profile.open"],
  },
  wide: {
    visibleControls: [
      "nav.back",
      "nav.forward",
      "nav.reload-stop",
      "nav.home",
      "omnibox",
      "bookmark.outline",
      "download.open",
      "cast.device",
      "profile.open",
      "menu.more",
    ],
    overflowControls: [],
  },
};
for (const viewportName of ["narrow", "wide"]) {
  assertExactKeys(tokens.viewports[viewportName], ["widthDip"], `${viewportName} viewport`);
  assertExactKeys(tokens.layouts[viewportName], ["visibleControls", "overflowControls"], `${viewportName} layout`);
  assertInteger(tokens.viewports[viewportName]?.widthDip, `${viewportName} viewport width`);
  assert(tokens.layouts[viewportName], `Missing ${viewportName} layout`);
  assert(
    tokens.layouts[viewportName].visibleControls.includes("cast.device"),
    `Cast must remain a first-level action in ${viewportName} layout`,
  );
  assert(
    JSON.stringify(tokens.layouts[viewportName].visibleControls) ===
      JSON.stringify(expectedLayouts[viewportName].visibleControls) &&
      JSON.stringify(tokens.layouts[viewportName].overflowControls) ===
        JSON.stringify(expectedLayouts[viewportName].overflowControls),
    `${viewportName} control priority changed without a schema review`,
  );
}
assertExactKeys(tokens.states, ["button", "tab", "omnibox", "cast"], "states");
for (const [stateName, expectedStates] of Object.entries({
  button: ["rest", "hover", "pressed", "focus-visible", "disabled"],
  tab: ["inactive", "hover", "active", "attention", "dragging"],
  omnibox: ["rest", "hover", "focused", "editing", "invalid"],
  cast: ["unavailable", "eligible", "selecting", "casting", "error"],
})) {
  assert(
    JSON.stringify(tokens.states[stateName]) === JSON.stringify(expectedStates),
    `${stateName} states changed without a schema review`,
  );
}

const manifest = await readJson("icons/manifest.json");
assertExactKeys(manifest, ["schemaVersion", "canvas", "source", "windowIdentity", "glyphs"], "icon manifest");
assert(manifest.schemaVersion === expectedSchemaVersion, "Unexpected icon manifest schema version");
assert(manifest.canvas === "0 0 24 24", "All functional glyphs must share a 24x24 canvas");
assert(
  manifest.windowIdentity?.source === "app-icon-v1:micro",
  "Window identity must use the managed app-icon-v1 micro asset",
);
assert(
  manifest.source === "crayon-original-glyphs",
  "Functional icons must remain owned Crayon glyphs",
);
assertExactKeys(manifest.windowIdentity, ["role", "source", "allowedSurfaces"], "window identity");
assert(manifest.windowIdentity.role === "app.window-identity", "Unexpected window identity role");
assert(
  JSON.stringify(manifest.windowIdentity.allowedSurfaces) ===
    JSON.stringify(["native-titlebar", "taskbar", "dock", "window-switcher"]),
  "The App icon may only appear on approved window-identity surfaces",
);
const glyphIds = manifest.glyphs.map((glyph) => glyph.id);
assert(new Set(glyphIds).size === glyphIds.length, "Glyph IDs must be unique");
assert(
  JSON.stringify([...glyphIds].sort()) === JSON.stringify([...requiredGlyphIds].sort()),
  "The title/tab/navigation glyph role set is incomplete or contains an unreviewed role",
);

const declaredFiles = [];
const allowedGlyphSurfaces = new Set([
  "custom-titlebar",
  "tab-strip",
  "navigation-bar",
  "overflow-menu",
  "omnibox",
  "new-tab",
]);
for (const glyph of manifest.glyphs) {
  assertExactKeys(glyph, ["id", "file", "labelKey", "surfaces", "mirrorInRtl"], `glyph ${glyph.id ?? "<unknown>"}`);
  assert(typeof glyph.file === "string" && /^[a-z0-9-]+\.svg$/.test(glyph.file), `Invalid file for ${glyph.id}`);
  assert(typeof glyph.labelKey === "string" && glyph.labelKey.includes("."), `Missing label key for ${glyph.id}`);
  assert(Array.isArray(glyph.surfaces) && glyph.surfaces.length > 0, `Missing surface for ${glyph.id}`);
  assert(glyph.surfaces.every((surface) => allowedGlyphSurfaces.has(surface)), `Unreviewed surface for ${glyph.id}`);
  assert(typeof glyph.mirrorInRtl === "boolean", `RTL behavior must be explicit for ${glyph.id}`);
  declaredFiles.push(glyph.file);
  const svg = await readFile(path.join(designRoot, "icons", glyph.file), "utf8");
  assert(/<svg\b/.test(svg), `${glyph.file} is not an SVG`);
  assert(/viewBox="0 0 24 24"/.test(svg), `${glyph.file} must use the shared 24x24 viewBox`);
  assert(/aria-hidden="true"/.test(svg), `${glyph.file} must defer its accessible name to the control`);
  assert(/currentColor/.test(svg), `${glyph.file} must inherit semantic state color`);
  const externalContentScan = svg.replace('xmlns="http://www.w3.org/2000/svg"', "");
  for (const pattern of forbiddenSvgPatterns) {
    assert(!pattern.test(externalContentScan), `${glyph.file} contains forbidden SVG content: ${pattern}`);
  }
}
assert(new Set(declaredFiles).size === declaredFiles.length, "Glyph files must not be reused across roles");
const actualSvgFiles = (await readdir(path.join(designRoot, "icons")))
  .filter((name) => name.endsWith(".svg"))
  .sort();
assert(
  JSON.stringify(actualSvgFiles) === JSON.stringify([...declaredFiles].sort()),
  "Every SVG must be declared exactly once in the icon manifest",
);

const expectedGoldenFiles = [];
for (const themeName of ["light", "dark"]) {
  for (const viewportName of ["narrow", "wide"]) {
    for (const scale of tokens.scales) {
      const scaleName = scale === 1 ? "100" : "200";
      const fileName = `${themeName}-${viewportName}-${scaleName}.json`;
      expectedGoldenFiles.push(fileName);
      const actual = await readJson(path.join("golden", fileName));
      assert(
        JSON.stringify(actual) === JSON.stringify(expectedGolden(tokens, themeName, viewportName, scale)),
        `${fileName} does not match the deterministic design specification`,
      );
    }
  }
}
const actualGoldenFiles = (await readdir(path.join(designRoot, "golden")))
  .filter((name) => name.endsWith(".json"))
  .sort();
assert(
  JSON.stringify(actualGoldenFiles) === JSON.stringify(expectedGoldenFiles.sort()),
  "Golden set must contain exactly eight theme/viewport/scale combinations",
);

console.log("UX-001 browser design contract passed");
