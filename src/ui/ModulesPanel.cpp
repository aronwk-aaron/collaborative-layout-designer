#include "ModulesPanel.h"

#include "../core/Map.h"
#include "../core/Module.h"
#include "../core/Sidecar.h"

#include <QContextMenuEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace cld::ui {

ModulesPanel::ModulesPanel(QWidget* parent)
    : QDockWidget(tr("Modules"), parent) {
    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(2, 2, 2, 2);
    col->setSpacing(2);

    auto* row = new QHBoxLayout();
    row->setSpacing(2);
    auto* createBtn = new QPushButton(tr("Create"), host);
    createBtn->setToolTip(tr("Create a module from the current selection"));
    auto* importBtn = new QPushButton(tr("Import…"), host);
    importBtn->setToolTip(tr("Import a .bbm file as a module"));
    auto* deleteBtn = new QPushButton(tr("Delete"), host);
    deleteBtn->setToolTip(tr("Delete the selected module"));
    deleteBtn->setEnabled(false);
    row->addWidget(createBtn);
    row->addWidget(importBtn);
    row->addWidget(deleteBtn);
    row->addStretch();
    col->addLayout(row);

    list_ = new QListWidget(host);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    col->addWidget(list_);

    setWidget(host);

    connect(createBtn, &QPushButton::clicked, this, &ModulesPanel::createModuleRequested);
    connect(importBtn, &QPushButton::clicked, this, &ModulesPanel::importBbmRequested);
    connect(deleteBtn, &QPushButton::clicked, this, [this]{
        auto* sel = list_->currentItem();
        if (!sel) return;
        const QString id = sel->toolTip();
        if (!id.isEmpty()) emit moduleDeleteRequested(id);
    });
    // Delete button enables only when a valid module row is selected.
    connect(list_, &QListWidget::currentItemChanged, this, [deleteBtn](QListWidgetItem* cur, QListWidgetItem*){
        deleteBtn->setEnabled(cur && !cur->toolTip().isEmpty());
    });

    connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = list_->itemAt(pos);
        if (!item) return;
        const QString id = item->toolTip();  // we stash the module id in the tooltip
        if (id.isEmpty()) return;
        QMenu menu(this);
        auto* del = menu.addAction(tr("Delete module"));
        connect(del, &QAction::triggered, [this, id]{ emit moduleDeleteRequested(id); });
        menu.exec(list_->mapToGlobal(pos));
    });
}

void ModulesPanel::setMap(const core::Map* map) {
    list_->clear();
    if (!map) return;
    for (const auto& m : map->sidecar.modules) {
        QString suffix;
        if (!m.sourceFile.isEmpty()) {
            suffix = QStringLiteral(" — %1").arg(QFileInfo(m.sourceFile).fileName());
        }
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 (%2 members%3)")
                .arg(m.name.isEmpty() ? tr("[unnamed]") : m.name)
                .arg(m.memberIds.size())
                .arg(suffix));
        item->setToolTip(m.id);
        list_->addItem(item);
    }
    if (list_->count() == 0) {
        auto* empty = new QListWidgetItem(tr("(no modules)"));
        empty->setFlags(Qt::NoItemFlags);
        list_->addItem(empty);
    }
}

}
