#include "MapView.h"

#include "../core/Layer.h"
#include "../core/LayerGrid.h"
#include "../core/Map.h"
#include "../rendering/SceneBuilder.h"

#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>

namespace cld::ui {

namespace {

constexpr double kMinZoom = 0.02;
constexpr double kMaxZoom = 40.0;

}

MapView::MapView(parts::PartsLibrary& parts, QWidget* parent)
    : QGraphicsView(parent), parts_(parts) {
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);

    auto* scene = new QGraphicsScene(this);
    scene->setBackgroundBrush(QColor(100, 149, 237));  // CornflowerBlue default
    setScene(scene);

    builder_ = std::make_unique<rendering::SceneBuilder>(*scene, parts_);
}

MapView::~MapView() = default;

void MapView::loadMap(std::unique_ptr<core::Map> map) {
    map_ = std::move(map);
    if (!map_) { builder_->clear(); return; }

    scene()->setBackgroundBrush(map_->backgroundColor.color);
    builder_->build(*map_);
    // Frame the scene on first load.
    if (!scene()->itemsBoundingRect().isEmpty()) {
        const auto r = scene()->itemsBoundingRect().adjusted(-50, -50, 50, 50);
        scene()->setSceneRect(r);
        fitInView(r, Qt::KeepAspectRatio);
    }
    viewport()->update();
}

void MapView::wheelEvent(QWheelEvent* e) {
    const double step = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const double current = transform().m11();
    const double next = current * step;
    if (next < kMinZoom || next > kMaxZoom) return;
    scale(step, step);
    e->accept();
}

void MapView::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);
    if (!map_) return;

    // Find the first visible grid layer and paint its lines across the exposed
    // rect. Matches LayerGrid in upstream: grid at `gridSizeInStud` studs, sub-
    // grid at `gridSizeInStud / subDivisionNumber`. One stud = 8 scene pixels.
    for (const auto& layer : map_->layers()) {
        if (layer->kind() != core::LayerKind::Grid || !layer->visible) continue;
        const auto& g = static_cast<const core::LayerGrid&>(*layer);
        if (!g.displayGrid && !g.displaySubGrid) continue;

        const double gridPx = g.gridSizeInStud * rendering::SceneBuilder::kPixelsPerStud;
        const double subPx  = gridPx / std::max(g.subDivisionNumber, 2);

        const double left   = std::floor(rect.left()   / subPx) * subPx;
        const double right  = rect.right();
        const double top    = std::floor(rect.top()    / subPx) * subPx;
        const double bottom = rect.bottom();

        if (g.displaySubGrid) {
            QPen subPen(g.subGridColor.color);
            subPen.setCosmetic(true);
            subPen.setWidthF(g.subGridThickness);
            painter->setPen(subPen);
            for (double x = left; x < right; x += subPx)
                painter->drawLine(QPointF(x, top), QPointF(x, bottom));
            for (double y = top; y < bottom; y += subPx)
                painter->drawLine(QPointF(left, y), QPointF(right, y));
        }
        if (g.displayGrid) {
            QPen gridPen(g.gridColor.color);
            gridPen.setCosmetic(true);
            gridPen.setWidthF(g.gridThickness);
            painter->setPen(gridPen);
            const double firstX = std::floor(rect.left() / gridPx) * gridPx;
            const double firstY = std::floor(rect.top()  / gridPx) * gridPx;
            for (double x = firstX; x < right; x += gridPx)
                painter->drawLine(QPointF(x, top), QPointF(x, bottom));
            for (double y = firstY; y < bottom; y += gridPx)
                painter->drawLine(QPointF(left, y), QPointF(right, y));
        }
        break;  // only the first grid layer paints (matches upstream UX)
    }
}

}
