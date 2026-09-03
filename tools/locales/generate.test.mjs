import assert from "node:assert/strict";
import {mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import {fileURLToPath} from "node:url";
import {
  buildOutputs,
  parseFlatStringCatalog,
  validateCatalogs,
  verifyOutputs,
  writeOutputs,
} from "./lib.mjs";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDirectory, "../..");

test("real catalogs generate deterministic three-language outputs", () => {
  const first = buildOutputs(repoRoot);
  const second = buildOutputs(repoRoot);
  assert.equal(first.keyCount, 156);
  assert.deepEqual([...first.outputs], [...second.outputs]);
  assert.deepEqual(first.manifest.locales.map(({tag}) => tag), ["en-US", "zh-CN", "zh-TW"]);
});

test("About menu uses the Crayon brand in every supported language", () => {
  for (const [locale, label] of [
    ["en-US", "About Crayon Browser"],
    ["zh-CN", "关于蜡笔浏览器"],
    ["zh-TW", "關於蠟筆瀏覽器"],
  ]) {
    const catalog = JSON.parse(readFileSync(
      path.join(repoRoot, `browser/shared-ui/locales/${locale}.json`), "utf8"));
    assert.equal(catalog["app.about"], label);
  }
});

test("strict parser rejects duplicate keys", () => {
  assert.throws(
    () => parseFlatStringCatalog(Buffer.from('{"app.title":"A","app.title":"B"}'), "duplicate"),
    /duplicate key/,
  );
});

test("strict parser rejects invalid UTF-8, empty values, controls and non-strings", () => {
  assert.throws(
    () => parseFlatStringCatalog(Buffer.from([0xc3, 0x28]), "utf8"),
    /invalid UTF-8/,
  );
  assert.throws(
    () => parseFlatStringCatalog(Buffer.from('{"app.title":"   "}'), "empty"),
    /must not be empty/,
  );
  assert.throws(
    () => parseFlatStringCatalog(Buffer.from('{"app.title":"line\\nfeed"}'), "control"),
    /control characters/,
  );
  assert.throws(
    () => parseFlatStringCatalog(Buffer.from('{"app.title":42}'), "type"),
    /expected JSON string/,
  );
});

test("catalog validation rejects missing, reordered and placeholder-drifted keys", () => {
  const valid = new Map([
    ["en-US", [["app.title", "Hello {name}"], ["nav.back", "&Back"]]],
    ["zh-CN", [["app.title", "你好 {name}"], ["nav.back", "&后退"]]],
    ["zh-TW", [["app.title", "你好 {name}"], ["nav.back", "&後退"]]],
  ]);
  assert.deepEqual(validateCatalogs(valid), ["app.title", "nav.back"]);

  const missing = new Map(valid);
  missing.set("zh-TW", [["app.title", "你好 {name}"]]);
  assert.throws(() => validateCatalogs(missing), /key count/);

  const reordered = new Map(valid);
  reordered.set("zh-TW", [["nav.back", "&後退"], ["app.title", "你好 {name}"]]);
  assert.throws(() => validateCatalogs(reordered), /key order\/parity mismatch/);

  const placeholder = new Map(valid);
  placeholder.set("zh-TW", [["app.title", "你好 {user}"], ["nav.back", "&後退"]]);
  assert.throws(() => validateCatalogs(placeholder), /placeholder set differs/);
});

test("check detects stale, missing and unexpected generated outputs", () => {
  const tempRoot = mkdtempSync(path.join(os.tmpdir(), "crayon-locales-"));
  try {
    const outputs = new Map([
      ["browser/shared-ui/localization/generated/a.txt", "alpha\n"],
      ["browser/shared-ui/localization/generated/sub/b.txt", "beta\n"],
    ]);
    writeOutputs(tempRoot, outputs);
    assert.doesNotThrow(() => verifyOutputs(tempRoot, outputs));

    writeFileSync(path.join(tempRoot, "browser/shared-ui/localization/generated/a.txt"), "stale\n");
    assert.throws(() => verifyOutputs(tempRoot, outputs), /stale/);
    writeOutputs(tempRoot, outputs);

    rmSync(path.join(tempRoot, "browser/shared-ui/localization/generated/a.txt"));
    assert.throws(() => verifyOutputs(tempRoot, outputs), /missing/);
    writeOutputs(tempRoot, outputs);

    mkdirSync(path.join(tempRoot, "browser/shared-ui/localization/generated/extra"), {recursive: true});
    writeFileSync(path.join(tempRoot, "browser/shared-ui/localization/generated/extra/unmanaged.txt"), "extra\n");
    assert.throws(() => verifyOutputs(tempRoot, outputs), /unexpected generated output/);
  } finally {
    rmSync(tempRoot, {recursive: true, force: true});
  }
});
