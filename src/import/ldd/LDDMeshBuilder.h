#pragma once

#include "../../geom/Mesh.h"
#include "../ldraw/LDrawReader.h"
#include "LDDLDrawMapping.h"
#include "LDDMaterials.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace cld::import {

class LifReader;

// Bake an LDD model's brick refs into a flat geom::Mesh by loading
// each part's `.g` geometry directly (bypassing the LDraw library
// and the ldraw.xml mapping).
//
// Used as the fallback path for LDD parts that have no ldraw.xml
// mapping (LDD-only decorations, exclusive prints) — the LDraw
// pipeline can't render those because there's no .dat. With this
// path we render LDD's own geometry and paint it with the LDD
// material colour from Materials.xml.
//
// Source of .g bytes is configurable:
//   * On-disk: <lddInstallPath>/Assets/db/Primitives/LOD0/<id>.g
//     — present after the user has run an external LIF extractor
//   * Virtual: a `LifReader` mounted on db.lif
//
// LDDLDrawBakedModel reports which design IDs we successfully
// rendered and which were skipped, so the caller's UI can summarise.
struct LDDLDrawBakedModel {
    geom::Mesh    mesh;
    QStringList   errors;
    int           rendered = 0;        // refs that produced geometry
    int           skipped  = 0;        // refs whose .g couldn't be found
};

class LDDMeshBuilder {
public:
    // Configure the part-byte source to use one of:
    //   * setOnDiskRoot — path to LDD install. We look up
    //     "Assets/db/Primitives/LOD0/<id>.g" inside it.
    //   * setLifReader — pre-opened LifReader (must outlive this
    //     builder). We look up "/Primitives/LOD0/<id>.g" inside the
    //     archive.
    // Both can be set; on-disk wins to allow a user-extracted set of
    // overrides to take precedence.
    void setOnDiskRoot(QString lddInstallPath) { diskRoot_ = std::move(lddInstallPath); }
    void setLifReader(const LifReader* lif) { lif_ = lif; }

    void setMaterials(const LDDMaterials* materials) { materials_ = materials; }
    void setMapping(const LDDLDrawMapping* mapping)  { mapping_   = mapping; }

    // Bake every ref in `read` whose designID is NOT covered by the
    // ldraw.xml mapping. Refs that ARE in the mapping are skipped —
    // they go through the LDraw pipeline instead. The result is a
    // partial mesh containing only LDD-only-rendered parts; callers
    // typically merge it with the LDraw-baked mesh before
    // rasterizing.
    LDDLDrawBakedModel bake(const LDrawReadResult& read);

private:
    QByteArray fetchPart(const QString& designId);

    QString diskRoot_;
    const LifReader*       lif_       = nullptr;
    const LDDMaterials*    materials_ = nullptr;
    const LDDLDrawMapping* mapping_   = nullptr;
};

}  // namespace cld::import
