import assert from 'node:assert/strict';
import {cp, mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

import {
  MAX_ARCHIVE_BYTES,
  MAX_ENTRY_BYTES,
  classifyImportSpecifier,
  collectRelativeImports,
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
    {path: 'package/dist/mermaid.esm.min.mjs', bytes: 'export default {};'},
  ]);
  const parsed = parseTarArchive(archive);
  assert.equal(parsed.get('package/package.json').toString('utf8'),
               '{"version":"test"}');
  assert.equal(parsed.get('package/dist/mermaid.esm.min.mjs').toString('utf8'),
               'export default {};');
});

test('tar parser rejects traversal, links, duplicate paths and oversized data',
     () => {
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

test('package identity and archive integrity fail closed', () => {
  assert.throws(
      () => verifyPackageMetadata({
        name: 'mermaid',
        version: '11.17.3',
        license: 'MIT',
      }), /package identity mismatch/);
  assert.throws(() => verifyTarball(Buffer.from('not the locked tarball')),
                /package archive integrity mismatch/);
});

test('import specifiers are classified and closed', () => {
  assert.equal(classifyImportSpecifier('./chunks/a.mjs'), 'relative');
  assert.equal(classifyImportSpecifier('https://cdn.example.com/x.mjs'),
               'network');
  assert.equal(classifyImportSpecifier('//cdn.example.com/x.mjs'), 'network');
  assert.equal(classifyImportSpecifier('data:text/javascript,x'), 'network');
  assert.equal(classifyImportSpecifier('d3'), 'bare');

  const source =
      'import{a}from"./chunks/a.mjs";import"./chunks/b.mjs";' +
      'const load=()=>import("./chunks/c.mjs");' +
      'x.debug("Moving label from (",n,");");var css="@import";' +
      'var kw={kw:"import",from:"from",fromPort:"fromPort"};';
  assert.deepEqual(collectRelativeImports(source, 'test.mjs'),
                   ['./chunks/a.mjs', './chunks/b.mjs', './chunks/c.mjs']);

  assert.throws(
      () => collectRelativeImports('import("https://cdn.example.com/x.mjs");',
                                   'test.mjs'),
      /non-relative import/);
  assert.throws(
      () => collectRelativeImports('export{a}from"d3";', 'test.mjs'),
      /non-relative import/);
});

test('vendored mermaid closure matches its manifest', async () => {
  const source = path.resolve(path.dirname(fileURLToPath(import.meta.url)),
                              '../../third_party/mermaid');
  const manifest = await verifyVendorDirectory(source);
  assert.equal(manifest.schema, 'crayon-mermaid-assets/v1');
  assert.equal(manifest.package.name, 'mermaid');
  assert.equal(manifest.package.version, '11.17.2');
  assert.equal(manifest.package.license, 'MIT');
  assert.equal(manifest.policy.entry, 'mermaid.esm.min.mjs');
  assert.equal(manifest.policy.externalImports, 0);
  assert.equal(manifest.policy.networkImports, 0);
  // Full distribution: no tiny fallback and no tree-shaken diagram types.
  for (const diagram of [
         'flowDiagram', 'sequenceDiagram', 'classDiagram-v2', 'stateDiagram-v2',
         'erDiagram', 'ganttDiagram', 'pieDiagram', 'gitGraphDiagram',
         'journeyDiagram', 'mindmap-definition', 'architectureDiagram',
         'c4Diagram', 'xychartDiagram', 'quadrantDiagram', 'vennDiagram',
         'sankeyDiagram', 'requirementDiagram', 'blockDiagram'
       ]) {
    assert.ok(
        manifest.files.some(({path: filePath}) =>
                                filePath.includes(`/${diagram}-`)),
        `diagram chunk present: ${diagram}`);
  }
  assert.equal(manifest.files.some(({path: filePath}) => filePath.endsWith('.map')),
               false, 'no source maps in the closure');
  assert.equal(manifest.files.every(({mime}) => mime === 'text/javascript'),
               true, 'closure is ESM JavaScript only');
  const chunkFiles =
      manifest.files.filter(({path: filePath}) => filePath.startsWith('chunks/'));
  assert.equal(chunkFiles.length, manifest.files.length - 1,
               'entry plus reachable chunks only');
});

test('vendor verifier rejects tamper, missing and extra files', async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), 'crayon-mermaid-test-'));
  const source = path.resolve(path.dirname(fileURLToPath(import.meta.url)),
                              '../../third_party/mermaid');
  try {
    await assert.rejects(() => verifyVendorDirectory(root), /missing manifest/);
    await cp(source, root, {recursive: true});
    const manifestPath = path.join(root, 'manifest.json');
    const readmePath = path.join(root, 'VENDORED.md');
    const manifestText = await readFile(manifestPath, 'utf8');
    const readmeText = await readFile(readmePath, 'utf8');
    await writeFile(manifestPath, manifestText.replace(/\n/g, '\r\n'));
    await writeFile(readmePath, readmeText.replace(/\n/g, '\r\n'));
    await verifyVendorDirectory(root);
    const firstAsset = manifestText.match(/"path": "([^"]+)"/)[1];
    await writeFile(path.join(root, 'assets', firstAsset), 'tampered');
    await assert.rejects(() => verifyVendorDirectory(root),
                         /vendored asset integrity mismatch/);
    await rm(root, {recursive: true, force: true});
    await cp(source, root, {recursive: true});
    await rm(path.join(root, 'assets', firstAsset), {force: true});
    await assert.rejects(() => verifyVendorDirectory(root),
                         /vendor file set mismatch/);
    await rm(root, {recursive: true, force: true});
    await cp(source, root, {recursive: true});
    await writeFile(path.join(root, 'unexpected.js'), 'x');
    await assert.rejects(() => verifyVendorDirectory(root),
                         /vendor file set mismatch/);
    await rm(root, {recursive: true, force: true});
    await cp(source, root, {recursive: true});
    const manifest = JSON.parse(manifestText);
    manifest.files.reverse();
    await writeFile(
        manifestPath,
        JSON.stringify(manifest, null, 2).replace(/\n/g, '\r\n') + '\n');
    await assert.rejects(() => verifyVendorDirectory(root),
                         /manifest does not match the derived import closure/);
  } finally {
    await rm(root, {recursive: true, force: true});
  }
});
