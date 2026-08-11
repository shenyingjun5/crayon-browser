# Browser shared design contract

`browser-design-v1` is the platform-neutral source of truth for the desktop browser chrome. It contains logical DIP metrics, light/dark semantic colors, responsive control priority, original functional SVG glyphs, and deterministic UX-001 specification goldens.

- `tokens.json` owns layout, density, component-state, theme, and responsive tokens.
- `icons/manifest.json` maps semantic roles to original 24×24 glyphs. Components provide localized accessible names; SVG files are decorative and inherit `currentColor`.
- `golden/` freezes the light/dark × narrow/wide × 100%/200% matrix as data. These are specification goldens, not substitutes for platform screenshots or device accessibility checks.
- `tests/verify-design.mjs` rejects missing roles, duplicate or undeclared files, external SVG content, embedded state colors, App-icon misuse, and stale goldens; `negative-contract.mjs` proves representative invalid inputs fail closed.
- `tools/generate-goldens.mjs` deterministically rewrites only the eight specification goldens from `tokens.json`; generated output is always checked by the independent verifier.

The application identity is not a functional glyph. Native title bars, taskbars, docks, and window switchers consume the managed `app-icon-v1:micro` platform asset. Cast, permission, connection, Agent, challenge, and error UI must use their own semantic glyphs and states.

Run independently:

```powershell
cmake -S browser/shared-ui/design -B .cache/build/browser-design
ctest --test-dir .cache/build/browser-design --output-on-failure
```
