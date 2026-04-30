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

namespace bld::ui {

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

// Compact per-kind visual prefix so the user can tell layer types apart
// at a glance. Unicode symbols keep this stateless / no-icon-resource.
QString layerKindGlyph(core::LayerKind k) {
    switch (k) {
        case core::LayerKind::Grid:         return QStringLiteral("◫");
        case core::LayerKind::Brick:        return QStringLiteral("▦");
        case core::LayerKind::Text:         return QStringLiteral("A");
        case core::LayerKind::Area:         return QStringLiteral("▣");
        case core::LayerKind::Ruler:        return QStringLiteral("⟷");
        case core::LayerKind::AnchoredText: return QStringLiteral("↳");
    }
    return QStringLiteral("?");
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
        const bool visible = item->checkState() == Qt::Checked;
        builder_->setLayerVisible(idx, visible);
        // Also persist on the underlying Layer so a later scene rebuild
        // (triggered by e.g. a move / undo / paste) honours the user's
        // visibility choice. Without this, any rebuildScene call would
        // read the Layer's default visible=true and make hidden layers
        // reappear despite the checkbox still showing unchecked.
        if (map_ && idx >= 0 && idx < static_cast<int>(map_->layers().size())) {
            map_->layers()[idx]->visible = visible;
        }
    });
    // Clicking (or arrow-keying) onto a row sets it as the active layer —
    // vanilla BlueBrick uses Map.selectedLayerIndex for new-item placements.
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row){
        if (row >= 0 && map_) {
            map_->selectedLayerIndex = row;
            emit activeLayerChanged(row);
            // Re-render the panel so the active row renders bold.
            setMap(map_, builder_);
        }
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item){
        if (item) emit layerOptionsRequested(list_->row(item));
    });

    connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = list_->itemAt(pos);
        QMenu menu(this);

        if (item) {
            const int row = list_->row(item);
            const bool visible = (item->checkState() == Qt::Checked);
            auto* setActive = menu.addAction(tr("Make Active Layer"));
            connect(setActive, &QAction::triggered, [this, row]{
                list_->setCurrentRow(row);
            });
            menu.addSeparator();
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
            auto* opts = menu.addAction(tr("Layer Options..."));
            connect(opts, &QAction::triggered, [this, row]{ emit layerOptionsRequested(row); });
            auto* ren = menu.addAction(tr("Rename..."));
            connect(ren, &QAction::triggered, [this, row, item]{
                bool ok = false;
                // The display text starts with "glyph [N] kind — name"; strip
                // to the current user-facing name for the prompt default.
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

void LayerPanel::setMap(core::Map* map, rendering::SceneBuilder* builder) {
    builder_ = builder;
    map_ = map;
    list_->blockSignals(true);
    list_->clear();
    if (map) {
        int i = 0;
        for (const auto& layer : map->layers()) {
            const bool isActive = (i == map->selectedLayerIndex);
            auto* item = new QListWidgetItem(
                QStringLiteral("%1  [%2] %3 — %4%5")
                    .arg(layerKindGlyph(layer->kind()))
                    .arg(i)
                    .arg(layerKindName(layer->kind()))
                    .arg(layer->name)
                    .arg(layer->transparency < 100
                            ? QStringLiteral("  (α%1)").arg(layer->transparency)
                            : QString()));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(layer->visible ? Qt::Checked : Qt::Unchecked);
            if (isActive) {
                QFont f = item->font(); f.setBold(true); item->setFont(f);
                item->setBackground(QColor(60, 120, 200, 60));
                item->setToolTip(tr("Active layer (receives new items)"));
            }
            list_->addItem(item);
            ++i;
        }
        if (map->selectedLayerIndex >= 0 && map->selectedLayerIndex < list_->count()) {
            list_->setCurrentRow(map->selectedLayerIndex);
        }
    }
    list_->blockSignals(false);
}

}
