#include "PartsLibrary.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace cld::parts {

namespace {

// Split a filename stem like "TS_TRACK18S.8" into ("TS_TRACK18S", "8"),
// or "table96x190" into ("table96x190", ""). Trailing dot is rejected.
std::pair<QString, QString> splitPartKey(const QString& baseName) {
    const int dot = baseName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == baseName.size() - 1) {
        return { baseName, QString() };
    }
    return { baseName.left(dot), baseName.mid(dot + 1) };
}

void readDescriptions(QXmlStreamReader& r, QList<PartDescription>& out) {
    while (r.readNextStartElement()) {
        PartDescription d;
        d.language = r.name().toString();
        d.text = r.readElementText().trimmed();
        if (!d.language.isEmpty()) out.push_back(std::move(d));
    }
}

QPointF readPositionBlock(QXmlStreamReader& r) {
    QPointF p;
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("x")) p.setX(r.readElementText().toDouble());
        else if (n == QStringLiteral("y")) p.setY(r.readElementText().toDouble());
        else r.skipCurrentElement();
    }
    return p;
}

void readConnexionList(QXmlStreamReader& r, QList<PartConnectionPoint>& out) {
    while (r.readNextStartElement()) {
        if (r.name() != QStringLiteral("connexion")) { r.skipCurrentElement(); continue; }
        PartConnectionPoint c;
        while (r.readNextStartElement()) {
            const auto n = r.name();
            if      (n == QStringLiteral("type"))     c.type = r.readElementText().trimmed();
            else if (n == QStringLiteral("position")) c.position = readPositionBlock(r);
            else if (n == QStringLiteral("angle"))    c.angleDegrees = r.readElementText().toDouble();
            else r.skipCurrentElement();
        }
        out.push_back(std::move(c));
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
            else if (n == QStringLiteral("ConnexionList")) readConnexionList(r, out.connections);
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

            const auto [partNum, colorCode] = splitPartKey(stem);
            if (partNum.isEmpty()) continue;

            PartMetadata meta;
            meta.partNumber = partNum;
            meta.colorCode  = colorCode;
            meta.xmlFilePath = xmlPath;

            // Sibling GIF (same basename).
            QString gifCandidate = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".gif");
            if (QFile::exists(gifCandidate)) meta.gifFilePath = gifCandidate;

            if (!parsePartXml(xmlPath, meta)) continue;

            // Library keys are the full stem (case-folded) so lookup matches
            // both "TABLE96X190" and "3811.1" naturally — the stored key
            // always includes the color suffix when the filename has one.
            const QString key = colorCode.isEmpty()
                ? partNum.toLower()
                : QStringLiteral("%1.%2").arg(partNum, colorCode).toLower();
            if (!index_.contains(key)) {
                index_.insert(key, meta);
                ++added;
            }
        }
    }
    return added;
}

std::optional<PartMetadata> PartsLibrary::metadata(const QString& key) const {
    auto it = index_.constFind(key.toLower());
    if (it == index_.constEnd()) return std::nullopt;
    return it.value();
}

QStringList PartsLibrary::keys() const {
    return index_.keys();
}

QPixmap PartsLibrary::pixmap(const QString& key) {
    const QString lk = key.toLower();
    auto cached = pixmapCache_.constFind(lk);
    if (cached != pixmapCache_.constEnd()) return cached.value();
    auto meta = metadata(lk);
    if (!meta || meta->gifFilePath.isEmpty()) return {};
    QPixmap pm;
    pm.load(meta->gifFilePath);
    pixmapCache_.insert(lk, pm);
    return pm;
}

void PartsLibrary::clear() {
    index_.clear();
    pixmapCache_.clear();
}

}
