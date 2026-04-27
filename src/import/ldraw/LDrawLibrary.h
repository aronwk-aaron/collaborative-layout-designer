#pragma once

#include <QString>
#include <QStringList>

namespace cld::import {

// Locates and resolves files inside a user-pointed LDraw library.
//
// LDraw distributes parts as plain-text .dat files under a root that
// has this canonical layout:
//
//   <root>/
//     LDConfig.ldr         - colour palette (mandatory for our renderer)
//     parts/
//       <partid>.dat       - top-level parts (e.g. 3001.dat for a 2x4 brick)
//       s/<partid>.dat     - sub-parts (vendor-internal split files)
//     p/
//       <name>.dat         - primitives shared across many parts
//       8/<name>.dat       - low-resolution primitive variants
//       48/<name>.dat      - high-resolution primitive variants
//     models/              - example models, ignored here
//
// `resolve()` returns the absolute on-disk path for a given `.dat`
// reference following LDraw's stock search order (parts/, p/, p/48/,
// p/8/, parts/s/, root). When nothing matches, returns an empty string
// and the caller treats the reference as "unknown — skip".
//
// Studio (.io) uses an LDraw library too — typically the bundled one
// at <Studio install>/Studio2/ldraw, sometimes the user's standard
// install. Either path works as the `root` here; we don't care which
// vendor produced the files as long as the layout matches.
class LDrawLibrary {
public:
    LDrawLibrary() = default;
    explicit LDrawLibrary(QString root);

    // Replace the root. Empty string means "no library configured" and
    // every resolve() will fail.
    void setRoot(QString root);
    const QString& root() const { return root_; }

    // True when the root looks like a real LDraw install — has
    // LDConfig.ldr at top level AND a parts/ subdirectory. Good enough
    // for a sanity check before we accept user-pointed paths.
    bool looksValid() const;

    // Resolve a `.dat` reference (case-insensitive on the filename
    // component, since LDraw filenames mix cases on disk but author
    // tools normalise to upper or lower depending on era). Returns the
    // absolute path or an empty string when the reference cannot be
    // satisfied. Cheap to call repeatedly: the search order is fixed
    // and the result isn't cached here (the resolver layer caches).
    QString resolve(const QString& filename) const;

private:
    QString root_;
};

}  // namespace cld::import
