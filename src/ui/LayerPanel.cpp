#include "LayerPanel.h"

#include "../core/Layer.h"
#include "../core/Map.h"
#include "../rendering/SceneBuilder.h"

#include <QListWidget>
#include <QListWidgetItem>

namespace cld::ui {

namespace {

const char* layerKindName(core::LayerKind k) {
    switch (k) {
        case core::LayerKind::Grid:         return "grid";
        case core::LayerKind::Brick:        return "brick";
        case core::LayerKind::Text:         return "text";
        case core::LayerKind::Area:         return "area";
        case core::LayerKind::Ruler:        return "ruler";
        case core::LayerKind::AnchoredText: return "anchored";
    }
    return "?";
}

}

LayerPanel::LayerPanel(QWidget* parent) : QDockWidget(tr("Layers"), parent) {
    list_ = new QListWidget(this);
    setWidget(list_);
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!builder_) return;
        const int idx = list_->row(item);
        builder_->setLayerVisible(idx, item->checkState() == Qt::Checked);
    });
}

void LayerPanel::setMap(const core::Map* map, rendering::SceneBuilder* builder) {
    builder_ = builder;
    list_->blockSignals(true);
    list_->clear();
    if (map) {
        int i = 0;
        for (const auto& layer : map->layers()) {
            auto* item = new QListWidgetItem(
                QStringLiteral("[%1] %2 — %3")
                    .arg(i++).arg(layerKindName(layer->kind())).arg(layer->name));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(layer->visible ? Qt::Checked : Qt::Unchecked);
            list_->addItem(item);
        }
    }
    list_->blockSignals(false);
}

}
