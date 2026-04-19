#include "PartsLibrary.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace cld::parts {

namespace {

// Split a filename like "TS_TRACK18S.8.xml" into ("TS_TRACK18S", "8").
// Returns std::nullopt if the basename doesn't have the expected two dots.
std::optional<std::pair<QString, QString>> splitPartKey(const QString& baseName) {
    // Strategy: find the last '.' (separates color code from extension is already
    // stripped) — the name we get is already "PartNumber.ColorCode". Split on the
    // *last* '.' to handle part numbers that themselves contain dots (rare but
    // possible in 4DBrix naming).
    const int dot = baseName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == baseName.size() - 1) return std::nullopt;
    return std::make_pair(baseName.left(dot), baseName.mid(dot + 1));
}

void readDescriptions(QXmlStreamReader& r, QList<PartDescription>& out) {
    while (r.readNextStartElement()) {
        PartDescription d;
        d.language = r.name().toString();
        d.text = r.readElementText().trimmed();
        if (!d.language.isEmpty()) out.push_back(std::move(d));
    }
}

bool parsePartXml(const QString& xmlPath, PartMetadata& out) {
    QFile f(xmlPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QXmlStreamReader r(&f);
    while (r.readNextStartElement()) {
        const auto root = r.name();
        if (root != QStringLiteral("part") && root != QStringLiteral("group")) {
            r.skipCurrentElement();
            continue;
        }
        out.kind = (root == QStringLiteral("group")) ? PartKind::Group : PartKind::Leaf;
        while (r.readNextStartElement()) {
            const auto n = r.name();
            if      (n == QStringLiteral("Author"))      out.author = r.readElementText().trimmed();
            else if (n == QStringLiteral("SortingKey")) out.sortingKey = r.readElementText().trimmed();
            else if (n == QStringLiteral("Description")) readDescriptions(r, out.descriptions);
            else r.skipCurrentElement();
        }
        return !r.hasError();
    }
    return false;
}

}

void PartsLibrary::addSearchPath(const QString& path) {
    if (!searchPaths_.contains(path)) searchPaths_.push_back(path);
}

int PartsLibrary::scan() {
    int added = 0;
    for (const QString& root : std::as_const(searchPaths_)) {
        QDirIterator it(root, { QStringLiteral("*.xml") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString xmlPath = it.next();
            QFileInfo info(xmlPath);

            // Strip ".xml". Then also strip ".set" if present (composite/group parts
            // use the ".set.xml" convention in BlueBrickParts). Remaining string is
            // "<PartNumber>.<ColorCode>".
            QString stem = info.completeBaseName();  // filename without ".xml"
            if (stem.endsWith(QStringLiteral(".set"), Qt::CaseInsensitive)) {
                stem.chop(4);
            }

            const auto split = splitPartKey(stem);
            if (!split) continue;

            PartMetadata meta;
            meta.partNumber = split->first;
            meta.colorCode  = split->second;
            meta.xmlFilePath = xmlPath;

            // Sibling GIF (same basename).
            QString gifCandidate = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".gif");
            if (QFile::exists(gifCandidate)) meta.gifFilePath = gifCandidate;

            if (!parsePartXml(xmlPath, meta)) continue;

            const QString key = QStringLiteral("%1.%2").arg(meta.partNumber, meta.colorCode);
            if (!index_.contains(key)) {
                index_.insert(key, meta);
                ++added;
            }
        }
    }
    return added;
}

std::optional<PartMetadata> PartsLibrary::metadata(const QString& key) const {
    auto it = index_.constFind(key);
    if (it == index_.constEnd()) return std::nullopt;
    return it.value();
}

QStringList PartsLibrary::keys() const {
    return index_.keys();
}

QPixmap PartsLibrary::pixmap(const QString& key) {
    auto cached = pixmapCache_.constFind(key);
    if (cached != pixmapCache_.constEnd()) return cached.value();
    auto meta = metadata(key);
    if (!meta || meta->gifFilePath.isEmpty()) return {};
    QPixmap pm;
    pm.load(meta->gifFilePath);
    pixmapCache_.insert(key, pm);
    return pm;
}

void PartsLibrary::clear() {
    index_.clear();
    pixmapCache_.clear();
}

}
