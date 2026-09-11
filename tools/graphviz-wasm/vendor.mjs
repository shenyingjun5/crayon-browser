#!/usr/bin/env node
import {createHash} from 'node:crypto';
import {existsSync, readFileSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../third_party/graphviz-wasm');
const m = JSON.parse(readFileSync(path.join(root, 'manifest.json'), 'utf8'));
const f = path.join(root, 'assets', m.policy.entry);
if (!existsSync(f)) { console.error('FAIL: missing'); process.exit(1); }
const actual = createHash('sha256').update(readFileSync(f)).digest('hex');
if (actual !== m.policy.closureSha256) { console.error('FAIL: sha256'); process.exit(1); }
const bytes = readFileSync(f).length;
if (bytes !== m.policy.totalBytes) { console.error('FAIL: bytes'); process.exit(1); }
console.log(`graphviz-wasm closure OK (${bytes} bytes)`);
