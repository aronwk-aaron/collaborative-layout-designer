#include "LDrawLibrary.h"

#include <QDir>
#include <QFileInfo>

namespace cld::import {

namespace {

// Returns absoluteFilePath for the first existing case-insensitive
// match of `name` inside `dir`. Linux is case-sensitive while LDraw
// filenames in the wild can be lower-, mixed-, or upper-case; falling
// back to a directory listing handles the case-mismatch path. Cheap
// fast path first: try the exact name verbatim.
QString findCaseInsensitive(const QDir& dir, const QString& name) {
    if (!dir.exists()) return {};
    {
        const QString fast = dir.absoluteFilePath(name);
        if (QFileInfo::exists(fast)) return fast;
    }
    // Fallback: scan the directory once, compare lowered.
    const QString needle = name.toLower();
    const QFileInfoList entries = dir.entryInfoList(QDir::Files);
    for (const QFileInfo& fi : entries) {
        if (fi.fileName().toLower() == needle) return fi.absoluteFilePath();
    }
    return {};
}

}  // namespace

LDrawLibrary::LDrawLibrary(QString root) : root_(std::move(root)) {}

void LDrawLibrary::setRoot(QString root) { root_ = std::move(root); }

bool LDrawLibrary::looksValid() const {
    if (root_.isEmpty()) return false;
    QDir d(root_);
    if (!d.exists()) return false;
    // LDConfig.ldr is the colour palette and is present in every
    // distribution we care about. Without it our colour resolution
    // would just hard-code the bundled palette table — pointing at a
    // root without it suggests the user picked the wrong directory.
    if (!QFileInfo::exists(d.absoluteFilePath(QStringLiteral("LDConfig.ldr")))) return false;
    if (!QFileInfo(d.absoluteFilePath(QStringLiteral("parts"))).isDir()) return false;
    return true;
}

QString LDrawLibrary::resolve(const QString& filename) const {
    if (root_.isEmpty() || filename.isEmpty()) return {};

    // Normalise the reference: LDraw `.dat` lines often write paths
    // with backslashes (Windows-era convention; many parts are still
    // shipped that way). Convert to forward slashes so we can split
    // out an optional subdirectory hint like "s/3001s01.dat" or
    // "48/box5.dat".
    QString rel = filename;
    rel.replace(QChar('\\'), QChar('/'));

    QDir rootDir(root_);

    // 1) If the filename already specifies a subdir prefix, resolve
    //    that exact relative path against root, parts/, and p/. e.g.
    //    "s/3001s01.dat" against parts/s/3001s01.dat.
    if (rel.contains(QChar('/'))) {
        const QStringList searches = {
            rootDir.absoluteFilePath(QStringLiteral("parts/") + rel),
            rootDir.absoluteFilePath(QStringLiteral("p/") + rel),
            rootDir.absoluteFilePath(rel),
        };
        for (const QString& p : searches) {
            if (QFileInfo::exists(p)) return p;
        }
        // Fall through to bare-name lookup using the trailing component.
        rel = rel.section(QChar('/'), -1);
    }

    // 2) Bare filename: walk LDraw's stock search order. parts/ first
    //    so a top-level part (e.g. "3001.dat") wins over a like-named
    //    primitive in p/. p/48/ and p/8/ are resolution variants we
    //    treat at the same priority as p/ — author tools have always
    //    been free to reference either depending on intended fidelity.
    const QStringList dirs = {
        QStringLiteral("parts"),
        QStringLiteral("p"),
        QStringLiteral("p/48"),
        QStringLiteral("p/8"),
        QStringLiteral("parts/s"),
        QStringLiteral(""),  // root itself, last
    };
    for (const QString& sub : dirs) {
        const QDir d(sub.isEmpty() ? root_ : rootDir.absoluteFilePath(sub));
        const QString hit = findCaseInsensitive(d, rel);
        if (!hit.isEmpty()) return hit;
    }
    return {};
}

}  // namespace cld::import
