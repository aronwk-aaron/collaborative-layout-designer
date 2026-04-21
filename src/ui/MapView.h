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

class SelectionOverlay;

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
    void nudgeSelected(double dxStuds, double dyStuds);
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
    enum class Tool {
        Select, PaintArea, EraseArea, DrawLinearRuler, DrawCircularRuler,
        // Click points to build a venue outline polygon (first point,
        // subsequent points make edges). Right-click or Enter finishes;
        // Escape cancels. Replaces any existing outline.
        DrawVenueOutline,
        // Same interaction, but appends a new obstacle polygon to the
        // current venue (must already have an outline).
        DrawVenueObstacle,
    };
    void setTool(Tool t) { tool_ = t; }
    Tool tool() const { return tool_; }

    // Area paint state (read by the paint handler).
    void setPaintColor(QColor c) { paintColor_ = c; }
    QColor paintColor() const { return paintColor_; }

signals:
    void selectionChanged();
    // Emitted when the layer set changes shape (a new layer was added as
    // part of a paste across layers, an imported module created layers,
    // etc.) so MainWindow can refresh the LayerPanel.
    void layersChanged();

protected:
    void wheelEvent(QWheelEvent* e) override;
    // Viewport event filter — installed on viewport() so wheel events
    // funnel through our wheelEvent() only and never reach the
    // QAbstractScrollArea base class, which would otherwise pan the
    // viewport via (hidden) scrollbars.
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    // Draws the persistent scale-indicator bar in the lower-left of the
    // viewport after the rest of the scene. Using drawForeground keeps
    // the readout pinned to the viewport corner regardless of pan / zoom.
    void drawForeground(QPainter* painter, const QRectF& rect) override;
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

    // Ruler / label drag snapshots. One per logical item (rulers may
    // render as multiple scene items; we store the first piece's
    // scenePosAtPress to compute the drag delta).
    struct RulerDragSnapshot {
        QGraphicsItem* anyPiece = nullptr;
        int     layerIndex = -1;
        QString guid;
        QPointF scenePosAtPress;
    };
    struct LabelDragSnapshot {
        QGraphicsItem* item = nullptr;
        QString labelId;
        QPointF scenePosAtPress;
    };

    void captureDragStart();
    void commitDragIfMoved();
    // Run connection snap as a single rigid group shift during drag. Called
    // after QGraphicsView::mouseMoveEvent has translated every selected
    // item by the mouse delta. If a snap fires, translates all dragged
    // items by the snap shift so connections take priority over Qt's
    // built-in per-item drag and over grid snap.
    void applyLiveConnectionSnap();
    // Generous snap radius around free connection points. BlueBrick's
    // getMovedSnapPoint uses roughly 2 × grid step as the search range;
    // we use the larger of 16 studs (half a brick-unit wide) and 2 × grid
    // step so connections feel "sticky" even on a 1-stud grid.
    double connectionSnapThresholdStuds() const;
    std::vector<BrickOriginSnapshot> selectedBrickSnapshots() const;

    parts::PartsLibrary& parts_;
    std::unique_ptr<core::Map> map_;
    std::unique_ptr<rendering::SceneBuilder> builder_;
    std::unique_ptr<QUndoStack> undoStack_;

    std::vector<BrickOriginSnapshot> dragStart_;
    std::vector<RulerDragSnapshot>   rulerDragStart_;
    std::vector<LabelDragSnapshot>   labelDragStart_;

    // Middle-mouse pan state.
    bool    panning_ = false;
    QPoint  panAnchor_;

    // Mouse position in scene coords, updated on every press and move. The
    // connection-snap code reads this so the connection point closest to
    // the cursor — not to some arbitrary "anchor" brick — drives snapping
    // for multi-brick group drags.
    QPointF lastMouseScenePos_;

    // BlueBrick-style master-brick snap anchor. Set at mouse-press from
    // whichever brick the user grabbed and which of its connections was
    // closest to the click. Every live-drag snap frame aligns THIS ONE
    // connection to the nearest free compatible target, and the whole
    // selected group follows the shift. Cleared on mouseRelease.
    QString grabBrickGuid_;
    int     grabBrickLayerIndex_ = -1;
    int     grabActiveConnIdx_   = -1;   // index into part meta->connections
    void captureGrabAnchor(QPointF clickScenePos);
    void clearGrabAnchor();

    // Internal (in-process) brick clipboard. Cross-process paste would require
    // serialising to QMimeData via the system clipboard; deferred until a user
    // actually needs it.
    // Clipboard entries carry the source layer's name so paste can route
    // each brick back to a matching layer (creating one if necessary).
    // Copying a multi-layer selection and pasting into any map preserves
    // the layering — otherwise track pieces would flatten onto the first
    // brick layer along with scenery and everything looks broken.
    struct ClipEntry {
        QString     sourceLayerName;
        core::Brick brick;
    };
    std::vector<ClipEntry> clipboard_;

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

    // Venue-draw state: accumulated polygon vertices in scene-stud coords
    // while the user is clicking points in DrawVenueOutline /
    // DrawVenueObstacle mode. Finalised via right-click or Enter key,
    // cancelled via Escape.
    QVector<QPointF> venueDrawPoints_;
    // Preview item shown while drawing so the user sees what's being built.
    class QGraphicsPathItem* venueDrawPreview_ = nullptr;
    void finishVenueDraw();
    void updateVenueDrawPreview(QPointF hoverScenePos = {});

    // Live overlay item that paints outlines around every selected item.
    // Lives in the scene with a very high z-value so it's always on top.
    // Redrawn whenever scene()->selectionChanged fires.
    SelectionOverlay* selectionOverlay_ = nullptr;
    void refreshSelectionOverlay();

    // Set by the live connection-snap hook when the dragged brick is
    // currently locked to a connection; read by the overlay so the
    // selection outline renders in connection-snap colour to give the
    // user live feedback. Also stored in scene coords so the overlay can
    // draw a ring at the exact connection point.
    bool    liveSnapActive_ = false;
    QPointF liveSnapPointScene_;
};

}
