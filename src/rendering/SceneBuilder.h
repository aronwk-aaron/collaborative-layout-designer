#pragma once

#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>

#include <functional>
#include <optional>

class QGraphicsItem;
class QGraphicsScene;

namespace cld::core { class Layer; class Map; }
namespace cld::parts { class PartsLibrary; }

namespace cld::rendering {

// Populates a QGraphicsScene with items for each layer of a core::Map.
// Scene units: 1 stud = 8 pixels (matches BlueBrickParts GIF sampling).
//
// Items are added directly to the scene (no QGraphicsItemGroup wrapper) so
// hit-testing / selection never gets intercepted by a parent group. We
// track per-layer items in a QHash<int, QList<QGraphicsItem*>> for
// visibility toggling.
class SceneBuilder {
public:
    static constexpr int kPixelsPerStud = 8;

    SceneBuilder(QGraphicsScene& scene, parts::PartsLibrary& parts);

    void build(const core::Map& map);
    void clear();

    // Configure live drag-snap. `snapStepStuds` of 0 disables snapping.
    // Applied by the per-item ItemPositionChange override so the brick snaps
    // under the cursor during drag, not only on release.
    static void setLiveSnapStepStuds(double snapStepStuds);

    // Install a hook the brick items consult on every live drag frame. If
    // the hook returns a non-empty position, that overrides grid snap — so
    // MapView can make connections win over the grid (vanilla's
    // getMovedSnapPoint flow: search for the nearest free connection point
    // of matching type; only fall back to the grid if none is close
    // enough). Passed (item, proposed scene pos); returns nullopt for
    // "no connection snap applicable".
    using LiveSnapHook = std::function<std::optional<QPointF>(QGraphicsItem*, QPointF)>;
    static void setLiveConnectionSnapHook(LiveSnapHook hook);

    // Toggle the visibility of a layer (by index). Returns false if out of range.
    bool setLayerVisible(int layerIndex, bool visible);

private:
    void addLayer(const core::Layer& layer, int layerIndex);
    void addSidecarContent(const core::Map& map);
    void addVenue(const core::Map& map);
    void addAnchoredLabels(const core::Map& map);
    void addModuleLabels(const core::Map& map);

    QGraphicsScene& scene_;
    parts::PartsLibrary& parts_;
    QHash<int, QList<QGraphicsItem*>> itemsByLayer_;  // layer index -> direct scene items
    QHash<QString, QGraphicsItem*>    brickByGuid_;   // brick.guid -> QGraphicsItem*
    QList<QGraphicsItem*>             venueItems_;
    QList<QGraphicsItem*>             worldLabelItems_;
    QList<QGraphicsItem*>             moduleLabelItems_;
};

}
