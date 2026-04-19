#pragma once

#include "../core/Brick.h"

#include <QColor>
#include <QGraphicsView>
#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QSet>
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

    // Snap step applied to brick drag-drop, in studs. 0 disables snapping.
    void  setSnapStepStuds(double studs);
    double snapStepStuds() const { return snapStepStuds_; }

    // Default rotation step in degrees — used by the menu Rotate CW/CCW
    // actions (the explicit rotateSelected(degrees) call still takes a
    // caller-supplied amount).
    void  setRotationStepDegrees(double deg) { rotationStepDegrees_ = deg; }
    double rotationStepDegrees() const { return rotationStepDegrees_; }

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

    void bringSelectionToFront();
    void sendSelectionToBack();

    // Grouping (same-layer vanilla groups — modules span layers).
    void groupSelection();
    void ungroupSelection();
    // Extend selection to every brick reachable via connection links from the
    // current selection (transitive closure over Connexion.linkedToId).
    void selectPath();

    // Text ops.
    void addTextAtViewCenter(const QString& text);
    void addTextAtScenePos(const QString& text, QPointF scenePosPx);
    // Prompt the user to edit the selected single text cell's content.
    void editSelectedTextContent();

    // Active tool — affects left-click behaviour on the map.
    enum class Tool { Select, PaintArea, EraseArea, DrawLinearRuler, DrawCircularRuler };
    void setTool(Tool t) { tool_ = t; }
    Tool tool() const { return tool_; }

    // Area paint state (read by the paint handler).
    void setPaintColor(QColor c) { paintColor_ = c; }
    QColor paintColor() const { return paintColor_; }

signals:
    void selectionChanged();

protected:
    void wheelEvent(QWheelEvent* e) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
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

    // Snap + rotation step config, updated by MainWindow from toolbar + QSettings.
    double snapStepStuds_       = 0.0;     // 0 disables snap
    double rotationStepDegrees_ = 90.0;

    Tool   tool_ = Tool::Select;
    QColor paintColor_ = QColor(0, 128, 0);
    // Track which cells we already painted in the current stroke so dragging
    // a single cell size across cells adds exactly one change per cell.
    QSet<QPoint> strokeCellsTouched_;

    // Ruler-draw state: scene pos of the first click-anchor when a ruler
    // tool is active. Released-position pairs with this to create the ruler.
    bool    drawingRuler_ = false;
    QPointF rulerStart_;
};

}
