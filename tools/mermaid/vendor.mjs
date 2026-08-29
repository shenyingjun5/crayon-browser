#!/usr/bin/env node

// Mermaid Full offline runtime closure vendor tool (MDV-14).
//
// Modes:
//   --check              offline: verify the checked-in vendor directory
//   --archive <tgz>      offline: rebuild the vendor directory from the
//                        explicitly provided locked tarball
//   --download           explicit network-only maintainer action: fetch the
//                        locked tarball from npm, then rebuild
//
// Normal builds never invoke this tool and never touch the network.

import {createHash} from 'node:crypto';
import {execFile} from 'node:child_process';
import {existsSync} from 'node:fs';
import {
  lstat,
  mkdir,
  mkdtemp,
  readFile,
  readdir,
  rename,
  rm,
  writeFile,
} from 'node:fs/promises';
import https from 'node:https';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {gunzipSync} from 'node:zlib';
import {promisify} from 'node:util';

const execFileP = promisify(execFile);

export const MAX_TARBALL_BYTES = 32 * 1024 * 1024;
export const MAX_ARCHIVE_BYTES = 256 * 1024 * 1024;
// Upstream dist ships source maps larger than any runtime asset; the entry
// budget only bounds tar parsing, while the vendored closure is bounded
// separately by MAX_CLOSURE_BYTES.
export const MAX_ENTRY_BYTES = 16 * 1024 * 1024;
const MAX_TAR_ENTRIES = 4096;
const MAX_CLOSURE_FILES = 256;
const MAX_CLOSURE_BYTES = 16 * 1024 * 1024;

const PACKAGE_NAME = 'mermaid';
const PACKAGE_VERSION = '11.17.2';
const PACKAGE_LICENSE = 'MIT';
const PACKAGE_URL =
    'https://registry.npmjs.org/mermaid/-/mermaid-11.17.2.tgz';
const PACKAGE_INTEGRITY =
    'sha512-V6K3C8EBdEsPFZXSKMJe6ppQOENxuHARr9GvHX4hh47lAbhMRD9qf4oEK7LoaRQxULMa80/qt5gHO73aCleBBg==';
const TARBALL_SHA256 =
    '6ad2f42c3fc26bbf9e45cbb6d11898972573ea52b33a5f4ff51952899f950ffd';

// The browser runtime closure is derived from the minified ESM entry. The
// npm package declares runtime dependencies for source consumers; the
// published dist bundle is self-contained, which the closure walk below
// enforces by rejecting any non-relative specifier.
const DIST_ENTRY = 'mermaid.esm.min.mjs';
const SCHEMA = 'crayon-mermaid-assets/v1';

const MIME_BY_EXTENSION = new Map([['.mjs', 'text/javascript']]);

function digest(algorithm, bytes, encoding = 'hex') {
  return createHash(algorithm).update(bytes).digest(encoding);
}

function readTarString(bytes) {
  const nul = bytes.indexOf(0);
  return bytes.subarray(0, nul === -1 ? bytes.length : nul).toString('utf8');
}

function readTarOctal(bytes, label) {
  const value = readTarString(bytes).trim();
  if (!/^[0-7]+$/.test(value)) {
    throw new Error(`invalid tar ${label}`);
  }
  const parsed = Number.parseInt(value, 8);
  if (!Number.isSafeInteger(parsed)) {
    throw new Error(`invalid tar ${label}`);
  }
  return parsed;
}

function assertSafeTarPath(value) {
  if (!value || value.length > 255 || value.startsWith('/') ||
      value.includes('\\') || value.includes('\0')) {
    throw new Error(`unsafe tar path: ${value}`);
  }
  const pieces = value.replace(/\/$/, '').split('/');
  if (pieces.some((piece) => !piece || piece === '.' || piece === '..') ||
      pieces[0] !== 'package') {
    throw new Error(`unsafe tar path: ${value}`);
  }
}

export function parseTarArchive(archive) {
  if (!Buffer.isBuffer(archive) || archive.length > MAX_ARCHIVE_BYTES) {
    throw new Error('archive exceeds budget');
  }
  const entries = new Map();
  let offset = 0;
  let entryCount = 0;
  while (offset + 512 <= archive.length) {
    const header = archive.subarray(offset, offset + 512);
    if (header.every((value) => value === 0)) {
      return entries;
    }
    if (++entryCount > MAX_TAR_ENTRIES) {
      throw new Error('tar entry count exceeds budget');
    }
    const expectedChecksum = readTarOctal(header.subarray(148, 156), 'checksum');
    let actualChecksum = 0;
    for (let index = 0; index < header.length; ++index) {
      actualChecksum += index >= 148 && index < 156 ? 32 : header[index];
    }
    if (actualChecksum !== expectedChecksum) {
      throw new Error('tar checksum mismatch');
    }
    const name = readTarString(header.subarray(0, 100));
    const prefix = readTarString(header.subarray(345, 500));
    const entryPath = prefix ? `${prefix}/${name}` : name;
    assertSafeTarPath(entryPath);
    const type = String.fromCharCode(header[156] || 48);
    if (type !== '0' && type !== '5') {
      throw new Error(`unsupported tar entry type: ${type}`);
    }
    const size = readTarOctal(header.subarray(124, 136), 'size');
    if (size > MAX_ENTRY_BYTES) {
      throw new Error(`tar entry exceeds budget: ${entryPath}`);
    }
    const dataOffset = offset + 512;
    const nextOffset = dataOffset + Math.ceil(size / 512) * 512;
    if (nextOffset > archive.length) {
      throw new Error(`truncated tar entry: ${entryPath}`);
    }
    if (type === '0') {
      if (entries.has(entryPath)) {
        throw new Error(`duplicate tar path: ${entryPath}`);
      }
      entries.set(entryPath, Buffer.from(archive.subarray(dataOffset,
                                                          dataOffset + size)));
    } else if (size !== 0) {
      throw new Error(`directory has data: ${entryPath}`);
    }
    offset = nextOffset;
  }
  throw new Error('tar archive has no terminator');
}

export function verifyPackageMetadata(metadata) {
  if (metadata.name !== PACKAGE_NAME || metadata.version !== PACKAGE_VERSION ||
      metadata.license !== PACKAGE_LICENSE) {
    throw new Error('package identity mismatch');
  }
  // The published dist bundle is self-contained; runtime dependencies in the
  // npm manifest serve source consumers only and must never leak into the
  // vendored closure.
  if (metadata.dependencies != null) {
    const bundled = {
      d3: true, uuid: true, dayjs: true, katex: true, khroma: true,
      marked: true, stylis: true, fastdom: true, roughjs: true,
      '@types/d3': true, cytoscape: true, 'd3-sankey': true,
      dompurify: true, 'ts-dedent': true, 'es-toolkit': true,
      'dagre-d3-es': true, '@iconify/utils': true, 'cytoscape-fcose': true,
      '@upsetjs/venn.js': true, '@mermaid-js/parser': true,
      'cytoscape-cose-bilkent': true, '@braintree/sanitize-url': true,
    };
    for (const [name, range] of Object.entries(metadata.dependencies)) {
      if (!bundled[name]) {
        throw new Error(`unexpected runtime dependency: ${name}`);
      }
      if (typeof range !== 'string' || range.length === 0 ||
          range.length > 64) {
        throw new Error(`invalid dependency range: ${name}`);
      }
    }
    if (Object.keys(metadata.dependencies).length !==
        Object.keys(bundled).length) {
      throw new Error('runtime dependency set mismatch');
    }
  }
}

export function verifyTarball(tarball) {
  if (!Buffer.isBuffer(tarball) || tarball.length > MAX_TARBALL_BYTES) {
    throw new Error('tarball exceeds budget');
  }
  if (digest('sha256', tarball) !== TARBALL_SHA256 ||
      `sha512-${digest('sha512', tarball, 'base64')}` !== PACKAGE_INTEGRITY) {
    throw new Error('package archive integrity mismatch');
  }
  let archive;
  try {
    archive = gunzipSync(tarball, {maxOutputLength: MAX_ARCHIVE_BYTES});
  } catch (error) {
    throw new Error(`invalid or oversized gzip archive: ${error.message}`);
  }
  return parseTarArchive(archive);
}

// One match per real ESM import/export-from statement in the terser-minified
// bundle, where the keyword is always immediately adjacent to the quote:
// `}from"./x.mjs"`, `;import"./x.mjs"`, `()=>import("./x.mjs")`. The
// statement/operator-boundary prefix keeps matches out of string literals
// (property values like "from", the CSS "@import" string, and debug strings
// such as `... from (",n,` never match because a quote must directly follow
// the keyword and identifier/quote/@/- prefixes are not accepted).
const IMPORT_PATTERN =
    /(?:^|[;,\)\}\{=!:?\&|+*%<>~^]\s*|\s)(?:from"([^"]+)"|import"([^"]+)"|import\("([^"]+)"\))/g;

export function classifyImportSpecifier(specifier) {
  if (specifier.startsWith('./')) return 'relative';
  if (/^(https?:)?\/\//.test(specifier) || specifier.startsWith('data:')) {
    return 'network';
  }
  return 'bare';
}

export function collectRelativeImports(source, fileLabel) {
  const specifiers = [];
  for (const match of source.matchAll(IMPORT_PATTERN)) {
    const specifier = match[1] ?? match[2] ?? match[3];
    const kind = classifyImportSpecifier(specifier);
    if (kind !== 'relative') {
      throw new Error(
          `non-relative import in ${fileLabel}: ${specifier} (${kind})`);
    }
    specifiers.push(specifier);
  }
  return specifiers;
}

export function deriveImportClosure(distFiles) {
  if (!distFiles.has(`dist/${DIST_ENTRY}`)) {
    throw new Error(`missing dist entry: ${DIST_ENTRY}`);
  }
  const closure = new Map();
  const queue = [`dist/${DIST_ENTRY}`];
  while (queue.length > 0) {
    if (closure.size >= MAX_CLOSURE_FILES) {
      throw new Error('import closure exceeds file budget');
    }
    const distPath = queue.pop();
    if (closure.has(distPath)) continue;
    const bytes = distFiles.get(distPath);
    const relative = distPath.slice('dist/'.length);
    const mime = MIME_BY_EXTENSION.get(path.extname(relative));
    if (!mime) {
      throw new Error(`unexpected closure file type: ${distPath}`);
    }
    closure.set(relative, {
      path: relative,
      bytes: bytes.length,
      sha256: digest('sha256', bytes),
      mime,
    });
    const source = bytes.toString('utf8');
    for (const specifier of collectRelativeImports(source, distPath)) {
      if (!specifier.startsWith('./')) {
        throw new Error(`invalid specifier: ${specifier}`);
      }
      const target = path.posix.normalize(
          path.posix.join(path.posix.dirname(distPath), specifier));
      if (!target.startsWith('dist/') || target.includes('//')) {
        throw new Error(`closure escape via ${specifier} in ${distPath}`);
      }
      if (!distFiles.has(target)) {
        throw new Error(`missing chunk: ${target} referenced by ${distPath}`);
      }
      queue.push(target);
    }
  }
  let totalBytes = 0;
  const files = [...closure.values()].sort((a, b) =>
                                                a.path < b.path ? -1 : 1);
  for (const file of files) {
    totalBytes += file.bytes;
  }
  if (totalBytes > MAX_CLOSURE_BYTES) {
    throw new Error('import closure exceeds byte budget');
  }
  return {files, totalBytes};
}

async function verifySyntax(root, files) {
  // CEF 150 ships a newer V8 than the toolchain Node, so any syntax this
  // Node parses as ESM is compatible. --check parses without executing.
  for (const file of files) {
    await execFileP(
        process.execPath,
        ['--check', path.join(root, 'assets', file.path)],
        {timeout: 30000});
  }
}

function expectedManifest(closure) {
  return {
    schema: SCHEMA,
    package: {
      name: PACKAGE_NAME,
      version: PACKAGE_VERSION,
      license: PACKAGE_LICENSE,
      source: PACKAGE_URL,
      npmIntegrity: PACKAGE_INTEGRITY,
      tarballSha256: TARBALL_SHA256,
      upstreamTag: `v${PACKAGE_VERSION}`,
    },
    policy: {
      entry: DIST_ENTRY,
      externalImports: 0,
      networkImports: 0,
      dynamicOnlyFromManifestRoutes: true,
      totalBytes: closure.totalBytes,
      totalByteBudget: MAX_CLOSURE_BYTES,
    },
    files: closure.files,
  };
}

function vendoredReadme(closure) {
  return `# Mermaid Full vendored closure\n\n` +
      `- Package: \`${PACKAGE_NAME}@${PACKAGE_VERSION}\` (upstream tag ` +
      `\`v${PACKAGE_VERSION}\`, MIT; see [LICENSE](LICENSE)).\n` +
      `- Source: ${PACKAGE_URL}\n` +
      `- npm integrity: \`${PACKAGE_INTEGRITY}\`\n` +
      `- Tarball SHA-256: \`${TARBALL_SHA256}\`\n` +
      `- Runtime closure: ESM entry \`${DIST_ENTRY}\` plus ` +
      `${closure.files.length - 1} reachable chunks (` +
      `${closure.totalBytes} bytes); the npm manifest's source-consumer ` +
      `runtime dependencies are pre-bundled upstream and none are vendored.\n` +
      `- Policy: no CDN/http imports, no tiny distribution, no tree-shaking ` +
      `of diagram types, no source maps, docs, tests or dev dependencies. ` +
      `SVG output stays untrusted and passes the Browser-owned SVG policy ` +
      `gate before injection.\n\n` +
      `Verify offline with \`node tools/mermaid/vendor.mjs --check\`. ` +
      `To reproduce an approved update, obtain the exact tarball and run ` +
      `\`node tools/mermaid/vendor.mjs --archive <tgz>\`; \`--download\` is ` +
      `an explicit network-only maintainer action.\n`;
}

// Line endings may differ between the generated LF text and a Git checkout
// with core.autocrlf=true; text documents are compared after normalization
// while asset integrity stays byte-exact via sha256.
function canonicalLfText(value) {
  const normalized = value.replace(/\r\n/g, '\n');
  if (normalized.includes('\r')) {
    throw new Error('vendored text contains an invalid carriage return');
  }
  return normalized;
}

export async function verifyVendorDirectory(root = vendorRoot()) {
  const manifestPath = path.join(root, 'manifest.json');
  if (!existsSync(manifestPath)) {
    throw new Error('missing manifest');
  }
  const manifest = JSON.parse(canonicalLfText(
      await readFile(manifestPath, 'utf8')));
  if (manifest.schema !== SCHEMA) {
    throw new Error('manifest schema mismatch');
  }
  const expectedFiles = new Set([
    'manifest.json',
    'VENDORED.md',
    'LICENSE',
    ...manifest.files.map(({path: filePath}) => `assets/${filePath}`),
  ]);
  const actualFiles = await listFiles(root);
  if (actualFiles.length !== expectedFiles.size ||
      actualFiles.some((item) => !expectedFiles.has(item))) {
    throw new Error('vendor file set mismatch');
  }
  const distFiles = new Map();
  for (const file of manifest.files) {
    const bytes = await readFile(path.join(root, 'assets', file.path));
    if (bytes.length !== file.bytes ||
        digest('sha256', bytes) !== file.sha256) {
      throw new Error(`vendored asset integrity mismatch: ${file.path}`);
    }
    distFiles.set(`dist/${file.path}`, bytes);
  }
  const closure = deriveImportClosure(distFiles);
  const expectedClosure = expectedManifest(closure);
  if (JSON.stringify(expectedClosure.files) !==
      JSON.stringify(manifest.files)) {
    throw new Error('manifest does not match the derived import closure');
  }
  await verifySyntax(root, manifest.files);
  if (canonicalLfText(await readFile(path.join(root, 'VENDORED.md'), 'utf8')) !==
      canonicalLfText(vendoredReadme(closure))) {
    throw new Error('vendored documentation mismatch');
  }
  const license = await readFile(path.join(root, 'LICENSE'));
  if (!license.includes('MIT License')) {
    throw new Error('vendored license mismatch');
  }
  return manifest;
}

async function listFiles(root, relative = '') {
  const current = path.join(root, relative);
  const found = [];
  for (const entry of await readdir(current, {withFileTypes: true})) {
    const child = path.posix.join(relative.split(path.sep).join('/'), entry.name);
    if (entry.isSymbolicLink()) {
      throw new Error(`symlink is forbidden in vendor closure: ${child}`);
    }
    if (entry.isDirectory()) {
      found.push(...await listFiles(root, child));
    } else if (entry.isFile()) {
      found.push(child);
    } else {
      throw new Error(`unsupported vendor file type: ${child}`);
    }
  }
  return found.sort();
}

async function writeVendorDirectory(vendored, root = vendorRoot()) {
  const parent = path.dirname(root);
  await mkdir(parent, {recursive: true});
  const temporary = await mkdtemp(path.join(parent, '.mermaid-vendor-'));
  const backup = `${root}.previous-${process.pid}`;
  try {
    for (const relative of vendored.selected) {
      const output = path.join(temporary, 'assets', relative);
      await mkdir(path.dirname(output), {recursive: true});
      await writeFile(output, vendored.distFiles.get(`dist/${relative}`));
    }
    await writeFile(path.join(temporary, 'LICENSE'), vendored.licenseBytes);
    const closure = deriveImportClosure(vendored.distFiles);
    await writeFile(path.join(temporary, 'manifest.json'),
                    `${JSON.stringify(expectedManifest(closure), null, 2)}\n`);
    await writeFile(path.join(temporary, 'VENDORED.md'),
                    vendoredReadme(closure));
    await verifyVendorDirectory(temporary);

    if (existsSync(root)) {
      const current = await lstat(root);
      if (!current.isDirectory() || current.isSymbolicLink()) {
        throw new Error('vendor root must be a real directory');
      }
      await rename(root, backup);
    }
    try {
      await rename(temporary, root);
    } catch (error) {
      if (existsSync(backup)) await rename(backup, root);
      throw error;
    }
    if (existsSync(backup)) await rm(backup, {recursive: true, force: true});
  } finally {
    if (existsSync(temporary)) {
      await rm(temporary, {recursive: true, force: true});
    }
  }
  return verifyVendorDirectory(root);
}

export function selectClosure(entries) {
  const packageJsonBytes = entries.get('package/package.json');
  if (!packageJsonBytes) {
    throw new Error('missing package metadata');
  }
  let metadata;
  try {
    metadata = JSON.parse(packageJsonBytes.toString('utf8'));
  } catch {
    throw new Error('invalid package metadata');
  }
  verifyPackageMetadata(metadata);
  const licenseBytes = entries.get('package/LICENSE');
  if (!licenseBytes || !licenseBytes.toString('utf8').includes('MIT License')) {
    throw new Error('missing or invalid LICENSE');
  }
  const distFiles = new Map();
  for (const [entryPath, bytes] of entries) {
    if (entryPath.startsWith('package/dist/')) {
      distFiles.set(entryPath.slice('package/'.length), bytes);
    }
  }
  const closure = deriveImportClosure(distFiles);
  const selected = closure.files.map(({path: filePath}) => filePath);
  return {selected, distFiles, licenseBytes};
}

async function downloadTarball() {
  return new Promise((resolve, reject) => {
    const request = https.get(PACKAGE_URL, {headers: {'User-Agent': 'crayon-vendor/1'}},
                              (response) => {
      if (response.statusCode !== 200) {
        response.resume();
        reject(new Error(`package download failed: HTTP ${response.statusCode}`));
        return;
      }
      const declared = Number(response.headers['content-length'] || 0);
      if (declared > MAX_TARBALL_BYTES) {
        response.destroy(new Error('tarball exceeds budget'));
        return;
      }
      const chunks = [];
      let total = 0;
      response.on('data', (chunk) => {
        total += chunk.length;
        if (total > MAX_TARBALL_BYTES) {
          response.destroy(new Error('tarball exceeds budget'));
          return;
        }
        chunks.push(chunk);
      });
      response.on('end', () => resolve(Buffer.concat(chunks)));
      response.on('error', reject);
    });
    request.on('error', reject);
  });
}

function vendorRoot() {
  return path.resolve(path.dirname(fileURLToPath(import.meta.url)),
                      '../../third_party/mermaid');
}

async function readBoundedTarball(filePath) {
  const info = await lstat(filePath);
  if (!info.isFile() || info.isSymbolicLink() || info.size > MAX_TARBALL_BYTES) {
    throw new Error('archive path must be a bounded regular file');
  }
  return readFile(filePath);
}

async function run(arguments_) {
  if (arguments_.length === 1 && arguments_[0] === '--check') {
    const manifest = await verifyVendorDirectory();
    console.log(`mermaid vendor OK: ${manifest.files.length} files, ` +
                `${manifest.policy.totalBytes} bytes`);
    return;
  }
  let tarball;
  if (arguments_.length === 2 && arguments_[0] === '--archive') {
    tarball = await readBoundedTarball(path.resolve(arguments_[1]));
  } else if (arguments_.length === 1 && arguments_[0] === '--download') {
    tarball = await downloadTarball();
  } else {
    throw new Error('usage: vendor.mjs --check | --archive <tgz> | --download');
  }
  const entries = verifyTarball(tarball);
  const vendored = selectClosure(entries);
  const manifest = await writeVendorDirectory(vendored);
  console.log(`mermaid vendor updated: ${manifest.files.length} files, ` +
              `${manifest.policy.totalBytes} bytes`);
}

const invokedPath = process.argv[1] ? path.resolve(process.argv[1]) : '';
if (invokedPath === fileURLToPath(import.meta.url)) {
  run(process.argv.slice(2)).catch((error) => {
    console.error(`mermaid vendor failed: ${error.message}`);
    process.exitCode = 1;
  });
}
