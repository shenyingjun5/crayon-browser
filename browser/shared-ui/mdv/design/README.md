# MDV toolbar design contract

`mdv-toolbar-v1` freezes the local Markdown editor's original functional
glyphs. It reuses the density metrics owned by
`browser/shared-ui/design/tokens.json` without extending the desktop chrome
glyph manifest.

- All glyphs use a 24×24 canvas, inherit `currentColor`, and are decorative.
- Controls provide localized accessible names; SVG files use
  `aria-hidden="true"` and `focusable="false"`.
- `icons/manifest.json` is the closed registry consumed by the MDV toolbar.
- The verifier rejects undeclared assets, duplicate roles, external content,
  embedded colors, scripts, event handlers, and invalid metrics.

Run independently:

```sh
cmake -S browser/shared-ui/mdv/design -B .cache/build/mdv-toolbar-design
ctest --test-dir .cache/build/mdv-toolbar-design --output-on-failure
```
