#include "PartsBrowser.h"

#include "../parts/PartsLibrary.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QVBoxLayout>
#include <QWidget>

namespace cld::ui {

namespace {

constexpr int kIconSize = 64;

// Store the part key on each item so we can retrieve it on activation.
constexpr int kPartKeyRole = Qt::UserRole + 1;
// Store the category (derived from parent folder) for filtering.
constexpr int kCategoryRole = Qt::UserRole + 2;

}

PartsBrowser::PartsBrowser(parts::PartsLibrary& lib, QWidget* parent)
    : QDockWidget(tr("Parts"), parent), lib_(lib) {
    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(4, 4, 4, 4);

    auto* row = new QHBoxLayout();
    category_ = new QComboBox(host);
    category_->setMinimumContentsLength(12);
    row->addWidget(category_, 1);

    filter_ = new QLineEdit(host);
    filter_->setPlaceholderText(tr("Filter…"));
    row->addWidget(filter_, 2);
    col->addLayout(row);

    grid_ = new QListWidget(host);
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setIconSize(QSize(kIconSize, kIconSize));
    grid_->setSpacing(6);
    grid_->setUniformItemSizes(true);
    grid_->setWordWrap(true);
    grid_->setTextElideMode(Qt::ElideRight);
    grid_->setGridSize(QSize(kIconSize + 24, kIconSize + 32));
    col->addWidget(grid_);

    setWidget(host);

    connect(category_, &QComboBox::currentTextChanged, this, [this](const QString&) { applyFilter(); });
    connect(filter_,   &QLineEdit::textChanged,        this, [this](const QString&) { applyFilter(); });
    connect(grid_, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) {
        if (it) emit partActivated(it->data(kPartKeyRole).toString());
    });

    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(grid_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos){
        auto* it = grid_->itemAt(pos);
        if (!it) return;
        QMenu menu(this);
        const QString key = it->data(kPartKeyRole).toString();
        auto* add = menu.addAction(tr("Add '%1' to map").arg(key));
        connect(add, &QAction::triggered, [this, key]{ emit partActivated(key); });
        menu.addSeparator();
        auto* copy = menu.addAction(tr("Copy part number"));
        connect(copy, &QAction::triggered, [key]{
            QApplication::clipboard()->setText(key);
        });
        menu.exec(grid_->mapToGlobal(pos));
    });

    rebuild();
}

QString PartsBrowser::categoryForPath(const QString& absPath) const {
    const QFileInfo f(absPath);
    const QString parent = f.dir().dirName();
    return parent.isEmpty() ? tr("Other") : parent;
}

void PartsBrowser::rebuild() {
    grid_->clear();
    const QString previousCat = category_->currentText();
    category_->blockSignals(true);
    category_->clear();
    category_->addItem(tr("All categories"));
    QSet<QString> cats;

    const auto keys = lib_.keys();
    for (const QString& key : keys) {
        auto meta = lib_.metadata(key);
        if (!meta) continue;
        const QString cat = categoryForPath(meta->xmlFilePath);
        cats.insert(cat);

        auto* item = new QListWidgetItem(key);
        // Description as tooltip (preferring English).
        for (const auto& d : meta->descriptions) {
            if (d.language == QStringLiteral("en")) {
                item->setToolTip(QStringLiteral("%1\n%2").arg(key, d.text));
                break;
            }
        }
        if (item->toolTip().isEmpty() && !meta->descriptions.isEmpty()) {
            item->setToolTip(QStringLiteral("%1\n%2").arg(key, meta->descriptions.front().text));
        }

        // Icon from the part GIF, scaled to kIconSize.
        if (!meta->gifFilePath.isEmpty()) {
            QPixmap pm(meta->gifFilePath);
            if (!pm.isNull()) {
                item->setIcon(QIcon(pm.scaled(kIconSize, kIconSize,
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation)));
            }
        }

        item->setData(kPartKeyRole,  key);
        item->setData(kCategoryRole, cat);
        grid_->addItem(item);
    }

    // Sort category list and re-select what was active.
    QStringList sortedCats = cats.values();
    std::sort(sortedCats.begin(), sortedCats.end());
    category_->addItems(sortedCats);
    const int restoreIdx = category_->findText(previousCat);
    category_->setCurrentIndex(restoreIdx > 0 ? restoreIdx : 0);
    category_->blockSignals(false);

    grid_->sortItems(Qt::AscendingOrder);
    applyFilter();
}

void PartsBrowser::applyFilter() {
    const QString needle = filter_->text().trimmed().toLower();
    const QString cat = category_->currentText();
    const bool allCats = (category_->currentIndex() <= 0);
    for (int i = 0; i < grid_->count(); ++i) {
        auto* it = grid_->item(i);
        const QString key = it->data(kPartKeyRole).toString();
        const QString itemCat = it->data(kCategoryRole).toString();
        const bool catOk  = allCats || (itemCat == cat);
        const bool textOk = needle.isEmpty()
            || key.toLower().contains(needle)
            || it->toolTip().toLower().contains(needle);
        it->setHidden(!(catOk && textOk));
    }
}

}
