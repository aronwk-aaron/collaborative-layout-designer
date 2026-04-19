#include "LayerIO.h"

#include "XmlPrimitives.h"

#include "../core/Layer.h"
#include "../core/LayerGrid.h"

#include <QXmlStreamAttributes>

namespace cld::saveload {

namespace {

// ----- LayerGrid -----

std::unique_ptr<core::LayerGrid> readLayerGrid(QXmlStreamReader& r, int dataVersion) {
    auto grid = std::make_unique<core::LayerGrid>();

    // Read common layer header first (sequential by upstream convention).
    bool commonDone = false;
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (!commonDone) {
            if (n == QStringLiteral("Name")) {
                grid->name = xml::readTextElement(r);
                continue;
            }
            if (n == QStringLiteral("Visible")) {
                grid->visible = xml::readBoolElement(r);
                continue;
            }
            if (n == QStringLiteral("Transparency") && dataVersion > 4) {
                grid->transparency = xml::readIntElement(r);
                continue;
            }
            if (n == QStringLiteral("HullProperties") && dataVersion >= 9) {
                grid->hull.displayHulls = r.attributes().value(QStringLiteral("isVisible"))
                                              .compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
                while (r.readNextStartElement()) {
                    const auto hn = r.name();
                    if      (hn == QStringLiteral("hullColor"))     grid->hull.color     = xml::readColor(r);
                    else if (hn == QStringLiteral("hullThickness")) grid->hull.thickness = xml::readIntElement(r);
                    else r.skipCurrentElement();
                }
                continue;
            }
            commonDone = true; // fall through to grid-specific handling
        }

        if      (n == QStringLiteral("GridColor"))           grid->gridColor        = xml::readColor(r);
        else if (n == QStringLiteral("GridThickness"))       grid->gridThickness    = xml::readFloatElement(r);
        else if (n == QStringLiteral("SubGridColor"))        grid->subGridColor     = xml::readColor(r);
        else if (n == QStringLiteral("SubGridThickness"))    grid->subGridThickness = xml::readFloatElement(r);
        else if (n == QStringLiteral("GridSizeInStud"))      grid->gridSizeInStud   = xml::readIntElement(r);
        else if (n == QStringLiteral("SubDivisionNumber"))   grid->subDivisionNumber = std::max(xml::readIntElement(r), 2);
        else if (n == QStringLiteral("DisplayGrid"))         grid->displayGrid       = xml::readBoolElement(r);
        else if (n == QStringLiteral("DisplaySubGrid"))      grid->displaySubGrid    = xml::readBoolElement(r);
        else if (n == QStringLiteral("DisplayCellIndex"))    grid->displayCellIndex  = xml::readBoolElement(r);
        else if (n == QStringLiteral("CellIndexFont"))       grid->cellIndexFont     = xml::readFont(r, dataVersion);
        else if (n == QStringLiteral("CellIndexColor"))      grid->cellIndexColor    = xml::readColor(r);
        else if (n == QStringLiteral("CellIndexColumnType")) grid->cellIndexColumnType = static_cast<core::CellIndexType>(xml::readIntElement(r));
        else if (n == QStringLiteral("CellIndexRowType"))    grid->cellIndexRowType    = static_cast<core::CellIndexType>(xml::readIntElement(r));
        else if (n == QStringLiteral("CellIndexCorner"))     grid->cellIndexCorner     = xml::readPoint(r);
        else r.skipCurrentElement();
    }
    return grid;
}

void writeLayerCommonHeader(QXmlStreamWriter& w, const core::Layer& layer, const QString& typeAttr) {
    w.writeStartElement(QStringLiteral("Layer"));
    w.writeAttribute(QStringLiteral("type"), typeAttr);
    w.writeAttribute(QStringLiteral("id"), layer.guid);

    xml::writeTextElement(w, QStringLiteral("Name"),    layer.name);
    xml::writeBoolElement(w, QStringLiteral("Visible"), layer.visible);
    xml::writeIntElement (w, QStringLiteral("Transparency"), layer.transparency);

    w.writeStartElement(QStringLiteral("HullProperties"));
    w.writeAttribute(QStringLiteral("isVisible"), xml::formatBool(layer.hull.displayHulls));
    xml::writeColor     (w, QStringLiteral("hullColor"),     layer.hull.color);
    xml::writeIntElement(w, QStringLiteral("hullThickness"), layer.hull.thickness);
    w.writeEndElement(); // HullProperties
}

void writeLayerGrid(QXmlStreamWriter& w, const core::LayerGrid& grid) {
    writeLayerCommonHeader(w, grid, QStringLiteral("grid"));

    xml::writeColor       (w, QStringLiteral("GridColor"),        grid.gridColor);
    xml::writeFloatElement(w, QStringLiteral("GridThickness"),    grid.gridThickness);
    xml::writeColor       (w, QStringLiteral("SubGridColor"),     grid.subGridColor);
    xml::writeFloatElement(w, QStringLiteral("SubGridThickness"), grid.subGridThickness);
    xml::writeIntElement  (w, QStringLiteral("GridSizeInStud"),    grid.gridSizeInStud);
    xml::writeIntElement  (w, QStringLiteral("SubDivisionNumber"), grid.subDivisionNumber);
    xml::writeBoolElement (w, QStringLiteral("DisplayGrid"),       grid.displayGrid);
    xml::writeBoolElement (w, QStringLiteral("DisplaySubGrid"),    grid.displaySubGrid);
    xml::writeBoolElement (w, QStringLiteral("DisplayCellIndex"),  grid.displayCellIndex);
    xml::writeFont        (w, QStringLiteral("CellIndexFont"),     grid.cellIndexFont);
    xml::writeColor       (w, QStringLiteral("CellIndexColor"),    grid.cellIndexColor);
    xml::writeIntElement  (w, QStringLiteral("CellIndexColumnType"), static_cast<int>(grid.cellIndexColumnType));
    xml::writeIntElement  (w, QStringLiteral("CellIndexRowType"),    static_cast<int>(grid.cellIndexRowType));
    xml::writePoint       (w, QStringLiteral("CellIndexCorner"),     grid.cellIndexCorner);

    w.writeEndElement(); // Layer
}

}

LayerReadOutcome readLayer(QXmlStreamReader& r, int dataVersion) {
    // Current token: StartElement <Layer type="..." id="...">
    const QXmlStreamAttributes attrs = r.attributes();
    const QString type = attrs.value(QStringLiteral("type")).toString();
    const QString guid = attrs.value(QStringLiteral("id")).toString();

    LayerReadOutcome out;
    if (type == QStringLiteral("grid")) {
        auto g = readLayerGrid(r, dataVersion);
        g->guid = guid;
        out.layer = std::move(g);
    } else if (type == QStringLiteral("brick")
            || type == QStringLiteral("text")
            || type == QStringLiteral("area")
            || type == QStringLiteral("ruler")) {
        r.skipCurrentElement();
        out.warning = QStringLiteral("Layer type '%1' is not yet implemented; skipped.").arg(type);
    } else {
        r.skipCurrentElement();
        out.warning = QStringLiteral("Unknown layer type '%1'; skipped.").arg(type);
    }
    return out;
}

void writeLayer(QXmlStreamWriter& w, const core::Layer& layer) {
    switch (layer.kind()) {
        case core::LayerKind::Grid:
            writeLayerGrid(w, static_cast<const core::LayerGrid&>(layer));
            break;
        case core::LayerKind::Brick:
        case core::LayerKind::Text:
        case core::LayerKind::Area:
        case core::LayerKind::Ruler:
        case core::LayerKind::AnchoredText:
            // Not yet implemented; emit an empty layer placeholder so the
            // stream remains well-formed. Real implementations land with
            // each layer-type port.
            w.writeStartElement(QStringLiteral("Layer"));
            w.writeAttribute(QStringLiteral("type"), QStringLiteral("unimplemented"));
            w.writeAttribute(QStringLiteral("id"),   layer.guid);
            w.writeEndElement();
            break;
    }
}

}
