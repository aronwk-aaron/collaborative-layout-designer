# `.set.xml` — BlueBrick-compatible set file format

A *set* is a reusable bundle of bricks, positions, and rotations
shipped as a drop-in for a BlueBrick parts library. When placed on
the map, BlueBrick (and this fork) expands the set into its
constituent bricks at their set-relative positions.

Sets are the schema the BrickTracks / TrixBrix contrib libraries use
for pre-assembled track packages (R104 railyards, compact switch
combos, crossover sets). The vanilla BlueBrick 1.9.2 parts under
`parts/BrickTracks/*.set.xml` and `parts/TrixBrix/*.set.xml` are the
reference files this schema was derived from.

## Why document this

Vanilla BlueBrick 1.9.2 ships parts and sets as a mixed pool — both
use `<part>` or `<group>` as their XML root. If you're authoring a
new library, you need the set schema to get the root element right.
The fork's **Save Selection as Set** feature writes this format, and
the set-expansion path (`MapView` → `addPartAtScenePos` set branch)
reads it.

## Filename convention

- Extension: `.set.xml`
- Stem: any safe filename; BlueBrick uses the human-readable
  description ("SET R104 RY4 LEFT.set.xml"), often under a
  manufacturer-named folder (`parts/BrickTracks/`).
- The stem (without `.set.xml`) also becomes the `part key` used as
  `<SubPart id="...">` when another set references this one.

## Schema

```xml
<?xml version="1.0" encoding="utf-8"?>
<group>
  <Author>Dr. Matthias Runte, www.mattzobricks.com</Author>    <!-- optional -->
  <Description>
    <en>Friendly name shown in the Parts panel</en>            <!-- 1 per language; 'en' strongly recommended -->
    <de>Freundlicher Name</de>                                 <!-- other languages optional -->
  </Description>
  <SortingKey>E54</SortingKey>                                 <!-- optional; sorts alphabetically in the Parts panel -->
  <ImageURL>https://shop.example.com/this-set</ImageURL>       <!-- optional; shown as a tooltip in vanilla -->
  <CanUngroup>true</CanUngroup>                                <!-- required; true lets the user flatten the set -->
  <SubPartList>
    <SubPart id="PART KEY AS SEEN IN THE LIBRARY">
      <position>
        <x>0</x>                                               <!-- studs, set-local; hull-bbox CENTRE of this subpart -->
        <y>0</y>                                               <!-- studs, set-local; hull-bbox CENTRE of this subpart -->
      </position>
      <angle>-45</angle>                                       <!-- degrees; CCW rotation of this subpart -->
    </SubPart>
    <!-- more SubPart entries follow -->
  </SubPartList>
  <GroupConnectionPreferenceList>                              <!-- optional; controls which subpart-connection the -->
    <nextIndex from="0">3</nextIndex>                          <!-- cursor-drag latches to when the set is being placed -->
    <nextIndex from="3">10</nextIndex>                         <!-- and the user hits Tab to cycle. 'from' + destination -->
    <nextIndex from="10">0</nextIndex>                         <!-- index both count across the set's flattened free- -->
  </GroupConnectionPreferenceList>                             <!-- connection list. Vanilla uses this; we don't yet. -->
</group>
```

## Position semantics — THE gotcha

`<position>` is the **rotated hull-bbox centre** of the subpart,
*not* the image pixmap centre. For parts with an asymmetric hull
(curves, switches, anything with a `<hull>` polygon in its part XML)
at a non-axis-aligned rotation, those two centres differ by a
couple of studs.

Concretely: let `M` be the transform `Rotate(angle)` followed by
`Translate(x, y)`. When the set is expanded, BlueBrick places each
subpart such that its **`Center`** property (= the centre of the
rotated hull's axis-aligned bbox) lands at
`(setDropPosition.x + x, setDropPosition.y + y)`.

The subpart's *image* centre (used for pixmap rotation) lands at
`Center + mOffsetFromOriginalImage`, where `mOffset` is computed per
orientation from the part's hull polygon vs image bbox — see
`LayerBrickBrick.cs::updateImage` upstream or
`parts::PartsLibrary::hullBboxOffsetStuds` in this fork.

If you author a set with the wrong convention (e.g. storing image
centres instead of hull centres), the subparts load at the right
positions but the connection dots between them will miss by that
`mOffset` amount — visually "close but not touching" at any joint
involving an asymmetric hull. This was the root cause of the
"tracks think they're connected but aren't lined up" bug fixed in
v0.1.2.

**This fork's `Save Selection as Set` feature writes hull-centres
automatically** by back-computing `Center = displayArea.center() -
mOffset` for each selected brick. Don't worry about this when
authoring sets via the UI — only relevant if you're hand-editing XML
or building sets in code.

## Angle normalization

Angles are CCW degrees. BlueBrick's own editor accumulates rotation
state, so set XMLs routinely contain angles well outside `[-180,
180]` — e.g. `697.5` (which is `-22.5° mod 360°` after three CCW
quarter-turns past a 180° flip). Readers should normalize modulo 360
and then fold into `(-180, 180]` before applying; writers may emit
the raw value or the normalized one interchangeably. Vanilla keeps
the raw value; we emit the raw value on Save Selection as Set so
the XML round-trips byte-for-byte through repeated rotate operations
in vanilla.

## References

- BlueBrick source:
  - `MapData/BrickLibrary.cs::readSubPartListTag` — the parser.
  - `MapData/BrickLibrary.cs:1000` — `mLocalTransformInStud =
    Rotate(angle) * Translate(pos)`.
  - `MapData/Layer.cs:704` — `worldTransform = localTransform *
    parentTransform`, translation becomes the brick's `Center`.
  - `MapData/LayerBrickBrick.cs::updateConnectionPosition` — adds
    `mOffsetFromOriginalImage` to `Center` before computing
    connection world positions.
- This fork:
  - [`src/saveload/SetIO.h`](../src/saveload/SetIO.h) — writer schema.
  - [`src/parts/PartsLibrary.cpp::readSubPartList`](../src/parts/PartsLibrary.cpp) — reader.
  - [`src/parts/PartsLibrary.cpp::hullBboxOffsetStuds`](../src/parts/PartsLibrary.cpp) — the mOffset port.
  - [`src/ui/MainWindow.cpp::onSaveSelectionAsSet`](../src/ui/MainWindow.cpp) — UI-driven export flow.
  - [`tests/saveload/SetIOTest.cpp`](../tests/saveload/SetIOTest.cpp) — round-trip + precision tests.
