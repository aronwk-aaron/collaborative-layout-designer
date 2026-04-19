#pragma once

#include "../core/Brick.h"

#include <QGraphicsView>
#include <QHash>
#include <QPointF>
#include <QString>

#include <memory>
#include <vector>

class QGraphicsItem;
class QUndoStack;

namespace cld::core    { class Map; }
namespace cld::parts   { class PartsLibrary; }
namespace cld::rendering { class SceneBuilder; }

namespace cld::ui {

class MapView : public QGraphicsView {
    Q_OBJECT
public:
    explicit MapView(parts::PartsLibrary& parts, QWidget* parent = nullptr);
    ~MapView() override;

    void loadMap(std::unique_ptr<core::Map> map);
    void rebuildScene();  // re-run SceneBuilder against current map (after edits)

    core::Map* currentMap() { return map_.get(); }
    rendering::SceneBuilder* builder() { return builder_.get(); }
    QUndoStack* undoStack() { return undoStack_.get(); }

    // Edit operations (invoked by menu/shortcut actions):
    void rotateSelected(float degrees);
    void deleteSelected();
    void addPartAtViewCenter(const QString& partKey);
    void addPartAtScenePos(const QString& partKey, QPointF scenePosPx);

    // Clipboard / selection ops.
    void copySelection();            // copy selected bricks to the internal clipboard
    void cutSelection();             // copy + delete
    void pasteClipboard();           // insert clipboard bricks with fresh guids + offset
    void duplicateSelection();       // copy + paste in one command
    void selectAll();
    void deselectAll();
    bool clipboardEmpty() const { return clipboard_.empty(); }

signals:
    void selectionChanged();

protected:
    void wheelEvent(QWheelEvent* e) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    struct BrickOriginSnapshot {
        QGraphicsItem* item = nullptr;
        int    layerIndex   = -1;
        QString guid;
        QPointF scenePosAtPress;      // item->pos() at mousePress
        QPointF studTopLeftAtPress;   // brick.displayArea.topLeft() at mousePress
    };

    void captureDragStart();
    void commitDragIfMoved();
    std::vector<BrickOriginSnapshot> selectedBrickSnapshots() const;

    parts::PartsLibrary& parts_;
    std::unique_ptr<core::Map> map_;
    std::unique_ptr<rendering::SceneBuilder> builder_;
    std::unique_ptr<QUndoStack> undoStack_;

    std::vector<BrickOriginSnapshot> dragStart_;

    // Middle-mouse pan state.
    bool    panning_ = false;
    QPoint  panAnchor_;

    // Internal (in-process) brick clipboard. Cross-process paste would require
    // serialising to QMimeData via the system clipboard; deferred until a user
    // actually needs it.
    std::vector<core::Brick> clipboard_;
};

}
