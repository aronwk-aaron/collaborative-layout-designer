#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <optional>

namespace cld::parts {

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

struct PartMetadata {
    QString  partNumber;
    QString  colorCode;
    PartKind kind = PartKind::Leaf;
    QString  xmlFilePath;
    QString  gifFilePath;   // may be empty if the image is missing
    QString  author;
    QString  sortingKey;
    QList<PartDescription> descriptions;
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

    void clear();

private:
    QStringList searchPaths_;
    QHash<QString, PartMetadata> index_;
    QHash<QString, QPixmap>      pixmapCache_;
};

}
