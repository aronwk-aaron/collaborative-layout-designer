#include "LayerPanel.h"

#include "../core/Layer.h"
#include "../core/Map.h"
#include "../rendering/SceneBuilder.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
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
    auto* addBtn = new QPushButton(tr("+"), host);
    addBtn->setToolTip(tr("Add a new layer"));
    auto* delBtn = new QPushButton(tr("−"), host);
    delBtn->setToolTip(tr("Delete the selected layer"));
    auto* upBtn  = new QPushButton(tr("▲"), host);
    upBtn->setToolTip(tr("Move selected layer up"));
    auto* dnBtn  = new QPushButton(tr("▼"), host);
    dnBtn->setToolTip(tr("Move selected layer down"));
    row->addWidget(showAll);
    row->addWidget(hideOthers);
    row->addStretch();
    row->addWidget(addBtn);
    row->addWidget(delBtn);
    row->addWidget(upBtn);
    row->addWidget(dnBtn);
    col->addLayout(row);

    connect(addBtn, &QPushButton::clicked, this, [this]{
        QMenu m(this);
        auto add = [&](const QString& label, core::LayerKind k){
            auto* a = m.addAction(label);
            connect(a, &QAction::triggered, [this, k]{ emit addLayerRequested(k); });
        };
        add(tr("Grid Layer"),  core::LayerKind::Grid);
        add(tr("Brick Layer"), core::LayerKind::Brick);
        add(tr("Text Layer"),  core::LayerKind::Text);
        add(tr("Area Layer"),  core::LayerKind::Area);
        add(tr("Ruler Layer"), core::LayerKind::Ruler);
        m.exec(QCursor::pos());
    });
    connect(delBtn, &QPushButton::clicked, this, [this]{
        const int row = list_->currentRow();
        if (row >= 0) emit deleteLayerRequested(row);
    });
    connect(upBtn, &QPushButton::clicked, this, [this]{
        const int row = list_->currentRow();
        if (row >= 0) emit moveLayerRequested(row, +1);
    });
    connect(dnBtn, &QPushButton::clicked, this, [this]{
        const int row = list_->currentRow();
        if (row >= 0) emit moveLayerRequested(row, -1);
    });

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
        QMenu menu(this);

        if (item) {
            const int row = list_->row(item);
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
            menu.addSeparator();
            auto* ren = menu.addAction(tr("Rename..."));
            connect(ren, &QAction::triggered, [this, row, item]{
                bool ok = false;
                // The display text starts with "[N] kind — name"; strip to the
                // current user-facing name for the prompt default.
                const QString shown = item->text();
                const int dash = shown.lastIndexOf(QStringLiteral(" — "));
                const QString def = dash >= 0 ? shown.mid(dash + 3) : shown;
                const QString name = QInputDialog::getText(
                    this, tr("Rename layer"), tr("Layer name:"),
                    QLineEdit::Normal, def, &ok);
                if (ok && !name.isEmpty()) emit renameLayerRequested(row, name);
            });
            auto* up = menu.addAction(tr("Move Up"));
            connect(up, &QAction::triggered, [this, row]{ emit moveLayerRequested(row, +1); });
            auto* dn = menu.addAction(tr("Move Down"));
            connect(dn, &QAction::triggered, [this, row]{ emit moveLayerRequested(row, -1); });
            menu.addSeparator();
            auto* del = menu.addAction(tr("Delete layer"));
            connect(del, &QAction::triggered, [this, row]{ emit deleteLayerRequested(row); });
            menu.addSeparator();
        }

        // "Add Layer" submenu — available whether or not a row is under the cursor.
        auto* addMenu = menu.addMenu(tr("Add Layer"));
        auto addKind = [&](const QString& label, core::LayerKind k){
            auto* a = addMenu->addAction(label);
            connect(a, &QAction::triggered, [this, k]{ emit addLayerRequested(k); });
        };
        addKind(tr("Grid Layer"),  core::LayerKind::Grid);
        addKind(tr("Brick Layer"), core::LayerKind::Brick);
        addKind(tr("Text Layer"),  core::LayerKind::Text);
        addKind(tr("Area Layer"),  core::LayerKind::Area);
        addKind(tr("Ruler Layer"), core::LayerKind::Ruler);

        menu.exec(list_->mapToGlobal(pos));
    });
}

int LayerPanel::currentRow() const { return list_->currentRow(); }

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
