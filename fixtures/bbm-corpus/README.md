# `.bbm` corpus

Real-world `.bbm` files used as goldens for load/round-trip tests.

## Provenance

| File | Source | Size |
|---|---|---|
| `tight-corner.bbm` | Provided by @aronwk from local BlueBrick 1.9.2 projects directory | 231 KB |
| `fordyce-2026.bbm` | Provided by @aronwk; 2026 Fordyce event layout | 557 KB |

These files are loaded read-only by the round-trip tests. Contributors adding
more fixtures should note author/origin here and make sure the file is
appropriate to ship under this repo's GPL-3.0 license.

## Byte-exact round-trip status

- **Enforced in CI** via `tests/saveload/RealFixtureTest::CorpusByteExactRoundTrip`
  — every `.bbm` here must load + re-save byte-for-byte identical.
- Vanilla format properties matched: CRLF line endings, 2-space indent,
  no BOM, no xmlns attributes on `<Map>`, `<EmptyTag />` spacing, lowercase
  `utf-8` in the XML declaration, lowercase hex unknown colors, known-color
  name preservation, no-trailing-newline.

## Rendering previews

Each fixture has a PNG preview in `docs/preview/` produced by `bld-render`,
refreshed manually when the rendering pipeline changes.
