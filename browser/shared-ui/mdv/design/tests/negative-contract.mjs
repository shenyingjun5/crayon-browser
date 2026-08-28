import { cp, mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const workIndex = process.argv.indexOf("--work-root");
const workRoot = workIndex === -1
  ? os.tmpdir()
  : path.resolve(process.argv[workIndex + 1] ?? "");
if (workIndex !== -1 && !process.argv[workIndex + 1]) {
  throw new Error("--work-root requires a path");
}
const sourceRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const verifier = path.join(sourceRoot, "tests/verify-icons.mjs");

async function expectRejected(name, mutate) {
  const fixtureBase = await mkdtemp(path.join(workRoot, `mdv-toolbar-${name}-`));
  const fixtureRoot = path.join(fixtureBase, "mdv", "design");
  try {
    await mkdir(path.dirname(fixtureRoot), { recursive: true });
    await cp(sourceRoot, fixtureRoot, { recursive: true });
    const sharedDesignRoot = path.resolve(sourceRoot, "../../design");
    await mkdir(path.join(fixtureBase, "design"), { recursive: true });
    await cp(
      path.join(sharedDesignRoot, "tokens.json"),
      path.join(fixtureBase, "design", "tokens.json"),
    );
    await mutate(fixtureRoot);
    const result = spawnSync(process.execPath, [verifier, "--root", fixtureRoot], { encoding: "utf8" });
    if (result.status === 0) {
      throw new Error(`${name} fixture was unexpectedly accepted`);
    }
  } finally {
    await rm(fixtureBase, { recursive: true, force: true });
  }
}

await expectRejected("duplicate-id", async (root) => {
  const manifestPath = path.join(root, "icons/manifest.json");
  const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
  manifest.glyphs[1].id = manifest.glyphs[0].id;
  await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
});

await expectRejected("external-svg", async (root) => {
  const iconPath = path.join(root, "icons/format-bold.svg");
  const svg = await readFile(iconPath, "utf8");
  await writeFile(iconPath, svg.replace("</svg>", '<image href="https://example.invalid/a.png"/></svg>'), "utf8");
});

await expectRejected("undeclared-svg", async (root) => {
  await writeFile(
    path.join(root, "icons/undeclared.svg"),
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" aria-hidden="true" focusable="false" stroke="currentColor"><path d="M1 1h2"/></svg>\n',
    "utf8",
  );
});

process.stdout.write("Verified MDV toolbar negative contracts\n");
