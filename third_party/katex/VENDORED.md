# KaTeX vendored browser closure

- Package: `katex@0.18.4`
- License: MIT; see [LICENSE](LICENSE).
- Source: https://registry.npmjs.org/katex/-/katex-0.18.4.tgz
- Runtime closure: one self-contained ESM renderer, deterministic WOFF2-only CSS, and 20 WOFF2 fonts. The package's Commander dependency is CLI-only and excluded.
- Policy: only Browser-owned `$...$`/`$$...$$` facts may call this runtime; trust/HTML/URL commands and user macro definitions are denied; no auto-render, contrib, Node CLI, network, source map, WOFF or TTF assets.

Normal builds consume only checked-in bytes. Verify offline with `node tools/katex/vendor.mjs --check`. Reproduce an approved update with `node tools/katex/vendor.mjs --archive <tarball>`; `--download` is an explicit maintainer-only network action.
