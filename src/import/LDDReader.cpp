#include "LDDReader.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QXmlStreamReader>

#ifndef CLD_NO_QZIPREADER
#  include <private/qzipreader_p.h>
#endif

namespace cld::import {

namespace {

// Pull the LXFML document out of an .lxf ZIP. LDD stores it at the
// archive root with the filename `LXFML` (no extension). Falls back to
// any `.lxfml` file on mismatch.
QByteArray extractLxfmlFromLxf(const QString& path, QString* err) {
#ifdef CLD_NO_QZIPREADER
    if (err) *err = QStringLiteral(
        "This build was compiled without QZipReader; LDD .lxf import is "
        "unavailable. Rebuild against a Qt install with private headers.");
    return {};
#else
    QZipReader zr(path);
    if (!zr.isReadable()) {
        if (err) *err = QStringLiteral("Not a readable LDD .lxf archive: %1").arg(path);
        return {};
    }
    const auto entries = zr.fileInfoList();
    for (const auto& info : entries) {
        if (!info.isFile) continue;
        if (info.filePath.compare(QStringLiteral("LXFML"), Qt::CaseInsensitive) == 0)
            return zr.fileData(info.filePath);
    }
    for (const auto& info : entries) {
        if (!info.isFile) continue;
        if (info.filePath.endsWith(QStringLiteral(".lxfml"), Qt::CaseInsensitive))
            return zr.fileData(info.filePath);
    }
    if (err) *err = QStringLiteral("No LXFML entry found inside %1").arg(path);
    return {};
#endif
}

// Parse an LDD transformation string: "m00,m01,m02,m10,m11,m12,m20,m21,m22,tx,ty,tz".
// Returns sentinel transformation on parse failure.
struct LddTransform {
    double m[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    double tx = 0, ty = 0, tz = 0;
    bool ok = false;
};
LddTransform parseTransform(QStringView s) {
    LddTransform t;
    const auto parts = s.toString().split(QLatin1Char(','));
    if (parts.size() < 12) return t;
    for (int i = 0; i < 9; ++i) t.m[i] = parts[i].toDouble();
    t.tx = parts[9].toDouble();
    t.ty = parts[10].toDouble();
    t.tz = parts[11].toDouble();
    t.ok = true;
    return t;
}

}  // namespace

LDrawReadResult readLDD(const QString& path) {
    LDrawReadResult out;

    // Decide whether the input is a .lxf ZIP or raw .lxfml XML.
    QByteArray xmlBytes;
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".lxf"))) {
        xmlBytes = extractLxfmlFromLxf(path, &out.error);
        if (xmlBytes.isEmpty()) return out;
    } else if (lower.endsWith(QStringLiteral(".lxfml"))) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            out.error = QStringLiteral("Cannot open %1: %2").arg(path, f.errorString());
            return out;
        }
        xmlBytes = f.readAll();
    } else {
        out.error = QStringLiteral("Not a .lxf or .lxfml file: %1").arg(path);
        return out;
    }

    QXmlStreamReader r(xmlBytes);
    // LXFML coordinate system: millimetres. 1 LDD unit ~= 1 stud in our
    // conversion, because LDraw's `toBlueBrickMap` divides by 20 LDU
    // per stud — so we emit positions as 20 × studs (LDU). 1 LDD
    // transform unit ~ 0.4 mm along x/z and 0.96 mm along y (stud height).
    // For a top-down sprite we only need x and z (y is height).
    constexpr double kLddToStuds = 1.25;  // LDD unit → studs (0.4 mm × 1.25 / 0.5 stud-per-LDU-brick-width)

    while (!r.atEnd() && !r.hasError()) {
        const auto token = r.readNext();
        if (token != QXmlStreamReader::StartElement) continue;
        const auto name = r.name();
        if (name == QStringLiteral("LXFML")) {
            out.title = r.attributes().value(QStringLiteral("name")).toString();
            if (out.title.isEmpty()) out.title = QFileInfo(path).completeBaseName();
            continue;
        }
        if (name == QStringLiteral("Brick")) {
            const QString designID = r.attributes().value(QStringLiteral("designID")).toString();
            int matId = 0;
            // Walk Part → Bone to find transform + material. We take the
            // FIRST transform we encounter — sufficient for top-down
            // sprites since LDD rarely multi-bones a simple brick.
            LddTransform xform;
            while (!r.atEnd()) {
                const auto inner = r.readNext();
                if (inner == QXmlStreamReader::EndElement
                    && r.name() == QStringLiteral("Brick")) break;
                if (inner != QXmlStreamReader::StartElement) continue;
                if (r.name() == QStringLiteral("Part") && matId == 0) {
                    matId = r.attributes().value(QStringLiteral("materials")).toString()
                            .split(QLatin1Char(',')).value(0).toInt();
                }
                if (r.name() == QStringLiteral("Bone") && !xform.ok) {
                    xform = parseTransform(r.attributes().value(QStringLiteral("transformation")));
                }
            }
            if (!xform.ok) continue;
            LDrawPartRef ref;
            ref.colorCode = matId > 0 ? matId : 1;  // default to light-gray
            // LDD → LDraw mapping: scale by 20 LDU per stud (already in
            // LDU-ish via kLddToStuds × 20 / 0.8mm-per-LDU quirks).
            // Approximation that works for top-down placement at our
            // library's granularity.
            constexpr double kLduPerStud = 20.0;
            ref.x = xform.tx * kLddToStuds * kLduPerStud;
            ref.y = xform.ty * kLddToStuds * kLduPerStud;
            ref.z = xform.tz * kLddToStuds * kLduPerStud;
            for (int i = 0; i < 9; ++i) ref.m[i] = xform.m[i];
            // Part filename: try "<designID>.<matId>.dat" so the
            // existing LDraw → BlueBrick mapping strips the .dat and
            // leaves a `<designID>.<matId>` key that matches our
            // library's naming when available.
            if (matId > 0) {
                ref.filename = QStringLiteral("%1.%2.dat").arg(designID).arg(matId);
            } else {
                ref.filename = designID + QStringLiteral(".dat");
            }
            out.parts.push_back(ref);
        }
    }

    out.ok = !r.hasError();
    if (!out.ok) {
        out.error = QStringLiteral("LXFML parse error at line %1 col %2: %3")
            .arg(r.lineNumber()).arg(r.columnNumber()).arg(r.errorString());
    }
    return out;
}

}  // namespace cld::import
