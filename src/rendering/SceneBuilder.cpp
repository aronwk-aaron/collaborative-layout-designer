#include "SceneBuilder.h"

#include "../core/Layer.h"
#include "../core/LayerArea.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QBrush>
#include <QFont>
#include <QGraphicsEllipseItem>
#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPen>
#include <QPixmap>

namespace cld::rendering {

namespace {

constexpr int kPx = SceneBuilder::kPixelsPerStud;

double studToPx(double s) { return s * kPx; }

QGraphicsItemGroup* makeGroup(QGraphicsScene& scene) {
    auto* g = new QGraphicsItemGroup();
    g->setHandlesChildEvents(false);
    scene.addItem(g);
    return g;
}

// Metadata keys attached to each brick QGraphicsItem via setData(). Used by
// the edit pipeline to look up the mutated brick on mouse release / key press.
constexpr int kBrickDataLayerIndex = 0;
constexpr int kBrickDataGuid       = 1;
constexpr int kBrickDataKind       = 2;  // value: "brick" to distinguish from other items

void addBrickLayer(const core::LayerBrick& L, QGraphicsItemGroup* group, parts::PartsLibrary& lib,
                   int layerIndex) {
    for (const auto& brick : L.bricks) {
        // BlueBrick bakes the color suffix into the PartNumber string itself
        // (e.g. "3811.1" for a blue 32x32 baseplate, or just "TABLE96X190"
        // for an uncolored composite). Lookup is case-insensitive because
        // upstream stores the part number upper-cased in .bbm but the vendored
        // library uses mixed case on disk.
        std::optional<parts::PartMetadata> meta = lib.metadata(brick.partNumber);
        if (!meta) {
            // Fallback: match by prefix so a .bbm referencing "3811" picks up
            // any color-specific variant.
            const QString needle = brick.partNumber.toLower() + QLatin1Char('.');
            for (const QString& key : lib.keys()) {
                if (key.toLower().startsWith(needle)) {
                    meta = lib.metadata(key);
                    break;
                }
            }
        }

        const QRectF areaPx(studToPx(brick.displayArea.x()),
                            studToPx(brick.displayArea.y()),
                            studToPx(brick.displayArea.width()),
                            studToPx(brick.displayArea.height()));
        const QPointF centerPx = areaPx.center();

        QGraphicsItem* item = nullptr;
        if (meta && !meta->gifFilePath.isEmpty()) {
            QPixmap pm(meta->gifFilePath);
            if (!pm.isNull()) {
                auto* p = new QGraphicsPixmapItem(pm);
                p->setTransformationMode(Qt::SmoothTransformation);
                p->setOffset(-pm.width() / 2.0, -pm.height() / 2.0);
                p->setTransformOriginPoint(0, 0);
                p->setRotation(brick.orientation);
                p->setPos(centerPx);
                p->setZValue(brick.altitude);
                item = p;
            }
        }
        if (!item) {
            // Fallback placeholder: dashed rectangle in the brick's AABB so the
            // layout shape still shows when a part GIF is missing.
            auto* r = new QGraphicsRectItem(areaPx);
            QPen pen(QColor(200, 80, 80));
            pen.setStyle(Qt::DashLine);
            r->setPen(pen);
            r->setBrush(QBrush(QColor(255, 200, 200, 80)));
            item = r;
        }

        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setFlag(QGraphicsItem::ItemIsMovable,    true);
        item->setData(kBrickDataLayerIndex, layerIndex);
        item->setData(kBrickDataGuid,       brick.guid);
        item->setData(kBrickDataKind,       QStringLiteral("brick"));
        group->addToGroup(item);
    }
}

void addTextLayer(const core::LayerText& L, QGraphicsItemGroup* group) {
    for (const auto& cell : L.textCells) {
        auto* t = new QGraphicsSimpleTextItem(cell.text);
        QFont f(cell.font.familyName, static_cast<int>(cell.font.sizePt));
        f.setBold(cell.font.styleString.contains(QStringLiteral("Bold")));
        f.setItalic(cell.font.styleString.contains(QStringLiteral("Italic")));
        t->setFont(f);
        t->setBrush(QBrush(cell.fontColor.color));
        const QRectF areaPx(studToPx(cell.displayArea.x()),
                            studToPx(cell.displayArea.y()),
                            studToPx(cell.displayArea.width()),
                            studToPx(cell.displayArea.height()));
        t->setPos(areaPx.topLeft());
        t->setRotation(cell.orientation);
        group->addToGroup(t);
    }
}

void addAreaLayer(const core::LayerArea& L, QGraphicsItemGroup* group) {
    const double sizePx = studToPx(L.areaCellSizeInStud);
    for (const auto& cell : L.cells) {
        auto* r = new QGraphicsRectItem(cell.x * sizePx, cell.y * sizePx, sizePx, sizePx);
        r->setPen(Qt::NoPen);
        r->setBrush(QBrush(cell.color));
        group->addToGroup(r);
    }
}

void addRulerLayer(const core::LayerRuler& L, QGraphicsItemGroup* group) {
    for (const auto& any : L.rulers) {
        if (any.kind == core::RulerKind::Linear) {
            const auto& r = any.linear;
            auto* line = new QGraphicsLineItem(
                studToPx(r.point1.x()), studToPx(r.point1.y()),
                studToPx(r.point2.x()), studToPx(r.point2.y()));
            QPen pen(r.color.color);
            pen.setWidthF(r.lineThickness);
            line->setPen(pen);
            group->addToGroup(line);
        } else {
            const auto& r = any.circular;
            const double rPx = studToPx(r.radius);
            auto* el = new QGraphicsEllipseItem(
                studToPx(r.center.x()) - rPx, studToPx(r.center.y()) - rPx,
                2 * rPx, 2 * rPx);
            QPen pen(r.color.color);
            pen.setWidthF(r.lineThickness);
            el->setPen(pen);
            el->setBrush(Qt::NoBrush);
            group->addToGroup(el);
        }
    }
}

}

SceneBuilder::SceneBuilder(QGraphicsScene& scene, parts::PartsLibrary& parts)
    : scene_(scene), parts_(parts) {}

void SceneBuilder::clear() {
    for (auto* g : std::as_const(layerGroups_)) scene_.removeItem(g), delete g;
    layerGroups_.clear();
}

void SceneBuilder::build(const core::Map& map) {
    clear();
    for (size_t i = 0; i < map.layers().size(); ++i) {
        addLayer(*map.layers()[i], static_cast<int>(i));
    }
}

void SceneBuilder::addLayer(const core::Layer& L, int layerIndex) {
    auto* group = makeGroup(scene_);
    group->setZValue(layerIndex);      // later layers render on top
    group->setVisible(L.visible);
    layerGroups_.insert(layerIndex, group);

    switch (L.kind()) {
        case core::LayerKind::Grid:
            // Grid lines are painted by MapView::drawBackground, not as scene
            // items. We still create an empty group so the layer panel can
            // surface a visibility toggle later — no-op if nothing to render.
            break;
        case core::LayerKind::Brick:
            addBrickLayer(static_cast<const core::LayerBrick&>(L), group, parts_, layerIndex);
            break;
        case core::LayerKind::Text:
            addTextLayer(static_cast<const core::LayerText&>(L), group);
            break;
        case core::LayerKind::Area:
            addAreaLayer(static_cast<const core::LayerArea&>(L), group);
            break;
        case core::LayerKind::Ruler:
            addRulerLayer(static_cast<const core::LayerRuler&>(L), group);
            break;
        case core::LayerKind::AnchoredText:
            break;
    }

    // Apply transparency (0..100 percent in upstream convention).
    if (L.transparency < 100) {
        group->setOpacity(L.transparency / 100.0);
    }
}

bool SceneBuilder::setLayerVisible(int layerIndex, bool visible) {
    auto it = layerGroups_.find(layerIndex);
    if (it == layerGroups_.end()) return false;
    (*it)->setVisible(visible);
    return true;
}

}
