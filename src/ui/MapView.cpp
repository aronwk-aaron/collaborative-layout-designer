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
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"
#include "PartsBrowser.h"   // kPartMimeType

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
#include <QScrollBar>
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

double studToPx() { return rendering::SceneBuilder::kPixelsPerStud; }

}

MapView::MapView(parts::PartsLibrary& parts, QWidget* parent)
    : QGraphicsView(parent), parts_(parts) {
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setAcceptDrops(true);   // accept part drags from the PartsBrowser panel
    // Full-viewport repaints avoid the "trails" behind rotated bricks that
    // SmartViewportUpdate leaves when the item's bounding rect in scene
    // coords changes more than its local rect signals.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    // Left-button drag: rubber-band select (item drag is still available via
    // ItemIsMovable on individual brick items). Middle-button drag: pan the
    // view (handled manually in mousePress/Move/Release).
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);

    auto* scene = new QGraphicsScene(this);
    scene->setBackgroundBrush(QColor(100, 149, 237));
    setScene(scene);
    connect(scene, &QGraphicsScene::selectionChanged, this, &MapView::selectionChanged);

    builder_ = std::make_unique<rendering::SceneBuilder>(*scene, parts_);
    undoStack_ = std::make_unique<QUndoStack>(this);
    // Every undo / redo mutates core::Map; the scene items were built before
    // the mutation, so we need to rebuild the scene afterwards for the UI to
    // reflect the restored state. Without this, Ctrl+Z appears to do nothing.
    connect(undoStack_.get(), &QUndoStack::indexChanged, this, [this](int){
        if (map_) {
            builder_->build(*map_);
            viewport()->update();
        }
    });
}

MapView::~MapView() = default;

void MapView::loadMap(std::unique_ptr<core::Map> map) {
    undoStack_->clear();
    map_ = std::move(map);
    if (!map_) { builder_->clear(); return; }

    scene()->setBackgroundBrush(map_->backgroundColor.color);
    builder_->build(*map_);
    if (!scene()->itemsBoundingRect().isEmpty()) {
        const auto r = scene()->itemsBoundingRect().adjusted(-50, -50, 50, 50);
        scene()->setSceneRect(r);
        fitInView(r, Qt::KeepAspectRatio);
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

struct ConnectionSnapResult {
    bool    applied = false;
    QPointF newCenter;            // in studs
    float   newOrientation = 0.0f;
};

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
    QPointF draggedCenter) {

    ConnectionSnapResult out;

    auto meta = lib.metadata(draggedPart);
    if (!meta || meta->connections.isEmpty()) return out;

    constexpr double kThresholdStuds = 4.0;
    double  bestDist = kThresholdStuds;
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
                    if (dc.type != tc.type || dc.type == 0) continue;
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

    if (bestDist >= kThresholdStuds) return out;

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

    std::vector<edit::MoveBricksCommand::Entry> entries;
    const double px = studToPx();

    for (const auto& s : dragStart_) {
        if (!s.item) continue;
        const QPointF newScenePos = s.item->scenePos();
        const QPointF delta = newScenePos - s.scenePosAtPress;
        if (std::abs(delta.x()) < 0.5 && std::abs(delta.y()) < 0.5) continue;

        edit::MoveBricksCommand::Entry e;
        e.ref.layerIndex = s.layerIndex;
        e.ref.guid = s.guid;
        e.beforeTopLeft = s.studTopLeftAtPress;
        e.afterTopLeft  = s.studTopLeftAtPress + QPointF(delta.x() / px, delta.y() / px);
        entries.push_back(e);
    }

    // Snap each brick's final top-left to the configured stud step.
    if (snapStepStuds_ > 0.0) {
        for (auto& e : entries) {
            e.afterTopLeft.setX(std::round(e.afterTopLeft.x() / snapStepStuds_) * snapStepStuds_);
            e.afterTopLeft.setY(std::round(e.afterTopLeft.y() / snapStepStuds_) * snapStepStuds_);
        }
    }

    // Connection snap (single-brick drag only — ambiguous for multi).
    std::optional<edit::RotateBricksCommand::Entry> connectionRotate;
    if (entries.size() == 1) {
        const auto& e = entries.front();
        // Locate the brick to pull its part number + current orientation.
        if (auto* L = map_->layers()[e.ref.layerIndex].get();
            L && L->kind() == core::LayerKind::Brick) {
            const auto& BL = static_cast<const core::LayerBrick&>(*L);
            for (const auto& b : BL.bricks) {
                if (b.guid != e.ref.guid) continue;
                const QPointF proposedCentre = e.afterTopLeft
                    + QPointF(b.displayArea.width() / 2.0, b.displayArea.height() / 2.0);
                QSet<QString> moving; moving.insert(e.ref.guid);
                auto snap = computeConnectionSnap(*map_, parts_, moving,
                                                   b.partNumber, b.orientation, proposedCentre);
                if (snap.applied) {
                    entries.front().afterTopLeft = snap.newCenter
                        - QPointF(b.displayArea.width() / 2.0, b.displayArea.height() / 2.0);
                    if (std::abs(snap.newOrientation - b.orientation) > 0.01f) {
                        edit::RotateBricksCommand::Entry re;
                        re.ref = e.ref;
                        re.beforeOrientation = b.orientation;
                        re.afterOrientation  = snap.newOrientation;
                        connectionRotate = re;
                    }
                }
                break;
            }
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
            L->guid = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
    if (e->button() == Qt::LeftButton) commitDragIfMoved();
}

void MapView::keyPressEvent(QKeyEvent* e) {
    if (!map_) { QGraphicsView::keyPressEvent(e); return; }
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
    QGraphicsView::keyPressEvent(e);
}

void MapView::rotateSelected(float degrees) {
    if (!map_) return;
    std::vector<edit::RotateBricksCommand::Entry> entries;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        const int li = it->data(kBrickDataLayerIndex).toInt();
        const QString guid = it->data(kBrickDataGuid).toString();
        if (li < 0 || li >= static_cast<int>(map_->layers().size())) continue;
        auto* L = map_->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
            if (b.guid == guid) {
                edit::RotateBricksCommand::Entry e;
                e.ref.layerIndex = li;
                e.ref.guid = guid;
                e.beforeOrientation = b.orientation;
                e.afterOrientation  = b.orientation + degrees;
                entries.push_back(e);
                break;
            }
        }
    }
    if (entries.empty()) return;
    undoStack_->push(new edit::RotateBricksCommand(*map_, std::move(entries)));
    rebuildScene();
}

void MapView::addPartAtViewCenter(const QString& partKey) {
    if (!map_) return;
    addPartAtScenePos(partKey, mapToScene(viewport()->rect().center()));
}

void MapView::addPartAtScenePos(const QString& partKey, QPointF sceneCenterPx) {
    if (!map_) return;

    // Pick the first brick layer as the add target. Later we can surface a
    // "current layer" selection in the layer panel.
    int targetLayer = -1;
    for (int i = 0; i < static_cast<int>(map_->layers().size()); ++i) {
        if (map_->layers()[i]->kind() == core::LayerKind::Brick) {
            targetLayer = i;
            break;
        }
    }
    if (targetLayer < 0) return;

    QPixmap pm = parts_.pixmap(partKey);
    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    const double widthStuds  = pm.isNull() ? 2.0 : pm.width()  / pxPerStud;
    const double heightStuds = pm.isNull() ? 2.0 : pm.height() / pxPerStud;

    const QPointF centreStuds(sceneCenterPx.x() / pxPerStud, sceneCenterPx.y() / pxPerStud);

    core::Brick b;
    b.guid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    b.partNumber = partKey;
    b.displayArea = QRectF(centreStuds.x() - widthStuds / 2.0,
                            centreStuds.y() - heightStuds / 2.0,
                            widthStuds, heightStuds);
    b.orientation = 0.0f;

    undoStack_->push(new edit::AddBrickCommand(*map_, targetLayer, std::move(b)));
    rebuildScene();
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
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        const int li = it->data(kBrickDataLayerIndex).toInt();
        const QString guid = it->data(kBrickDataGuid).toString();
        if (li < 0 || li >= static_cast<int>(map_->layers().size())) continue;
        auto* L = map_->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
            if (b.guid == guid) { clipboard_.push_back(b); break; }
        }
    }
}

void MapView::cutSelection() {
    copySelection();
    deleteSelected();
}

void MapView::pasteClipboard() {
    if (!map_ || clipboard_.empty()) return;
    int targetLayer = -1;
    for (int i = 0; i < static_cast<int>(map_->layers().size()); ++i) {
        if (map_->layers()[i]->kind() == core::LayerKind::Brick) { targetLayer = i; break; }
    }
    if (targetLayer < 0) return;

    // Place pasted bricks offset from their originals so they don't sit on
    // top of the source. Offset of 2 studs matches vanilla BlueBrick.
    constexpr double kPasteOffsetStuds = 2.0;
    std::vector<core::Brick> pasted;
    pasted.reserve(clipboard_.size());
    for (const auto& src : clipboard_) {
        core::Brick b = src;
        b.guid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        b.displayArea.translate(kPasteOffsetStuds, kPasteOffsetStuds);
        // Pasted brick keeps no previous group membership — modules are
        // tracked separately in the sidecar.
        b.myGroupId.clear();
        pasted.push_back(std::move(b));
    }
    undoStack_->push(new edit::AddBricksCommand(*map_, targetLayer, std::move(pasted)));
    rebuildScene();
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
    if (auto* under = itemAt(e->pos()); under && isTextItem(under)) {
        scene()->clearSelection();
        under->setSelected(true);
        editSelectedTextContent();
        e->accept();
        return;
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
        L->guid = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
    c.guid = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
    if (e->mimeData()->hasFormat(QString::fromLatin1(PartsBrowser::kPartMimeType))) {
        e->acceptProposedAction();
        return;
    }
    QGraphicsView::dragEnterEvent(e);
}

void MapView::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat(QString::fromLatin1(PartsBrowser::kPartMimeType))) {
        e->acceptProposedAction();
        return;
    }
    QGraphicsView::dragMoveEvent(e);
}

void MapView::dropEvent(QDropEvent* e) {
    const QString mime = QString::fromLatin1(PartsBrowser::kPartMimeType);
    if (!e->mimeData()->hasFormat(mime)) { QGraphicsView::dropEvent(e); return; }
    const QString key = QString::fromUtf8(e->mimeData()->data(mime));
    if (key.isEmpty()) { e->ignore(); return; }
    // Drop position is in viewport coords — convert to scene coords so the
    // brick lands exactly where the cursor released.
    const QPointF scenePos = mapToScene(e->position().toPoint());
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
