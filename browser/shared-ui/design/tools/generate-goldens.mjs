import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const designRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const tokens = JSON.parse(await readFile(path.join(designRoot, "tokens.json"), "utf8"));
const outputRoot = path.join(designRoot, "golden");

function makeGolden(themeName, viewportName, scale) {
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

await mkdir(outputRoot, { recursive: true });
for (const themeName of ["light", "dark"]) {
  for (const viewportName of ["narrow", "wide"]) {
    for (const scale of tokens.scales) {
      const scaleName = scale === 1 ? "100" : "200";
      const fileName = `${themeName}-${viewportName}-${scaleName}.json`;
      const contents = `${JSON.stringify(makeGolden(themeName, viewportName, scale), null, 2)}\n`;
      await writeFile(path.join(outputRoot, fileName), contents, "utf8");
    }
  }
}

console.log("Generated eight UX-001 specification goldens");
