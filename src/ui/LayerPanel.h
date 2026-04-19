#pragma once

#include "../core/Layer.h"  // for core::LayerKind

#include <QDockWidget>

class QListWidget;

namespace cld::core { class Map; }
namespace cld::rendering { class SceneBuilder; }

namespace cld::ui {

// Dockable panel listing the current map's layers, with a per-layer visibility
// checkbox that forwards to the SceneBuilder. Also emits signals for layer
// lifecycle operations so MainWindow can route them through the undo stack.
class LayerPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);

    void setMap(const core::Map* map, rendering::SceneBuilder* builder);

    // Currently-selected layer row, or -1 if none.
    int currentRow() const;

signals:
    // MainWindow handles these by pushing the corresponding undo commands.
    void addLayerRequested(core::LayerKind kind);
    void deleteLayerRequested(int index);
    void moveLayerRequested(int index, int delta);
    void renameLayerRequested(int index, const QString& newName);

private:
    QListWidget* list_ = nullptr;
    rendering::SceneBuilder* builder_ = nullptr;
};

}
