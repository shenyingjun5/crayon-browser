import assert from 'node:assert/strict';
import {cp, mkdtemp, readFile, rm, unlink, writeFile} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath, pathToFileURL} from 'node:url';

import {
  MAX_ARCHIVE_BYTES,
  MAX_ENTRY_BYTES,
  MAX_MATH_BRACE_DEPTH,
  MAX_MATH_SOURCE_BYTES,
  MAX_MATH_TOKENS,
  parseTarArchive,
  verifyMathSourcePolicy,
  verifyPackageMetadata,
  verifyTarball,
  verifyVendorDirectory,
} from './vendor.mjs';

function tarHeader(entry) {
  const header = Buffer.alloc(512);
  const name = Buffer.from(entry.path, 'utf8');
  if (name.length > 100) throw new Error('test tar path is too long');
  name.copy(header, 0);
  Buffer.from('0000644\0').copy(header, 100);
  Buffer.from('0000000\0').copy(header, 108);
  Buffer.from('0000000\0').copy(header, 116);
  Buffer.from(`${entry.bytes.length.toString(8).padStart(11, '0')}\0`).copy(
      header, 124);
  Buffer.from('00000000000\0').copy(header, 136);
  header.fill(32, 148, 156);
  header[156] = (entry.type || '0').charCodeAt(0);
  Buffer.from('ustar\0').copy(header, 257);
  Buffer.from('00').copy(header, 263);
  const checksum = [...header].reduce((sum, value) => sum + value, 0);
  Buffer.from(`${checksum.toString(8).padStart(6, '0')}\0 `).copy(header, 148);
  return header;
}

function buildTar(entries) {
  const blocks = [];
  for (const entry of entries) {
    const bytes = Buffer.isBuffer(entry.bytes) ? entry.bytes :
                                                   Buffer.from(entry.bytes);
    const normalized = {...entry, bytes};
    blocks.push(tarHeader(normalized), bytes);
    const padding = (512 - bytes.length % 512) % 512;
    if (padding) blocks.push(Buffer.alloc(padding));
  }
  blocks.push(Buffer.alloc(1024));
  return Buffer.concat(blocks);
}

function vendorRoot() {
  return path.resolve(path.dirname(fileURLToPath(import.meta.url)),
                      '../../third_party/katex');
}

test('tar parser accepts bounded regular files', () => {
  const archive = buildTar([
    {path: 'package/package.json', bytes: '{"version":"test"}'},
    {path: 'package/dist/katex.mjs', bytes: 'export default {}'},
  ]);
  const parsed = parseTarArchive(archive);
  assert.equal(parsed.get('package/package.json').toString('utf8'),
               '{"version":"test"}');
  assert.equal(parsed.get('package/dist/katex.mjs').toString('utf8'),
               'export default {}');
});

test('tar parser rejects traversal, links, duplicates and oversized data', () => {
  assert.throws(() => parseTarArchive(buildTar([
    {path: 'package/../escape', bytes: 'x'},
  ])), /unsafe tar path/);
  assert.throws(() => parseTarArchive(buildTar([
    {path: 'package/link', bytes: '', type: '2'},
  ])), /unsupported tar entry type/);
  assert.throws(() => parseTarArchive(buildTar([
    {path: 'package/repeated', bytes: 'a'},
    {path: 'package/repeated', bytes: 'b'},
  ])), /duplicate tar path/);
  assert.throws(() => parseTarArchive(buildTar([
    {path: 'package/large', bytes: Buffer.alloc(MAX_ENTRY_BYTES + 1)},
  ])), /tar entry exceeds budget/);
  assert.throws(() => parseTarArchive(Buffer.alloc(MAX_ARCHIVE_BYTES + 1)),
                /archive exceeds budget/);
});

test('vendor verifier rejects tamper, missing and extra files', async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), 'crayon-katex-test-'));
  try {
    await assert.rejects(() => verifyVendorDirectory(root), /missing manifest/);
    await cp(vendorRoot(), root, {recursive: true});
    await verifyVendorDirectory(root);
    await writeFile(path.join(root, 'assets/katex.mjs'), 'tampered');
    await assert.rejects(() => verifyVendorDirectory(root),
                         /vendored asset integrity mismatch/);
    await rm(root, {recursive: true, force: true});
    await cp(vendorRoot(), root, {recursive: true});
    await unlink(path.join(root, 'assets/fonts/KaTeX_Main-Regular.woff2'));
    await assert.rejects(() => verifyVendorDirectory(root),
                         /vendor file set mismatch/);
    await rm(root, {recursive: true, force: true});
    await cp(vendorRoot(), root, {recursive: true});
    await writeFile(path.join(root, 'unexpected.js'), 'x');
    await assert.rejects(() => verifyVendorDirectory(root),
                         /vendor file set mismatch/);
  } finally {
    await rm(root, {recursive: true, force: true});
  }
});

test('package identity, CLI dependency and archive integrity fail closed', () => {
  const base = {
    name: 'katex',
    version: '0.18.4',
    license: 'MIT',
    dependencies: {commander: '^8.3.0'},
    exports: {'.': {import: {default: './dist/katex.mjs'}}},
  };
  verifyPackageMetadata(base);
  for (const invalid of [
    {...base, version: 'latest'},
    {...base, license: 'GPL'},
    {...base, dependencies: {commander: '^9.0.0'}},
    {...base, dependencies: {commander: '^8.3.0', extra: '1.0.0'}},
  ]) {
    assert.throws(() => verifyPackageMetadata(invalid),
                  /package identity or dependency mismatch/);
  }
  assert.throws(() => verifyTarball(Buffer.from('not the locked tarball')),
                /package archive integrity mismatch/);
});

test('manifest and CSS freeze the WOFF2-only offline closure', async () => {
  const manifest = JSON.parse(await readFile(
      path.join(vendorRoot(), 'manifest.json'), 'utf8'));
  assert.equal(manifest.package.version, '0.18.4');
  assert.equal(manifest.policy.browserRuntimeDependencies, 0);
  assert.equal(manifest.policy.fontCount, 20);
  assert.equal(manifest.files.length, 23);
  const css = await readFile(path.join(vendorRoot(), 'assets/katex.min.css'),
                             'utf8');
  const urls = [...css.matchAll(/url\(([^)]+)\)/g)]
                   .map((match) => match[1]);
  assert.equal(urls.length, 20);
  assert.ok(urls.every((url) => /^fonts\/KaTeX_[A-Za-z0-9-]+\.woff2$/.test(url)));
  assert.equal(/https?:|data:|\.woff\)|\.ttf\)/.test(css), false);
  assert.deepEqual(manifest.policy.renderOptions, {
    output: 'htmlAndMathml',
    throwOnError: true,
    strict: 'error',
    trust: false,
    globalGroup: false,
    maxSize: 16,
    maxExpand: 256,
    macros: 'fresh-empty-null-prototype-per-render',
  });
});

test('syntax golden covers the closed inline and block matrix', async () => {
  const vectors = JSON.parse(await readFile(
      path.join(path.dirname(fileURLToPath(import.meta.url)),
                'syntax-vectors.json'), 'utf8'));
  assert.equal(vectors.schema, 'crayon-katex-syntax-vectors/v1');
  assert.equal(vectors.vectors.length, 22);
  const ids = new Set(vectors.vectors.map(({id}) => id));
  assert.equal(ids.size, vectors.vectors.length);
  for (const required of [
    'inline-basic', 'inline-escaped-opener', 'inline-code-excluded',
    'inline-link-destination-excluded', 'block-multiline',
    'block-single-line', 'block-four-space-code', 'block-list-excluded',
    'block-quote-excluded', 'block-crosses-blank-paragraph',
    'alternate-delimiters-disabled',
  ]) {
    assert.ok(ids.has(required), `missing syntax vector ${required}`);
  }
  for (const vector of vectors.vectors) {
    assert.ok(Buffer.byteLength(vector.markdown) <= MAX_MATH_SOURCE_BYTES);
    assert.ok(Array.isArray(vector.expected));
    for (const expected of vector.expected) {
      assert.ok(expected.kind === 'inline' || expected.kind === 'block');
      assert.ok(expected.source.length > 0);
    }
  }
});

test('math source policy rejects commands and bounded hostile inputs', () => {
  for (const source of [
    String.raw`\href{https://example.test}{x}`,
    String.raw`\url{file:///tmp/secret}`,
    String.raw`\includegraphics{https://example.test/x.png}`,
    String.raw`\htmlClass{owned}{x}`,
    String.raw`\htmlFuture{owned}{x}`,
    String.raw`\gdef\x{y}`,
    String.raw`\csname href\endcsname{x}`,
    String.raw`\expandafter\def\csname x\endcsname{y}`,
  ]) {
    assert.deepEqual(verifyMathSourcePolicy(source),
                     {ok: false, reason: 'denied_command'});
  }
  assert.equal(verifyMathSourcePolicy('').reason, 'invalid_source');
  assert.equal(verifyMathSourcePolicy('x'.repeat(MAX_MATH_SOURCE_BYTES + 1)).reason,
               'invalid_source');
  assert.equal(verifyMathSourcePolicy('x '.repeat(MAX_MATH_TOKENS + 1)).reason,
               'token_budget');
  assert.equal(verifyMathSourcePolicy(
      '{'.repeat(MAX_MATH_BRACE_DEPTH + 1)).reason, 'depth_budget');
  assert.deepEqual(verifyMathSourcePolicy(String.raw`\frac{x^2 + 1}{2}`),
                   {ok: true, reason: 'allowed'});
});

test('vendored ESM renders accessible math without active content', async () => {
  const runtimeUrl = pathToFileURL(
      path.join(vendorRoot(), 'assets/katex.mjs')).href;
  const katex = await import(runtimeUrl);
  assert.equal(katex.version, '0.18.4');
  const options = {
    output: 'htmlAndMathml',
    throwOnError: true,
    strict: 'error',
    trust: false,
    globalGroup: false,
    maxSize: 16,
    maxExpand: 256,
    macros: Object.create(null),
  };
  const rendered = katex.renderToString(
      String.raw`\frac{x^2 + 1}{\sqrt{2}}`, options);
  assert.match(rendered, /class="katex"/);
  assert.match(rendered, /<math/);
  const hostileText = katex.renderToString(
      String.raw`\text{<img src=x onerror=alert(1)>}`, options);
  assert.equal(hostileText.includes('<img'), false);
  assert.equal(/<[^>]+\son[a-z]+=|<script|<iframe|<object|<embed/i
                   .test(hostileText), false);
});
