#include "LayerPanel.h"

#include "../core/Layer.h"
#include "../core/Map.h"
#include "../rendering/SceneBuilder.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

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
    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(2, 2, 2, 2);
    col->setSpacing(2);

    auto* row = new QHBoxLayout();
    row->setSpacing(2);
    auto* showAll = new QPushButton(tr("Show all"), host);
    auto* hideOthers = new QPushButton(tr("Solo"), host);
    hideOthers->setToolTip(tr("Show only the selected layer"));
    row->addWidget(showAll);
    row->addWidget(hideOthers);
    row->addStretch();
    col->addLayout(row);

    list_ = new QListWidget(host);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    col->addWidget(list_);

    setWidget(host);

    connect(showAll, &QPushButton::clicked, this, [this]{
        for (int i = 0; i < list_->count(); ++i) list_->item(i)->setCheckState(Qt::Checked);
    });
    connect(hideOthers, &QPushButton::clicked, this, [this]{
        auto* sel = list_->currentItem();
        if (!sel) return;
        for (int i = 0; i < list_->count(); ++i) {
            list_->item(i)->setCheckState(list_->item(i) == sel ? Qt::Checked : Qt::Unchecked);
        }
    });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!builder_) return;
        const int idx = list_->row(item);
        builder_->setLayerVisible(idx, item->checkState() == Qt::Checked);
    });
    connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = list_->itemAt(pos);
        if (!item) return;
        QMenu menu(this);
        const bool visible = (item->checkState() == Qt::Checked);
        auto* toggle = menu.addAction(visible ? tr("Hide") : tr("Show"));
        connect(toggle, &QAction::triggered, [item, visible]{
            item->setCheckState(visible ? Qt::Unchecked : Qt::Checked);
        });
        auto* allOn = menu.addAction(tr("Show all layers"));
        connect(allOn, &QAction::triggered, [this]{
            for (int i = 0; i < list_->count(); ++i) list_->item(i)->setCheckState(Qt::Checked);
        });
        auto* allOff = menu.addAction(tr("Hide all other layers"));
        connect(allOff, &QAction::triggered, [this, item]{
            for (int i = 0; i < list_->count(); ++i) {
                list_->item(i)->setCheckState(list_->item(i) == item ? Qt::Checked : Qt::Unchecked);
            }
        });
        menu.exec(list_->mapToGlobal(pos));
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
