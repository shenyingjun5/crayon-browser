import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { createIcns, createIco } from "./containers.mjs";
import { resetManagedDirectory, resolveWithinRoot } from "./managed-paths.mjs";
import { composite, decodePng, encodePng, recoverAlpha, resizeArea, solidImage } from "./png.mjs";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "../..");
const manifestPath = path.join(repoRoot, "assets/brand/manifest.json");
const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
const generatedRoot = path.join(repoRoot, "assets/brand/generated");
const renderRoot = path.join(repoRoot, "target/brand-assets/render");
const chromePath = findChrome();

resetManagedDirectory(repoRoot, generatedRoot);
resetManagedDirectory(repoRoot, renderRoot);

const masterSvg = readSource(manifest.sources.master);
const microSvg = readSource(manifest.sources.micro);
const macMasterSvg = squareBackground(masterSvg);
const macMicroSvg = squareBackground(microSvg);

const rendered = {
  master: renderSvg(masterSvg, "master"),
  micro: renderSvg(microSvg, "micro"),
  macMaster: renderSvg(macMasterSvg, "mac-master"),
  macMicro: renderSvg(macMicroSvg, "mac-micro"),
};

const windowsImages = new Map();
for (const size of manifest.windows.sizes) {
  const image = resizeArea(size <= manifest.render.micro_max_size ? rendered.micro : rendered.master, size);
  windowsImages.set(size, image);
  writePng(`windows/png/app-icon-${size}.png`, image);
}
writeGenerated(
  "windows/app.ico",
  createIco(manifest.windows.sizes.map((size) => ({ size, png: encodePng(windowsImages.get(size)) }))),
);

const macImages = new Map();
for (const item of manifest.macos.iconset) {
  const source = item.size <= manifest.render.micro_max_size ? rendered.macMicro : rendered.macMaster;
  const image = resizeArea(source, item.size);
  macImages.set(item.size, image);
  writePng(`macos/AppIcon.iconset/${item.file}`, image);
}
const icnsEntries = [...new Set(manifest.macos.iconset.map(({ size }) => size))]
  .sort((left, right) => left - right)
  .map((size) => ({ size, png: encodePng(macImages.get(size)) }));
writeGenerated("macos/app.icns", createIcns(icnsEntries));

for (const size of manifest.harmony.sizes) {
  writePng(`harmony/app-icon/app-icon-${size}.png`, resizeArea(rendered.master, size));
}

writePng("previews/master-1024.png", rendered.master);
writePng("previews/macos-1024.png", rendered.macMaster);
const previewSizes = [...new Set([...manifest.windows.sizes, 512, manifest.render.canvas_size])].sort((left, right) => left - right);
const previewImages = new Map(windowsImages);
for (const size of previewSizes) {
  if (!previewImages.has(size)) previewImages.set(size, resizeArea(rendered.master, size));
}
writePng("previews/contact-sheet.png", contactSheet(previewImages, previewSizes));

const chromeVersion = readChromeVersion();
const lock = {
  schema_version: 1,
  brand_version: manifest.brand_version,
  renderer: chromeVersion,
  source_files: sourceInventory(),
  generated_files: generatedInventory(),
};
writeGenerated("manifest-lock.json", Buffer.from(`${JSON.stringify(lock, null, 2)}\n`));
process.stdout.write(`${JSON.stringify({ passed: true, brand_version: manifest.brand_version, renderer: chromeVersion, files: lock.generated_files.length })}\n`);

function readSource(relativePath) {
  const sourcePath = resolveWithinRoot(repoRoot, relativePath);
  const source = readFileSync(sourcePath, "utf8");
  const sourceWithoutNamespace = source.replace("http://www.w3.org/2000/svg", "");
  if (!/^<svg[\s>]/.test(source.trim())) throw new Error(`brand source is not an SVG root: ${relativePath}`);
  if (/<(?:script|foreignObject|image)\b/i.test(source)) throw new Error(`brand source contains executable or raster content: ${relativePath}`);
  if (/(?:https?:|data:|file:)/i.test(sourceWithoutNamespace)) throw new Error(`brand source contains an external or embedded URL: ${relativePath}`);
  return source;
}

function squareBackground(svg) {
  const replaced = svg.replace(
    /(<rect id="background"[^>]*\brx=")[^"]+("[^>]*>)/,
    (_match, prefix, suffix) => `${prefix}0${suffix}`,
  );
  if (replaced === svg) throw new Error("SVG background rect is missing an rx attribute");
  return replaced;
}

function renderSvg(svg, name) {
  const blackMatte = renderSvgMatte(svg, name, "black", "#000000");
  const whiteMatte = renderSvgMatte(svg, name, "white", "#ffffff");
  return recoverAlpha(blackMatte, whiteMatte);
}

function renderSvgMatte(svg, name, matteName, matteColor) {
  const htmlPath = path.join(renderRoot, `${name}.html`);
  const screenshotPath = path.join(renderRoot, `${name}-${matteName}.png`);
  const profilePath = path.join(renderRoot, `${name}-${matteName}-profile`);
  const html = `<!doctype html><meta charset="utf-8"><style>html,body{margin:0;width:100%;height:100%;overflow:hidden;background:${matteColor}}svg{display:block;width:1024px;height:1024px}</style>${svg}`;
  writeFileSync(htmlPath, html);
  const result = spawnSync(chromePath, [
    "--headless=new",
    "--disable-gpu",
    "--hide-scrollbars",
    "--no-first-run",
    "--force-device-scale-factor=1",
    "--default-background-color=00000000",
    "--window-size=1024,1024",
    `--user-data-dir=${profilePath}`,
    `--screenshot=${screenshotPath}`,
    pathToFileURL(htmlPath).href,
  ], { encoding: "utf8", timeout: 30_000 });
  if (result.error) throw result.error;
  waitForFile(screenshotPath);
  return decodePng(readFileSync(screenshotPath));
}

function findChrome() {
  const explicit = process.argv.find((argument) => argument.startsWith("--chrome="))?.slice(9)
    || process.env.CRAYON_CHROME;
  const candidates = [
    explicit,
    process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES, "Google/Chrome/Application/chrome.exe"),
    process.env["PROGRAMFILES(X86)"] && path.join(process.env["PROGRAMFILES(X86)"], "Google/Chrome/Application/chrome.exe"),
    process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES, "Microsoft/Edge/Application/msedge.exe"),
    process.platform === "darwin" && "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    process.platform === "darwin" && "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
  ].filter(Boolean);
  const found = candidates.find((candidate) => existsSync(candidate));
  if (!found) throw new Error("Chrome/Edge not found; set CRAYON_CHROME to an explicit executable");
  return found;
}

function readChromeVersion() {
  if (process.platform === "win32") {
    if (!process.env.SystemRoot) throw new Error("SystemRoot is required to read Chromium file version");
    const powershell = path.join(process.env.SystemRoot, "System32/WindowsPowerShell/v1.0/powershell.exe");
    const escapedPath = chromePath.replaceAll("'", "''");
    const result = spawnSync(powershell, ["-NoProfile", "-Command", `(Get-Item -LiteralPath '${escapedPath}').VersionInfo.ProductVersion`], { encoding: "utf8" });
    if (result.status === 0 && result.stdout.trim()) return `Chromium ${result.stdout.trim()}`;
    throw new Error(`cannot read Chromium file version: ${result.stderr.trim()}`);
  }
  const result = spawnSync(chromePath, ["--version"], { encoding: "utf8" });
  if (result.status === 0 && result.stdout.trim()) return result.stdout.trim();
  throw new Error(`cannot read Chromium version: ${result.stderr.trim()}`);
}

function waitForFile(filePath) {
  const deadline = Date.now() + 5_000;
  const waitArray = new Int32Array(new SharedArrayBuffer(4));
  while (!existsSync(filePath) && Date.now() < deadline) Atomics.wait(waitArray, 0, 0, 50);
  if (!existsSync(filePath)) throw new Error(`renderer did not create ${filePath}`);
}

function writePng(relativePath, image) {
  writeGenerated(relativePath, encodePng(image));
}

function writeGenerated(relativePath, data) {
  const target = path.resolve(generatedRoot, relativePath);
  const relative = path.relative(generatedRoot, target);
  if (relative.startsWith("..") || path.isAbsolute(relative)) throw new Error(`output escaped generated root: ${relativePath}`);
  mkdirSync(path.dirname(target), { recursive: true });
  writeFileSync(target, data);
}

function contactSheet(images, sizes) {
  const padding = 20;
  const gap = 24;
  const rowHeight = Math.max(...sizes) + padding * 2;
  const width = sizes.reduce((sum, size) => sum + size + gap, gap);
  const sheet = solidImage(width, rowHeight * 2, 243, 244, 246);
  for (let y = rowHeight; y < sheet.height; y += 1) {
    for (let x = 0; x < sheet.width; x += 1) {
      const offset = (y * sheet.width + x) * 4;
      sheet.pixels[offset] = 21;
      sheet.pixels[offset + 1] = 25;
      sheet.pixels[offset + 2] = 34;
    }
  }
  let x = gap;
  for (const size of sizes) {
    composite(sheet, images.get(size), x, Math.floor((rowHeight - size) / 2));
    composite(sheet, images.get(size), x, rowHeight + Math.floor((rowHeight - size) / 2));
    x += size + gap;
  }
  return sheet;
}

function sourceInventory() {
  const paths = [manifestPath, ...Object.values(manifest.sources).map((item) => resolveWithinRoot(repoRoot, item))];
  return paths.map((filePath) => inventoryEntry(filePath));
}

function generatedInventory() {
  return walkFiles(generatedRoot)
    .filter((filePath) => path.basename(filePath) !== "manifest-lock.json")
    .map((filePath) => inventoryEntry(filePath));
}

function walkFiles(directory) {
  return readdirSync(directory, { withFileTypes: true })
    .sort((left, right) => left.name.localeCompare(right.name, "en"))
    .flatMap((entry) => {
      const filePath = path.join(directory, entry.name);
      return entry.isDirectory() ? walkFiles(filePath) : [filePath];
    });
}

function inventoryEntry(filePath) {
  const data = readFileSync(filePath);
  return {
    path: path.relative(repoRoot, filePath).replaceAll(path.sep, "/"),
    bytes: data.length,
    sha256: createHash("sha256").update(data).digest("hex"),
  };
}
