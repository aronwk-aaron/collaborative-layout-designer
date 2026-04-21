# Render goldens

Reference PNGs rendered from every `.bbm` in `../bbm-corpus/` at a
fixed resolution (1600 × 1200, see `kGoldenWidth`/`kGoldenHeight` in
[../../tests/rendering/RenderGoldensTest.cpp](../../tests/rendering/RenderGoldensTest.cpp)).
Each test fixture's golden sits alongside it as `<stem>.png`; the
golden is what this fork is *expected* to render today. Drift beyond
the per-channel tolerance documented in the test harness = a
regression.

## Opt-in — not default

The `RenderGoldens.*` tests **skip unless `CLD_ENABLE_RENDER_GOLDENS=1`**
is set in the environment when you run `ctest`. Cross-environment
pixel parity is essentially impossible — Qt versions, font hinting,
Mesa/ANGLE versions, GIF decoders all drift enough that an 8/255
channel tolerance over 0.1% of the frame isn't survivable across
Linux/Windows/macOS (or even across Qt 6.7 → 6.10 on the same box).

Treat them as a local regression gate:

```sh
CLD_ENABLE_RENDER_GOLDENS=1 ctest --test-dir build -R RenderGoldens
```

Run the goldens on the same machine you'll regenerate them on. If
you change the renderer intentionally, regenerate (below) and
re-run to lock in the new output.

## Initial capture

Ideally captured once on Windows from **vanilla BlueBrick 1.9.2** so
the goldens enforce visual parity, not just "don't change". Until
that's done, the test harness writes a `*.suggested.png` next to
each expected golden when no reference exists — promote it to
`<stem>.png` once a human has eyeballed it and confirmed it looks
right.

See [`../../scripts/capture-render-goldens.sh`](../../scripts/capture-render-goldens.sh)
for the local-capture path (produces the same renders the test runs
against, good for a quick "accept all" after an intentional change).

## When a golden-test fails

1. The rendered output for the failing fixture is written to
   `actual/<stem>.png` — compare it side-by-side with `<stem>.png`
   in an image diff tool.
2. If the change is intentional (new feature, updated font,
   refactored renderer), regenerate with the capture script above
   and promote the replacement.
3. If it's a regression, the `actual/` image is your reproduction —
   fix the code, rerun until green.

The test uses per-channel tolerance 8/255 and a 0.1% pixel-fraction
limit, generous enough for font hinting drift across Qt minor
versions but tight enough to flag a moved / rotated / missing brick.
