import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { readIcnsDirectory, readIcoDirectory } from "./containers.mjs";
import { decodePng, readPngHeader } from "./png.mjs";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "../..");
const manifest = readJson("assets/brand/manifest.json");
const lock = readJson("assets/brand/generated/manifest-lock.json");
const failures = [];

checkReference();
checkSvgSources();
checkInventory(lock.source_files);
checkInventory(lock.generated_files);
checkWindows();
checkMacos();
checkHarmony();

if (failures.length > 0) {
  process.stderr.write(`${JSON.stringify({ passed: false, failures }, null, 2)}\n`);
  process.exitCode = 1;
} else {
  process.stdout.write(`${JSON.stringify({ passed: true, brand_version: manifest.brand_version, checks: 8, generated_files: lock.generated_files.length })}\n`);
}

function checkReference() {
  const data = readFile(manifest.reference.path);
  const header = readPngHeader(data);
  expect(sha256(data) === manifest.reference.sha256, "BI-001 reference SHA-256 mismatch");
  expect(header.width === manifest.reference.width && header.height === manifest.reference.height, "BI-001 reference dimensions mismatch");
  expect(header.colorType === manifest.reference.png_color_type, "BI-001 reference PNG color type mismatch");
}

function checkSvgSources() {
  for (const [name, relativePath] of Object.entries(manifest.sources)) {
    const source = readFileSync(resolve(relativePath), "utf8");
    const sourceWithoutNamespace = source.replace("http://www.w3.org/2000/svg", "");
    expect(/^<svg[\s>]/.test(source.trim()), `BI-002 ${name} is not an SVG root`);
    expect(/viewBox="0 0 (1024 1024|48 48)"/.test(source), `BI-002 ${name} has an unexpected viewBox`);
    expect(!/<(?:script|foreignObject|image)\b/i.test(source), `BI-002 ${name} contains forbidden executable or raster content`);
    expect(!/(?:https?:|data:|file:)/i.test(sourceWithoutNamespace), `BI-002 ${name} contains an external or embedded URL`);
  }
}

function checkInventory(entries) {
  for (const entry of entries) {
    const data = readFile(entry.path);
    expect(data.length === entry.bytes, `BI-003 byte length drift: ${entry.path}`);
    expect(sha256(data) === entry.sha256, `BI-003 hash drift: ${entry.path}`);
  }
}

function checkWindows() {
  for (const size of manifest.windows.sizes) {
    const relativePath = `assets/brand/generated/windows/png/app-icon-${size}.png`;
    const image = decodePng(readFile(relativePath));
    checkDimensions(image, size, relativePath);
    checkTransparentCorners(image, relativePath);
    checkNoDarkFringe(image, relativePath);
    if (size <= manifest.render.micro_max_size) checkMicroSignals(image, relativePath);
  }
  const ico = readFile("assets/brand/generated/windows/app.ico");
  const entries = readIcoDirectory(ico);
  expect(equalNumbers(entries.map(({ width }) => width), manifest.windows.sizes), "BI-006 ICO size directory mismatch");
  for (const entry of entries) {
    expect(entry.width === entry.height && entry.planes === 1 && entry.bitCount === 32, `BI-006 invalid ICO entry ${entry.width}`);
    const payload = ico.subarray(entry.payloadOffset, entry.payloadOffset + entry.length);
    const header = readPngHeader(payload);
    expect(header.width === entry.width && header.height === entry.height && header.colorType === 6, `BI-006 invalid ICO PNG ${entry.width}`);
  }
}

function checkMacos() {
  const sizes = [];
  for (const item of manifest.macos.iconset) {
    const relativePath = `assets/brand/generated/macos/AppIcon.iconset/${item.file}`;
    const image = decodePng(readFile(relativePath));
    checkDimensions(image, item.size, relativePath);
    const corners = cornerOffsets(image);
    expect(corners.every((offset) => image.pixels[offset + 3] === 255), `BI-004 macOS background is not full square: ${relativePath}`);
    expect(corners.every((offset) => image.pixels[offset + 2] > image.pixels[offset]), `BI-004 macOS corner is not blue: ${relativePath}`);
    sizes.push(item.size);
  }
  const requiredTypes = ["icp4", "icp5", "icp6", "ic07", "ic08", "ic09", "ic10"];
  const entries = readIcnsDirectory(readFile("assets/brand/generated/macos/app.icns"));
  expect(requiredTypes.every((type) => entries.some((entry) => entry.type === type)), "BI-007 ICNS chunks are incomplete");
  expect(new Set(sizes).size === 7, "BI-007 macOS iconset unique size count mismatch");
}

function checkHarmony() {
  for (const size of manifest.harmony.sizes) {
    const relativePath = `assets/brand/generated/harmony/app-icon/app-icon-${size}.png`;
    const image = decodePng(readFile(relativePath));
    checkDimensions(image, size, relativePath);
    checkTransparentCorners(image, relativePath);
    checkNoDarkFringe(image, relativePath);
  }
}

function checkDimensions(image, size, relativePath) {
  expect(image.width === size && image.height === size && image.colorType === 6, `BI-004 dimensions/color mismatch: ${relativePath}`);
}

function checkTransparentCorners(image, relativePath) {
  expect(cornerOffsets(image).every((offset) => image.pixels[offset + 3] <= 64), `BI-004 transparent corners missing: ${relativePath}`);
}

function checkNoDarkFringe(image, relativePath) {
  let darkTranslucent = 0;
  for (let offset = 0; offset < image.pixels.length; offset += 4) {
    const alpha = image.pixels[offset + 3];
    if (alpha > 0 && alpha < 255 && image.pixels[offset] < 12 && image.pixels[offset + 1] < 12 && image.pixels[offset + 2] < 12) {
      darkTranslucent += 1;
    }
  }
  expect(darkTranslucent === 0, `BI-004 dark alpha fringe: ${relativePath}`);
}

function checkMicroSignals(image, relativePath) {
  let cream = 0;
  let green = 0;
  for (let offset = 0; offset < image.pixels.length; offset += 4) {
    const [red, greenValue, blue, alpha] = image.pixels.subarray(offset, offset + 4);
    if (alpha > 160 && red > 210 && greenValue > 205 && blue > 190) cream += 1;
    if (alpha > 160 && greenValue > red * 1.15 && greenValue > blue * 1.05) green += 1;
  }
  expect(cream >= Math.max(2, Math.floor(image.width * image.height * 0.08)), `BI-005 cream browser/crayon signal missing: ${relativePath}`);
  expect(green >= 1, `BI-005 green crayon tip signal missing: ${relativePath}`);
}

function cornerOffsets(image) {
  return [
    0,
    (image.width - 1) * 4,
    (image.height - 1) * image.width * 4,
    (image.width * image.height - 1) * 4,
  ];
}

function readJson(relativePath) {
  return JSON.parse(readFileSync(resolve(relativePath), "utf8"));
}

function readFile(relativePath) {
  const filePath = resolve(relativePath);
  if (!existsSync(filePath)) {
    failures.push(`missing file: ${relativePath}`);
    return Buffer.alloc(0);
  }
  return readFileSync(filePath);
}

function resolve(relativePath) {
  const absolute = path.resolve(repoRoot, relativePath);
  const relative = path.relative(repoRoot, absolute);
  if (!relative || relative.startsWith("..") || path.isAbsolute(relative)) throw new Error(`path escaped repository: ${relativePath}`);
  return absolute;
}

function sha256(buffer) {
  return createHash("sha256").update(buffer).digest("hex");
}

function equalNumbers(left, right) {
  return left.length === right.length && left.every((value, index) => value === right[index]);
}

function expect(condition, message) {
  if (!condition) failures.push(message);
}
