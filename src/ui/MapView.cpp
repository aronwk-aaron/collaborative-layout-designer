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
#include "../edit/Connectivity.h"
#include "../edit/EditCommands.h"
#include "../edit/RulerCommands.h"
#include "../edit/LabelCommands.h"
#include "../edit/TextCommands.h"
#include "../edit/VenueCommands.h"
#include "../core/Venue.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"
#include "ConnectionSnap.h"
#include "MapViewInternal.h"
#include "SelectionOverlay.h"
#include "EditDialogs.h"
#include "ModuleLibraryPanel.h"   // kModuleDragMimeType
#include "PartsBrowser.h"         // kPartMimeType
#include "VenueDialog.h"
#include "../edit/ModuleCommands.h"
#include "../saveload/BbmReader.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
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

using detail::kBrickDataLayerIndex;
using detail::kBrickDataGuid;
using detail::kBrickDataKind;
using detail::isBrickItem;
using detail::isTextItem;
using detail::isRulerItem;
using detail::isLabelItem;
using detail::isVenueItem;
using detail::studToPx;

namespace {
constexpr double kMinZoom = 0.02;
constexpr double kMaxZoom = 40.0;
}  // namespace

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
    // No scrollbars: pan is middle-button drag. Visible scrollbars would
    // steal wheel events (breaking zoom when the cursor is over them) and
    // can take keyboard focus. The internal scroll-position machinery is
    // still used by mouseMoveEvent's pan code via scrollBar()->setValue().
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    horizontalScrollBar()->setFocusPolicy(Qt::NoFocus);
    verticalScrollBar()->setFocusPolicy(Qt::NoFocus);
    // Install ourselves as an event filter on the viewport. The filter
    // routes wheel events straight into our wheelEvent() and consumes
    // them so QAbstractScrollArea's base class never gets to pan the
    // viewport via the (invisible) scrollbars. Without this, trackpad
    // wheels ended up zooming AND panning vertically at the same time.
    viewport()->installEventFilter(this);

    auto* scene = new QGraphicsScene(this);
    scene->setBackgroundBrush(QColor(100, 149, 237));
    setScene(scene);

    // Selection overlay: a persistent scene item that paints outlines
    // around every selected brick. Lives with the highest z-value so it's
    // always on top of bricks/text/rulers.
    selectionOverlay_ = new SelectionOverlay();
    scene->addItem(selectionOverlay_);

    connect(scene, &QGraphicsScene::selectionChanged, this, [this]{
        // When the user clicks any single piece of a ruler (one line
        // segment or the midtext label), extend the scene selection to
        // every other piece sharing the same ruler guid. With unique
        // guids (restored by migrateNonNumericIds in 3.78) this scopes
        // to exactly one ruler. Needed so Qt's built-in multi-item drag
        // translates all pieces of the ruler rigidly when the user
        // drags any of them.
        static bool reentrant = false;
        if (!reentrant) {
            reentrant = true;
            QSet<QString> rulerGuids;
            for (QGraphicsItem* it : this->scene()->selectedItems()) {
                if (it && isRulerItem(it)) {
                    const QString g = it->data(kBrickDataGuid).toString();
                    if (!g.isEmpty()) rulerGuids.insert(g);
                }
            }
            if (!rulerGuids.isEmpty()) {
                for (QGraphicsItem* any : this->scene()->items()) {
                    if (!isRulerItem(any)) continue;
                    if (any->isSelected()) continue;
                    const QString g = any->data(kBrickDataGuid).toString();
                    if (g.isEmpty()) continue;
                    if (rulerGuids.contains(g)) any->setSelected(true);
                }
            }
            reentrant = false;
        }
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

    // No per-item live snap hook: connection snap runs as a single rigid
    // group shift in MapView::mouseMoveEvent after Qt has moved each item
    // by the drag delta. See applyLiveConnectionSnap().

    undoStack_ = std::make_unique<QUndoStack>(this);
    // Every undo / redo mutates core::Map; the scene items were built before
    // the mutation, so we need to rebuild the scene afterwards for the UI to
    // reflect the restored state. Without this, Ctrl+Z appears to do nothing.
    connect(undoStack_.get(), &QUndoStack::indexChanged, this, [this](int){
        if (!map_) return;
        // Recompute every brick's linkedToId against current world
        // positions before rebuilding the scene. Otherwise a brick moved
        // away from its partner keeps the old link, which makes the
        // connection appear "occupied" to snap + display.
        edit::rebuildConnectivity(*map_, parts_);
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

MapView::~MapView() {
    // Teardown order matters. QGraphicsView's base destructor will
    // destroy the scene, which deletes items individually. As each
    // selectable item is removed Qt fires selectionChanged on the
    // scene — our handler then walks scene->items() / setSelected on
    // survivors that may already be mid-destruction. Disconnect from
    // the scene signals BEFORE any of that runs.
    if (scene()) scene()->disconnect(this);
}

void MapView::loadMap(std::unique_ptr<core::Map> map) {
    undoStack_->clear();
    // Any in-flight drag snapshots reference QGraphicsItems in the
    // previous scene — builder_->clear() / build() below will delete
    // those. Drop the snapshots first so no mouse event re-enters with
    // dangling pointers.
    dragStart_.clear();
    rulerDragStart_.clear();
    labelDragStart_.clear();
    liveSnapActive_ = false;
    map_ = std::move(map);
    if (!map_) { builder_->clear(); return; }

    scene()->setBackgroundBrush(map_->backgroundColor.color);
    // Freshly-loaded .bbm may have stale linkedToId values (the file
    // stores the last known state, which can disagree with current world
    // positions after e.g. external edits). Rebuild the connection graph
    // from positions before first render so snap + dot markers are
    // correct from the start.
    edit::rebuildConnectivity(*map_, parts_);
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
    // SceneBuilder::build() deletes every QGraphicsItem* before
    // repopulating the scene. Any raw pointers we're holding in drag
    // snapshots (dragStart_ / rulerDragStart_ / labelDragStart_)
    // become dangling the moment we call build(). If a drag is in
    // progress when something triggers a rebuild (undo/redo, context
    // menu action, tool op) the next mouseMove will segfault on
    // s.item->scenePos(). Cancel any in-flight drag here so callers
    // don't have to remember. Mid-drag rebuild → drag aborts cleanly.
    dragStart_.clear();
    rulerDragStart_.clear();
    labelDragStart_.clear();
    liveSnapActive_ = false;
    builder_->build(*map_);
    viewport()->update();
    emit selectionChanged();
}

bool MapView::eventFilter(QObject* obj, QEvent* ev) {
    // Intercept wheel events on the viewport so they go through our
    // zoom-only wheelEvent() and never fall through to the base class
    // (which would pan via scrollbars). Returning true marks the event
    // as handled.
    if (obj == viewport() && ev->type() == QEvent::Wheel) {
        wheelEvent(static_cast<QWheelEvent*>(ev));
        return true;
    }
    return QGraphicsView::eventFilter(obj, ev);
}

void MapView::wheelEvent(QWheelEvent* e) {
    // The wheel is ZOOM ONLY. We always accept the event so
    // QAbstractScrollArea's base class doesn't fall through to
    // scrollbar-driven pan — that made horizontal trackpad gestures
    // and "wheel with no vertical delta" events slide the map around,
    // which the user never wants here. Middle-click drag is the only
    // pan mechanism.
    //
    // Step size: proportional to angleDelta (120 units per notch on a
    // classic mouse wheel; smaller numbers for high-res trackpads). The
    // base factor 1.0015^delta_y yields ~1.20× for a full notch —
    // noticeably gentler than a fixed 1.15× step, and fractional
    // increments feel continuous instead of twitchy.
    const double rawDelta = e->angleDelta().y();
    e->accept();   // swallow every wheel event regardless — no pan fallthrough
    if (rawDelta == 0.0) return;
    constexpr double kBase = 1.0015;
    // High-res trackpads (especially on macOS) can emit angle deltas
    // >1000 in a single event. 1.0015^1000 ≈ 4.5e6 — one wheel tick
    // blowing through the whole zoom range and yanking the anchored
    // scene point far off-screen. Cap the delta per event so no
    // single wheel event can zoom more than ~2× either direction.
    // Users still accumulate fast zoom across multiple events; we
    // just don't fly across the map in one frame.
    constexpr double kMaxAbsDeltaPerEvent = 480.0;   // ~2× at 1.0015 base
    const double clampedDelta = std::clamp(rawDelta,
                                           -kMaxAbsDeltaPerEvent,
                                            kMaxAbsDeltaPerEvent);
    const double step = std::pow(kBase, clampedDelta);
    const double current = transform().m11();
    const double next = std::clamp(current * step, kMinZoom, kMaxZoom);
    const double actualStep = next / current;
    if (std::abs(actualStep - 1.0) < 1e-5) return;
    scale(actualStep, actualStep);
}


// MapView drag mechanics (selectedBrickSnapshots, captureDragStart, snap
// computations, commitDragIfMoved) live in MapViewDrag.cpp. Clipboard
// ops live in MapViewClipboard.cpp.

void MapView::mousePressEvent(QMouseEvent* e) {
    lastMouseScenePos_ = mapToScene(e->pos());
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
    if (e->button() == Qt::LeftButton) {
        captureDragStart();
        // Master-brick snap anchor (BlueBrick-style): remember WHICH brick
        // was clicked and which of its connections was nearest. That one
        // connection is the sole snap lead for the whole drag.
        captureGrabAnchor(lastMouseScenePos_);
    }
}

void MapView::mouseMoveEvent(QMouseEvent* e) {
    lastMouseScenePos_ = mapToScene(e->pos());
    // While dragging a selected brick, cursor hint "drop here to delete" when
    // the pointer leaves the viewport (over any dock — typically the Parts
    // panel on the left). Restore on re-entry so in-scene dragging keeps the
    // normal arrow/hand cursor.
    if (!dragStart_.empty() && (e->buttons() & Qt::LeftButton)) {
        if (!viewport()->rect().contains(e->pos())) setCursor(Qt::ForbiddenCursor);
        else                                         unsetCursor();
    }

    // Live ruler tooltip: while dragging in one of the ruler tools,
    // show the current length (linear) or radius (circular) in the
    // status bar so the user knows how long the ruler will be before
    // they release.
    if (drawingRuler_ && (e->buttons() & Qt::LeftButton)) {
        const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
        const QPointF endScene = mapToScene(e->pos());
        const QPointF dStuds((endScene.x() - rulerStart_.x()) / pxPerStud,
                             (endScene.y() - rulerStart_.y()) / pxPerStud);
        const double lenStuds = std::hypot(dStuds.x(), dStuds.y());
        const double lenMm = lenStuds * 8.0;
        QString unit;
        if (lenMm >= 1000) unit = QStringLiteral("%1 m").arg(lenMm / 1000.0, 0, 'f', 2);
        else               unit = QStringLiteral("%1 mm").arg(lenMm, 0, 'f', 0);
        if (auto* mw = window())
            if (auto* sb = mw->findChild<QStatusBar*>())
                sb->showMessage(tool_ == Tool::DrawLinearRuler
                    ? tr("Ruler length: %1 studs  (%2)").arg(lenStuds, 0, 'f', 1).arg(unit)
                    : tr("Ruler radius: %1 studs  (%2)").arg(lenStuds, 0, 'f', 1).arg(unit),
                    1200);
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
    // After Qt has translated every selected movable item by the mouse
    // delta, run connection snap as a single rigid group shift. This
    // guarantees connections win over the per-item grid snap and keeps
    // a multi-brick group perfectly aligned while snapping.
    if (!dragStart_.empty() && (e->buttons() & Qt::LeftButton)) {
        applyLiveConnectionSnap();
    }
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
        clearGrabAnchor();
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
        clearGrabAnchor();
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
    const QPointF delta(dxStuds, dyStuds);
    std::vector<edit::MoveBricksCommand::Entry> brickEntries;

    // Nudge also moves selected rulers + labels. Collect the ruler guids
    // we've already handled so nudging a multi-piece ruler (seg1+seg2+
    // label) only fires one command.
    QSet<QString> rulerSeen;
    QSet<QString> labelSeen;
    struct RulerHit { int li; QString guid; };
    std::vector<RulerHit> rulerHits;
    QStringList labelIds;

    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (isBrickItem(it)) {
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
                e.afterTopLeft  = b.displayArea.topLeft() + delta;
                brickEntries.push_back(e);
                break;
            }
        } else if (isRulerItem(it)) {
            const QString guid = it->data(kBrickDataGuid).toString();
            if (guid.isEmpty() || rulerSeen.contains(guid)) continue;
            rulerSeen.insert(guid);
            rulerHits.push_back({ it->data(kBrickDataLayerIndex).toInt(), guid });
        } else if (isLabelItem(it)) {
            const QString guid = it->data(kBrickDataGuid).toString();
            if (guid.isEmpty() || labelSeen.contains(guid)) continue;
            labelSeen.insert(guid);
            labelIds.append(guid);
        }
    }
    if (brickEntries.empty() && rulerHits.empty() && labelIds.isEmpty()) return;

    undoStack_->beginMacro(tr("Move selection"));
    if (!brickEntries.empty()) {
        undoStack_->push(new edit::MoveBricksCommand(*map_, std::move(brickEntries)));
    }
    for (const auto& h : rulerHits) {
        undoStack_->push(new edit::MoveRulerItemCommand(*map_, h.li, h.guid, delta));
    }
    for (const QString& id : labelIds) {
        undoStack_->push(new edit::MoveAnchoredLabelCommand(*map_, id, delta));
    }
    undoStack_->endMacro();
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

    // Sets (PartKind::Group): the XML enumerates SubPartList children.
    // Expand the set into one brick per subpart placed at
    //   setCentre + rotatePoint(subpart.localPos, 0) in stud coords
    // and automatically wrap the new bricks in a sidecar Module so the
    // user can move the whole thing as a unit. All under a single undo
    // macro.
    {
        const auto meta = parts_.metadata(partKey);
        if (meta && meta->kind == parts::PartKind::Group && !meta->subparts.isEmpty()) {
            const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
            const QPointF centreStuds(sceneCenterPx.x() / pxPerStud,
                                        sceneCenterPx.y() / pxPerStud);
            undoStack_->beginMacro(tr("Place set %1").arg(partKey));
            std::vector<edit::CreateModuleCommand::Member> members;
            for (const auto& sp : meta->subparts) {
                auto subMeta = parts_.metadata(sp.subKey);
                double wStuds = 2.0, hStuds = 2.0;
                if (subMeta && !subMeta->gifFilePath.isEmpty()) {
                    QPixmap pm(subMeta->gifFilePath);
                    if (!pm.isNull()) {
                        wStuds = pm.width()  / pxPerStud;
                        hStuds = pm.height() / pxPerStud;
                    }
                }
                // BlueBrick convention (verified against MapData/
                // BrickLibrary.cs::readSubPartListTag + LayerBrickBrick.cs
                // ::init + updateConnectionPosition): sp.position is the
                // ROTATED HULL BBOX CENTER for that subpart in set-local
                // studs. Our render/connectivity code works on the IMAGE
                // BBOX CENTER (pixmap rotates around its own center and
                // conn world positions = displayArea.center + rotated
                // connXMLpos). For asymmetric hulls (curves, switches) at
                // off-axis rotations the image bbox center and hull bbox
                // center diverge — that's the mOffsetFromOriginalImage
                // BlueBrick applies in updateConnectionPosition. Computing
                // the same offset here and adding it to subCentre is what
                // makes the tracks line up properly when the set contains
                // rotated curves/switches.
                double orientDeg = sp.angleDegrees;
                orientDeg = std::fmod(orientDeg, 360.0);
                if (orientDeg >  180.0) orientDeg -= 360.0;
                if (orientDeg <= -180.0) orientDeg += 360.0;
                const QPointF mOffsetStuds = parts_.hullBboxOffsetStuds(
                    sp.subKey, orientDeg);
                const QPointF subCentre = centreStuds + sp.position + mOffsetStuds;
                core::Brick b;
                b.guid = core::newBbmId();
                b.partNumber = sp.subKey;
                b.displayArea = QRectF(subCentre.x() - wStuds / 2.0,
                                       subCentre.y() - hStuds / 2.0,
                                       wStuds, hStuds);
                b.orientation = static_cast<float>(orientDeg);
                members.push_back({ targetLayer, b.guid });
                undoStack_->push(new edit::AddBrickCommand(*map_, targetLayer, std::move(b)));
            }
            // Name the module after the set's description, falling back
            // to the set key. Creates the module as a single undoable
            // step so Ctrl+Z unwinds the whole placement.
            QString setName = partKey;
            for (const auto& d : meta->descriptions) {
                if (d.language == QStringLiteral("en")) { setName = d.text; break; }
            }
            if (!members.empty()) {
                undoStack_->push(new edit::CreateModuleCommand(
                    *map_, setName, std::move(members)));
            }
            undoStack_->endMacro();
            // Set files declare positions + angles but NOT connectivity.
            // Without this, every subpart has empty linkedToId, so the
            // tracks look "not connected" even though they're positioned
            // to abut one another. Rebuild from world positions so the
            // connection dots light up and snapping recognises the
            // already-joined track network.
            edit::rebuildConnectivity(*map_, parts_);
            rebuildScene();
            if (auto* mw = window())
                if (auto* sb = mw->findChild<QStatusBar*>())
                    sb->showMessage(tr("Placed set: %1 (%2 parts)")
                        .arg(setName).arg(meta->subparts.size()), 3000);
            return;
        }
    }

    QPixmap pm = parts_.pixmap(partKey);
    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    const double widthStuds  = pm.isNull() ? 2.0 : pm.width()  / pxPerStud;
    const double heightStuds = pm.isNull() ? 2.0 : pm.height() / pxPerStud;

    QPointF centreStuds(sceneCenterPx.x() / pxPerStud, sceneCenterPx.y() / pxPerStud);
    float   orientation = 0.0f;

    // Try connection snap at drop time (vanilla parity: dropping near an
    // existing compatible connection locks the new part to it). This runs
    // BEFORE grid snap so connections always take priority.
    const double threshold = connectionSnapThresholdStuds();
    auto snap = newPartPlacementSnap(*map_, parts_, partKey,
                                     centreStuds, orientation, threshold);
    bool connectionSnapped = false;
    if (snap.applied) {
        // New-part placement rotates to align with the connection.
        if (snap.newOrientation) {
            centreStuds += snap.rotationAlignedTranslationStuds;
            orientation = *snap.newOrientation;
        } else {
            centreStuds += snap.translationStuds;
        }
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


void MapView::setSnapStepStuds(double studs) {
    snapStepStuds_ = studs;
    // Propagate to the rendering side so item-level ItemPositionChange snaps
    // bricks live while dragging (in addition to commit-time snap on release).
    rendering::SceneBuilder::setLiveSnapStepStuds(studs);
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
        } else if (isLabelItem(under)) {
            // Anchored labels: quick text edit via input dialog.
            QString current;
            for (const auto& lbl : map_->sidecar.anchoredLabels) {
                if (lbl.id == guid) { current = lbl.text; break; }
            }
            bool ok = false;
            const QString next = QInputDialog::getText(
                this, tr("Edit label"), tr("Label text:"),
                QLineEdit::Normal, current, &ok);
            if (ok && next != current) {
                undoStack_->push(new edit::EditAnchoredLabelTextCommand(
                    *map_, guid, next));
                handled = true;
            }
        } else if (isVenueItem(under)) {
            // Open the venue-properties dialog; commit any changes
            // through SetVenueCommand.
            VenueDialog dlg(map_->sidecar.venue, this);
            if (dlg.exec() == QDialog::Accepted) {
                undoStack_->push(new edit::SetVenueCommand(*map_,
                    dlg.cleared() ? std::nullopt : dlg.result()));
                handled = true;
            }
        }
        if (handled || isBrickItem(under) || isRulerItem(under)
            || isTextItem(under) || isLabelItem(under) || isVenueItem(under)) {
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
    // Keyed hits for other types — bundled into one undo macro.
    struct TextHit  { int li; QString guid; };
    struct RulerHit { int li; QString guid; };
    std::vector<TextHit>  textHits;
    std::vector<RulerHit> rulerHits;
    QStringList labelIds;
    bool venueSelected = false;

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
        } else if (isRulerItem(it)) {
            rulerHits.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                                    it->data(kBrickDataGuid).toString() });
        } else if (isLabelItem(it)) {
            labelIds.append(it->data(kBrickDataGuid).toString());
        } else if (isVenueItem(it)) {
            venueSelected = true;
        }
    }
    if (brickEntries.empty() && textHits.empty() &&
        rulerHits.empty() && labelIds.isEmpty() && !venueSelected) return;

    undoStack_->beginMacro(tr("Delete selection"));
    if (!brickEntries.empty()) {
        undoStack_->push(new edit::DeleteBricksCommand(*map_, std::move(brickEntries)));
    }
    for (const auto& h : textHits) {
        undoStack_->push(new edit::DeleteTextCellCommand(*map_, h.li, h.guid));
    }
    for (const auto& h : rulerHits) {
        undoStack_->push(new edit::DeleteRulerItemCommand(*map_, h.li, h.guid));
    }
    for (const QString& id : labelIds) {
        undoStack_->push(new edit::DeleteAnchoredLabelCommand(*map_, id));
    }
    if (venueSelected) {
        const auto btn = QMessageBox::question(this, tr("Delete venue"),
            tr("Remove the entire venue (outline + obstacles)?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn == QMessageBox::Yes) {
            undoStack_->push(new edit::SetVenueCommand(*map_, std::nullopt));
        }
    }
    undoStack_->endMacro();
    // The undo-stack indexChanged handler rebuilds + preserves selection.
}

}
