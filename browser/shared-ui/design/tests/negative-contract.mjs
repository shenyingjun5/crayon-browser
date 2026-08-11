import { cp, mkdtemp, mkdir, readFile, realpath, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const testsRoot = path.dirname(fileURLToPath(import.meta.url));
const sourceRoot = path.resolve(testsRoot, "..");
const verifier = path.join(testsRoot, "verify-design.mjs");
const workArgumentIndex = process.argv.indexOf("--work-root");
const workRootArgument = process.argv[workArgumentIndex + 1];
if (workArgumentIndex === -1 || !workRootArgument) {
  throw new Error("--work-root is required");
}

const workRoot = path.resolve(workRootArgument);
await mkdir(workRoot, { recursive: true });
const canonicalWorkRoot = await realpath(workRoot);
const temporaryRoot = await mkdtemp(path.join(canonicalWorkRoot, "design-negative-"));
if (path.dirname(temporaryRoot) !== canonicalWorkRoot) {
  throw new Error("Temporary contract root escaped the configured build directory");
}

function runVerifier(designRoot) {
  return spawnSync(process.execPath, [verifier, "--root", designRoot], {
    encoding: "utf8",
    windowsHide: true,
  });
}

async function withFixture(name, mutate) {
  const fixtureRoot = path.join(temporaryRoot, name);
  await cp(sourceRoot, fixtureRoot, { recursive: true });
  await mutate(fixtureRoot);
  const result = runVerifier(fixtureRoot);
  if (result.status === 0) {
    throw new Error(`${name} mutation was accepted unexpectedly`);
  }
}

try {
  await withFixture("missing-cast-role", async (fixtureRoot) => {
    const manifestPath = path.join(fixtureRoot, "icons", "manifest.json");
    const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
    manifest.glyphs = manifest.glyphs.filter((glyph) => glyph.id !== "cast.device");
    await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
  });

  await withFixture("external-svg", async (fixtureRoot) => {
    const iconPath = path.join(fixtureRoot, "icons", "nav-back.svg");
    const svg = await readFile(iconPath, "utf8");
    await writeFile(iconPath, svg.replace("</svg>", "  <image href=\"https://example.invalid/icon.png\"/>\n</svg>"), "utf8");
  });

  await withFixture("stale-golden", async (fixtureRoot) => {
    const goldenPath = path.join(fixtureRoot, "golden", "light-wide-100.json");
    const golden = JSON.parse(await readFile(goldenPath, "utf8"));
    golden.logical.shellHeightDip += 1;
    await writeFile(goldenPath, `${JSON.stringify(golden, null, 2)}\n`, "utf8");
  });

  await withFixture("missing-theme", async (fixtureRoot) => {
    const tokensPath = path.join(fixtureRoot, "tokens.json");
    const tokens = JSON.parse(await readFile(tokensPath, "utf8"));
    delete tokens.themes.dark;
    await writeFile(tokensPath, `${JSON.stringify(tokens, null, 2)}\n`, "utf8");
  });

  await withFixture("unreviewed-state", async (fixtureRoot) => {
    const tokensPath = path.join(fixtureRoot, "tokens.json");
    const tokens = JSON.parse(await readFile(tokensPath, "utf8"));
    tokens.states.cast.push("page-claimed-success");
    await writeFile(tokensPath, `${JSON.stringify(tokens, null, 2)}\n`, "utf8");
  });

  await withFixture("undeclared-icon", async (fixtureRoot) => {
    await writeFile(
      path.join(fixtureRoot, "icons", "unreviewed.svg"),
      '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"><path d="M1 1h1"/></svg>\n',
      "utf8",
    );
  });
} finally {
  const canonicalTemporaryRoot = await realpath(temporaryRoot);
  if (path.dirname(canonicalTemporaryRoot) !== canonicalWorkRoot) {
    throw new Error("Refusing to clean an unexpected contract directory");
  }
  await rm(canonicalTemporaryRoot, { recursive: true });
}

console.log("UX-001 browser design rejection contract passed");
