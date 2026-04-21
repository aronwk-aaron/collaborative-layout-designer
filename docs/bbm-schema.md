# `.bbm` file format — reverse-engineered from BlueBrick 1.9.2.0

This document describes the vanilla BlueBrick `.bbm` save format as derived from the upstream C# source at [`Lswbanban/BlueBrick`](https://github.com/Lswbanban/BlueBrick) (commit checked into `.reference/BlueBrick/`). It is the canonical specification the fork must read and write byte-faithfully.

**Also in this directory:**

- [`bbm-schema.xsd`](bbm-schema.xsd) — formal XSD for the `.bbm` XML
  format. Documentation, not a strict validator (see caveats in the
  XSD header). Good for diff tools and IDE autocomplete.
- [`bbm-cld-schema.md`](bbm-cld-schema.md) — prose doc for the
  sibling `.bbm.cld` sidecar (fork-only metadata: modules, anchored
  labels, venues).
- [`bbm-cld-schema.json`](bbm-cld-schema.json) — JSON Schema (draft-07)
  for the same sidecar.
- [`set-schema.md`](set-schema.md) — the `.set.xml` format used by
  the BrickTracks / TrixBrix contrib libraries for pre-assembled
  multi-brick sets (what **Save Selection as Set** writes).

The schema isn't formally documented upstream — the C# `System.Xml.Serialization.XmlSerializer` output, driven by `Map.WriteXml` and each `Layer` subclass's `WriteXml`, **is** the format.

## Wire-level facts

Verified empirically against real `.bbm` files saved by BlueBrick 1.9.2.0
(`fixtures/bbm-corpus/tight-corner.bbm` and others in the private user corpus):

- **Transport**: `XmlSerializer.Serialize(StreamWriter, Map)` with `new StreamWriter(filename, false)` (no explicit encoding).
  - Encoding: UTF-8, **no BOM** (file starts with `3c 3f 78 6d 6c` / `<?xml`).
  - Indentation: **yes — 2 spaces**. Contrary to the default `XmlWriter` behavior, .NET Framework 4.8's `XmlSerializer.Serialize(TextWriter, ...)` internally uses `XmlTextWriter` with `Formatting = Formatting.Indented` and 2-space indents. Real `.bbm` files are pretty-printed.
  - Line endings: **CRLF** (saved on Windows; `file(1)` reports "with CRLF line terminators").
  - XML declaration: `<?xml version="1.0" encoding="utf-8"?>`.
  - No trailing newline — file ends with `</Map>` at column 0.
- **Root element**: **`<Map>` with NO namespace attributes**. The `XmlSerializer` does not emit `xmlns:xsi`/`xmlns:xsd` when the serialized type implements `IXmlSerializable` and writes its own body via `WriteXml`. Our writer must match (earlier drafts incorrectly emitted these).
- **Empty elements**: written as `<Tag />` (space before `/>`). Qt's `QXmlStreamWriter` defaults to `<Tag/>` — byte-exact CI will need a post-processor or a patched writer to match.
- **Line endings inside text content**: upstream's `Comment` read path replaces `\n` with `Environment.NewLine`; write path inverts it. So multi-line text contains platform newlines as captured.

Open items for byte-exact round-trip CI (planned Phase 1.5+):
- CRLF output from `QXmlStreamWriter` (it emits LF; need to post-process or wrap the device).
- Empty-element spacing `<Tag />` vs `<Tag/>`.
- Attribute order within elements (vanilla emits `type`, `id` in that order on `<Layer>`; Qt preserves the order of `writeAttribute` calls — we control this).

## Schema (version 9 — CURRENT_DATA_VERSION in Map.cs)

Pseudo-XML with element order strictly preserved:

```xml
<Map xmlns:xsi="..." xmlns:xsd="...">
  <Version>9</Version>
  <nbItems>N</nbItems>                      <!-- v3+; sum of NbItems across all layers -->
  <BackgroundColor>
    <IsKnownColor>true|false</IsKnownColor>
    <Name>colorname_or_argb_hex</Name>       <!-- see Color encoding below -->
  </BackgroundColor>
  <Author>string</Author>
  <LUG>string</LUG>
  <Event>string</Event>
  <Date>
    <Day>int</Day><Month>int</Month><Year>int</Year>
  </Date>
  <Comment>string</Comment>                  <!-- v1+ -->
  <ExportInfo>                               <!-- v5+ -->
    <ExportPath>relative-path-or-empty</ExportPath>
    <ExportFileType>int</ExportFileType>
    <ExportArea>                             <!-- RectangleF -->
      <X>float</X><Y>float</Y><Width>float</Width><Height>float</Height>
    </ExportArea>
    <ExportScale>double (invariant)</ExportScale>
    <!-- v8+: -->
    <ExportWatermark>bool</ExportWatermark>
    <!-- NB: v8 only had a now-dead mExportBrickHull bool here; v9 dropped it on write -->
    <ExportElectricCircuit>bool</ExportElectricCircuit>
    <ExportConnectionPoints>bool</ExportConnectionPoints>
  </ExportInfo>
  <SelectedLayerIndex>int</SelectedLayerIndex>
  <Layers>
    <Layer type="grid|brick|text|area|ruler" id="GUID">...</Layer>
    ...
  </Layers>
</Map>
```

## Primitive encoding

All helpers live in `BlueBrick.MapData.XmlReadWrite` upstream. Matching invariants for our canonical writer:

| Type | Format |
|---|---|
| `float` / `double` | `InvariantCulture`; default `ToString()` (≈ `"G7"` for float, `"G15"` for double). A few sites use `"R"` — check per call site. |
| `int` | `InvariantCulture`; default `ToString()`. |
| `bool` | `flag.ToString().ToLower()` → `"true"` / `"false"`. |
| `Color` | Two-element: `IsKnownColor` (bool) + `Name` (string). If known, `Name` is the color name (e.g. `"White"`); otherwise the signed 32-bit ARGB as hex. |
| `Font` | `FontFamily` (string) + `Size` (float, invariant) + `Style` (flags enum name, e.g. `"Regular"` or `"Bold, Italic"`; only written for v6+). |
| `Point` | `X` (int) + `Y` (int). |
| `PointF` | `X` (float, invariant) + `Y` (float, invariant). A variant `writePointFLowerCase` emits `x`/`y`. |
| `RectangleF` | `X` + `Y` + `Width` + `Height` (all float invariant). |
| `UniqueId` | As string. `GUID` on layers / items; also serialized in content for links. |

## Layer common (`Layer.cs` — `writeHeaderAndCommonProperties`)

```xml
<Layer type="grid|brick|text|area|ruler" id="GUID">
  <Name>string</Name>
  <Visible>bool</Visible>
  <Transparency>int</Transparency>           <!-- v5+; percent -->
  <HullProperties isVisible="bool">          <!-- v9+ -->
    <hullColor>
      <IsKnownColor>...</IsKnownColor>
      <Name>...</Name>
    </hullColor>
    <hullThickness>int</hullThickness>
  </HullProperties>
  <!-- layer-type-specific content follows -->
  <Groups>
    <Group id="GUID">...</Group>
    ...
  </Groups>
</Layer>
```

## LayerGrid (`LayerGrid.cs`)

```xml
<GridColor>...</GridColor>
<GridThickness>float</GridThickness>
<SubGridColor>...</SubGridColor>
<SubGridThickness>float</SubGridThickness>
<GridSizeInStud>int</GridSizeInStud>
<SubDivisionNumber>int</SubDivisionNumber>      <!-- clamped min 2 -->
<DisplayGrid>bool</DisplayGrid>
<DisplaySubGrid>bool</DisplaySubGrid>
<DisplayCellIndex>bool</DisplayCellIndex>
<CellIndexFont>...</CellIndexFont>
<CellIndexColor>...</CellIndexColor>
<CellIndexColumnType>int</CellIndexColumnType>  <!-- 0=LETTERS, 1=NUMBERS -->
<CellIndexRowType>int</CellIndexRowType>
<CellIndexCorner>...</CellIndexCorner>           <!-- Point (int/int) -->
```

`LayerGrid` has no items and no groups.

## Layers still to document

To be filled in as we port: `LayerBrick` + `LayerBrick.Brick`, `LayerText` + `LayerTextCell`, `LayerArea`, `LayerRuler` + `LayerRulerItem`, plus `Group`, `ConnectionPoint`, `FreeConnectionSet`, `RulerAttachementSet`.

## Forward compatibility strategy

Our fork writes `<Map>` at `Version=9` matching vanilla. Fork-only metadata (anchored text labels, cross-layer modules, event venues) is stored **outside** the `.bbm` in a sidecar `.bbm.cld` file whose format we control. This keeps the `.bbm` opening cleanly in vanilla BlueBrick 1.9.2.
