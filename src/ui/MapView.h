#pragma once

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
};

}
