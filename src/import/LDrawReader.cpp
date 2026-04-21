#include "LDrawReader.h"

#include "../core/Brick.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QUuid>
#include <QtMath>

#include <cmath>

namespace cld::import {

namespace {

constexpr double kLduPerStud = 20.0;

// Extract rotation around Y axis (up) from the 3x3 matrix. For flat layouts
// this is the dominant rotation; tilted parts will approximate here.
// Matrix layout:   [ m0 m1 m2 ]
//                  [ m3 m4 m5 ]
//                  [ m6 m7 m8 ]
// Pure Y rotation: m0 =  cos θ, m2 = sin θ,  m6 = -sin θ, m8 = cos θ.
double yRotationDegrees(const double m[9]) {
    return qRadiansToDegrees(std::atan2(m[2], m[0]));
}

QString partNumberFromFilename(const QString& f) {
    QString s = f.trimmed();
    // Strip any directory prefix ("s/", "48/", etc.).
    const int slash = s.lastIndexOf(QLatin1Char('/'));
    const int bslsh = s.lastIndexOf(QLatin1Char('\\'));
    const int cut = std::max(slash, bslsh);
    if (cut >= 0) s = s.mid(cut + 1);
    // Strip ".dat" (any case).
    if (s.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive)) s.chop(4);
    return s.toUpper();
}

}

LDrawReadResult readLDraw(const QString& path) {
    LDrawReadResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.error = f.errorString();
        return r;
    }
    QTextStream in(&f);

    bool firstComment = true;
    QString line;
    while (in.readLineInto(&line)) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        // Split on any whitespace.
        const auto parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        bool ok = false;
        const int code = parts[0].toInt(&ok);
        if (!ok) continue;

        if (code == 0 && firstComment) {
            // Line-0 comments start with "0" followed by text.
            if (parts.size() > 1) {
                r.title = trimmed.mid(1).trimmed();
            }
            firstComment = false;
            continue;
        }
        if (code == 1) {
            // Expect: 1 color x y z a b c d e f g h i filename
            if (parts.size() < 15) continue;

            LDrawPartRef p;
            p.colorCode = parts[1].toInt();
            p.x = parts[2].toDouble();
            p.y = parts[3].toDouble();
            p.z = parts[4].toDouble();
            for (int i = 0; i < 9; ++i) {
                p.m[i] = parts[5 + i].toDouble();
            }
            // Filename is the rest of the line (can contain spaces).
            p.filename = parts.mid(14).join(QLatin1Char(' '));
            r.parts.push_back(std::move(p));
            continue;
        }
        // Primitive: 2 = line (2 verts), 3 = tri (3 verts), 4 = quad (4 verts).
        // Type 5 is conditional-line, skipped entirely — it's a rendering
        // hint for edge detection, not geometry we want to rasterize.
        if (code == 2 || code == 3 || code == 4) {
            const int nVerts = code;
            // Expect: <code> colour  <3 * nVerts floats>
            if (parts.size() < 2 + 3 * nVerts) continue;
            LDrawPrimitive p;
            p.kind = code;
            p.colorCode = parts[1].toInt();
            for (int i = 0; i < nVerts; ++i) {
                p.v[i][0] = parts[2 + i * 3 + 0].toDouble();
                p.v[i][1] = parts[2 + i * 3 + 1].toDouble();
                p.v[i][2] = parts[2 + i * 3 + 2].toDouble();
            }
            r.primitives.push_back(p);
        }
    }

    r.ok = true;
    return r;
}

std::unique_ptr<core::Map> toBlueBrickMap(const LDrawReadResult& src) {
    auto map = std::make_unique<core::Map>();
    map->author = QStringLiteral("LDraw import");
    if (!src.title.isEmpty()) map->comment = src.title;

    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    layer->name = QStringLiteral("Imported");

    for (const auto& ref : src.parts) {
        core::Brick b;
        b.guid = core::newBbmId();
        const QString pn = partNumberFromFilename(ref.filename);
        // BlueBrick format bakes color into PartNumber as "<part>.<color>".
        b.partNumber = QStringLiteral("%1.%2").arg(pn).arg(ref.colorCode);
        b.orientation = static_cast<float>(yRotationDegrees(ref.m));
        b.altitude = static_cast<float>(ref.y / kLduPerStud);

        const double xStuds = ref.x / kLduPerStud;
        const double zStuds = ref.z / kLduPerStud;
        // Default brick footprint is 2x2 studs until the parts library resolves
        // real dimensions; the .bbm writer doesn't care, and our renderer
        // re-derives size from the part's GIF at render time.
        constexpr double defaultStuds = 2.0;
        b.displayArea = QRectF(xStuds - defaultStuds / 2.0,
                                zStuds - defaultStuds / 2.0,
                                defaultStuds, defaultStuds);
        layer->bricks.push_back(std::move(b));
    }

    map->layers().push_back(std::move(layer));
    return map;
}

}
