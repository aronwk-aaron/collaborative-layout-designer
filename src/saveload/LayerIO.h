#pragma once

#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <memory>

namespace bld::core { class Layer; }

namespace bld::saveload {

struct LayerReadOutcome {
    std::unique_ptr<core::Layer> layer;
    QString warning;  // non-empty if we skipped an unknown/unsupported layer type
};

// Read one <Layer type="..." id="..."> element. The reader must be positioned
// at the StartElement. Returns a Layer (populated) or a warning on skip.
LayerReadOutcome readLayer(QXmlStreamReader& r, int dataVersion);

// Write one <Layer type="..." id="..."> element for the given layer.
void writeLayer(QXmlStreamWriter& w, const core::Layer& layer);

}
