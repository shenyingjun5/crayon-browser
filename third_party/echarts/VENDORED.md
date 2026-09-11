# Vendored: echarts 6.1.0 (MRT-12)

Source: https://registry.npmjs.org/echarts/-/echarts-6.1.0.tgz
License: Apache-2.0 (see LICENSE)
Closure: dist/echarts.esm.min.js (single-file offline runtime)

## Provenance

- Tarball pinned by npm integrity + sha256 in manifest.json.
- Closure copied verbatim from the tarball (zero relative imports, zero
  network imports — enforced by tools/echarts/vendor.mjs --check).
- Rebuild: `node tools/echarts/vendor.mjs --archive <tgz>`.
- Maintainer upgrade: `node tools/echarts/vendor.mjs --download`
  (explicit network action; never runs in normal builds).

## Usage boundary (MRT-13 owns enforcement)

The runtime loads this file only for `echarts`-fenced blocks. Series/
component allowlist and the no-function/no-eval/no-URL option schema
are enforced by MRT-13 before any option reaches the renderer.
