#include "StudioReader.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

// Qt's QZipReader is in the private-header area, but it's a stable
// internal API Qt ships everywhere we care about. The CMake for this
// target locates the private include dir and adds it; when the
// headers aren't available (stripped Qt install) we compile with
// BLD_NO_QZIPREADER and emit a runtime error instead of build-breaking.
#ifndef BLD_NO_QZIPREADER
#  include <private/qzipreader_p.h>
#endif

namespace bld::import {

namespace {

// Case-insensitive search for a candidate "model.ldr" entry anywhere
// in the archive. Studio usually uses `model.ldr` at the root, but
// older / variant exports might nest it.
QByteArray readModelLdr(QZipReader& zr) {
    const auto entries = zr.fileInfoList();
    // Prefer root-level model.ldr (case-insensitive match).
    for (const auto& info : entries) {
        if (!info.isFile) continue;
        if (info.filePath.compare(QStringLiteral("model.ldr"),
                                  Qt::CaseInsensitive) == 0) {
            return zr.fileData(info.filePath);
        }
    }
    // Fallback: any .ldr anywhere. Studio sometimes wraps inside
    // subfolders; take the first match.
    for (const auto& info : entries) {
        if (!info.isFile) continue;
        if (info.filePath.endsWith(QStringLiteral(".ldr"), Qt::CaseInsensitive)) {
            return zr.fileData(info.filePath);
        }
    }
    return {};
}

}  // namespace

LDrawReadResult readStudioIo(const QString& path) {
    LDrawReadResult out;
#ifdef BLD_NO_QZIPREADER
    out.error = QStringLiteral(
        "This build was compiled without QZipReader; Studio .io import is "
        "unavailable. Rebuild against a Qt install that includes the private "
        "headers package.");
    return out;
#else
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        out.error = QStringLiteral("File not found: %1").arg(path);
        return out;
    }
    QZipReader zr(path);
    if (!zr.isReadable()) {
        out.error = QStringLiteral(
            "Not a readable Studio archive (expected a ZIP containing model.ldr): %1")
            .arg(path);
        return out;
    }
    const QByteArray ldr = readModelLdr(zr);
    if (ldr.isEmpty()) {
        out.error = QStringLiteral(
            "Studio archive has no model.ldr entry: %1").arg(path);
        return out;
    }
    // Dump the extracted LDR to a temp path and feed it through the
    // existing readLDraw() so we share every line-handler and title
    // extraction. The file is tiny relative to disk; the extra write
    // is cheap.
    QString tmpPath = QDir::tempPath() + QStringLiteral("/bld-studio-extract-")
                    + fi.completeBaseName()
                    + QStringLiteral(".ldr");
    {
        QFile tmp(tmpPath);
        if (!tmp.open(QIODevice::WriteOnly)) {
            out.error = QStringLiteral("Cannot write temp extract: %1").arg(tmp.errorString());
            return out;
        }
        tmp.write(ldr);
    }
    LDrawReadResult nested = readLDraw(tmpPath);
    QFile::remove(tmpPath);
    return nested;
#endif  // BLD_NO_QZIPREADER
}

}  // namespace bld::import
