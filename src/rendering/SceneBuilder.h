#pragma once

#include <QHash>
#include <QString>

class QGraphicsItem;
class QGraphicsScene;

namespace cld::core { class Layer; class Map; }
namespace cld::parts { class PartsLibrary; }

namespace cld::rendering {

// Populates a QGraphicsScene with items for each layer of a core::Map.
// Scene units: 1 stud = 8 pixels (matches BlueBrickParts GIF sampling).
// Keeps a per-layer group of items so visibility can toggle at layer granularity.
class SceneBuilder {
public:
    static constexpr int kPixelsPerStud = 8;

    SceneBuilder(QGraphicsScene& scene, parts::PartsLibrary& parts);

    void build(const core::Map& map);
    void clear();

    // Toggle the visibility of a layer (by index). Returns false if out of range.
    bool setLayerVisible(int layerIndex, bool visible);

private:
    void addLayer(const core::Layer& layer, int layerIndex);
    void addSidecarContent(const core::Map& map);
    void addVenue(const core::Map& map);
    void addAnchoredLabels(const core::Map& map);

    QGraphicsScene& scene_;
    parts::PartsLibrary& parts_;
    QHash<int, QGraphicsItem*> layerGroups_;     // layer index -> item group root
    QHash<QString, QGraphicsItem*> brickByGuid_; // brick.guid -> QGraphicsItem*
    QGraphicsItem* venueGroup_ = nullptr;
    QGraphicsItem* worldLabelGroup_ = nullptr;
};

}
