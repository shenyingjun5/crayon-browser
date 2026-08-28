# Highlight.js vendored closure

- Package: `@highlightjs/cdn-assets@11.12.0`
- License: BSD-3-Clause; see [LICENSE](LICENSE).
- Source: https://registry.npmjs.org/@highlightjs/cdn-assets/-/cdn-assets-11.12.0.tgz
- Runtime closure: ESM core plus 25 explicit grammars; zero npm runtime dependencies.
- Policy: no auto-detection, network, plugins, workers, themes, source maps or all-language bundle. Unknown languages remain escaped plaintext.

Normal builds only consume these checked-in bytes. Verify offline with `node tools/highlight/vendor.mjs --check`. To reproduce an approved update, obtain the exact tarball and run `node tools/highlight/vendor.mjs --archive <tarball>`; `--download` is an explicit network-only maintainer action.
