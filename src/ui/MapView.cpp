#include "MapView.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/Map.h"
#include "../edit/EditCommands.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"
#include "PartsBrowser.h"   // kPartMimeType

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QUndoStack>
#include <QUuid>
#include <QWheelEvent>

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

double studToPx() { return rendering::SceneBuilder::kPixelsPerStud; }

}

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
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);

    auto* scene = new QGraphicsScene(this);
    scene->setBackgroundBrush(QColor(100, 149, 237));
    setScene(scene);
    connect(scene, &QGraphicsScene::selectionChanged, this, &MapView::selectionChanged);

    builder_ = std::make_unique<rendering::SceneBuilder>(*scene, parts_);
    undoStack_ = std::make_unique<QUndoStack>(this);
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

    dragStart_.clear();
    if (!entries.empty()) {
        undoStack_->push(new edit::MoveBricksCommand(*map_, std::move(entries)));
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
    QGraphicsView::mousePressEvent(e);
    if (e->button() == Qt::LeftButton) captureDragStart();
}

void MapView::mouseMoveEvent(QMouseEvent* e) {
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
    const bool hasSel = !scene()->selectedItems().isEmpty();

    if (hasSel) {
        auto* ccw = menu.addAction(tr("Rotate 90° CCW"));
        connect(ccw, &QAction::triggered, [this]{ rotateSelected(-90.0f); });
        auto* cw = menu.addAction(tr("Rotate 90° CW"));
        connect(cw, &QAction::triggered, [this]{ rotateSelected(90.0f); });
        menu.addSeparator();
        auto* del = menu.addAction(tr("Delete"));
        del->setShortcut(Qt::Key_Delete);
        connect(del, &QAction::triggered, [this]{ deleteSelected(); });
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
    std::vector<edit::DeleteBricksCommand::Entry> entries;
    // Build per-layer lists first so we record the correct pre-deletion index.
    struct PerLayerHit { int li; QString guid; };
    std::vector<PerLayerHit> hits;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        hits.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                         it->data(kBrickDataGuid).toString() });
    }
    for (const auto& h : hits) {
        if (h.li < 0 || h.li >= static_cast<int>(map_->layers().size())) continue;
        auto* L = map_->layers()[h.li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        auto& BL = static_cast<core::LayerBrick&>(*L);
        for (int i = 0; i < static_cast<int>(BL.bricks.size()); ++i) {
            if (BL.bricks[i].guid == h.guid) {
                edit::DeleteBricksCommand::Entry e;
                e.layerIndex = h.li;
                e.indexInLayer = i;
                e.brick = BL.bricks[i];
                entries.push_back(std::move(e));
                break;
            }
        }
    }
    if (entries.empty()) return;
    undoStack_->push(new edit::DeleteBricksCommand(*map_, std::move(entries)));
    rebuildScene();
}

}
