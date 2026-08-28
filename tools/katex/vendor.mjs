#!/usr/bin/env node

import {createHash} from 'node:crypto';
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

export const MAX_TARBALL_BYTES = 4 * 1024 * 1024;
export const MAX_ARCHIVE_BYTES = 16 * 1024 * 1024;
export const MAX_ENTRY_BYTES = 8 * 1024 * 1024;
export const MAX_MATH_SOURCE_BYTES = 64 * 1024;
export const MAX_MATH_TOKENS = 8192;
export const MAX_MATH_BRACE_DEPTH = 64;
const MAX_TAR_ENTRIES = 1024;
const MAX_SELECTED_BYTES = 2 * 1024 * 1024;

const PACKAGE_NAME = 'katex';
const PACKAGE_VERSION = '0.18.4';
const PACKAGE_LICENSE = 'MIT';
const PACKAGE_URL =
    'https://registry.npmjs.org/katex/-/katex-0.18.4.tgz';
const PACKAGE_INTEGRITY =
    'sha512-IMPntbRLOU+eu88XDiFKqQ8Akhr9Tv7jDMXqPhjG9SI1JMA4DIgXk4x9k4skJz2NZJXBRbC+2pYBLj9olqcZow==';
const TARBALL_SHA256 =
    '0090b1ebccc77d1402ec95e85ee539e1da514d6cd6934156c00baf39dcb0e3aa';

const RAW_FILES = [
  file('package/LICENSE', 'LICENSE', 1107,
       '766ccc1f306c885aa45542a9846bbd0a505b27a0374f146778171c2254ce18e3',
       'license', 'text/plain'),
  file('package/dist/katex.mjs', 'assets/katex.mjs', 601882,
       '9069225f8307ea43f5c98e3c4772e26572a234a8d740018724acfe57f7467cbe',
       'runtime', 'text/javascript'),
  font('KaTeX_AMS-Regular', 28076,
       '0cdd387c9590a1a9f9794560022dbb59654a7d86f187aa0c81495ad42d3a7308'),
  font('KaTeX_Caligraphic-Bold', 6912,
       'de7701e42cf1f4cf0b766c03fb27977207eee2f4fd5d76fa82188406da43ea4c'),
  font('KaTeX_Caligraphic-Regular', 6908,
       '5d53e70ad607c2352162dec9e0923fb54ecdafaccbf604cd8dcf7d00facb989b'),
  font('KaTeX_Fraktur-Bold', 11348,
       '74444efd593c005e3f4573b44524704c0af0a937fe911cca9e94068d0d140d3f'),
  font('KaTeX_Fraktur-Regular', 11316,
       '51814d270d06ff0255dba0799994fa4d8c84d11f09951d47595f4abb1f3602dc'),
  font('KaTeX_Main-Bold', 25324,
       '0f60d1b897938ec918c8ce073092411baf9438f6739465693ff18b0f9d20b021'),
  font('KaTeX_Main-BoldItalic', 16780,
       '99cd42a3c072d918f2f44984a807cf7aa16e13545fd0875fc07c6c65f99e715b'),
  font('KaTeX_Main-Italic', 16988,
       '97479ca6cce906abc961ecac96faa5f9ca2e61b8e7670d475826bcdee9a7c267'),
  font('KaTeX_Main-Regular', 26272,
       'c2342cd8b869e01752a9321dc17213fc40d4d04c79688c1d43f2cf316abd7866'),
  font('KaTeX_Math-BoldItalic', 16400,
       'dc47344dbb6cb5b655c8460d561f4df5f501b90c804ad3c6cec65fe322351ab1'),
  font('KaTeX_Math-Italic', 16440,
       '7af58c5ec8f132a2ddde9027c6d7814decce4d3b822a11192a42a20e2e973264'),
  font('KaTeX_SansSerif-Bold', 12216,
       'e99ae51144bf1232efcc1bfe5add36262c6866b0faab24fa75740e1b98577a62'),
  font('KaTeX_SansSerif-Italic', 12028,
       '00b26ac825e2095056396e0553b8ac26d3f8ad158c3826e28b4c45b385c4714a'),
  font('KaTeX_SansSerif-Regular', 10344,
       '68e8c73ef42afd3ccec58bf0fba302cce448938e7fc020a5e31f8a952eee1342'),
  font('KaTeX_Script-Regular', 9644,
       '036d4e95149b69ff9bcc0cd55771efeb25ffa3947293e69acd78d5ac328c684b'),
  font('KaTeX_Size1-Regular', 5468,
       '6b47c40166b6dbe21a5dfca7718413f2147fd2399be1ba605d8ad39cedf25dfe'),
  font('KaTeX_Size2-Regular', 5208,
       'd04c54219f9eaec6d4d4fd42dfb28785975a4794d6b2fc71e566b9cd6db842dd'),
  font('KaTeX_Size3-Regular', 3624,
       '73d591271b1604960cb10bb90fee021670af7297017e0e98480b332d11f51995'),
  font('KaTeX_Size4-Regular', 4928,
       'a4af7d414440a1c1790825cfb700cf9cf43b0f2c4b04f0ebc523011ad9853ec0'),
  font('KaTeX_Typewriter-Regular', 13568,
       '71d517d67827787cfabdf186914cc3358eda539e37931941f2b2fd4a21f68c0b'),
];

const CSS_SOURCE = file(
    'package/dist/katex.min.css', '', 24727,
    '180c2d77d434d7da51d6625c50a964d4fd6fdbdb9bc8796a0a016c30c49931fb',
    'source', 'text/css');
const CSS_OUTPUT = file(
    CSS_SOURCE.source, 'assets/katex.min.css', 22593,
    '012a04cc949cc3a171c467965933772e41c7ca99c7887f98442be049657506cc',
    'stylesheet', 'text/css');
const OUTPUT_FILES = [RAW_FILES[0], RAW_FILES[1], CSS_OUTPUT,
                      ...RAW_FILES.slice(2)];

const DENIED_COMMANDS = Object.freeze([
  'href', 'url', 'includegraphics', 'htmlclass', 'htmlid', 'htmlstyle',
  'htmldata', 'def', 'gdef', 'edef', 'xdef', 'let', 'futurelet',
  'newcommand', 'renewcommand', 'providecommand', 'global', 'csname',
  'endcsname', 'expandafter', 'noexpand',
]);

function file(source, output, bytes, sha256, kind, mime) {
  return {source, output, bytes, sha256, kind, mime};
}

function font(name, bytes, sha256) {
  return file(`package/dist/fonts/${name}.woff2`,
              `assets/fonts/${name}.woff2`, bytes, sha256, 'font',
              'font/woff2');
}

function digest(algorithm, bytes, encoding = 'hex') {
  return createHash(algorithm).update(bytes).digest(encoding);
}

function readTarString(bytes) {
  const nul = bytes.indexOf(0);
  return bytes.subarray(0, nul === -1 ? bytes.length : nul).toString('utf8');
}

function readTarOctal(bytes, label) {
  const value = readTarString(bytes).trim();
  if (!/^[0-7]+$/.test(value)) throw new Error(`invalid tar ${label}`);
  const parsed = Number.parseInt(value, 8);
  if (!Number.isSafeInteger(parsed)) throw new Error(`invalid tar ${label}`);
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
    if (header.every((value) => value === 0)) return entries;
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
      entries.set(entryPath,
                  Buffer.from(archive.subarray(dataOffset, dataOffset + size)));
    } else if (size !== 0) {
      throw new Error(`directory has data: ${entryPath}`);
    }
    offset = nextOffset;
  }
  throw new Error('tar archive has no terminator');
}

export function verifyPackageMetadata(metadata) {
  const dependencies = metadata?.dependencies || {};
  if (metadata?.name !== PACKAGE_NAME ||
      metadata?.version !== PACKAGE_VERSION ||
      metadata?.license !== PACKAGE_LICENSE ||
      metadata?.exports?.['.']?.import?.default !== './dist/katex.mjs' ||
      Object.keys(dependencies).length !== 1 ||
      dependencies.commander !== '^8.3.0') {
    throw new Error('package identity or dependency mismatch');
  }
}

function validateRuntime(runtime) {
  const source = runtime.toString('utf8');
  const forbidden = [
    /\bimport\s*\(/,
    /(^|[;\n])\s*import\s+/m,
    /\beval\s*\(/,
    /\bnew\s+Function\s*\(/,
    /\b(?:globalThis|window|self)\.fetch\s*\(/,
    /\b(?:XMLHttpRequest|WebSocket|EventSource)\b/,
    /\b(?:localStorage|sessionStorage)\b/,
    /\bdocument\.cookie\b/,
    /\bnew\s+(?:Worker|SharedWorker)\s*\(/,
  ];
  if (forbidden.some((pattern) => pattern.test(source)) ||
      !source.includes('export {') || !source.includes('renderToString')) {
    throw new Error('selected browser runtime is not a closed ESM module');
  }
}

export function transformKatexCss(sourceBytes) {
  let replacements = 0;
  const transformed = sourceBytes.toString('utf8').replace(
      /src:url\(fonts\/([A-Za-z0-9_-]+)\.woff2\) format\("woff2"\),url\(fonts\/\1\.woff\) format\("woff"\),url\(fonts\/\1\.ttf\) format\("truetype"\)/g,
      (_match, name) => {
        ++replacements;
        return `src:url(fonts/${name}.woff2) format("woff2")`;
      });
  if (replacements !== 20) {
    throw new Error('KaTeX CSS font fallback transform mismatch');
  }
  const urls = [...transformed.matchAll(/url\(([^)]+)\)/g)]
                   .map((match) => match[1]);
  const expected = RAW_FILES.filter(({kind}) => kind === 'font')
                           .map(({output}) => output.replace('assets/', ''));
  if (JSON.stringify(urls) !== JSON.stringify(expected) ||
      urls.some((url) => url.includes('..') || url.includes(':') ||
                         url.startsWith('/') || !url.endsWith('.woff2'))) {
    throw new Error('KaTeX CSS references an unowned font resource');
  }
  const bytes = Buffer.from(transformed);
  if (bytes.length !== CSS_OUTPUT.bytes ||
      digest('sha256', bytes) !== CSS_OUTPUT.sha256) {
    throw new Error('transformed KaTeX CSS integrity mismatch');
  }
  return bytes;
}

export function verifyMathSourcePolicy(source) {
  if (typeof source !== 'string' || Buffer.byteLength(source) === 0 ||
      Buffer.byteLength(source) > MAX_MATH_SOURCE_BYTES ||
      /[\0\u0001-\u0008\u000b\u000c\u000e-\u001f\u007f]/.test(source)) {
    return {ok: false, reason: 'invalid_source'};
  }
  const tokens = source.match(/\\[A-Za-z@]+|\\.|\S/g) || [];
  if (tokens.length > MAX_MATH_TOKENS) {
    return {ok: false, reason: 'token_budget'};
  }
  let depth = 0;
  for (let index = 0; index < source.length; ++index) {
    if (source[index] === '\\') {
      ++index;
      continue;
    }
    if (source[index] === '{' && ++depth > MAX_MATH_BRACE_DEPTH) {
      return {ok: false, reason: 'depth_budget'};
    }
    if (source[index] === '}' && depth > 0) --depth;
  }
  const denied = new Set(DENIED_COMMANDS);
  for (const match of source.matchAll(/\\([A-Za-z@]+|.)/g)) {
    const command = match[1].toLowerCase();
    if (denied.has(command) || command.startsWith('html')) {
      return {ok: false, reason: 'denied_command'};
    }
  }
  return {ok: true, reason: 'allowed'};
}

function expectedManifest() {
  const selectedBytes = OUTPUT_FILES.reduce((sum, item) => sum + item.bytes, 0);
  return {
    schema: 'crayon-katex-assets/v1',
    package: {
      name: PACKAGE_NAME,
      version: PACKAGE_VERSION,
      license: PACKAGE_LICENSE,
      source: PACKAGE_URL,
      npmIntegrity: PACKAGE_INTEGRITY,
      tarballSha256: TARBALL_SHA256,
      upstreamTag: 'v0.18.4',
      upstreamCommit: '49dc3d986747fd7d3bb25b597bcb98b071ae6035',
    },
    policy: {
      browserRuntimeDependencies: 0,
      excludedPackageDependency: 'commander@^8.3.0 (CLI only)',
      selectedBytes,
      selectedByteBudget: MAX_SELECTED_BYTES,
      fontFormat: 'woff2',
      fontCount: 20,
      delimiters: {inline: '$...$', block: '$$...$$'},
      maxSourceBytes: MAX_MATH_SOURCE_BYTES,
      maxTokens: MAX_MATH_TOKENS,
      maxBraceDepth: MAX_MATH_BRACE_DEPTH,
      deniedCommands: [...DENIED_COMMANDS],
      deniedCommandPrefixes: ['html'],
      renderOptions: {
        output: 'htmlAndMathml',
        throwOnError: true,
        strict: 'error',
        trust: false,
        globalGroup: false,
        maxSize: 16,
        maxExpand: 256,
        macros: 'fresh-empty-null-prototype-per-render',
      },
    },
    files: OUTPUT_FILES.map(({output, bytes, sha256, kind, mime}) => ({
      path: output, bytes, sha256, kind, mime,
    })),
  };
}

function vendoredReadme() {
  return `# KaTeX vendored browser closure

- Package: \`katex@${PACKAGE_VERSION}\`
- License: MIT; see [LICENSE](LICENSE).
- Source: ${PACKAGE_URL}
- Runtime closure: one self-contained ESM renderer, deterministic WOFF2-only CSS, and 20 WOFF2 fonts. The package's Commander dependency is CLI-only and excluded.
- Policy: only Browser-owned \`$...$\`/\`$$...$$\` facts may call this runtime; trust/HTML/URL commands and user macro definitions are denied; no auto-render, contrib, Node CLI, network, source map, WOFF or TTF assets.

Normal builds consume only checked-in bytes. Verify offline with \`node tools/katex/vendor.mjs --check\`. Reproduce an approved update with \`node tools/katex/vendor.mjs --archive <tarball>\`; \`--download\` is an explicit maintainer-only network action.
`;
}

function verifySelectedEntries(entries) {
  const packageJsonBytes = entries.get('package/package.json');
  if (!packageJsonBytes) throw new Error('missing package metadata');
  let metadata;
  try {
    metadata = JSON.parse(packageJsonBytes.toString('utf8'));
  } catch {
    throw new Error('invalid package metadata');
  }
  verifyPackageMetadata(metadata);
  const selected = new Map();
  for (const item of RAW_FILES) {
    const bytes = entries.get(item.source);
    if (!bytes) throw new Error(`missing selected asset: ${item.source}`);
    if (bytes.length !== item.bytes || digest('sha256', bytes) !== item.sha256) {
      throw new Error(`selected asset integrity mismatch: ${item.source}`);
    }
    selected.set(item.output, bytes);
  }
  const cssSource = entries.get(CSS_SOURCE.source);
  if (!cssSource || cssSource.length !== CSS_SOURCE.bytes ||
      digest('sha256', cssSource) !== CSS_SOURCE.sha256) {
    throw new Error('selected asset integrity mismatch: KaTeX CSS');
  }
  selected.set(CSS_OUTPUT.output, transformKatexCss(cssSource));
  validateRuntime(selected.get('assets/katex.mjs'));
  const selectedBytes = [...selected.values()]
                            .reduce((sum, bytes) => sum + bytes.length, 0);
  if (selectedBytes !== expectedManifest().policy.selectedBytes ||
      selectedBytes > MAX_SELECTED_BYTES) {
    throw new Error('selected asset closure exceeds budget');
  }
  return selected;
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
  return verifySelectedEntries(parseTarArchive(archive));
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

export async function verifyVendorDirectory(root = vendorRoot()) {
  const manifestPath = path.join(root, 'manifest.json');
  if (!existsSync(manifestPath)) throw new Error('missing manifest');
  const expectedText = `${JSON.stringify(expectedManifest(), null, 2)}\n`;
  if (await readFile(manifestPath, 'utf8') !== expectedText) {
    throw new Error('manifest content mismatch');
  }
  const expectedFiles = new Set([
    'manifest.json', 'VENDORED.md', ...OUTPUT_FILES.map(({output}) => output),
  ]);
  const actualFiles = await listFiles(root);
  if (actualFiles.length !== expectedFiles.size ||
      actualFiles.some((item) => !expectedFiles.has(item))) {
    throw new Error('vendor file set mismatch');
  }
  if (await readFile(path.join(root, 'VENDORED.md'), 'utf8') !==
      vendoredReadme()) {
    throw new Error('vendored documentation mismatch');
  }
  for (const item of OUTPUT_FILES) {
    const bytes = await readFile(path.join(root, item.output));
    if (bytes.length !== item.bytes || digest('sha256', bytes) !== item.sha256) {
      throw new Error(`vendored asset integrity mismatch: ${item.output}`);
    }
  }
  const cssText = await readFile(path.join(root, CSS_OUTPUT.output), 'utf8');
  const cssUrls = [...cssText.matchAll(/url\(([^)]+)\)/g)]
                      .map((match) => match[1]);
  const expectedUrls = RAW_FILES.filter(({kind}) => kind === 'font')
                                .map(({output}) => output.replace('assets/', ''));
  if (JSON.stringify(cssUrls) !== JSON.stringify(expectedUrls)) {
    throw new Error('vendored CSS resource closure mismatch');
  }
  validateRuntime(await readFile(path.join(root, 'assets/katex.mjs')));
  return expectedManifest();
}

async function writeVendorDirectory(selected, root = vendorRoot()) {
  const parent = path.dirname(root);
  await mkdir(parent, {recursive: true});
  const temporary = await mkdtemp(path.join(parent, '.katex-vendor-'));
  const backup = `${root}.previous-${process.pid}`;
  try {
    for (const [relative, bytes] of selected) {
      const output = path.join(temporary, relative);
      await mkdir(path.dirname(output), {recursive: true});
      await writeFile(output, bytes);
    }
    await writeFile(path.join(temporary, 'manifest.json'),
                    `${JSON.stringify(expectedManifest(), null, 2)}\n`);
    await writeFile(path.join(temporary, 'VENDORED.md'), vendoredReadme());
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

async function downloadTarball() {
  return new Promise((resolve, reject) => {
    const request = https.get(
        PACKAGE_URL, {headers: {'User-Agent': 'crayon-vendor/1'}}, (response) => {
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
                      '../../third_party/katex');
}

async function readBoundedTarball(filePath) {
  const info = await lstat(filePath);
  if (!info.isFile() || info.isSymbolicLink() ||
      info.size > MAX_TARBALL_BYTES) {
    throw new Error('archive path must be a bounded regular file');
  }
  return readFile(filePath);
}

async function run(arguments_) {
  if (arguments_.length === 1 && arguments_[0] === '--check') {
    const manifest = await verifyVendorDirectory();
    console.log(`KaTeX vendor OK: ${manifest.files.length} assets, ` +
                `${manifest.policy.selectedBytes} bytes`);
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
  const manifest = await writeVendorDirectory(verifyTarball(tarball));
  console.log(`KaTeX vendor updated: ${manifest.files.length} assets, ` +
              `${manifest.policy.selectedBytes} bytes`);
}

const invokedPath = process.argv[1] ? path.resolve(process.argv[1]) : '';
if (invokedPath === fileURLToPath(import.meta.url)) {
  run(process.argv.slice(2)).catch((error) => {
    console.error(`KaTeX vendor failed: ${error.message}`);
    process.exitCode = 1;
  });
}
