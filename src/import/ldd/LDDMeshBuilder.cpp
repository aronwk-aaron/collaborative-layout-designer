#include "LDDMeshBuilder.h"

#include "../lif/LifReader.h"
#include "LDDGeomReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace cld::import {

namespace {

QString stripDesignDecorations(QString s) {
    // LDDReader produces "<designID>.dat" or "<designID>.<matId>.dat".
    if (s.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive)) s.chop(4);
    const int dot = s.indexOf(QChar('.'));
    if (dot > 0) s = s.left(dot);
    return s;
}

// LDD/LU place .g files in Primitives/LOD0/. Some bricks have multi-
// mesh splits as <id>.g1, <id>.g2 — we only ask for the base .g for
// now; LU's brickprimitives uses the same base name with an LOD
// suffix in the filename instead of multi-mesh splitting.
QString primitivePath(const QString& designId) {
    return QStringLiteral("Primitives/LOD0/%1.g").arg(designId);
}

geom::Mat4 ldrawTransformOf(const LDrawPartRef& ref) {
    return geom::Mat4::fromTranslationAndRotation(
        geom::Vec3{ ref.x, ref.y, ref.z },
        ref.m[0], ref.m[1], ref.m[2],
        ref.m[3], ref.m[4], ref.m[5],
        ref.m[6], ref.m[7], ref.m[8]);
}

}  // namespace

QByteArray LDDMeshBuilder::fetchPart(const QString& designId) {
    const QString relPath = primitivePath(designId);

    // 1) On-disk override: the user has already extracted db.lif (e.g.
    //    via the python LIF extractor). Reading from disk is faster
    //    and gives easy modding.
    if (!diskRoot_.isEmpty()) {
        const QString abs = QDir(diskRoot_).filePath(
            QStringLiteral("Assets/db/") + relPath);
        QFile f(abs);
        if (f.open(QIODevice::ReadOnly)) return f.readAll();
    }

    // 2) Virtual: read from the open LIF archive. The TOC inside
    //    db.lif uses leading-slash absolute paths.
    if (lif_) {
        const QString lifPath = QStringLiteral("/db/") + relPath;
        QByteArray bytes = lif_->read(lifPath);
        if (!bytes.isEmpty()) return bytes;
    }
    return {};
}

LDDLDrawBakedModel LDDMeshBuilder::bake(const LDrawReadResult& read) {
    LDDLDrawBakedModel out;

    for (const auto& ref : read.parts) {
        const QString designId = stripDesignDecorations(ref.filename);
        // If we have an LDraw mapping for this designID, the LDraw
        // pipeline already handled (or will handle) it. Skip here.
        if (mapping_ && !mapping_->partFor(designId).isEmpty()) continue;

        const QByteArray bytes = fetchPart(designId);
        if (bytes.isEmpty()) {
            out.errors.append(QStringLiteral("missing .g for designID %1").arg(designId));
            ++out.skipped;
            continue;
        }
        const auto geomResult = readLDDGeom(bytes);
        if (!geomResult.ok) {
            out.errors.append(QStringLiteral("bad .g for %1: %2")
                                  .arg(designId, geomResult.error));
            ++out.skipped;
            continue;
        }

        // Resolve the part's colour: LDD materialID lives on the
        // LDrawPartRef as colorCode (LDDReader copies it through).
        QColor partColor = QColor::fromRgb(220, 220, 220);  // grey fallback
        if (materials_) {
            const QColor c = materials_->color(ref.colorCode);
            if (c.isValid()) partColor = c;
        }

        const geom::Mat4 xform = ldrawTransformOf(ref);
        for (const auto& tri : geomResult.mesh.tris) {
            geom::Triangle baked;
            for (int k = 0; k < 3; ++k) baked.v[k] = xform.transform(tri.v[k]);
            baked.color = partColor;
            out.mesh.tris.push_back(baked);
        }
        ++out.rendered;
    }
    return out;
}

}  // namespace cld::import
