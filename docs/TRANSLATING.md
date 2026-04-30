# Translating Brick Layout Designer

## Languages we ship

Current target set (matches upstream BlueBrick's shipped set plus Japanese):

| Code | Language            | Status                                                    |
|------|---------------------|-----------------------------------------------------------|
| en   | English             | Base — built into `tr()`                                  |
| fr   | French              | ~75 strings machine-seeded — needs native-speaker review |
| de   | German              | ~75 strings machine-seeded — needs native-speaker review |
| es   | Spanish             | ~75 strings machine-seeded — needs native-speaker review |
| it   | Italian             | ~75 strings machine-seeded — needs native-speaker review |
| nl   | Dutch               | ~75 strings machine-seeded — needs native-speaker review |
| pt   | Portuguese          | ~75 strings machine-seeded — needs native-speaker review |
| zh   | Chinese (Simplified)| ~75 strings machine-seeded — needs native-speaker review |
| ja   | Japanese            | ~75 strings machine-seeded — needs native-speaker review |

The seeded translations cover menu titles, common dialog buttons, and
the most-visible UI labels (top-level menu items, panel names, Yes/No/
Cancel kinds of prompts). Error messages, tooltips, and formatted
multi-arg strings are still `type="unfinished"` pending proper review.
See [`scripts/apply-translations.py`](../scripts/apply-translations.py)
for the translation dictionary — edit that file and rerun the script
to extend coverage; the seeded strings are a starting point, not a
finished product.

Each language has a `translations/bld_<code>.ts` file checked in.
Running `lupdate` populates the file with every `tr()` call from the
source; translators fill in the `<translation>` bodies; CMake runs
`lrelease` at build time to produce `bld_<code>.qm` bundled next to the
binary.

## Workflow — running `lupdate`

From the repo root:

```sh
# Populate / refresh every language's .ts file from source
for lang in fr de es it nl pt zh ja; do
    lupdate -recursive src -ts translations/bld_${lang}.ts
done
```

`lupdate` is part of Qt's linguist tools (package `qt6-tools-dev-tools`
on Ubuntu / Debian, `qt@6` formula on macOS, or shipped with the Qt
installer on Windows).

`lupdate` **preserves** existing translations — rerunning it after new
`tr()` strings are added will add the new entries without disturbing
completed translations. Removed strings are marked `type="obsolete"`
instead of deleted, so translators can see them and decide to port or
drop.

## Workflow — bulk-applying translations via the script

`scripts/apply-translations.py` ships a `TRANS` dictionary keyed by
language, where each entry maps a `<source>` string (XML-escaped as
Qt writes it — `&amp;File` etc) to the target translation. Running:

```sh
python3 scripts/apply-translations.py
```

walks every `translations/bld_<lang>.ts`, substitutes the
`<translation>` body for any source appearing in `TRANS[<lang>]`, and
strips `type="unfinished"`. Missing sources stay unfinished so Qt
falls back to English at runtime.

Native speakers: the easiest way to contribute a pass is to grow the
relevant language block inside `TRANS` in that script and rerun it.

## Workflow — adding translations

Option A — **Qt Linguist (GUI):**

```sh
linguist translations/bld_fr.ts
```

Opens a three-pane editor: source string → translation → context.
Write the translation, mark as Done, save. Qt Linguist also flags
translations that include placeholders (`%1`, `%2`) that don't match
the source arity.

Option B — **plain text editor:**

Open the `.ts` file, find `<message>` entries with empty
`<translation type="unfinished"></translation>` bodies, fill in the
translated string, and remove the `type="unfinished"` attribute:

```xml
<message>
    <source>Open...</source>
    <translation>Ouvrir...</translation>
</message>
```

Save and commit.

## Workflow — building

CMake auto-detects the `.ts` files; a successful build produces
`.qm` files in the build directory (under `<build>/translations/`).
The `main.cpp` loader picks them up based on the user's language
preference (Preferences → General → Language).

When you ship a release, the package step copies `.qm` files from the
build directory to `<app>/translations/` so the installed binary
finds them at runtime.

## Translator guidelines

- **Preserve placeholders** (`%1`, `%2`, `&File`, `Ctrl+S`). Moving
  them is fine — translating them is a bug.
- **Keep mnemonic ampersands** (`&File`, `Sa&ve`). Qt turns these into
  Alt-key shortcuts; removing one breaks keyboard nav in that menu.
- **LEGO terms stay in English** where they're brand names — "brick",
  "stud", "BlueBrick", "LUG" — unless the native-language LEGO
  community has a widely-used equivalent.
- **Matched punctuation**: if the source ends with `:` or `...`,
  preserve it in the translation. Dialog labels and menu items are
  wired to those.
- **When in doubt, ask on the PR.** Context helps — BlueBrick
  Windows-only for 20 years means many existing community
  conventions. If there's an established vanilla-BlueBrick
  translation, we match.

## Adding a new language

1. Add the ISO 639-1 code to `BLD_LANGUAGES` in the root
   `CMakeLists.txt`.
2. Create `translations/bld_<code>.ts` as an empty TS (copy an
   existing stub, change the `language="..."` attribute).
3. Run `lupdate` to populate it (see above).
4. Add the language option to `PreferencesDialog`'s language combo.
5. Translate.
6. Open a PR.

## Rebuilding from scratch

If a `.ts` file gets corrupted or you want to nuke all translations
and start over:

```sh
# Clear one
echo '<?xml version="1.0" encoding="utf-8"?><!DOCTYPE TS>
<TS version="2.1" language="fr"><context><name>BrickLayoutDesigner</name></context></TS>' > translations/bld_fr.ts

# Repopulate
lupdate -recursive src -ts translations/bld_fr.ts
```
