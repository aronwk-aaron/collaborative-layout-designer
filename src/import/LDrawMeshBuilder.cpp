#include "LDrawMeshBuilder.h"

namespace cld::import {

namespace {

// Convert a 3x3 rotation + (x,y,z) translation from an LDraw type-1
// line into a geom::Mat4. The 3x3 matrix is in row-major.
geom::Mat4 refTransform(const LDrawPartRef& ref) {
    return geom::Mat4::fromTranslationAndRotation(
        geom::Vec3{ ref.x, ref.y, ref.z },
        ref.m[0], ref.m[1], ref.m[2],
        ref.m[3], ref.m[4], ref.m[5],
        ref.m[6], ref.m[7], ref.m[8]);
}

}  // namespace

BakedModel bakeMeshFromLDraw(const LDrawReadResult& src,
                              LDrawMeshLoader& loader,
                              const LDrawPalette& palette) {
    BakedModel out;

    // First pass: resolved subfile refs. Each ref's mesh comes back in
    // part-local coords; we transform it into model coords by walking
    // every triangle through the ref's 4x4. The mesh-loader caches the
    // per-part bake, so a model that uses the same brick a hundred
    // times only parses the .dat once.
    for (const auto& ref : src.parts) {
        const geom::Mesh partMesh = loader.loadPart(ref.filename, ref.colorCode);
        if (partMesh.tris.empty()) {
            out.unresolvedRefs++;
            continue;
        }
        out.resolvedRefs++;
        const geom::Mat4 xform = refTransform(ref);
        for (const auto& t : partMesh.tris) {
            geom::Triangle world;
            for (int k = 0; k < 3; ++k) world.v[k] = xform.transform(t.v[k]);
            world.color = t.color;
            out.mesh.tris.push_back(world);
        }
    }

    // Second pass: inline primitives (Studio exports + hand-authored
    // .ldr snippets sometimes carry geometry directly). Treat them as
    // if they were under an identity transform. Colour 16 ("inherit")
    // is meaningless at top level; treat it as code 7 (light grey)
    // for visibility — same convention LDView and most other tools
    // use when there's no parent ref to inherit from.
    for (const auto& prim : src.primitives) {
        const int resolvedCode = (prim.colorCode == 16) ? 7 : prim.colorCode;
        const QColor finalColor = palette.color(resolvedCode);
        if (prim.kind == 3) {
            geom::Triangle t;
            for (int k = 0; k < 3; ++k) {
                t.v[k] = { prim.v[k][0], prim.v[k][1], prim.v[k][2] };
            }
            t.color = finalColor;
            out.mesh.tris.push_back(t);
        } else if (prim.kind == 4) {
            geom::Vec3 p[4];
            for (int k = 0; k < 4; ++k) {
                p[k] = { prim.v[k][0], prim.v[k][1], prim.v[k][2] };
            }
            geom::Triangle t1{ { p[0], p[1], p[2] }, finalColor };
            geom::Triangle t2{ { p[0], p[2], p[3] }, finalColor };
            out.mesh.tris.push_back(t1);
            out.mesh.tris.push_back(t2);
        }
    }

    out.errors = loader.errors();
    return out;
}

}  // namespace cld::import
