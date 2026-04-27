#include "LDrawMeshLoader.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

namespace cld::import {

namespace {

// Pull N doubles out of `toks` starting at index `i`. Returns {} on
// any conversion failure so the caller can skip a malformed line.
std::optional<std::vector<double>> readDoubles(const QStringList& toks, int i, int n) {
    std::vector<double> out; out.reserve(n);
    if (i + n > toks.size()) return {};
    for (int k = 0; k < n; ++k) {
        bool ok = false;
        const double v = toks[i + k].toDouble(&ok);
        if (!ok) return {};
        out.push_back(v);
    }
    return out;
}

}  // namespace

LDrawMeshLoader::LDrawMeshLoader(const LDrawLibrary& library, const LDrawPalette& palette)
    : lib_(library), palette_(palette) {}

const LDrawMeshLoader::ParsedDat* LDrawMeshLoader::parse(const QString& absolutePath) {
    if (auto it = cache_.constFind(absolutePath); it != cache_.constEnd()) {
        return &it.value();
    }
    QFile f(absolutePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errors_.append(QStringLiteral("Could not open %1").arg(absolutePath));
        return nullptr;
    }
    ParsedDat parsed;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        const QStringList toks = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
        if (toks.isEmpty()) continue;
        bool typeOk = false;
        const int type = toks[0].toInt(&typeOk);
        if (!typeOk) continue;
        switch (type) {
            case 1: {
                // 1 <c> x y z a b c d e f g h i <file...>
                if (toks.size() < 15) continue;
                bool colorOk = false;
                const int color = toks[1].toInt(&colorOk);
                if (!colorOk) continue;
                auto vals = readDoubles(toks, 2, 12);
                if (!vals) continue;
                ParsedDat::SubRef ref;
                ref.color = color;
                // Filename can have spaces / a path prefix; rejoin from
                // index 14 to end.
                ref.child = toks.mid(14).join(QChar(' ')).trimmed();
                ref.transform = geom::Mat4::fromTranslationAndRotation(
                    geom::Vec3{ (*vals)[0], (*vals)[1], (*vals)[2] },
                    (*vals)[3], (*vals)[4], (*vals)[5],
                    (*vals)[6], (*vals)[7], (*vals)[8],
                    (*vals)[9], (*vals)[10], (*vals)[11]);
                parsed.subrefs.push_back(std::move(ref));
                break;
            }
            case 3: {
                // 3 <c> x1 y1 z1 x2 y2 z2 x3 y3 z3
                if (toks.size() < 11) continue;
                bool colorOk = false;
                const int color = toks[1].toInt(&colorOk);
                if (!colorOk) continue;
                auto vals = readDoubles(toks, 2, 9);
                if (!vals) continue;
                ParsedDat::RawTri rt;
                rt.color = color;
                rt.v[0] = { (*vals)[0], (*vals)[1], (*vals)[2] };
                rt.v[1] = { (*vals)[3], (*vals)[4], (*vals)[5] };
                rt.v[2] = { (*vals)[6], (*vals)[7], (*vals)[8] };
                parsed.primitives.push_back(rt);
                break;
            }
            case 4: {
                // 4 <c> v0 v1 v2 v3 — split into triangles (0,1,2) and
                // (0,2,3). Same color for both halves.
                if (toks.size() < 14) continue;
                bool colorOk = false;
                const int color = toks[1].toInt(&colorOk);
                if (!colorOk) continue;
                auto vals = readDoubles(toks, 2, 12);
                if (!vals) continue;
                geom::Vec3 p[4] = {
                    { (*vals)[0],  (*vals)[1],  (*vals)[2] },
                    { (*vals)[3],  (*vals)[4],  (*vals)[5] },
                    { (*vals)[6],  (*vals)[7],  (*vals)[8] },
                    { (*vals)[9],  (*vals)[10], (*vals)[11] },
                };
                ParsedDat::RawTri t1{ color, { p[0], p[1], p[2] } };
                ParsedDat::RawTri t2{ color, { p[0], p[2], p[3] } };
                parsed.primitives.push_back(t1);
                parsed.primitives.push_back(t2);
                break;
            }
            // 2 / 5 / other: ignore.
            default: break;
        }
    }
    auto inserted = cache_.insert(absolutePath, std::move(parsed));
    return &inserted.value();
}

void LDrawMeshLoader::appendBaked(geom::Mesh& out,
                                   const ParsedDat& dat,
                                   const geom::Mat4& parentXform,
                                   int parentColor) {
    // Resolve colour code -> final QColor. 16 = inherit parent. 24 is
    // for edges (which we don't render here); fall through to grey.
    auto resolveColor = [&](int code) -> QColor {
        if (code == 16) return palette_.color(parentColor == 16 ? 7 : parentColor);
        if (code == 24) return palette_.color(0);
        return palette_.color(code);
    };

    // Bake each primitive: parent transform applied, colour resolved.
    for (const auto& prim : dat.primitives) {
        const QColor finalColor = resolveColor(prim.color);
        geom::Triangle t;
        for (int k = 0; k < 3; ++k) t.v[k] = parentXform.transform(prim.v[k]);
        t.color = finalColor;
        out.tris.push_back(t);
    }

    // Recurse into each subfile reference. Compose transforms (parent *
    // child) so child-local coords land correctly in the part frame.
    for (const auto& ref : dat.subrefs) {
        const QString absChild = lib_.resolve(ref.child);
        if (absChild.isEmpty()) {
            errors_.append(QStringLiteral("Unresolved subfile %1").arg(ref.child));
            continue;
        }
        const ParsedDat* childDat = parse(absChild);
        if (!childDat) continue;
        // Inherited colour: child's own ref colour 16 means "use my
        // parent's colour", which here is `parentColor` after we
        // resolve through ref.color.
        const int inherited = (ref.color == 16) ? parentColor : ref.color;
        const geom::Mat4 composed = parentXform * ref.transform;
        appendBaked(out, *childDat, composed, inherited);
    }
}

geom::Mesh LDrawMeshLoader::loadPart(const QString& datRef, int topColor) {
    geom::Mesh out;
    const QString abs = lib_.resolve(datRef);
    if (abs.isEmpty()) {
        errors_.append(QStringLiteral("Unresolved part %1").arg(datRef));
        return out;
    }
    const ParsedDat* dat = parse(abs);
    if (!dat) return out;
    appendBaked(out, *dat, geom::Mat4::identity(), topColor);
    return out;
}

}  // namespace cld::import
