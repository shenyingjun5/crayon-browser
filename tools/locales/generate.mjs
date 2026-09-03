#!/usr/bin/env node

import path from "node:path";
import {fileURLToPath} from "node:url";
import {buildOutputs, verifyOutputs, writeOutputs} from "./lib.mjs";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDirectory, "../..");
const args = new Set(process.argv.slice(2));
if ([...args].some((argument) => argument !== "--check")) {
  throw new Error("usage: node tools/locales/generate.mjs [--check]");
}

const {manifest, outputs, keyCount} = buildOutputs(repoRoot);
if (args.has("--check")) {
  verifyOutputs(repoRoot, outputs);
} else {
  writeOutputs(repoRoot, outputs);
}
process.stdout.write(`${JSON.stringify({passed: true, mode: args.has("--check") ? "check" : "write", catalog_version: manifest.catalog_version, locales: manifest.locales.length, keys: keyCount, files: outputs.size})}\n`);
