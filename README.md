# Collaborative Layout Designer

A cross-platform maintainer fork of [BlueBrick](https://github.com/Lswbanban/BlueBrick) — the LEGO train layout design tool — rewritten in C++20 / Qt 6 to run natively on Windows, Linux, and macOS (Intel + Apple Silicon).

## Goals

- **Native on every desktop**: Windows x64, Linux x64, macOS universal (x86_64 + arm64). No .NET / Mono dependency.
- **Save-file compatible**: byte-faithful round-trip of vanilla BlueBrick `.bbm` files so projects move freely between the two tools.
- **Library compatible**: uses the existing [BlueBrickParts](https://github.com/Lswbanban/BlueBrickParts) library (bundled as a git submodule).
- **Built for train-club collaboration** with two new capabilities:
  - **Cross-layer modules** — bundle items spanning multiple layers into a named module; move or rotate the whole module as a single unit; save and re-import modules as standalone `.bbm` files.
  - **Anchored text labels** — labels that stick to bricks, groups, and modules when they move or rotate, instead of drifting in world coordinates.

Forward-compatibility with vanilla BlueBrick 1.9.2 is maintained via a sidecar `.bbm.cld` file for fork-only metadata.

## Status

Pre-alpha. Phase 0 bootstrap in progress.

## Building

Requires CMake 3.25+, a C++20 compiler, and Qt 6.7+.

```sh
git clone --recurse-submodules <this-repo>
cd collaborative-layout-designer
cmake -S . -B build -G Ninja
cmake --build build
```

## License

GPL-3.0 — see [LICENSE](LICENSE). This project is a derivative work of BlueBrick (GPL-3.0, © Alban Nanty and contributors).
