#pragma once

#include <QDockWidget>

class QListWidget;

namespace cld::core { class Map; }
namespace cld::rendering { class SceneBuilder; }

namespace cld::ui {

// Dockable panel listing the current map's layers, with a per-layer visibility
// checkbox that forwards to the SceneBuilder.
class LayerPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);

    void setMap(const core::Map* map, rendering::SceneBuilder* builder);

private:
    QListWidget* list_ = nullptr;
    rendering::SceneBuilder* builder_ = nullptr;
};

}
