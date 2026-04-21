// Custom painting overlays drawn on top of the QGraphicsScene:
//   * drawBackground  — grid lines (driven by core::LayerGrid metadata)
//   * drawForeground  — scale indicator bar (always visible, follows zoom)
//   * refreshSelectionOverlay — rebuild the SelectionOverlay polygons from
//     the current scene selection (rulers union multi-segment highlights)
//
// Split out of MapView.cpp so the main translation unit focuses on
// interaction (mouse / key / drag pipeline) rather than painting.

#include "MapView.h"

#include "../core/Layer.h"
#include "../core/LayerGrid.h"
#include "../core/Map.h"
#include "../rendering/SceneBuilder.h"
#include "MapViewInternal.h"
#include "SelectionOverlay.h"

#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QSet>

#include <cmath>
#include <limits>

namespace cld::ui {

using detail::kBrickDataGuid;
using detail::isRulerItem;
using detail::studToPx;

void MapView::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);
    if (!map_) return;

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
        break;
    }
}

void MapView::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawForeground(painter, rect);

    // Scale-indicator bar pinned to the lower-left viewport corner.
    // Picks a round stud value that renders somewhere near 100-160 px
    // wide so the label stays legible across zoom levels.
    const double pxPerStudScene = studToPx();
    const double vpPxPerScene = transform().m11();
    const double vpPxPerStud = pxPerStudScene * vpPxPerScene;
    if (vpPxPerStud <= 0.0) return;

    static constexpr double kNiceSteps[] = {
        1, 2, 5, 10, 16, 20, 32, 48, 64, 96, 128, 192, 256,
        384, 512, 768, 1024, 1536, 2048, 3072, 4096
    };
    double bestStuds = 32.0;
    double bestScore = std::numeric_limits<double>::max();
    for (double s : kNiceSteps) {
        const double pxWide = s * vpPxPerStud;
        if (pxWide < 40.0 || pxWide > 320.0) continue;
        const double dev = std::abs(pxWide - 120.0);
        if (dev < bestScore) { bestScore = dev; bestStuds = s; }
    }
    const double barPx = bestStuds * vpPxPerStud;
    // 1 stud = 8 mm per BlueBrick convention — show a human-friendly
    // secondary label in mm / m depending on magnitude.
    const double mm = bestStuds * 8.0;
    QString primary  = QStringLiteral("%1 studs").arg(bestStuds, 0, 'f', 0);
    QString secondary;
    if (mm >= 1000) secondary = QStringLiteral("%1 m").arg(mm / 1000.0, 0, 'f', 2);
    else            secondary = QStringLiteral("%1 mm").arg(mm, 0, 'f', 0);

    painter->save();
    painter->resetTransform();
    const QRectF pillRect(8, viewport()->height() - 44,
                          barPx + 60, 36);
    painter->setBrush(QColor(255, 255, 255, 200));
    painter->setPen(QPen(QColor(0, 0, 0, 120), 1));
    painter->drawRoundedRect(pillRect, 6, 6);
    QPen barPen(QColor(20, 20, 20));
    barPen.setWidthF(2.0);
    barPen.setCosmetic(true);
    painter->setPen(barPen);
    const double barY = viewport()->height() - 24;
    const double barX0 = 20;
    const double barX1 = 20 + barPx;
    painter->drawLine(QPointF(barX0, barY), QPointF(barX1, barY));
    painter->drawLine(QPointF(barX0, barY - 4), QPointF(barX0, barY + 4));
    painter->drawLine(QPointF(barX1, barY - 4), QPointF(barX1, barY + 4));
    QFont f = painter->font();
    f.setPixelSize(11);
    painter->setFont(f);
    painter->drawText(QPointF(barX0, barY - 8), primary);
    painter->drawText(QPointF(barX0, barY + 14), secondary);
    painter->restore();
    (void)rect;
}

void MapView::refreshSelectionOverlay() {
    if (!selectionOverlay_) return;
    QList<QPolygonF> polys;

    // A ruler can render as multiple scene items (two line segments + a
    // rotated text label when displayDistance is on). Tagging every piece
    // with the same guid lets us union their scene bounding rects into a
    // single highlight instead of drawing one box per segment.
    QSet<QString> rulerGuidsSeen;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!it || it == selectionOverlay_) continue;
        if (isRulerItem(it)) {
            const QString guid = it->data(kBrickDataGuid).toString();
            // Empty guid → every ruler would share it. Fall back to the
            // single-item path so we highlight just the clicked piece.
            if (!guid.isEmpty()) {
                if (rulerGuidsSeen.contains(guid)) continue;
                rulerGuidsSeen.insert(guid);
                QRectF sbr;
                for (QGraphicsItem* any : scene()->items()) {
                    if (!isRulerItem(any)) continue;
                    if (any->data(kBrickDataGuid).toString() != guid) continue;
                    sbr = sbr.united(any->sceneBoundingRect());
                }
                if (sbr.width()  < 2.0) sbr.adjust(-3.0, 0.0, 3.0, 0.0);
                if (sbr.height() < 2.0) sbr.adjust(0.0, -3.0, 0.0, 3.0);
                if (!sbr.isEmpty()) polys.append(QPolygonF(sbr));
                continue;
            }
        }
        const QRectF local = it->boundingRect();
        const bool localThin = (local.width() < 1.0 || local.height() < 1.0);
        QPolygonF poly;
        if (!localThin && !local.isEmpty()) {
            // Solid rect / pixmap: map the local rect to scene to
            // preserve rotation for rotated bricks.
            poly = it->mapToScene(local);
        } else {
            // Thin items (lines, zero-height rect, etc.): scene-space
            // AABB inflated in whichever dimension is near zero.
            QRectF sbr = it->sceneBoundingRect();
            if (sbr.width()  < 2.0) sbr.adjust(-3.0, 0.0, 3.0, 0.0);
            if (sbr.height() < 2.0) sbr.adjust(0.0, -3.0, 0.0, 3.0);
            if (sbr.isEmpty()) sbr = QRectF(it->mapToScene(QPointF()) - QPointF(6, 6),
                                             QSizeF(12, 12));
            poly = QPolygonF(sbr);
        }
        polys.append(poly);
    }
    auto* ov = static_cast<SelectionOverlay*>(selectionOverlay_);
    ov->setSnapState(liveSnapActive_, liveSnapPointScene_);
    ov->setOutlines(std::move(polys));
}

}  // namespace cld::ui
