#include "MapView.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerArea.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"
#include "../core/Map.h"
#include "../core/TextCell.h"
#include "../edit/AreaCommands.h"
#include "../edit/EditCommands.h"
#include "../edit/RulerCommands.h"
#include "../edit/TextCommands.h"
#include "../edit/VenueCommands.h"
#include "../core/Venue.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"
#include "EditDialogs.h"
#include "ModuleLibraryPanel.h"   // kModuleDragMimeType
#include "PartsBrowser.h"         // kPartMimeType
#include "../edit/ModuleCommands.h"
#include "../saveload/BbmReader.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QFileInfo>
#include <QScrollBar>
#include <QStatusBar>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSet>
#include <QUndoStack>
#include <QUuid>
#include <QWheelEvent>

#include <cmath>

namespace cld::ui {

namespace {

constexpr double kMinZoom = 0.02;
constexpr double kMaxZoom = 40.0;

// Keep these in sync with SceneBuilder.cpp (anonymous namespace).
constexpr int kBrickDataLayerIndex = 0;
constexpr int kBrickDataGuid       = 1;
constexpr int kBrickDataKind       = 2;

bool isBrickItem(const QGraphicsItem* it) {
    return it && it->data(kBrickDataKind).toString() == QStringLiteral("brick");
}

bool isTextItem(const QGraphicsItem* it) {
    return it && it->data(kBrickDataKind).toString() == QStringLiteral("text");
}

bool isRulerItem(const QGraphicsItem* it) {
    return it && it->data(kBrickDataKind).toString() == QStringLiteral("ruler");
}

double studToPx() { return rendering::SceneBuilder::kPixelsPerStud; }

// Forward declarations for helpers defined further down so the MapView
// constructor (which hands a lambda into SceneBuilder::setLiveConnectionSnapHook)
// can refer to them.
struct ConnectionSnapResult {
    bool    applied = false;
    QPointF newCenter;
    float   newOrientation = 0.0f;
};
ConnectionSnapResult computeConnectionSnap(
    const core::Map& map, parts::PartsLibrary& lib,
    const QSet<QString>& movingGuids, const QString& draggedPart,
    float draggedOrientation, QPointF draggedCenter, double thresholdStuds);

}  // namespace

// Dedicated overlay item: a single scene item that paints the selection
// outline around every currently-selected brick/text/ruler. Living in the
// scene itself guarantees it actually renders — it's just another
// QGraphicsItem that Qt paints in its normal pass. Huge z-value so it's
// always on top of everything else. Updated from refreshSelectionOverlay()
// whenever scene()->selectionChanged fires.
class SelectionOverlay : public QGraphicsItem {
public:
    SelectionOverlay() {
        setZValue(1e9);
        setFlag(QGraphicsItem::ItemIsSelectable, false);
        setFlag(QGraphicsItem::ItemIsMovable,    false);
    }

    // Bounding rect tracks the union of outline polys so this item doesn't
    // inflate scene()->itemsBoundingRect() (which drives fitInView).
    QRectF boundingRect() const override { return bounds_; }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        if (polys_.isEmpty()) return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        // Normal selection = black + yellow double stroke.
        // Live-snapped selection = black + bright green, plus a big green
        // ring at the connection point so the user sees exactly where
        // the lock happened.
        const QColor inColor = snapActive_ ? QColor(80, 255, 120)
                                            : QColor(255, 215, 0);
        const QColor fillColor = snapActive_ ? QColor(80, 255, 120, 90)
                                              : QColor(255, 215, 0, 80);
        QPen outer(QColor(0, 0, 0, 230)); outer.setWidthF(5.0); outer.setCosmetic(true);
        outer.setJoinStyle(Qt::MiterJoin);
        QPen inner(inColor); inner.setWidthF(2.5); inner.setCosmetic(true);
        inner.setJoinStyle(Qt::MiterJoin);
        const QBrush fill(fillColor);

        for (const QPolygonF& poly : polys_) {
            p->setPen(outer); p->setBrush(Qt::NoBrush); p->drawPolygon(poly);
            p->setPen(inner); p->setBrush(fill);        p->drawPolygon(poly);
        }

        // Connection-lock ring at the snap point.
        if (snapActive_) {
            QPen ring(QColor(20, 180, 80)); ring.setWidthF(3.0); ring.setCosmetic(true);
            p->setPen(ring);
            p->setBrush(QColor(80, 255, 120, 80));
            p->drawEllipse(snapPoint_, 10.0, 10.0);
        }
        p->restore();
    }

    void setOutlines(QList<QPolygonF> polys) {
        prepareGeometryChange();
        polys_ = std::move(polys);
        QRectF total;
        for (const QPolygonF& poly : polys_) {
            total = total.united(poly.boundingRect());
        }
        if (snapActive_) total = total.united(QRectF(snapPoint_ - QPointF(12, 12), QSizeF(24, 24)));
        bounds_ = total.isEmpty() ? QRectF() : total.adjusted(-6, -6, 6, 6);
        update();
    }

    void setSnapState(bool active, QPointF snapPoint) {
        prepareGeometryChange();
        snapActive_ = active;
        snapPoint_  = snapPoint;
        update();
    }

private:
    QList<QPolygonF> polys_;
    QRectF bounds_;
    bool   snapActive_ = false;
    QPointF snapPoint_;
};

MapView::MapView(parts::PartsLibrary& parts, QWidget* parent)
    : QGraphicsView(parent), parts_(parts) {
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setAcceptDrops(true);   // accept part drags from the PartsBrowser panel
    // Left-button drag: rubber-band select (item drag is still available via
    // ItemIsMovable on individual brick items). Middle-button drag: pan the
    // view (handled manually in mousePress/Move/Release).
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    // Full-viewport repaints avoid the "trails" behind rotated bricks that
    // SmartViewportUpdate leaves when the item's bounding rect in scene
    // coords changes more than its local rect signals.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    auto* scene = new QGraphicsScene(this);
    scene->setBackgroundBrush(QColor(100, 149, 237));
    setScene(scene);

    // Selection overlay: a persistent scene item that paints outlines
    // around every selected brick. Lives with the highest z-value so it's
    // always on top of bricks/text/rulers.
    selectionOverlay_ = new SelectionOverlay();
    scene->addItem(selectionOverlay_);

    connect(scene, &QGraphicsScene::selectionChanged, this, [this]{
        refreshSelectionOverlay();
        emit selectionChanged();
    });
    // Also refresh the overlay whenever anything in the scene changes
    // geometry — this keeps the outline glued to the brick while the user
    // drags it. Qt debounces `changed` to once per paint cycle, so this
    // doesn't over-fire during live drag.
    connect(scene, &QGraphicsScene::changed, this, [this]{
        if (!this->scene()->selectedItems().isEmpty()) refreshSelectionOverlay();
    });

    builder_ = std::make_unique<rendering::SceneBuilder>(*scene, parts_);

    // Install the live connection-priority snap hook. Vanilla's
    // getMovedSnapPoint does the same thing on every mouse-move: find the
    // nearest free connection of matching type and, if close enough, snap
    // there instead of to the grid. We only handle position in the live
    // hook (drop-time rotation is done in commitDragIfMoved); this keeps
    // the live feel "magnetic" without the complexity of rotating an item
    // mid-drag.
    rendering::SceneBuilder::setLiveConnectionSnapHook(
        [this](QGraphicsItem* item, QPointF proposedScenePos) -> std::optional<QPointF> {
            if (!map_ || !item) { liveSnapActive_ = false; return std::nullopt; }
            const QString guid = item->data(kBrickDataGuid).toString();
            const int li       = item->data(kBrickDataLayerIndex).toInt();
            if (guid.isEmpty() || li < 0 ||
                li >= static_cast<int>(map_->layers().size())) { liveSnapActive_ = false; return std::nullopt; }
            auto* L = map_->layers()[li].get();
            if (!L || L->kind() != core::LayerKind::Brick) { liveSnapActive_ = false; return std::nullopt; }

            // Multi-select drag: only the "anchor" item (the one under the
            // mouse grab, which we approximate as the first item in the
            // dragStart snapshot) runs the connection search. The other
            // selected siblings translate by Qt's built-in multi-drag
            // delta, so the group moves rigidly AND can still snap to a
            // connection via the anchor. This matches BlueBrick's
            // mCurrentBrickUnderMouse-anchored getMovedSnapPoint flow.
            if (this->scene()->selectedItems().size() > 1) {
                if (dragStart_.empty() || dragStart_.front().item != item) {
                    return std::nullopt;
                }
            }

            const core::LayerBrick& BL = static_cast<const core::LayerBrick&>(*L);
            const core::Brick* b = nullptr;
            for (const auto& br : BL.bricks) if (br.guid == guid) { b = &br; break; }
            if (!b) { liveSnapActive_ = false; return std::nullopt; }

            const double px = rendering::SceneBuilder::kPixelsPerStud;
            const QPointF proposedCentreStuds(proposedScenePos.x() / px,
                                                proposedScenePos.y() / px);
            QSet<QString> moving; moving.insert(guid);
            const double threshold = std::max(4.0, snapStepStuds_);
            auto snap = computeConnectionSnap(*map_, parts_, moving,
                                              b->partNumber, b->orientation,
                                              proposedCentreStuds, threshold);
            if (!snap.applied) { liveSnapActive_ = false; return std::nullopt; }
            liveSnapActive_ = true;
            liveSnapPointScene_ = QPointF(snap.newCenter.x() * px, snap.newCenter.y() * px);
            viewport()->update();
            return liveSnapPointScene_;
        });

    undoStack_ = std::make_unique<QUndoStack>(this);
    // Every undo / redo mutates core::Map; the scene items were built before
    // the mutation, so we need to rebuild the scene afterwards for the UI to
    // reflect the restored state. Without this, Ctrl+Z appears to do nothing.
    connect(undoStack_.get(), &QUndoStack::indexChanged, this, [this](int){
        if (!map_) return;
        // Snapshot the currently-selected (layer, guid, kind) triples before
        // the rebuild wipes the scene so we can reselect the same logical
        // items on the rebuilt pixmaps. Without this, moving a brick
        // deselected it the instant the move committed.
        struct SelKey { int layer; QString guid; QString kind; };
        QList<SelKey> preserve;
        for (QGraphicsItem* it : this->scene()->selectedItems()) {
            if (!it) continue;
            const QString kind = it->data(kBrickDataKind).toString();
            if (kind.isEmpty()) continue;  // e.g. overlay item itself
            preserve.append({ it->data(kBrickDataLayerIndex).toInt(),
                              it->data(kBrickDataGuid).toString(), kind });
        }
        builder_->build(*map_);
        // Reselect by (layer, guid, kind) — builds a quick index of the new
        // items once so each lookup is O(1).
        if (!preserve.isEmpty()) {
            QHash<QString, QGraphicsItem*> byKey;
            for (QGraphicsItem* it : this->scene()->items()) {
                const QString kind = it->data(kBrickDataKind).toString();
                if (kind.isEmpty()) continue;
                byKey.insert(QString::number(it->data(kBrickDataLayerIndex).toInt())
                                 + QLatin1Char('|') + it->data(kBrickDataGuid).toString()
                                 + QLatin1Char('|') + kind,
                             it);
            }
            for (const auto& k : preserve) {
                const QString key = QString::number(k.layer) + QLatin1Char('|')
                                    + k.guid + QLatin1Char('|') + k.kind;
                if (auto* it = byKey.value(key)) it->setSelected(true);
            }
        }
        refreshSelectionOverlay();
        viewport()->update();
    });
}

MapView::~MapView() = default;

void MapView::loadMap(std::unique_ptr<core::Map> map) {
    undoStack_->clear();
    map_ = std::move(map);
    if (!map_) { builder_->clear(); return; }

    scene()->setBackgroundBrush(map_->backgroundColor.color);
    builder_->build(*map_);
    // Give the view a much bigger scene rect than the current content so
    // the user can pan well outside the existing bricks to add new ones
    // or extend the layout. ~50 000 px = ~6 250 studs on each side, which
    // is well beyond any realistic train-club layout. Without this the
    // sceneRect defaulted to the items bounding box and pan stopped at
    // the last brick.
    const QRectF content = scene()->itemsBoundingRect();
    const double kPadPx = 50000.0;
    const QRectF bigRect = content.isEmpty()
        ? QRectF(-kPadPx, -kPadPx, kPadPx * 2, kPadPx * 2)
        : content.adjusted(-kPadPx, -kPadPx, kPadPx, kPadPx);
    scene()->setSceneRect(bigRect);
    if (!content.isEmpty()) {
        fitInView(content.adjusted(-50, -50, 50, 50), Qt::KeepAspectRatio);
    }
    viewport()->update();
    emit selectionChanged();
}

void MapView::rebuildScene() {
    if (!map_) return;
    builder_->build(*map_);
    viewport()->update();
    emit selectionChanged();
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

void MapView::refreshSelectionOverlay() {
    if (!selectionOverlay_) return;
    QList<QPolygonF> polys;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!it || it == selectionOverlay_) continue;
        QRectF r = it->boundingRect();
        if (r.isEmpty()) r = it->sceneBoundingRect();
        QPolygonF poly = it->mapToScene(r);
        if (r.width() < 1.0 || r.height() < 1.0) {
            r = QRectF(it->scenePos(), QSizeF(0, 0)).adjusted(-6, -6, 6, 6);
            poly = QPolygonF(r);
        }
        polys.append(poly);
    }
    auto* ov = static_cast<SelectionOverlay*>(selectionOverlay_);
    ov->setSnapState(liveSnapActive_, liveSnapPointScene_);
    ov->setOutlines(std::move(polys));
}

std::vector<MapView::BrickOriginSnapshot> MapView::selectedBrickSnapshots() const {
    std::vector<BrickOriginSnapshot> out;
    if (!map_) return out;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        BrickOriginSnapshot s;
        s.item = it;
        s.layerIndex = it->data(kBrickDataLayerIndex).toInt();
        s.guid       = it->data(kBrickDataGuid).toString();
        s.scenePosAtPress = it->scenePos();
        // look up corresponding brick
        if (s.layerIndex >= 0 && s.layerIndex < static_cast<int>(map_->layers().size())) {
            auto* L = map_->layers()[s.layerIndex].get();
            if (L && L->kind() == core::LayerKind::Brick) {
                auto& BL = static_cast<core::LayerBrick&>(*L);
                for (const auto& b : BL.bricks) {
                    if (b.guid == s.guid) {
                        s.studTopLeftAtPress = b.displayArea.topLeft();
                        break;
                    }
                }
            }
        }
        out.push_back(s);
    }
    return out;
}

void MapView::captureDragStart() { dragStart_ = selectedBrickSnapshots(); }

namespace {

// Rotate a 2D point around the origin by `degrees` (clockwise to match Qt
// convention used by QGraphicsItem::setRotation).
QPointF rotatePoint(QPointF p, double degrees) {
    const double r = degrees * M_PI / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    return { p.x() * c - p.y() * s, p.x() * s + p.y() * c };
}

// (ConnectionSnapResult + declaration are forward-declared near the top
// of this file so MapView's constructor can refer to the helper.)

// Look for a connection point on any brick OTHER than those in `movingGuids`
// that's close (in world stud coords) to one of the dragged brick's
// connection points. If found, compute the orientation + centre that makes
// the two connections align (angles opposite, positions coincident).
// Threshold: 4 studs — close enough to feel intentional.
ConnectionSnapResult computeConnectionSnap(
    const core::Map& map,
    parts::PartsLibrary& lib,
    const QSet<QString>& movingGuids,
    const QString& draggedPart,
    float   draggedOrientation,
    QPointF draggedCenter,
    double  thresholdStuds) {

    ConnectionSnapResult out;

    auto meta = lib.metadata(draggedPart);
    if (!meta || meta->connections.isEmpty()) return out;

    double  bestDist = thresholdStuds;
    QPointF bestTargetWorld;
    double  bestTargetAngle = 0.0;
    QPointF bestDraggedLocal;
    double  bestDraggedLocalAngle = 0.0;

    // Static side: gather world connection points from all non-dragged bricks.
    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        const auto& bl = static_cast<const core::LayerBrick&>(*layerPtr);
        for (const auto& b : bl.bricks) {
            if (movingGuids.contains(b.guid)) continue;
            auto tmeta = lib.metadata(b.partNumber);
            if (!tmeta) continue;
            const QPointF tCentre = b.displayArea.center();
            for (const auto& tc : tmeta->connections) {
                const QPointF tWorldPos = tCentre + rotatePoint(tc.position, b.orientation);
                // For each dragged connection, measure distance at the
                // CURRENT dragged orientation so we only snap when the drop
                // lands near an existing connection — we don't pull a brick
                // from the far side of the layout.
                for (const auto& dc : meta->connections) {
                    if (dc.type != tc.type || dc.type.isEmpty()) continue;
                    const QPointF dWorldPos =
                        draggedCenter + rotatePoint(dc.position, draggedOrientation);
                    const QPointF diff = dWorldPos - tWorldPos;
                    const double dist = std::hypot(diff.x(), diff.y());
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestTargetWorld = tWorldPos;
                        bestTargetAngle = tc.angleDegrees + b.orientation;
                        bestDraggedLocal = dc.position;
                        bestDraggedLocalAngle = dc.angleDegrees;
                    }
                }
            }
        }
    }

    if (bestDist >= thresholdStuds) return out;

    // Align angles: dragged.localAngle + newOrient == target.worldAngle + 180.
    double newOrient = bestTargetAngle + 180.0 - bestDraggedLocalAngle;
    // Normalise to (-180, 180] for tidy storage.
    while (newOrient >  180.0) newOrient -= 360.0;
    while (newOrient <= -180.0) newOrient += 360.0;
    // Solve newCentre: (newCentre + rotate(localConn, newOrient)) == targetWorld.
    const QPointF newCentre = bestTargetWorld - rotatePoint(bestDraggedLocal, newOrient);

    out.applied = true;
    out.newCenter = newCentre;
    out.newOrientation = static_cast<float>(newOrient);
    return out;
}

}

void MapView::commitDragIfMoved() {
    if (dragStart_.empty() || !map_) return;

    const double px = studToPx();

    // Group-level delta: Qt's multi-drag translates every selected movable
    // item by the same vector, so the group moves rigidly. We derive the
    // drag delta from whichever entry the user grabbed (treating the first
    // as the anchor) and apply the same delta to every entry — no per-
    // brick divergence. This matches BlueBrick's behaviour: dragging a
    // selection moves it as a whole, and any snap (connection or grid)
    // applies to the anchor's position with the same translation
    // replicated across the group.
    std::vector<edit::MoveBricksCommand::Entry> entries;
    QPointF groupDelta(0, 0);
    bool haveDelta = false;
    for (const auto& s : dragStart_) {
        if (!s.item) continue;
        if (!haveDelta) {
            const QPointF d = s.item->scenePos() - s.scenePosAtPress;
            if (std::abs(d.x()) < 0.5 && std::abs(d.y()) < 0.5) { dragStart_.clear(); return; }
            groupDelta = QPointF(d.x() / px, d.y() / px);
            haveDelta = true;
        }
        edit::MoveBricksCommand::Entry e;
        e.ref.layerIndex = s.layerIndex;
        e.ref.guid = s.guid;
        e.beforeTopLeft = s.studTopLeftAtPress;
        e.afterTopLeft  = s.studTopLeftAtPress + groupDelta;
        entries.push_back(e);
    }
    if (entries.empty()) { dragStart_.clear(); return; }

    // Connection snap (group-aware). Using the first entry as the anchor,
    // we look for a nearby compatible connection. If one fires, compute
    // the extra translation needed to align the anchor's connection and
    // apply that same extra to EVERY entry so the group keeps its shape.
    bool connectionSnapped = false;
    std::optional<edit::RotateBricksCommand::Entry> connectionRotate;
    {
        const auto& anchor = entries.front();
        if (auto* L = map_->layers()[anchor.ref.layerIndex].get();
            L && L->kind() == core::LayerKind::Brick) {
            const auto& BL = static_cast<const core::LayerBrick&>(*L);
            for (const auto& b : BL.bricks) {
                if (b.guid != anchor.ref.guid) continue;
                const QPointF proposedCentre = anchor.afterTopLeft
                    + QPointF(b.displayArea.width() / 2.0, b.displayArea.height() / 2.0);
                QSet<QString> moving;
                for (const auto& e : entries) moving.insert(e.ref.guid);
                const double threshold = std::max(4.0, snapStepStuds_);
                auto snap = computeConnectionSnap(*map_, parts_, moving,
                                                   b.partNumber, b.orientation,
                                                   proposedCentre, threshold);
                if (snap.applied) {
                    const QPointF anchorNewTopLeft = snap.newCenter
                        - QPointF(b.displayArea.width() / 2.0, b.displayArea.height() / 2.0);
                    const QPointF extra = anchorNewTopLeft - anchor.afterTopLeft;
                    for (auto& e : entries) e.afterTopLeft += extra;
                    // Rotation is only applied for single-brick drags.
                    // Group rotation would need to pivot around the
                    // anchor brick's connection point and rotate every
                    // sibling; skipped for now in the interest of
                    // predictable group translation.
                    if (entries.size() == 1 && std::abs(snap.newOrientation - b.orientation) > 0.01f) {
                        edit::RotateBricksCommand::Entry re;
                        re.ref = anchor.ref;
                        re.beforeOrientation = b.orientation;
                        re.afterOrientation  = snap.newOrientation;
                        connectionRotate = re;
                    }
                    connectionSnapped = true;
                }
                break;
            }
        }
    }

    // Grid snap (group-aware): snap the anchor's top-left to the grid and
    // translate every entry by the same extra delta. Preserves the
    // group's relative positions. Only applies when connection snap
    // didn't fire.
    if (snapStepStuds_ > 0.0 && !connectionSnapped) {
        const QPointF tl = entries.front().afterTopLeft;
        const QPointF snapped(std::round(tl.x() / snapStepStuds_) * snapStepStuds_,
                              std::round(tl.y() / snapStepStuds_) * snapStepStuds_);
        const QPointF extra = snapped - tl;
        if (!extra.isNull()) {
            for (auto& e : entries) e.afterTopLeft += extra;
        }
    }

    dragStart_.clear();
    if (!entries.empty()) {
        if (connectionRotate) {
            undoStack_->beginMacro(tr("Snap to connection"));
            undoStack_->push(new edit::MoveBricksCommand(*map_, std::move(entries)));
            undoStack_->push(new edit::RotateBricksCommand(*map_, { *connectionRotate }));
            undoStack_->endMacro();
        } else {
            undoStack_->push(new edit::MoveBricksCommand(*map_, std::move(entries)));
        }
    }

    if (auto* mw = window())
        if (auto* sb = mw->findChild<QStatusBar*>())
            sb->showMessage(connectionSnapped ? tr("Connection snap")
                                              : tr("Moved"), 1500);
}

void MapView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        panAnchor_ = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    // Ruler draw tools: record press position; create the ruler on release.
    if (e->button() == Qt::LeftButton && map_ &&
        (tool_ == Tool::DrawLinearRuler || tool_ == Tool::DrawCircularRuler)) {
        drawingRuler_ = true;
        rulerStart_ = mapToScene(e->pos());
        e->accept();
        return;
    }

    // Venue outline / obstacle drawing: each left-click adds a vertex.
    // Right-click (or Enter) finishes the polygon. Escape cancels.
    if (e->button() == Qt::LeftButton && map_ &&
        (tool_ == Tool::DrawVenueOutline || tool_ == Tool::DrawVenueObstacle)) {
        const double px = rendering::SceneBuilder::kPixelsPerStud;
        const QPointF scenePos = mapToScene(e->pos());
        venueDrawPoints_.append(QPointF(scenePos.x() / px, scenePos.y() / px));
        updateVenueDrawPreview();
        e->accept();
        return;
    }
    if (e->button() == Qt::RightButton && map_ &&
        (tool_ == Tool::DrawVenueOutline || tool_ == Tool::DrawVenueObstacle)) {
        finishVenueDraw();
        e->accept();
        return;
    }

    // Paint / erase: swallow the event so the graphics view doesn't start a
    // rubber band or drag, and stamp the cell under the cursor.
    if (e->button() == Qt::LeftButton && map_ &&
        (tool_ == Tool::PaintArea || tool_ == Tool::EraseArea)) {
        strokeCellsTouched_.clear();
        // Find top-most visible area layer; paint there.
        int targetLayer = -1;
        core::LayerArea* target = nullptr;
        for (int i = static_cast<int>(map_->layers().size()) - 1; i >= 0; --i) {
            auto* L = map_->layers()[i].get();
            if (L && L->kind() == core::LayerKind::Area && L->visible) {
                targetLayer = i;
                target = static_cast<core::LayerArea*>(L);
                break;
            }
        }
        if (target) {
            const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
            const QPointF sp = mapToScene(e->pos());
            const double cell = std::max(1, target->areaCellSizeInStud) * pxPerStud;
            const int cx = static_cast<int>(std::floor(sp.x() / cell));
            const int cy = static_cast<int>(std::floor(sp.y() / cell));
            strokeCellsTouched_.insert(QPoint(cx, cy));
            std::vector<edit::PaintAreaCellsCommand::Change> chg;
            chg.push_back({ cx, cy,
                tool_ == Tool::PaintArea ? std::optional<QColor>(paintColor_)
                                          : std::nullopt });
            undoStack_->push(new edit::PaintAreaCellsCommand(*map_, targetLayer, std::move(chg)));
            rebuildScene();
        }
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
    if (e->button() == Qt::LeftButton) captureDragStart();
}

void MapView::mouseMoveEvent(QMouseEvent* e) {
    // While dragging a selected brick, cursor hint "drop here to delete" when
    // the pointer leaves the viewport (over any dock — typically the Parts
    // panel on the left). Restore on re-entry so in-scene dragging keeps the
    // normal arrow/hand cursor.
    if (!dragStart_.empty() && (e->buttons() & Qt::LeftButton)) {
        if (!viewport()->rect().contains(e->pos())) setCursor(Qt::ForbiddenCursor);
        else                                         unsetCursor();
    }

    // Continue a paint/erase stroke as long as the left button is held down
    // and we're in a paint tool. Group each cell change as its own command so
    // Ctrl+Z rolls back one cell at a time (matches vanilla BlueBrick's
    // per-click behaviour).
    if (map_ && (tool_ == Tool::PaintArea || tool_ == Tool::EraseArea)
        && (e->buttons() & Qt::LeftButton)) {
        int targetLayer = -1;
        core::LayerArea* target = nullptr;
        for (int i = static_cast<int>(map_->layers().size()) - 1; i >= 0; --i) {
            auto* L = map_->layers()[i].get();
            if (L && L->kind() == core::LayerKind::Area && L->visible) {
                targetLayer = i; target = static_cast<core::LayerArea*>(L); break;
            }
        }
        if (target) {
            const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
            const QPointF sp = mapToScene(e->pos());
            const double cell = std::max(1, target->areaCellSizeInStud) * pxPerStud;
            const int cx = static_cast<int>(std::floor(sp.x() / cell));
            const int cy = static_cast<int>(std::floor(sp.y() / cell));
            const QPoint k(cx, cy);
            if (!strokeCellsTouched_.contains(k)) {
                strokeCellsTouched_.insert(k);
                std::vector<edit::PaintAreaCellsCommand::Change> chg;
                chg.push_back({ cx, cy,
                    tool_ == Tool::PaintArea ? std::optional<QColor>(paintColor_)
                                              : std::nullopt });
                undoStack_->push(new edit::PaintAreaCellsCommand(*map_, targetLayer, std::move(chg)));
                rebuildScene();
            }
        }
        e->accept();
        return;
    }
    if (panning_ && (e->buttons() & Qt::MiddleButton)) {
        const QPoint delta = e->pos() - panAnchor_;
        panAnchor_ = e->pos();
        // Scroll by the delta — negated because scrolling right *shows* the
        // left side, i.e. moves scene content the opposite way of the cursor.
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void MapView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        unsetCursor();
        e->accept();
        return;
    }
    // "Drag out to delete": if the user started a drag in Select mode and
    // released outside the map viewport (typically over the Parts panel or
    // any other dock), treat it as a delete rather than a move.
    if (e->button() == Qt::LeftButton && !dragStart_.empty()
        && !viewport()->rect().contains(e->pos())) {
        dragStart_.clear();
        QGraphicsView::mouseReleaseEvent(e);
        deleteSelected();
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && drawingRuler_ && map_) {
        drawingRuler_ = false;
        const QPointF endScene = mapToScene(e->pos());
        const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
        // Find or implicitly create a ruler layer.
        int targetLayer = -1;
        for (int i = 0; i < static_cast<int>(map_->layers().size()); ++i) {
            if (map_->layers()[i]->kind() == core::LayerKind::Ruler) { targetLayer = i; break; }
        }
        if (targetLayer < 0) {
            auto L = std::make_unique<core::LayerRuler>();
            L->guid = core::newBbmId();
            L->name = tr("Rulers");
            map_->layers().push_back(std::move(L));
            targetLayer = static_cast<int>(map_->layers().size()) - 1;
        }

        core::LayerRuler::AnyRuler any;
        const QPointF p1(rulerStart_.x() / pxPerStud, rulerStart_.y() / pxPerStud);
        const QPointF p2(endScene.x()    / pxPerStud, endScene.y()    / pxPerStud);

        if (tool_ == Tool::DrawLinearRuler) {
            any.kind = core::RulerKind::Linear;
            any.linear.point1 = p1;
            any.linear.point2 = p2;
            const QPointF tl(std::min(p1.x(), p2.x()), std::min(p1.y(), p2.y()));
            const QPointF br(std::max(p1.x(), p2.x()), std::max(p1.y(), p2.y()));
            any.linear.displayArea = QRectF(tl, br);
            any.linear.color = core::ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
            any.linear.lineThickness = 1.0f;
            any.linear.displayDistance = true;
            any.linear.displayUnit = true;
        } else {
            any.kind = core::RulerKind::Circular;
            any.circular.center = p1;
            const QPointF d = p2 - p1;
            const double r = std::hypot(d.x(), d.y());
            any.circular.radius = static_cast<float>(r);
            any.circular.displayArea = QRectF(p1.x() - r, p1.y() - r, 2 * r, 2 * r);
            any.circular.color = core::ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
            any.circular.lineThickness = 1.0f;
            any.circular.displayDistance = true;
            any.circular.displayUnit = true;
        }
        undoStack_->push(new edit::AddRulerItemCommand(*map_, targetLayer, std::move(any)));
        rebuildScene();
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton) {
        commitDragIfMoved();
        // Drag is done; clear the "live snap active" indicator so the
        // selection outline returns to its normal yellow colour.
        liveSnapActive_ = false;
        refreshSelectionOverlay();
    }
}

void MapView::updateVenueDrawPreview(QPointF /*hoverScenePos*/) {
    // Draw / refresh the in-progress polygon as a dashed outline so the
    // user sees what they're building. Persistent scene item; replaced on
    // every click.
    if (venueDrawPreview_) {
        scene()->removeItem(venueDrawPreview_);
        delete venueDrawPreview_;
        venueDrawPreview_ = nullptr;
    }
    if (venueDrawPoints_.isEmpty()) return;
    const double px = rendering::SceneBuilder::kPixelsPerStud;
    QPainterPath path;
    path.moveTo(venueDrawPoints_.first() * px);
    for (int i = 1; i < venueDrawPoints_.size(); ++i) path.lineTo(venueDrawPoints_[i] * px);
    venueDrawPreview_ = new QGraphicsPathItem(path);
    QPen pen(QColor(230, 40, 40));
    pen.setWidthF(2.0); pen.setCosmetic(true); pen.setStyle(Qt::DashLine);
    venueDrawPreview_->setPen(pen);
    venueDrawPreview_->setZValue(1e8);
    scene()->addItem(venueDrawPreview_);
}

void MapView::finishVenueDraw() {
    if (!map_) { venueDrawPoints_.clear(); return; }
    if (venueDrawPoints_.size() < 3) {
        venueDrawPoints_.clear();
        updateVenueDrawPreview();
        if (auto* mw = window())
            if (auto* sb = mw->findChild<QStatusBar*>())
                sb->showMessage(tr("Venue polygon needs at least 3 points"), 2500);
        return;
    }

    core::Venue v = map_->sidecar.venue.value_or(core::Venue{});
    v.enabled = true;

    if (tool_ == Tool::DrawVenueOutline) {
        // Replace outline: one VenueEdge per polygon side (closed loop).
        v.edges.clear();
        for (int i = 0; i < venueDrawPoints_.size(); ++i) {
            const QPointF a = venueDrawPoints_[i];
            const QPointF b = venueDrawPoints_[(i + 1) % venueDrawPoints_.size()];
            core::VenueEdge e;
            e.polyline = { a, b };
            e.kind = core::EdgeKind::Wall;
            v.edges.push_back(e);
        }
    } else {   // DrawVenueObstacle
        core::VenueObstacle ob;
        ob.polygon = venueDrawPoints_;
        v.obstacles.push_back(ob);
    }

    undoStack_->push(new edit::SetVenueCommand(*map_, std::make_optional(v)));
    venueDrawPoints_.clear();
    updateVenueDrawPreview();

    // Drop back to Select so normal editing resumes after a polygon lands.
    tool_ = Tool::Select;

    if (auto* mw = window())
        if (auto* sb = mw->findChild<QStatusBar*>())
            sb->showMessage(tr("Venue updated"), 2000);
}

void MapView::keyPressEvent(QKeyEvent* e) {
    if (!map_) { QGraphicsView::keyPressEvent(e); return; }
    // Venue-draw commit/cancel keys.
    if (tool_ == Tool::DrawVenueOutline || tool_ == Tool::DrawVenueObstacle) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            finishVenueDraw();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            venueDrawPoints_.clear();
            updateVenueDrawPreview();
            tool_ = Tool::Select;
            e->accept();
            return;
        }
    }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelected();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_R) {
        rotateSelected(e->modifiers().testFlag(Qt::ShiftModifier) ? -90.0f : 90.0f);
        e->accept();
        return;
    }
    // Arrow keys: nudge selection by the current snap step (1 stud default).
    if (e->key() == Qt::Key_Left  || e->key() == Qt::Key_Right ||
        e->key() == Qt::Key_Up    || e->key() == Qt::Key_Down) {
        const double step = snapStepStuds_ > 0.0 ? snapStepStuds_ : 1.0;
        double dx = 0.0, dy = 0.0;
        if (e->key() == Qt::Key_Left)  dx = -step;
        if (e->key() == Qt::Key_Right) dx =  step;
        if (e->key() == Qt::Key_Up)    dy = -step;
        if (e->key() == Qt::Key_Down)  dy =  step;
        nudgeSelected(dx, dy);
        e->accept();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void MapView::nudgeSelected(double dxStuds, double dyStuds) {
    if (!map_ || (dxStuds == 0.0 && dyStuds == 0.0)) return;
    std::vector<edit::MoveBricksCommand::Entry> entries;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        const int li = it->data(kBrickDataLayerIndex).toInt();
        const QString guid = it->data(kBrickDataGuid).toString();
        if (li < 0 || li >= static_cast<int>(map_->layers().size())) continue;
        auto* L = map_->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
            if (b.guid != guid) continue;
            edit::MoveBricksCommand::Entry e;
            e.ref.layerIndex = li;
            e.ref.guid = guid;
            e.beforeTopLeft = b.displayArea.topLeft();
            e.afterTopLeft  = b.displayArea.topLeft() + QPointF(dxStuds, dyStuds);
            entries.push_back(e);
            break;
        }
    }
    if (entries.empty()) return;
    // The undoStack's indexChanged handler rebuilds the scene and preserves
    // the current selection — we don't need to call rebuildScene() ourselves,
    // and doing so would wipe the selection the handler just restored.
    undoStack_->push(new edit::MoveBricksCommand(*map_, std::move(entries)));
}

void MapView::rotateSelected(float degrees) {
    if (!map_) return;
    // Collect every selected brick's current state so we can compute the
    // group centroid. Single-brick rotation is a special case where the
    // centroid IS the brick's centre, so the position update is a no-op.
    struct Hit { int li; QString guid; QPointF centre; QSizeF size; float orientation; };
    std::vector<Hit> hits;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        const int li = it->data(kBrickDataLayerIndex).toInt();
        const QString guid = it->data(kBrickDataGuid).toString();
        if (li < 0 || li >= static_cast<int>(map_->layers().size())) continue;
        auto* L = map_->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
            if (b.guid != guid) continue;
            hits.push_back({ li, guid, b.displayArea.center(),
                             b.displayArea.size(), b.orientation });
            break;
        }
    }
    if (hits.empty()) return;

    // Centroid in stud coords. For multi-selection this is the pivot we
    // rotate around; for single selection it equals the brick's centre
    // so movement-delta is zero.
    QPointF pivot(0, 0);
    for (const auto& h : hits) pivot += h.centre;
    pivot /= hits.size();

    const double rad = degrees * M_PI / 180.0;
    const double c = std::cos(rad), s = std::sin(rad);

    std::vector<edit::MoveBricksCommand::Entry>    moves;
    std::vector<edit::RotateBricksCommand::Entry>  rotates;
    for (const auto& h : hits) {
        edit::RotateBricksCommand::Entry r;
        r.ref.layerIndex = h.li;
        r.ref.guid = h.guid;
        r.beforeOrientation = h.orientation;
        r.afterOrientation  = h.orientation + degrees;
        rotates.push_back(r);

        const QPointF rel = h.centre - pivot;
        const QPointF rotated(rel.x() * c - rel.y() * s, rel.x() * s + rel.y() * c);
        const QPointF newCentre = pivot + rotated;
        const QPointF delta = newCentre - h.centre;
        if (std::abs(delta.x()) > 1e-6 || std::abs(delta.y()) > 1e-6) {
            edit::MoveBricksCommand::Entry m;
            m.ref.layerIndex = h.li;
            m.ref.guid = h.guid;
            m.beforeTopLeft = h.centre - QPointF(h.size.width() / 2.0, h.size.height() / 2.0);
            m.afterTopLeft  = newCentre - QPointF(h.size.width() / 2.0, h.size.height() / 2.0);
            moves.push_back(m);
        }
    }

    if (moves.empty()) {
        undoStack_->push(new edit::RotateBricksCommand(*map_, std::move(rotates)));
    } else {
        undoStack_->beginMacro(tr("Rotate %1°").arg(degrees, 0, 'f', 1));
        undoStack_->push(new edit::MoveBricksCommand(*map_, std::move(moves)));
        undoStack_->push(new edit::RotateBricksCommand(*map_, std::move(rotates)));
        undoStack_->endMacro();
    }
    // Selection is preserved automatically by the undoStack indexChanged
    // handler, which rebuilds the scene + reselects every item by guid.
}

void MapView::addPartAtViewCenter(const QString& partKey) {
    if (!map_) return;
    addPartAtScenePos(partKey, mapToScene(viewport()->rect().center()));
}

void MapView::addPartAtScenePos(const QString& partKey, QPointF sceneCenterPx) {
    if (!map_) return;

    // Use the active (selected) layer if it's a brick layer — matches
    // BlueBrick's selectedLayerIndex-driven placement. Fall back to the
    // first brick layer if the active one is a different type.
    int targetLayer = -1;
    if (map_->selectedLayerIndex >= 0 &&
        map_->selectedLayerIndex < static_cast<int>(map_->layers().size()) &&
        map_->layers()[map_->selectedLayerIndex]->kind() == core::LayerKind::Brick) {
        targetLayer = map_->selectedLayerIndex;
    } else {
        for (int i = 0; i < static_cast<int>(map_->layers().size()); ++i) {
            if (map_->layers()[i]->kind() == core::LayerKind::Brick) {
                targetLayer = i;
                break;
            }
        }
    }
    if (targetLayer < 0) return;

    QPixmap pm = parts_.pixmap(partKey);
    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    const double widthStuds  = pm.isNull() ? 2.0 : pm.width()  / pxPerStud;
    const double heightStuds = pm.isNull() ? 2.0 : pm.height() / pxPerStud;

    QPointF centreStuds(sceneCenterPx.x() / pxPerStud, sceneCenterPx.y() / pxPerStud);
    float   orientation = 0.0f;

    // Try connection snap at drop time (vanilla parity: dropping near an
    // existing compatible connection locks the new part to it). This runs
    // BEFORE grid snap so connections always take priority.
    QSet<QString> moving;  // nothing is moving; just searches other bricks
    const double threshold = std::max(4.0, snapStepStuds_);
    auto snap = computeConnectionSnap(*map_, parts_, moving,
                                       partKey, orientation,
                                       centreStuds, threshold);
    bool connectionSnapped = false;
    if (snap.applied) {
        centreStuds = snap.newCenter;
        orientation = snap.newOrientation;
        connectionSnapped = true;
    } else if (snapStepStuds_ > 0.0) {
        // Grid snap the TOP-LEFT (matches drag-commit semantics), then
        // recompute centre from snapped top-left.
        QPointF topLeft(centreStuds.x() - widthStuds / 2.0,
                        centreStuds.y() - heightStuds / 2.0);
        topLeft.setX(std::round(topLeft.x() / snapStepStuds_) * snapStepStuds_);
        topLeft.setY(std::round(topLeft.y() / snapStepStuds_) * snapStepStuds_);
        centreStuds = topLeft + QPointF(widthStuds / 2.0, heightStuds / 2.0);
    }

    core::Brick b;
    b.guid = core::newBbmId();
    b.partNumber = partKey;
    b.displayArea = QRectF(centreStuds.x() - widthStuds / 2.0,
                            centreStuds.y() - heightStuds / 2.0,
                            widthStuds, heightStuds);
    b.orientation = orientation;

    undoStack_->push(new edit::AddBrickCommand(*map_, targetLayer, std::move(b)));
    rebuildScene();

    if (auto* mw = window())
        if (auto* sb = mw->findChild<QStatusBar*>())
            sb->showMessage(
                connectionSnapped
                    ? tr("Connection snap: %1").arg(partKey)
                    : tr("Placed: %1").arg(partKey), 2000);
}

void MapView::contextMenuEvent(QContextMenuEvent* e) {
    // If the right-click happens on an item that is not part of the existing
    // selection, clear the selection and select that one item so the menu's
    // actions act on what the user clicked.
    if (auto* under = itemAt(e->pos())) {
        if (!under->isSelected()) {
            scene()->clearSelection();
            under->setSelected(true);
        }
    }

    QMenu menu(this);
    const auto sel = scene()->selectedItems();
    const bool hasSel = !sel.isEmpty();
    // Categorise: is the selection pure-text, pure-brick, or mixed.
    int brickCount = 0, textCount = 0;
    for (QGraphicsItem* it : sel) {
        if (isBrickItem(it)) ++brickCount;
        else if (isTextItem(it)) ++textCount;
    }
    const bool onlyText  = textCount > 0 && brickCount == 0;
    const bool onlyBrick = brickCount > 0 && textCount == 0;
    const bool singleText = onlyText && sel.size() == 1;

    if (hasSel) {
        // Properties... opens the type-appropriate dialog for a single item.
        if (sel.size() == 1) {
            QGraphicsItem* only = sel.front();
            const int li = only->data(kBrickDataLayerIndex).toInt();
            const QString guid = only->data(kBrickDataGuid).toString();
            if (isBrickItem(only)) {
                auto* prop = menu.addAction(tr("Properties..."));
                connect(prop, &QAction::triggered, [this, li, guid]{
                    if (editBrickDialog(this, *map_, li, guid, parts_, *undoStack_))
                        rebuildScene();
                });
            } else if (isRulerItem(only)) {
                auto* prop = menu.addAction(tr("Properties..."));
                connect(prop, &QAction::triggered, [this, li, guid]{
                    if (editRulerDialog(this, *map_, li, guid, *undoStack_))
                        rebuildScene();
                });
            } else if (isTextItem(only)) {
                auto* prop = menu.addAction(tr("Properties..."));
                connect(prop, &QAction::triggered, [this, li, guid]{
                    if (editTextDialog(this, *map_, li, guid, *undoStack_))
                        rebuildScene();
                });
            }
        }
        if (singleText) {
            auto* edit = menu.addAction(tr("Edit Text..."));
            connect(edit, &QAction::triggered, [this]{ editSelectedTextContent(); });
        }

        auto* ccw = menu.addAction(tr("Rotate CCW"));
        connect(ccw, &QAction::triggered,
                [this]{ rotateSelected(static_cast<float>(-rotationStepDegrees_)); });
        auto* cw = menu.addAction(tr("Rotate CW"));
        connect(cw, &QAction::triggered,
                [this]{ rotateSelected(static_cast<float>(rotationStepDegrees_)); });
        menu.addSeparator();

        if (onlyBrick) {
            auto* bringFront = menu.addAction(tr("Bring to Front"));
            connect(bringFront, &QAction::triggered, [this]{ bringSelectionToFront(); });
            auto* sendBack = menu.addAction(tr("Send to Back"));
            connect(sendBack, &QAction::triggered, [this]{ sendSelectionToBack(); });
            menu.addSeparator();

            if (sel.size() >= 2) {
                auto* grp = menu.addAction(tr("Group"));
                connect(grp, &QAction::triggered, [this]{ groupSelection(); });
            }
            auto* ungrp = menu.addAction(tr("Ungroup"));
            connect(ungrp, &QAction::triggered, [this]{ ungroupSelection(); });
            auto* selPath = menu.addAction(tr("Select Path"));
            connect(selPath, &QAction::triggered, [this]{ selectPath(); });
            menu.addSeparator();

            auto* cut = menu.addAction(tr("Cut"));
            connect(cut, &QAction::triggered, [this]{ cutSelection(); });
            auto* copy = menu.addAction(tr("Copy"));
            connect(copy, &QAction::triggered, [this]{ copySelection(); });
            auto* dup = menu.addAction(tr("Duplicate"));
            connect(dup, &QAction::triggered, [this]{ duplicateSelection(); });
            menu.addSeparator();
        }

        auto* del = menu.addAction(tr("Delete"));
        del->setShortcut(Qt::Key_Delete);
        connect(del, &QAction::triggered, [this]{ deleteSelected(); });
        menu.addSeparator();
    } else {
        // Empty-area menu: offer paste and quick-add actions tied to the
        // current cursor position.
        if (!clipboard_.empty()) {
            auto* paste = menu.addAction(tr("Paste"));
            connect(paste, &QAction::triggered, [this]{ pasteClipboard(); });
            menu.addSeparator();
        }
        const QPointF scenePos = mapToScene(e->pos());
        auto* addText = menu.addAction(tr("Add Text Here..."));
        connect(addText, &QAction::triggered, [this, scenePos]{
            if (!map_) return;
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, tr("Add text"), tr("Label text:"),
                QLineEdit::Normal, {}, &ok);
            if (!ok || text.isEmpty()) return;
            addTextAtScenePos(text, scenePos);
        });
        menu.addSeparator();
    }

    auto* undo = menu.addAction(tr("Undo"));
    undo->setEnabled(undoStack_->canUndo());
    connect(undo, &QAction::triggered, undoStack_.get(), &QUndoStack::undo);
    auto* redo = menu.addAction(tr("Redo"));
    redo->setEnabled(undoStack_->canRedo());
    connect(redo, &QAction::triggered, undoStack_.get(), &QUndoStack::redo);

    menu.exec(e->globalPos());
    e->accept();
}

void MapView::setSnapStepStuds(double studs) {
    snapStepStuds_ = studs;
    // Propagate to the rendering side so item-level ItemPositionChange snaps
    // bricks live while dragging (in addition to commit-time snap on release).
    rendering::SceneBuilder::setLiveSnapStepStuds(studs);
}

void MapView::copySelection() {
    clipboard_.clear();
    if (!map_) return;
    // scene->selectedItems() returns items in no particular order. If we
    // copied in that order, pasting would scramble back-to-front
    // z-ordering within the source layer. Instead collect the selected
    // guids and walk each brick layer's vector in order — that preserves
    // the within-layer z-order (earlier in the vector = further back)
    // across copy + paste.
    QSet<QString> selectedGuids;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (isBrickItem(it)) selectedGuids.insert(it->data(kBrickDataGuid).toString());
    }
    if (selectedGuids.isEmpty()) return;
    for (const auto& L : map_->layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
            if (selectedGuids.contains(b.guid)) {
                clipboard_.push_back({ L->name, b });
            }
        }
    }
}

void MapView::cutSelection() {
    copySelection();
    deleteSelected();
}

void MapView::pasteClipboard() {
    if (!map_ || clipboard_.empty()) return;

    // Compute the clipboard group's own centre (stud coords) across every
    // entry regardless of source layer, so the paste group stays rigid.
    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    QPoint viewPos = viewport()->mapFromGlobal(QCursor::pos());
    QPointF targetSceneCentrePx;
    if (viewport()->rect().contains(viewPos)) {
        targetSceneCentrePx = mapToScene(viewPos);
    } else {
        targetSceneCentrePx = mapToScene(viewport()->rect().center());
    }
    const QPointF targetCentreStuds(targetSceneCentrePx.x() / pxPerStud,
                                     targetSceneCentrePx.y() / pxPerStud);
    QPointF srcCentre;
    for (const auto& src : clipboard_) srcCentre += src.brick.displayArea.center();
    srcCentre /= clipboard_.size();
    const QPointF translation = targetCentreStuds - srcCentre;

    // Group clipboard entries by source layer name so each group lands on
    // a matching layer in the current map (creating one if none exists).
    // Preserves the original layering — a multi-layer copy pastes back as
    // a multi-layer set, not flattened to a single layer.
    QHash<QString, std::vector<core::Brick>> byLayer;
    QStringList layerOrder;    // stable iteration order
    QSet<QString> newGuids;
    for (const auto& src : clipboard_) {
        core::Brick b = src.brick;
        b.guid = core::newBbmId();
        newGuids.insert(b.guid);
        b.displayArea.translate(translation);
        b.myGroupId.clear();
        const QString key = src.sourceLayerName.isEmpty()
            ? QStringLiteral("Bricks") : src.sourceLayerName;
        if (!byLayer.contains(key)) layerOrder << key;
        byLayer[key].push_back(std::move(b));
    }

    auto findOrCreateLayer = [this](const QString& name) -> int {
        for (int i = 0; i < static_cast<int>(map_->layers().size()); ++i) {
            auto* L = map_->layers()[i].get();
            if (L && L->kind() == core::LayerKind::Brick && L->name == name) return i;
        }
        // No match — create a new brick layer with that name.
        auto L = std::make_unique<core::LayerBrick>();
        L->guid = core::newBbmId();
        L->name = name.isEmpty() ? QStringLiteral("Bricks") : name;
        const int idx = static_cast<int>(map_->layers().size());
        map_->layers().push_back(std::move(L));
        return idx;
    };

    undoStack_->beginMacro(tr("Paste (%1 bricks across %2 layer(s))")
                               .arg(clipboard_.size()).arg(layerOrder.size()));
    for (const QString& name : layerOrder) {
        const int li = findOrCreateLayer(name);
        if (li < 0) continue;
        auto& bricks = byLayer[name];
        undoStack_->push(new edit::AddBricksCommand(*map_, li, std::move(bricks)));
    }
    undoStack_->endMacro();
    // endMacro fires undoStack.indexChanged, which rebuilds the scene AND
    // restores prior selection by guid. We then clear that and re-select
    // only the freshly-pasted bricks — the indexChanged handler's
    // re-selection is a no-op in practice (nothing was selected before)
    // but we do it explicitly here so "pasted bricks are active" UX
    // survives any future restore path.
    emit layersChanged();
    scene()->clearSelection();
    for (QGraphicsItem* it : scene()->items()) {
        if (!isBrickItem(it)) continue;
        if (newGuids.contains(it->data(kBrickDataGuid).toString())) {
            it->setSelected(true);
        }
    }
}

void MapView::duplicateSelection() {
    copySelection();
    pasteClipboard();
}

void MapView::selectAll() {
    for (QGraphicsItem* it : scene()->items()) {
        if (isBrickItem(it)) it->setSelected(true);
    }
}

void MapView::deselectAll() {
    scene()->clearSelection();
}

void MapView::bringSelectionToFront() {
    if (!map_) return;
    std::vector<edit::ReorderBricksCommand::Target> targets;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        targets.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                            it->data(kBrickDataGuid).toString() });
    }
    if (targets.empty()) return;
    undoStack_->push(new edit::ReorderBricksCommand(
        *map_, std::move(targets), edit::ReorderBricksCommand::ToFront));
    rebuildScene();
}

void MapView::sendSelectionToBack() {
    if (!map_) return;
    std::vector<edit::ReorderBricksCommand::Target> targets;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        targets.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                            it->data(kBrickDataGuid).toString() });
    }
    if (targets.empty()) return;
    undoStack_->push(new edit::ReorderBricksCommand(
        *map_, std::move(targets), edit::ReorderBricksCommand::ToBack));
    rebuildScene();
}

void MapView::groupSelection() {
    if (!map_) return;
    std::vector<edit::BrickRef> targets;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        targets.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                            it->data(kBrickDataGuid).toString() });
    }
    if (targets.size() < 2) return;   // nothing to group
    undoStack_->push(new edit::GroupBricksCommand(*map_, std::move(targets)));
}

void MapView::ungroupSelection() {
    if (!map_) return;
    std::vector<edit::BrickRef> targets;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        targets.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                            it->data(kBrickDataGuid).toString() });
    }
    if (targets.empty()) return;
    undoStack_->push(new edit::UngroupBricksCommand(*map_, std::move(targets)));
}

void MapView::selectPath() {
    if (!map_) return;
    // Build a guid -> (layerIndex, QGraphicsItem*, Brick*) index, plus the
    // starting frontier from the current selection.
    QHash<QString, QGraphicsItem*> itemByGuid;
    QHash<QString, core::Brick*>   brickByGuid;
    QSet<QString> toVisit;
    for (int li = 0; li < static_cast<int>(map_->layers().size()); ++li) {
        auto* L = map_->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
            brickByGuid.insert(b.guid, &b);
        }
    }
    for (QGraphicsItem* it : scene()->items()) {
        if (!isBrickItem(it)) continue;
        const QString guid = it->data(kBrickDataGuid).toString();
        itemByGuid.insert(guid, it);
    }
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        toVisit.insert(it->data(kBrickDataGuid).toString());
    }
    if (toVisit.isEmpty()) return;

    // Transitive BFS over each brick's Connexion.linkedToId. LinkedToId points
    // at the partner's *connection point* guid — not the brick guid — so we
    // build a reverse index: connPointGuid -> brickGuid.
    QHash<QString, QString> brickGuidForConnGuid;
    for (auto it = brickByGuid.constBegin(); it != brickByGuid.constEnd(); ++it) {
        for (const auto& c : it.value()->connections) {
            brickGuidForConnGuid.insert(c.guid, it.key());
        }
    }

    QSet<QString> visited = toVisit;
    while (!toVisit.isEmpty()) {
        const QString cur = *toVisit.constBegin();
        toVisit.remove(cur);
        auto bit = brickByGuid.constFind(cur);
        if (bit == brickByGuid.constEnd()) continue;
        for (const auto& c : bit.value()->connections) {
            if (c.linkedToId.isEmpty()) continue;
            const QString neighbourBrick = brickGuidForConnGuid.value(c.linkedToId);
            if (neighbourBrick.isEmpty() || visited.contains(neighbourBrick)) continue;
            visited.insert(neighbourBrick);
            toVisit.insert(neighbourBrick);
        }
    }
    for (const QString& guid : visited) {
        if (auto* it = itemByGuid.value(guid)) it->setSelected(true);
    }
}

void MapView::editSelectedTextContent() {
    if (!map_) return;
    QGraphicsItem* sel = nullptr;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (isTextItem(it)) { sel = it; break; }
    }
    if (!sel) return;
    const int li = sel->data(kBrickDataLayerIndex).toInt();
    const QString guid = sel->data(kBrickDataGuid).toString();
    if (li < 0 || li >= static_cast<int>(map_->layers().size())) return;
    auto* L = map_->layers()[li].get();
    if (!L || L->kind() != core::LayerKind::Text) return;
    auto& TL = static_cast<core::LayerText&>(*L);
    QString current;
    for (const auto& c : TL.textCells) if (c.guid == guid) { current = c.text; break; }
    bool ok = false;
    const QString next = QInputDialog::getMultiLineText(
        this, tr("Edit text"), tr("Label text:"), current, &ok);
    if (!ok || next == current) return;
    undoStack_->push(new edit::EditTextCellTextCommand(*map_, li, guid, next));
    rebuildScene();
}

void MapView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (auto* under = itemAt(e->pos())) {
        scene()->clearSelection();
        under->setSelected(true);
        const int li = under->data(kBrickDataLayerIndex).toInt();
        const QString guid = under->data(kBrickDataGuid).toString();
        bool handled = false;
        if (isBrickItem(under)) {
            handled = editBrickDialog(this, *map_, li, guid, parts_, *undoStack_);
        } else if (isRulerItem(under)) {
            handled = editRulerDialog(this, *map_, li, guid, *undoStack_);
        } else if (isTextItem(under)) {
            handled = editTextDialog(this, *map_, li, guid, *undoStack_);
        }
        if (handled) rebuildScene();
        if (handled || isBrickItem(under) || isRulerItem(under) || isTextItem(under)) {
            e->accept();
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(e);
}

void MapView::addTextAtViewCenter(const QString& text) {
    if (!map_) return;
    addTextAtScenePos(text, mapToScene(viewport()->rect().center()));
}

void MapView::addTextAtScenePos(const QString& text, QPointF sceneCenterPx) {
    if (!map_ || text.isEmpty()) return;
    // Pick the first text layer; create one if none exists so calling this on
    // a layout that doesn't have a text layer still works.
    int targetLayer = -1;
    for (int i = 0; i < static_cast<int>(map_->layers().size()); ++i) {
        if (map_->layers()[i]->kind() == core::LayerKind::Text) { targetLayer = i; break; }
    }
    if (targetLayer < 0) {
        auto L = std::make_unique<core::LayerText>();
        L->guid = core::newBbmId();
        L->name = tr("Labels");
        map_->layers().push_back(std::move(L));
        targetLayer = static_cast<int>(map_->layers().size()) - 1;
    }

    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    // Default label box: sized so the fit-to-box renderer shows the text at
    // a readable height. Width scales with character count, height is a
    // fixed line height in studs.
    const double heightStuds = 10.0;
    const double widthStuds  = std::max(heightStuds * 0.6 * text.size(), heightStuds * 2.0);

    core::TextCell c;
    c.guid = core::newBbmId();
    c.text = text;
    c.orientation = 0.0f;
    c.fontColor = core::ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
    c.font.familyName = QStringLiteral("Arial");
    c.font.sizePt = 12.0f;
    c.font.styleString = QStringLiteral("Regular");
    c.alignment = core::TextAlignment::Center;
    c.displayArea = QRectF(sceneCenterPx.x() / pxPerStud - widthStuds / 2.0,
                            sceneCenterPx.y() / pxPerStud - heightStuds / 2.0,
                            widthStuds, heightStuds);
    undoStack_->push(new edit::AddTextCellCommand(*map_, targetLayer, std::move(c)));
    rebuildScene();
}

void MapView::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(QString::fromLatin1(PartsBrowser::kPartMimeType)) ||
        e->mimeData()->hasFormat(QString::fromLatin1(kModuleDragMimeType))) {
        e->acceptProposedAction();
        return;
    }
    QGraphicsView::dragEnterEvent(e);
}

void MapView::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat(QString::fromLatin1(PartsBrowser::kPartMimeType)) ||
        e->mimeData()->hasFormat(QString::fromLatin1(kModuleDragMimeType))) {
        e->acceptProposedAction();
        return;
    }
    QGraphicsView::dragMoveEvent(e);
}

void MapView::dropEvent(QDropEvent* e) {
    const QString partMime   = QString::fromLatin1(PartsBrowser::kPartMimeType);
    const QString moduleMime = QString::fromLatin1(kModuleDragMimeType);
    const QPointF scenePos = mapToScene(e->position().toPoint());

    if (e->mimeData()->hasFormat(moduleMime)) {
        const QString bbmPath = QString::fromUtf8(e->mimeData()->data(moduleMime));
        if (bbmPath.isEmpty()) { e->ignore(); return; }
        auto res = saveload::readBbm(bbmPath);
        if (!res.ok()) { e->ignore(); return; }
        const double px = rendering::SceneBuilder::kPixelsPerStud;

        // Build per-layer batches (preserves the module's z-order /
        // layering so tracks don't land on top of scenery) and translate
        // every batch's bricks so the module's centroid lands at the
        // drop position.
        std::vector<edit::ImportBbmAsModuleCommand::LayerBatch> batches;
        QPointF srcCentre; int count = 0;
        for (const auto& L : res.map->layers()) {
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            edit::ImportBbmAsModuleCommand::LayerBatch batch;
            batch.layerName = L->name.isEmpty() ? QStringLiteral("Module") : L->name;
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
                srcCentre += b.displayArea.center(); ++count;
                core::Brick copy = b;
                copy.guid.clear();
                batch.bricks.push_back(std::move(copy));
            }
            if (!batch.bricks.empty()) batches.push_back(std::move(batch));
        }
        if (batches.empty() || count == 0) { e->ignore(); return; }
        srcCentre /= count;
        const QPointF targetCentre(scenePos.x() / px, scenePos.y() / px);
        const QPointF translation = targetCentre - srcCentre;
        for (auto& batch : batches)
            for (auto& b : batch.bricks) b.displayArea.translate(translation);

        const QString name = QFileInfo(bbmPath).completeBaseName();
        undoStack_->push(new edit::ImportBbmAsModuleCommand(
            *map_, bbmPath, name, std::move(batches)));
        rebuildScene();
        e->acceptProposedAction();
        return;
    }

    if (!e->mimeData()->hasFormat(partMime)) { QGraphicsView::dropEvent(e); return; }
    const QString key = QString::fromUtf8(e->mimeData()->data(partMime));
    if (key.isEmpty()) { e->ignore(); return; }
    addPartAtScenePos(key, scenePos);
    e->acceptProposedAction();
}

void MapView::deleteSelected() {
    if (!map_) return;
    std::vector<edit::DeleteBricksCommand::Entry> brickEntries;
    // (layerIndex, text guid) hits for text cells — deleted with one command
    // each inside a single macro so undo collapses the whole deletion.
    struct TextHit { int li; QString guid; };
    std::vector<TextHit> textHits;

    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (isBrickItem(it)) {
            const int li = it->data(kBrickDataLayerIndex).toInt();
            const QString guid = it->data(kBrickDataGuid).toString();
            if (li < 0 || li >= static_cast<int>(map_->layers().size())) continue;
            auto* L = map_->layers()[li].get();
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            auto& BL = static_cast<core::LayerBrick&>(*L);
            for (int i = 0; i < static_cast<int>(BL.bricks.size()); ++i) {
                if (BL.bricks[i].guid == guid) {
                    edit::DeleteBricksCommand::Entry e;
                    e.layerIndex = li;
                    e.indexInLayer = i;
                    e.brick = BL.bricks[i];
                    brickEntries.push_back(std::move(e));
                    break;
                }
            }
        } else if (isTextItem(it)) {
            textHits.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                                  it->data(kBrickDataGuid).toString() });
        }
    }
    if (brickEntries.empty() && textHits.empty()) return;

    undoStack_->beginMacro(tr("Delete selection"));
    if (!brickEntries.empty()) {
        undoStack_->push(new edit::DeleteBricksCommand(*map_, std::move(brickEntries)));
    }
    for (const auto& h : textHits) {
        undoStack_->push(new edit::DeleteTextCellCommand(*map_, h.li, h.guid));
    }
    undoStack_->endMacro();
    rebuildScene();
}

}
