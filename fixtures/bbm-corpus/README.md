# `.bbm` corpus

Real-world `.bbm` files used as goldens for load/round-trip tests.

## Provenance

| File | Source | Size |
|---|---|---|
| `tight-corner.bbm` | Provided by @aronwk from local BlueBrick 1.9.2 projects directory | 231 KB |

These files are loaded read-only by the round-trip tests. Contributors adding
more fixtures should note author/origin here and make sure the file is
appropriate to ship under this repo's GPL-3.0 license.

## Byte-exact round-trip status

- Current round-trip tests assert **successful load + semantic equality**, not
  byte-exact output. Vanilla BlueBrick emits CRLF, 2-space indentation, no BOM,
  no xmlns attributes on `<Map>`, and `<EmptyTag />` (space before `/>`).
- The writer matches indent style and drops xmlns; CRLF/self-close-spacing
  are tracked for a follow-up pass that enables byte-diff CI.
