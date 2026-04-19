#include "ModuleLibraryPanel.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>

namespace cld::ui {

namespace {
constexpr const char* kSettingsKey = "modules/libraryPath";

QString defaultModuleLibraryPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : base + QStringLiteral("/modules");
}
}

ModuleLibraryPanel::ModuleLibraryPanel(QWidget* parent)
    : QDockWidget(tr("Module Library"), parent) {
    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(2, 2, 2, 2);
    col->setSpacing(2);

    auto* row = new QHBoxLayout();
    row->setSpacing(2);
    header_ = new QLabel(host);
    header_->setWordWrap(true);
    auto* chooseBtn = new QPushButton(tr("Folder…"), host);
    chooseBtn->setToolTip(tr("Choose the folder to scan for module .bbm files"));
    auto* refreshBtn = new QPushButton(tr("Refresh"), host);
    row->addWidget(header_, 1);
    row->addWidget(chooseBtn);
    row->addWidget(refreshBtn);
    col->addLayout(row);

    list_ = new QListWidget(host);
    col->addWidget(list_);

    setWidget(host);

    // Load persisted folder (or default) and populate.
    QSettings s;
    const QString stored = s.value(QString::fromLatin1(kSettingsKey)).toString();
    path_ = stored.isEmpty() ? defaultModuleLibraryPath() : stored;
    refresh();

    connect(chooseBtn,  &QPushButton::clicked,     this, &ModuleLibraryPanel::onChooseFolder);
    connect(refreshBtn, &QPushButton::clicked,     this, &ModuleLibraryPanel::refresh);
    connect(list_, &QListWidget::itemActivated,    this, &ModuleLibraryPanel::onActivated);
    // Right-click → Import action.
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos){
        auto* it = list_->itemAt(pos);
        if (!it) return;
        QMenu menu(this);
        auto* act = menu.addAction(tr("Import into map"));
        connect(act, &QAction::triggered, [this, it]{ onActivated(it); });
        menu.exec(list_->mapToGlobal(pos));
    });
}

QString ModuleLibraryPanel::libraryPath() const { return path_; }

void ModuleLibraryPanel::setLibraryPath(const QString& dir) {
    path_ = dir;
    QSettings().setValue(QString::fromLatin1(kSettingsKey), path_);
    refresh();
}

void ModuleLibraryPanel::refresh() {
    list_->clear();
    if (path_.isEmpty() || !QDir(path_).exists()) {
        header_->setText(tr("No folder set. Click \"Folder…\" to choose one."));
        return;
    }
    header_->setText(path_);
    QDir d(path_);
    const QStringList files = d.entryList({ QStringLiteral("*.bbm") },
                                             QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& f : files) {
        auto* item = new QListWidgetItem(QFileInfo(f).completeBaseName());
        item->setData(Qt::UserRole, d.absoluteFilePath(f));
        item->setToolTip(d.absoluteFilePath(f));
        list_->addItem(item);
    }
    if (files.isEmpty()) {
        auto* e = new QListWidgetItem(tr("(no modules in this folder)"));
        e->setFlags(Qt::NoItemFlags);
        list_->addItem(e);
    }
}

void ModuleLibraryPanel::onChooseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Module library folder"), path_,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) setLibraryPath(dir);
}

void ModuleLibraryPanel::onActivated(QListWidgetItem* item) {
    if (!item) return;
    const QString p = item->data(Qt::UserRole).toString();
    if (!p.isEmpty()) emit moduleImportRequested(p);
}

}
