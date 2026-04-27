#include "LDDGeomReader.h"

#include <QtEndian>

#include <cstring>

namespace cld::import {

namespace {

// LDD .g vertex coords are in LDD coordinate units = studs (8 mm
// each). LDraw uses LDU at 0.4 mm, so 1 LDD unit = 20 LDU. Apply
// that scaling so .g triangles share the LDU coord system used by
// the LDraw mesh-loader and the rasterizer.
constexpr double kLddToLdu = 20.0;

bool readU32(const QByteArray& d, qsizetype& off, quint32& out) {
    if (off + 4 > d.size()) return false;
    out = qFromLittleEndian<quint32>(d.constData() + off);
    off += 4;
    return true;
}

bool readF32(const QByteArray& d, qsizetype& off, float& out) {
    if (off + 4 > d.size()) return false;
    quint32 raw = qFromLittleEndian<quint32>(d.constData() + off);
    std::memcpy(&out, &raw, 4);
    off += 4;
    return true;
}

}  // namespace

LDDGeomReadResult readLDDGeom(const QByteArray& bytes) {
    LDDGeomReadResult r;
    qsizetype off = 0;
    if (bytes.size() < 16) {
        r.error = QStringLiteral("File too small to contain a .g header (%1 bytes)")
                      .arg(bytes.size());
        return r;
    }
    // Magic bytes: ASCII "10GB" → 0x42 0x47 0x30 0x31 read as LE u32.
    // Verified against both LU brickprimitives/*.g and LDD
    // Assets/db/Primitives/LOD0/*.g.
    if (bytes.left(4) != QByteArray("10GB", 4)) {
        r.error = QStringLiteral("Not a .g file (magic mismatch)");
        return r;
    }
    off = 4;
    quint32 vCount = 0, iCount = 0, options = 0;
    if (!readU32(bytes, off, vCount) ||
        !readU32(bytes, off, iCount) ||
        !readU32(bytes, off, options)) {
        r.error = QStringLiteral("Truncated header");
        return r;
    }
    if (iCount % 3 != 0) {
        r.error = QStringLiteral("Index count not divisible by 3 (%1)").arg(iCount);
        return r;
    }

    const bool hasUVs   = (options & 3)  == 3;
    const bool hasBones = (options & 48) == 48;
    (void)hasBones;  // bone data is at the tail and skipped — we only need pos+tri.

    // Position buffer. We don't need normals for the top-down sprite,
    // but we have to read them to advance the cursor past the buffer.
    std::vector<geom::Vec3> positions;
    positions.reserve(vCount);
    for (quint32 i = 0; i < vCount; ++i) {
        float x, y, z;
        if (!readF32(bytes, off, x) || !readF32(bytes, off, y) || !readF32(bytes, off, z)) {
            r.error = QStringLiteral("Truncated vertex positions at vertex %1").arg(i);
            return r;
        }
        positions.push_back(geom::Vec3{ x * kLddToLdu, y * kLddToLdu, z * kLddToLdu });
    }
    // Skip normals (vCount * 12 bytes).
    if (off + qsizetype(vCount) * 12 > bytes.size()) {
        r.error = QStringLiteral("Truncated vertex normals");
        return r;
    }
    off += qsizetype(vCount) * 12;

    // Skip UVs when present.
    if (hasUVs) {
        if (off + qsizetype(vCount) * 8 > bytes.size()) {
            r.error = QStringLiteral("Truncated UVs");
            return r;
        }
        off += qsizetype(vCount) * 8;
    }

    // Indices. Triangle = (i*3, i*3+1, i*3+2). Each index is a u32.
    const quint32 triCount = iCount / 3;
    if (off + qsizetype(iCount) * 4 > bytes.size()) {
        r.error = QStringLiteral("Truncated index buffer");
        return r;
    }
    r.mesh.tris.reserve(triCount);
    const QColor whiteOpaque = QColor::fromRgb(220, 220, 220);
    for (quint32 t = 0; t < triCount; ++t) {
        quint32 ia, ib, ic;
        if (!readU32(bytes, off, ia) || !readU32(bytes, off, ib) || !readU32(bytes, off, ic)) {
            r.error = QStringLiteral("Truncated index buffer at triangle %1").arg(t);
            return r;
        }
        if (ia >= vCount || ib >= vCount || ic >= vCount) {
            r.error = QStringLiteral("Index out of range at triangle %1 (%2,%3,%4 vs %5)")
                          .arg(t).arg(ia).arg(ib).arg(ic).arg(vCount);
            return r;
        }
        geom::Triangle tri;
        tri.v[0] = positions[ia];
        tri.v[1] = positions[ib];
        tri.v[2] = positions[ic];
        tri.color = whiteOpaque;
        r.mesh.tris.push_back(tri);
    }
    r.ok = true;
    return r;
}

}  // namespace cld::import
