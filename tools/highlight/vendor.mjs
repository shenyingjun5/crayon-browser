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

export const MAX_TARBALL_BYTES = 2 * 1024 * 1024;
export const MAX_ARCHIVE_BYTES = 8 * 1024 * 1024;
export const MAX_ENTRY_BYTES = 2 * 1024 * 1024;
const MAX_TAR_ENTRIES = 2048;
const MAX_SELECTED_BYTES = 512 * 1024;

const PACKAGE_NAME = '@highlightjs/cdn-assets';
const PACKAGE_VERSION = '11.12.0';
const PACKAGE_LICENSE = 'BSD-3-Clause';
const PACKAGE_URL =
    'https://registry.npmjs.org/@highlightjs/cdn-assets/-/cdn-assets-11.12.0.tgz';
const PACKAGE_INTEGRITY =
    'sha512-KvOKXODaiFmId9xaq3xc5xCL66wVLUuOngDbO9B/kewbFTqdGbn2nJxNhN3H5R1cgDTVj6R8vH0zgiNDEGjpDw==';
const TARBALL_SHA256 =
    'b8a006d30f45afe783072569f3d69c5b60c0e7b9ca28cd474e12f2584e2a3bd9';

const LANGUAGES = [
  {id: 'bash', aliases: ['bash', 'sh', 'shell', 'zsh'], dependencies: []},
  {id: 'c', aliases: ['c', 'h'], dependencies: []},
  {id: 'cpp', aliases: ['cpp', 'c++', 'cc', 'cxx', 'hpp', 'hxx'], dependencies: []},
  {id: 'csharp', aliases: ['csharp', 'c#', 'cs'], dependencies: []},
  {id: 'css', aliases: ['css'], dependencies: []},
  {id: 'diff', aliases: ['diff', 'patch'], dependencies: []},
  {id: 'dockerfile', aliases: ['dockerfile', 'docker'], dependencies: ['bash']},
  {id: 'go', aliases: ['go', 'golang'], dependencies: []},
  {id: 'graphql', aliases: ['graphql', 'gql'], dependencies: []},
  {id: 'java', aliases: ['java'], dependencies: []},
  {id: 'javascript', aliases: ['javascript', 'js', 'jsx', 'mjs', 'cjs'], dependencies: ['css', 'graphql', 'xml']},
  {id: 'json', aliases: ['json'], dependencies: []},
  {id: 'kotlin', aliases: ['kotlin', 'kt', 'kts'], dependencies: []},
  {id: 'markdown', aliases: ['markdown', 'md', 'mkdown', 'mkd'], dependencies: ['xml']},
  {id: 'objectivec', aliases: ['objectivec', 'objective-c', 'objc', 'obj-c'], dependencies: []},
  {id: 'php', aliases: ['php'], dependencies: []},
  {id: 'powershell', aliases: ['powershell', 'ps1', 'pwsh'], dependencies: []},
  {id: 'python', aliases: ['python', 'py'], dependencies: []},
  {id: 'ruby', aliases: ['ruby', 'rb'], dependencies: []},
  {id: 'rust', aliases: ['rust', 'rs'], dependencies: []},
  {id: 'sql', aliases: ['sql'], dependencies: []},
  {id: 'swift', aliases: ['swift'], dependencies: []},
  {id: 'typescript', aliases: ['typescript', 'ts', 'tsx', 'mts', 'cts'], dependencies: ['css', 'graphql', 'xml']},
  {id: 'xml', aliases: ['xml', 'html', 'xhtml', 'rss', 'atom', 'svg', 'xsd', 'xsl', 'plist'], dependencies: ['css', 'javascript']},
  {id: 'yaml', aliases: ['yaml', 'yml'], dependencies: ['ruby']},
];

const FILES = [
  file('package/LICENSE', 'LICENSE', 1514,
       '6c081431591d9df696c82dc598fe1423765b8a299b200ed00b281afd0f64c490',
       'license'),
  file('package/es/core.min.js', 'assets/core.min.js', 20501,
       '67a339aa68880a40c9803def9c8cebb2225dec76230382db86e349370dee70e9',
       'core'),
  grammar('bash', 3182, '7db1758fe6f04cd410760c29879812a3774edc54edcc91323511a24efc0b9e61'),
  grammar('c', 4788, 'edada72825cceb7dec51cc785efcc3e6378970917e11e4c7dfab29d0079e08a5'),
  grammar('cpp', 6161, 'aa67eeacdbbda9f40ca34c59147581aef1601f19b71a55966de4ccf4246e8c31'),
  grammar('csharp', 4159, '74bb1a32b37d581173fc9d880ab5be6c7e448bbb12552c3e2a3ac7fc3c57cf8f'),
  grammar('css', 13544, '2f4264d9a07a4f5446e51e2d93b0c0e031b4fd910e8e44984cd17f996a990b99'),
  grammar('diff', 688, 'cbd7cf3b49bee36d6a0cf97789a7c5e68fbfd4231e86a81966f9b78c7bc127d0'),
  grammar('dockerfile', 510, '5a83ccd455a2910e9018f591fc1d2856ff5151ffa9889fc000542e16b329b039'),
  grammar('go', 1563, '05658f281f7f12233cd891e879c8b9ebfd5998f5dc3f1e80632889ce61178534'),
  grammar('graphql', 823, 'd33b3b12e4106cb21e930c0c5905aa0882ec547e5c34d9803a7b76df623781d5'),
  grammar('java', 2850, '7987da1c2dcc1d83f7a5a670cdc622fdac834cb1c140ad503bf8d7ea9237ac36'),
  grammar('javascript', 6599, 'c3dbd515a79e2c00ff4535aa5ad2fe539fb2587d047f9ab826107d0784da7bb6'),
  grammar('json', 689, '8f2343ffbcc4fdea4b336a4241d54d223d52f99b67729d2d6d3556bbf66908ae'),
  grammar('kotlin', 3468, 'afce4ebc96165516de9ea9fc4c8b146efb556806bf093dc98ea73d3e9acdd3cf'),
  grammar('markdown', 2206, '73cb474b4687421a0949ded1ba8314ec74c26058688dc7f6154084240352cc79'),
  grammar('objectivec', 2939, '3a7ff444af83d285aff34b7f66e06309bccc5820957c5d915b5711a94ff5fd43'),
  grammar('php', 6585, 'd47d3e20ee16dd1a34bdc8bfa8a605801de7dac8bb33ee40e85cd28f2a6a7bf9'),
  grammar('powershell', 4438, '67c95a71dae92f010a15e959158aaf682cd850a008d466f1c5d40394cbe22f12'),
  grammar('python', 3655, 'fd5d479dcc9d07b957cf07b1df76d3d8fb6ee6f6d1437238d9530a9be724c046'),
  grammar('ruby', 4027, '1b4c8f07774a13770cd4e0445fa47c555a41f29dad4d3ca9d1d7c3aad9e4ea79'),
  grammar('rust', 3050, 'fde151d2b246bef44c98da80e7cac3cbb05de794911305fe9a3adf6beff87659'),
  grammar('sql', 6545, '79bead5cf2bfa93d3982073d000a97802f394fe287736644bab30f40cb5c7380'),
  grammar('swift', 8294, '9522716ca5b45f102b26c2ee8fc13b16d7707b2a10b650872207a17882a4109c'),
  grammar('typescript', 7868, '0e10ce9a81d42f8d43a54007ca7e31fe22ad801b2a2e1693d31736aa79fa5404'),
  grammar('xml', 1992, '2e8c8ba1e7a200cd83dfbf9c746b8e00f7d7bad966dbfd83eca142cf2caa4dd7'),
  grammar('yaml', 1947, '7010955f1f87aef1de085483d79d42817544aaf03c4f57a6fd4eb838f2cdf609'),
];

function file(source, output, bytes, sha256, kind, language = null) {
  return {source, output, bytes, sha256, kind, language};
}

function grammar(language, bytes, sha256) {
  return file(`package/es/languages/${language}.min.js`,
              `assets/languages/${language}.min.js`, bytes, sha256,
              'grammar', language);
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

function expectedManifest() {
  const selectedBytes = FILES.reduce((sum, item) => sum + item.bytes, 0);
  return {
    schema: 'crayon-highlight-assets/v1',
    package: {
      name: PACKAGE_NAME,
      version: PACKAGE_VERSION,
      license: PACKAGE_LICENSE,
      source: PACKAGE_URL,
      npmIntegrity: PACKAGE_INTEGRITY,
      tarballSha256: TARBALL_SHA256,
    },
    policy: {
      autoDetect: false,
      unknownLanguage: 'plaintext',
      runtimeDependencies: 0,
      selectedBytes,
      selectedByteBudget: MAX_SELECTED_BYTES,
    },
    plaintextAliases: ['plaintext', 'text', 'txt', 'plain', 'nohighlight'],
    languages: LANGUAGES,
    files: FILES.map(({output, bytes, sha256, kind, language}) => ({
      path: output,
      bytes,
      sha256,
      kind,
      ...(language ? {language} : {}),
    })),
  };
}

function vendoredReadme() {
  return `# Highlight.js vendored closure\n\n` +
      `- Package: \`${PACKAGE_NAME}@${PACKAGE_VERSION}\`\n` +
      `- License: ${PACKAGE_LICENSE}; see [LICENSE](LICENSE).\n` +
      `- Source: ${PACKAGE_URL}\n` +
      `- Runtime closure: ESM core plus ${LANGUAGES.length} explicit grammars; zero npm runtime dependencies.\n` +
      `- Policy: no auto-detection, network, plugins, workers, themes, source maps or all-language bundle. Unknown languages remain escaped plaintext.\n\n` +
      `Normal builds only consume these checked-in bytes. Verify offline with \`node tools/highlight/vendor.mjs --check\`. ` +
      `To reproduce an approved update, obtain the exact tarball and run \`node tools/highlight/vendor.mjs --archive <tarball>\`; ` +
      `\`--download\` is an explicit network-only maintainer action.\n`;
}

function gitattributesContent() {
  // Byte-exact vendor closure: Git must not rewrite line endings on checkout
  // (a CRLF worktree would fail the sha256 asset checks on Windows).
  return '# Vendored bytes must stay byte-identical across platforms.\n* -text\n';
}

function validateLanguageClosure() {
  const ids = new Set(LANGUAGES.map(({id}) => id));
  const aliases = new Set();
  for (const language of LANGUAGES) {
    if (!/^[a-z][a-z0-9]*$/.test(language.id)) {
      throw new Error(`invalid canonical language: ${language.id}`);
    }
    for (const dependency of language.dependencies) {
      if (!ids.has(dependency)) {
        throw new Error(`missing grammar dependency: ${dependency}`);
      }
    }
    for (const alias of language.aliases) {
      if (!/^[a-z0-9][a-z0-9+#.-]{0,31}$/.test(alias) || aliases.has(alias)) {
        throw new Error(`invalid or duplicate language alias: ${alias}`);
      }
      aliases.add(alias);
    }
  }
}

export function verifyPackageMetadata(metadata) {
  if (metadata.name !== PACKAGE_NAME || metadata.version !== PACKAGE_VERSION ||
      metadata.license !== PACKAGE_LICENSE || metadata.dependencies != null) {
    throw new Error('package identity or dependency mismatch');
  }
}

function verifySelectedEntries(entries) {
  validateLanguageClosure();
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
  const selected = new Map();
  let selectedBytes = 0;
  for (const item of FILES) {
    const bytes = entries.get(item.source);
    if (!bytes) {
      throw new Error(`missing selected asset: ${item.source}`);
    }
    if (bytes.length !== item.bytes || digest('sha256', bytes) !== item.sha256) {
      throw new Error(`selected asset integrity mismatch: ${item.source}`);
    }
    selectedBytes += bytes.length;
    selected.set(item.output, bytes);
  }
  for (const language of LANGUAGES) {
    const source = selected.get(`assets/languages/${language.id}.min.js`)
                       .toString('utf8');
    const discovered = [...source.matchAll(/subLanguage:"([a-z0-9-]+)"/g)]
                           .map((match) => match[1]);
    const actual = [...new Set(discovered)].sort();
    const expected = [...language.dependencies].sort();
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
      throw new Error(`grammar dependency mismatch: ${language.id}`);
    }
  }
  if (selectedBytes > MAX_SELECTED_BYTES) {
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

// Line endings may differ between the generated LF text and a Git checkout
// with core.autocrlf=true; text documents are compared after normalization
// while asset integrity stays byte-exact via sha256.
function normalizeCheckoutText(text) {
  return text.replace(/\r\n/g, '\n');
}

export async function verifyVendorDirectory(root = vendorRoot()) {
  const manifestPath = path.join(root, 'manifest.json');
  if (!existsSync(manifestPath)) {
    throw new Error('missing manifest');
  }
  const expectedText = `${JSON.stringify(expectedManifest(), null, 2)}\n`;
  const actualText = await readFile(manifestPath, 'utf8');
  if (normalizeCheckoutText(actualText) !== expectedText) {
    throw new Error('manifest content mismatch');
  }
  const expectedFiles = new Set([
    'manifest.json',
    'VENDORED.md',
    '.gitattributes',
    ...FILES.map(({output}) => output),
  ]);
  const actualFiles = await listFiles(root);
  if (actualFiles.length !== expectedFiles.size ||
      actualFiles.some((item) => !expectedFiles.has(item))) {
    throw new Error('vendor file set mismatch');
  }
  if (normalizeCheckoutText(
          await readFile(path.join(root, 'VENDORED.md'), 'utf8')) !==
      vendoredReadme()) {
    throw new Error('vendored documentation mismatch');
  }
  for (const item of FILES) {
    const bytes = await readFile(path.join(root, item.output));
    if (bytes.length !== item.bytes || digest('sha256', bytes) !== item.sha256) {
      throw new Error(`vendored asset integrity mismatch: ${item.output}`);
    }
  }
  return expectedManifest();
}

async function writeVendorDirectory(selected, root = vendorRoot()) {
  const parent = path.dirname(root);
  await mkdir(parent, {recursive: true});
  const temporary = await mkdtemp(path.join(parent, '.highlight-vendor-'));
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
    await writeFile(path.join(temporary, '.gitattributes'),
                    gitattributesContent());
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
                      '../../third_party/highlight');
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
    console.log(`highlight vendor OK: ${manifest.languages.length} grammars, ` +
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
  const selected = verifyTarball(tarball);
  const manifest = await writeVendorDirectory(selected);
  console.log(`highlight vendor updated: ${manifest.languages.length} grammars, ` +
              `${manifest.policy.selectedBytes} bytes`);
}

const invokedPath = process.argv[1] ? path.resolve(process.argv[1]) : '';
if (invokedPath === fileURLToPath(import.meta.url)) {
  run(process.argv.slice(2)).catch((error) => {
    console.error(`highlight vendor failed: ${error.message}`);
    process.exitCode = 1;
  });
}
