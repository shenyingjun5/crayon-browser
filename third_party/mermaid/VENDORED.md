# Mermaid Full vendored closure

- Package: `mermaid@11.17.2` (upstream tag `v11.17.2`, MIT; see [LICENSE](LICENSE)).
- Source: https://registry.npmjs.org/mermaid/-/mermaid-11.17.2.tgz
- npm integrity: `sha512-V6K3C8EBdEsPFZXSKMJe6ppQOENxuHARr9GvHX4hh47lAbhMRD9qf4oEK7LoaRQxULMa80/qt5gHO73aCleBBg==`
- Tarball SHA-256: `6ad2f42c3fc26bbf9e45cbb6d11898972573ea52b33a5f4ff51952899f950ffd`
- Runtime closure: ESM entry `mermaid.esm.min.mjs` plus 103 reachable chunks (3522090 bytes); the npm manifest's source-consumer runtime dependencies are pre-bundled upstream and none are vendored.
- Policy: no CDN/http imports, no tiny distribution, no tree-shaking of diagram types, no source maps, docs, tests or dev dependencies. SVG output stays untrusted and passes the Browser-owned SVG policy gate before injection.

Verify offline with `node tools/mermaid/vendor.mjs --check`. To reproduce an approved update, obtain the exact tarball and run `node tools/mermaid/vendor.mjs --archive <tgz>`; `--download` is an explicit network-only maintainer action.
