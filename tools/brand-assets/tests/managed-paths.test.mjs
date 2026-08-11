import assert from "node:assert/strict";
import { existsSync, mkdirSync, mkdtempSync, rmSync, symlinkSync, writeFileSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { assertWithinRoot, resetManagedDirectory, resolveWithinRoot } from "../managed-paths.mjs";

test("managed paths reject root and lexical escapes", () => {
  const root = path.resolve("fixture-root");
  assert.throws(() => assertWithinRoot(root, root), /escaped root/);
  assert.throws(() => resolveWithinRoot(root, "../outside"), /escaped root/);
});

test("managed directory reset rejects symlink or junction parents", () => {
  const temporaryRoot = os.tmpdir();
  const fixture = mkdtempSync(path.join(temporaryRoot, "crayon-brand-paths-"));
  const relativeFixture = path.relative(temporaryRoot, fixture);
  assert.ok(relativeFixture && !relativeFixture.startsWith("..") && !path.isAbsolute(relativeFixture));
  try {
    const repository = path.join(fixture, "repo");
    const outside = path.join(fixture, "outside");
    const linked = path.join(repository, "linked");
    mkdirSync(repository);
    mkdirSync(outside);
    writeFileSync(path.join(outside, "sentinel.txt"), "preserve");
    symlinkSync(outside, linked, process.platform === "win32" ? "junction" : "dir");

    assert.throws(() => resetManagedDirectory(repository, path.join(linked, "generated")), /symlink or junction/);
    assert.equal(existsSync(path.join(outside, "sentinel.txt")), true);
  } finally {
    rmSync(fixture, { recursive: true, force: false });
  }
});

test("managed directory reset recreates an in-root directory", () => {
  const temporaryRoot = os.tmpdir();
  const fixture = mkdtempSync(path.join(temporaryRoot, "crayon-brand-reset-"));
  const relativeFixture = path.relative(temporaryRoot, fixture);
  assert.ok(relativeFixture && !relativeFixture.startsWith("..") && !path.isAbsolute(relativeFixture));
  try {
    const repository = path.join(fixture, "repo");
    const generated = path.join(repository, "generated");
    mkdirSync(generated, { recursive: true });
    writeFileSync(path.join(generated, "stale.txt"), "stale");

    resetManagedDirectory(repository, generated);

    assert.equal(existsSync(generated), true);
    assert.equal(existsSync(path.join(generated, "stale.txt")), false);
  } finally {
    rmSync(fixture, { recursive: true, force: false });
  }
});
