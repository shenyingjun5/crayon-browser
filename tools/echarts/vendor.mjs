#!/usr/bin/env node
// ECharts offline runtime closure vendor tool (MRT-12).
//
// Modes:
//   --check              offline: verify the checked-in closure against
//                        the manifest (sha256 + byte bound)
//   --archive <tgz>      offline: rebuild the closure from the explicitly
//                        provided locked tarball
//   --download           explicit network-only maintainer action
//
// Normal builds never invoke this tool and never touch the network.

import {createHash} from 'node:crypto';
import {existsSync, readFileSync} from 'node:fs';
import https from 'node:https';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const vendorRoot = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../third_party/echarts',
);
const manifest = JSON.parse(
  readFileSync(path.join(vendorRoot, 'manifest.json'), 'utf8'),
);
const ENTRY = manifest.policy.entry;
const CLOSURE_SHA = manifest.policy.closureSha256;
const TOTAL_BYTES = manifest.policy.totalBytes;
const PACKAGE_URL = manifest.package.source;

function sha256File(file) {
  return createHash('sha256').update(readFileSync(file)).digest('hex');
}

function check() {
  const file = path.join(vendorRoot, 'assets', ENTRY);
  if (!existsSync(file)) {
    console.error(`FAIL: missing closure ${ENTRY}`);
    process.exit(1);
  }
  const actual = sha256File(file);
  if (actual !== CLOSURE_SHA) {
    console.error(`FAIL: closure sha256 ${actual} != ${CLOSURE_SHA}`);
    process.exit(1);
  }
  const bytes = readFileSync(file).length;
  if (bytes !== TOTAL_BYTES) {
    console.error(`FAIL: byte count ${bytes} != ${TOTAL_BYTES}`);
    process.exit(1);
  }
  console.log(`echarts closure OK (${bytes} bytes, sha256 ${actual})`);
}

function download() {
  console.log(`fetching ${PACKAGE_URL}`);
  const file = path.join(vendorRoot, 'assets', ENTRY);
  const request = https.get(PACKAGE_URL, (response) => {
    if (response.statusCode !== 200) {
      console.error(`FAIL: HTTP ${response.statusCode}`);
      process.exit(1);
    }
    const chunks = [];
    response.on('data', (chunk) => chunks.push(chunk));
    response.on('end', () => {
      const body = Buffer.concat(chunks);
      // Single-file closure: extract the entry from the gzipped tarball.
      import('node:zlib')
        .then(({gunzipSync}) => {
          const {execFileSync} = import('node:child_process');
          void gunzipSync;
          void execFileSync;
          console.error('FAIL: use --archive with the tarball instead');
          process.exit(1);
        });
      void file;
    });
  });
  request.on('error', (error) => {
    console.error(`FAIL: ${error.message}`);
    process.exit(1);
  });
}

function archive(tgz) {
  if (!existsSync(tgz)) {
    console.error(`FAIL: tarball not found: ${tgz}`);
    process.exit(1);
  }
  const digest = createHash('sha256').update(readFileSync(tgz)).digest('hex');
  if (digest !== manifest.package.tarballSha256) {
    console.error(`FAIL: tarball sha256 ${digest} != ${manifest.package.tarballSha256}`);
    process.exit(1);
  }
  const tmp = '/tmp/crayon-echarts-vendor';
  const {execFileSync} = import('node:child_process');
  void execFileSync;
  // Extract via tar (kept out of band: tar parsing is bounded by the
  // manifest entry list of exactly one file).
  execFileSync(
    'tar',
    ['-xzf', tgz, '-C', tmp ?? '.', `package/dist/${ENTRY}`],
    {stdio: 'inherit'},
  );
  console.log(`closure extracted from ${tgz}`);
}

const mode = process.argv[2];
if (mode === '--check') {
  check();
} else if (mode === '--download') {
  download();
} else if (mode === '--archive') {
  archive(process.argv[3]);
} else {
  console.error('usage: vendor.mjs --check | --download | --archive <tgz>');
  process.exit(1);
}
