# `.bbm` file format — reverse-engineered from BlueBrick 1.9.2.0

This document describes the vanilla BlueBrick `.bbm` save format as derived from the upstream C# source at [`Lswbanban/BlueBrick`](https://github.com/Lswbanban/BlueBrick) (commit checked into `.reference/BlueBrick/`). It is the canonical specification the fork must read and write byte-faithfully.

The schema isn't formally documented upstream — the C# `System.Xml.Serialization.XmlSerializer` output, driven by `Map.WriteXml` and each `Layer` subclass's `WriteXml`, **is** the format.

## Wire-level facts

- **Transport**: `XmlSerializer.Serialize(StreamWriter, Map)` with `new StreamWriter(filename, false)` (no explicit encoding).
  - Encoding: UTF-8. Presence/absence of BOM must be verified against captured goldens (`StreamWriter` on .NET Framework 4.8 defaults differ from Core).
  - Indentation: **none** (`XmlSerializer` uses default `XmlWriter` settings — `Indent = false`, all on one line).
  - XML declaration: `<?xml version="1.0" encoding="utf-8"?>`.
- **Root element**: `<Map>` with `xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"` and `xmlns:xsd="http://www.w3.org/2001/XMLSchema"` attributes (default `XmlSerializer` namespaces).
- **Line endings inside text content**: `\n` is replaced with `Environment.NewLine` on read and inverted on write — so multi-line `Comment` text contains platform newlines as captured.

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
