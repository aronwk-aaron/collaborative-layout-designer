#include "ModulesPanel.h"

#include "../core/Map.h"
#include "../core/Module.h"
#include "../core/Sidecar.h"

#include <QFileInfo>
#include <QListWidget>
#include <QListWidgetItem>

namespace cld::ui {

ModulesPanel::ModulesPanel(QWidget* parent)
    : QDockWidget(tr("Modules"), parent) {
    list_ = new QListWidget(this);
    setWidget(list_);
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
