#pragma once

#include <QHash>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QStringList>

#include <optional>

namespace cld::parts {

// Connection types are STRING ids in BlueBrick's part XMLs — e.g.
// "rail", "road", "coaster", "monorail", plus a growing long tail of
// domain-specific types (4DBrix, TrixBrix, etc.). Only same-type points
// may connect. An empty string means "no type" / never connects.
//
// Connection points are stored in each part's LOCAL coord system (in studs,
// with (0,0) at the part's centre). angleDegrees points "outward" — two
// connected points should face 180° apart.
struct PartConnectionPoint {
    QString type;
    QPointF position;
    double  angleDegrees = 0.0;
    // Electric-plug index: 0 or 1 marks one of the two rails on a 9V
    // track piece, -1 (the default) means this connection carries no
    // electrical signal (e.g. plain rails, roads, monorails). When two
    // connected bricks both have electricPlug != -1 on their joined
    // connections, they form an edge in the electric-circuit graph.
    int     electricPlug = -1;
};

// One entry in the parts library. Matches the BlueBrick file-naming convention:
// each part is a `<PartNumber>.<ColorCode>.xml` paired with a matching `.gif`.
// Leaf parts live under <part>; grouped composite parts under <group>. Both
// share the same filename convention — we capture the kind here.
enum class PartKind {
    Leaf,
    Group,
};

struct PartDescription {
    QString language;  // e.g. "en", "fr"
    QString text;
};

// A single child of a set (<group>). BlueBrick's SubPart carries a
// subpart key (matches another part's library key), a local position in
// studs (offset from the set's reference origin), and an orientation
// in degrees.
struct PartSubPart {
    QString subKey;           // e.g. "TS_TRACK18S.8"
    QPointF position;         // studs, in set-local coords
    double  angleDegrees = 0.0;
};

struct PartMetadata {
    QString  partNumber;
    QString  colorCode;
    PartKind kind = PartKind::Leaf;
    QString  xmlFilePath;
    QString  gifFilePath;   // may be empty if the image is missing
    QString  author;
    QString  sortingKey;
    QList<PartDescription>     descriptions;
    QList<PartConnectionPoint> connections;  // from <ConnexionList> in part XML
    // Populated only when kind == Group — the parts that make up the
    // set (from <SubPartList> in the XML).
    QList<PartSubPart>         subparts;
};

class PartsLibrary {
public:
    void addSearchPath(const QString& path);
    const QStringList& searchPaths() const { return searchPaths_; }

    // Walk every search path recursively, indexing every `<PartNumber>.<Color>.xml`
    // pair found. Returns the number of parts indexed. Safe to call repeatedly —
    // subsequent calls append new parts without clearing the index.
    int scan();

    int partCount() const { return static_cast<int>(index_.size()); }

    // Lookup key is "<PartNumber>.<ColorCode>" (e.g. "3001.1" or "TS_TRACK18S.8").
    std::optional<PartMetadata> metadata(const QString& key) const;
    QStringList keys() const;

    // Lazy-load a decoded QPixmap for a part. Returns a null QPixmap if the
    // part isn't indexed or its .gif is missing.
    QPixmap pixmap(const QString& key);

    // Convex hull of the part's opaque pixels, in part-local STUD coords
    // (origin at the pixmap centre, x right, y down). Cached per-key.
    // Empty QPolygonF if the part has no pixmap or no opaque pixels —
    // callers fall back to the brick's displayArea rect in that case.
    //
    // Used for selection-shape hit testing and the "Display Hulls"
    // render toggle so irregular parts (curves, switches) outline
    // their real silhouette rather than a loose bounding rect.
    QPolygonF hullPolygonStuds(const QString& key);

    void clear();

private:
    QStringList searchPaths_;
    QHash<QString, PartMetadata> index_;
    QHash<QString, QPixmap>      pixmapCache_;
    QHash<QString, QPolygonF>    hullCache_;
};

}
