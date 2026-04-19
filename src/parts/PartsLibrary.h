#pragma once

#include <QPixmap>
#include <QString>
#include <QStringList>

#include <optional>
#include <unordered_map>

namespace cld::parts {

struct PartMetadata {
    QString partNumber;
    int     colorCode = 0;
    QString author;
    QString description;
    // Phase 1: connection points, snap margins, bounding, LDraw mapping
};

class PartsLibrary {
public:
    void addSearchPath(const QString& path);
    const QStringList& searchPaths() const { return searchPaths_; }

    // Phase 1: scan paths, build index, expose loaders.
    int scan();
    int partCount() const { return static_cast<int>(index_.size()); }

    std::optional<PartMetadata> metadata(const QString& key) const;
    QPixmap pixmap(const QString& key);

private:
    QStringList searchPaths_;
    std::unordered_map<std::string, PartMetadata> index_;
};

}
