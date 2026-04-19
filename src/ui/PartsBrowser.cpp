#include "PartsBrowser.h"

#include "../parts/PartsLibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

namespace cld::ui {

PartsBrowser::PartsBrowser(parts::PartsLibrary& lib, QWidget* parent)
    : QDockWidget(tr("Parts"), parent), lib_(lib) {
    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(4, 4, 4, 4);

    filter_ = new QLineEdit(host);
    filter_->setPlaceholderText(tr("Filter (part number / description)"));
    col->addWidget(filter_);

    tree_ = new QTreeWidget(host);
    tree_->setHeaderLabels({ tr("Part"), tr("Description") });
    tree_->header()->setStretchLastSection(true);
    tree_->setRootIsDecorated(true);
    col->addWidget(tree_);

    setWidget(host);

    connect(filter_, &QLineEdit::textChanged, this, &PartsBrowser::applyFilter);
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        const QString key = item->data(0, Qt::UserRole).toString();
        if (!key.isEmpty()) emit partActivated(key);
    });

    rebuild();
}

QString PartsBrowser::categoryForPath(const QString& absPath) const {
    // Infer category from the parent directory name. Matches the BlueBrickParts
    // layout where each top-level folder (Baseplate, Monorail, 4DBrix, ...) is a
    // category; nested subfolders fold up to the nearest category folder.
    const QFileInfo f(absPath);
    const QString parent = f.dir().dirName();
    return parent.isEmpty() ? tr("Other") : parent;
}

void PartsBrowser::rebuild() {
    tree_->clear();
    QHash<QString, QTreeWidgetItem*> cats;
    const auto keys = lib_.keys();
    for (const QString& key : keys) {
        auto meta = lib_.metadata(key);
        if (!meta) continue;
        const QString cat = categoryForPath(meta->xmlFilePath);
        QTreeWidgetItem*& catNode = cats[cat];
        if (!catNode) {
            catNode = new QTreeWidgetItem(tree_);
            catNode->setText(0, cat);
            catNode->setExpanded(false);
        }
        auto* leaf = new QTreeWidgetItem(catNode);
        leaf->setText(0, key);
        QString desc;
        for (const auto& d : meta->descriptions) {
            if (d.language == QStringLiteral("en")) { desc = d.text; break; }
        }
        if (desc.isEmpty() && !meta->descriptions.isEmpty()) {
            desc = meta->descriptions.front().text;
        }
        leaf->setText(1, desc);
        leaf->setData(0, Qt::UserRole, key);
    }
    tree_->sortItems(0, Qt::AscendingOrder);
}

void PartsBrowser::applyFilter(const QString& text) {
    const QString needle = text.trimmed().toLower();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* cat = tree_->topLevelItem(i);
        bool anyChildVisible = false;
        for (int j = 0; j < cat->childCount(); ++j) {
            QTreeWidgetItem* c = cat->child(j);
            const bool match = needle.isEmpty()
                || c->text(0).toLower().contains(needle)
                || c->text(1).toLower().contains(needle);
            c->setHidden(!match);
            if (match) anyChildVisible = true;
        }
        cat->setHidden(!anyChildVisible);
        if (!needle.isEmpty() && anyChildVisible) cat->setExpanded(true);
    }
}

}
