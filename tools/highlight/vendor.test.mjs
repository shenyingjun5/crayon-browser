import assert from 'node:assert/strict';
import {cp, mkdtemp, readFile, rm, unlink, writeFile} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

import {
  MAX_ARCHIVE_BYTES,
  MAX_ENTRY_BYTES,
  parseTarArchive,
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

test('tar parser accepts bounded regular files', () => {
  const archive = buildTar([
    {path: 'package/package.json', bytes: '{"version":"test"}'},
    {path: 'package/es/core.min.js', bytes: 'export default {};'},
  ]);
  const parsed = parseTarArchive(archive);
  assert.equal(parsed.get('package/package.json').toString('utf8'),
               '{"version":"test"}');
  assert.equal(parsed.get('package/es/core.min.js').toString('utf8'),
               'export default {};');
});

test('tar parser rejects traversal, links, duplicate paths and oversized data', () => {
  assert.throws(
      () => parseTarArchive(buildTar([
        {path: 'package/../escape', bytes: 'x'},
      ])), /unsafe tar path/);
  assert.throws(
      () => parseTarArchive(buildTar([
        {path: 'package/link', bytes: '', type: '2'},
      ])), /unsupported tar entry type/);
  assert.throws(
      () => parseTarArchive(buildTar([
        {path: 'package/repeated', bytes: 'a'},
        {path: 'package/repeated', bytes: 'b'},
      ])), /duplicate tar path/);
  assert.throws(
      () => parseTarArchive(buildTar([
        {path: 'package/large', bytes: Buffer.alloc(MAX_ENTRY_BYTES + 1)},
      ])), /tar entry exceeds budget/);
  assert.throws(
      () => parseTarArchive(Buffer.alloc(MAX_ARCHIVE_BYTES + 1)),
      /archive exceeds budget/);
});

test('vendor verifier rejects tamper, missing and extra files', async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), 'crayon-highlight-test-'));
  const source = path.resolve(path.dirname(fileURLToPath(import.meta.url)),
                              '../../third_party/highlight');
  try {
    await assert.rejects(() => verifyVendorDirectory(root), /missing manifest/);
    await cp(source, root, {recursive: true});
    await verifyVendorDirectory(root);
    await writeFile(path.join(root, 'assets/core.min.js'), 'tampered');
    await assert.rejects(() => verifyVendorDirectory(root),
                         /vendored asset integrity mismatch/);
    await rm(root, {recursive: true, force: true});
    await cp(source, root, {recursive: true});
    await unlink(path.join(root, 'assets/languages/rust.min.js'));
    await assert.rejects(() => verifyVendorDirectory(root),
                         /vendor file set mismatch/);
    await rm(root, {recursive: true, force: true});
    await cp(source, root, {recursive: true});
    await writeFile(path.join(root, 'unexpected.js'), 'x');
    await assert.rejects(() => verifyVendorDirectory(root),
                         /vendor file set mismatch/);
  } finally {
    await rm(root, {recursive: true, force: true});
  }
});

test('package identity and archive integrity fail closed', () => {
  assert.throws(
      () => verifyPackageMetadata({
        name: '@highlightjs/cdn-assets',
        version: 'latest',
        license: 'BSD-3-Clause',
      }), /package identity or dependency mismatch/);
  assert.throws(() => verifyTarball(Buffer.from('not the locked tarball')),
                /package archive integrity mismatch/);
});

test('all selected ESM grammars register and keep hostile source as text', async () => {
  const source = path.resolve(path.dirname(fileURLToPath(import.meta.url)),
                              '../../third_party/highlight');
  const manifest = JSON.parse(await readFile(path.join(source, 'manifest.json')));
  const importAsset = async (relative) => {
    const bytes = await readFile(path.join(source, relative));
    const url = `data:text/javascript;base64,${bytes.toString('base64')}`;
    return import(url);
  };
  const core = (await importAsset('assets/core.min.js')).default;
  for (const language of manifest.languages) {
    const grammar =
        (await importAsset(`assets/languages/${language.id}.min.js`)).default;
    core.registerLanguage(language.id, grammar);
  }
  assert.deepEqual(core.listLanguages().sort(),
                   manifest.languages.map(({id}) => id).sort());
  for (const language of manifest.languages) {
    const result = core.highlight(
        '<script>alert(1)</script><img src=x onerror=alert(2)>',
        {language: language.id, ignoreIllegals: true});
    assert.equal(typeof result.value, 'string');
    assert.equal(result.value.includes('<script>'), false);
    assert.equal(result.value.includes('<img'), false);
  }
});
