#include "MainWindow.h"

#include "LayerPanel.h"
#include "LibraryPathsDialog.h"
#include "MapView.h"
#include "ModulesPanel.h"
#include "PartsBrowser.h"

#include "../core/Map.h"
#include "../parts/PartsLibrary.h"
#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/AnchoredLabel.h"
#include "../edit/EditCommands.h"
#include "../edit/LabelCommands.h"
#include "../edit/ModuleCommands.h"
#include "../saveload/BbmReader.h"
#include "../saveload/BbmWriter.h"
#include "../saveload/SidecarIO.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QUndoStack>
#include <QUuid>

namespace cld::ui {

MainWindow::MainWindow(parts::PartsLibrary& parts, QWidget* parent)
    : QMainWindow(parent), parts_(parts) {
    resize(1400, 900);

    // Scan every configured library path on startup so the PartsBrowser below
    // reflects the user's full set, not just the vendored submodule.
    QStringList allPaths;
    const QString vendored = defaultVendoredPartsRoot();
    if (!vendored.isEmpty() && QDir(vendored).exists()) allPaths << vendored;
    for (const QString& p : loadUserLibraryPaths()) {
        if (!allPaths.contains(p) && QDir(p).exists()) allPaths << p;
    }
    rescanLibrary(allPaths);

    mapView_ = new MapView(parts_, this);
    setCentralWidget(mapView_);

    layerPanel_ = new LayerPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, layerPanel_);

    partsBrowser_ = new PartsBrowser(parts_, this);
    addDockWidget(Qt::LeftDockWidgetArea, partsBrowser_);
    connect(partsBrowser_, &PartsBrowser::partActivated,
            mapView_, &MapView::addPartAtViewCenter);

    modulesPanel_ = new ModulesPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, modulesPanel_);
    connect(modulesPanel_, &ModulesPanel::moduleDeleteRequested, this, [this](const QString& id){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(
            new edit::DeleteModuleCommand(*mapView_->currentMap(), id));
        modulesPanel_->setMap(mapView_->currentMap());
    });

    setupMenus();
    updateTitle();

    connect(mapView_->undoStack(), &QUndoStack::indexChanged,
            this, [this](int){ updateTitle(); });
    connect(mapView_->undoStack(), &QUndoStack::cleanChanged,
            this, [this](bool){ updateTitle(); });

    statusBar()->showMessage(
        tr("Parts library: %1 parts indexed").arg(parts_.partCount()));
}

MainWindow::~MainWindow() = default;

void MainWindow::setupMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* openAct = file->addAction(tr("&Open..."));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpen);

    auto* saveAct = file->addAction(tr("&Save"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::onSave);

    auto* saveAsAct = file->addAction(tr("Save &As..."));
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::onSaveAs);

    file->addSeparator();
    auto* quit = file->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QMainWindow::close);

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    undoAct_ = mapView_->undoStack()->createUndoAction(this, tr("&Undo"));
    undoAct_->setShortcut(QKeySequence::Undo);
    edit->addAction(undoAct_);
    redoAct_ = mapView_->undoStack()->createRedoAction(this, tr("&Redo"));
    redoAct_->setShortcut(QKeySequence::Redo);
    edit->addAction(redoAct_);
    edit->addSeparator();
    auto* del = edit->addAction(tr("&Delete"));
    del->setShortcut(Qt::Key_Delete);
    connect(del, &QAction::triggered, [this]{ mapView_->deleteSelected(); });
    auto* rotCCW = edit->addAction(tr("Rotate 90° &CCW"));
    rotCCW->setShortcut(Qt::Key_R);
    connect(rotCCW, &QAction::triggered, [this]{ mapView_->rotateSelected(-90.0f); });
    auto* rotCW = edit->addAction(tr("Rotate 90° C&W"));
    rotCW->setShortcut(QKeySequence(tr("Shift+R")));
    connect(rotCW, &QAction::triggered, [this]{ mapView_->rotateSelected(90.0f); });

    edit->addSeparator();
    auto* addLabel = edit->addAction(tr("Add &Anchored Label..."));
    addLabel->setShortcut(QKeySequence(tr("Ctrl+L")));
    connect(addLabel, &QAction::triggered, this, [this]{
        auto* map = mapView_->currentMap();
        if (!map) return;
        // If a single brick is selected, anchor to it; otherwise anchor to the
        // view centre in world coords.
        QString targetId;
        core::AnchorKind kind = core::AnchorKind::World;
        QPointF offsetStuds;
        auto sel = mapView_->scene()->selectedItems();
        if (sel.size() == 1 && sel[0]->data(2).toString() == QStringLiteral("brick")) {
            targetId = sel[0]->data(1).toString();
            kind = core::AnchorKind::Brick;
            offsetStuds = QPointF(2.0, -2.0);  // small initial offset from centre
        } else {
            const QPointF scenePos = mapView_->mapToScene(mapView_->viewport()->rect().center());
            offsetStuds = QPointF(scenePos.x() / 8.0, scenePos.y() / 8.0);
        }
        bool ok = false;
        const QString text = QInputDialog::getText(
            this, tr("Anchored label"), tr("Label text:"),
            QLineEdit::Normal, {}, &ok);
        if (!ok || text.isEmpty()) return;

        core::AnchoredLabel L;
        L.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        L.text = text;
        L.color = core::ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
        L.kind = kind;
        L.targetId = targetId;
        L.offset = offsetStuds;
        mapView_->undoStack()->push(new edit::AddAnchoredLabelCommand(*map, std::move(L)));
        mapView_->rebuildScene();
    });

    auto* view = menuBar()->addMenu(tr("&View"));
    auto* zIn = view->addAction(tr("Zoom &In"));
    zIn->setShortcut(QKeySequence::ZoomIn);
    connect(zIn, &QAction::triggered, this, &MainWindow::onZoomIn);

    auto* zOut = view->addAction(tr("Zoom &Out"));
    zOut->setShortcut(QKeySequence::ZoomOut);
    connect(zOut, &QAction::triggered, this, &MainWindow::onZoomOut);

    auto* fit = view->addAction(tr("&Fit to View"));
    fit->setShortcut(QKeySequence(Qt::Key_F));
    connect(fit, &QAction::triggered, this, &MainWindow::onFitToView);

    auto* tools = menuBar()->addMenu(tr("&Tools"));
    auto* libAct = tools->addAction(tr("Manage Parts &Libraries..."));
    connect(libAct, &QAction::triggered, this, &MainWindow::onManageLibraries);

    menuBar()->addMenu(tr("&Layers"));

    auto* modules = menuBar()->addMenu(tr("&Modules"));
    auto* createModAct = modules->addAction(tr("Create from &Selection..."));
    connect(createModAct, &QAction::triggered, this, [this]{
        auto* map = mapView_->currentMap();
        if (!map) return;
        std::vector<edit::CreateModuleCommand::Member> members;
        for (QGraphicsItem* it : mapView_->scene()->selectedItems()) {
            if (it->data(2).toString() != QStringLiteral("brick")) continue;
            members.push_back({ it->data(0).toInt(), it->data(1).toString() });
        }
        if (members.empty()) {
            QMessageBox::information(this, tr("Create module"),
                tr("Select one or more bricks first."));
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Create module"), tr("Module name:"),
            QLineEdit::Normal, tr("New Module"), &ok);
        if (!ok || name.isEmpty()) return;
        mapView_->undoStack()->push(new edit::CreateModuleCommand(*map, name, std::move(members)));
        modulesPanel_->setMap(map);
        statusBar()->showMessage(tr("Module created"), 3000);
    });

    auto* importModAct = modules->addAction(tr("&Import .bbm as Module..."));
    connect(importModAct, &QAction::triggered, this, [this]{
        auto* map = mapView_->currentMap();
        if (!map) return;
        int targetLayer = -1;
        for (int i = 0; i < static_cast<int>(map->layers().size()); ++i) {
            if (map->layers()[i]->kind() == core::LayerKind::Brick) { targetLayer = i; break; }
        }
        if (targetLayer < 0) {
            QMessageBox::warning(this, tr("Import module"), tr("No brick layer in the current map."));
            return;
        }
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import .bbm as module"), {},
            tr("BlueBrick map (*.bbm)"));
        if (path.isEmpty()) return;
        auto loaded = saveload::readBbm(path);
        if (!loaded.ok()) {
            QMessageBox::warning(this, tr("Import failed"), loaded.error);
            return;
        }
        std::vector<core::Brick> bricks;
        for (const auto& L : loaded.map->layers()) {
            if (L->kind() != core::LayerKind::Brick) continue;
            const auto& BL = static_cast<const core::LayerBrick&>(*L);
            for (const auto& b : BL.bricks) {
                core::Brick copy = b;
                copy.guid.clear();  // will be regenerated inside the command
                bricks.push_back(std::move(copy));
            }
        }
        if (bricks.empty()) {
            QMessageBox::information(this, tr("Import module"),
                tr("The selected file has no brick layers to import."));
            return;
        }
        const QString name = QFileInfo(path).baseName();
        mapView_->undoStack()->push(new edit::ImportBbmAsModuleCommand(
            *map, targetLayer, path, name, std::move(bricks)));
        mapView_->rebuildScene();
        modulesPanel_->setMap(map);
        statusBar()->showMessage(
            tr("Imported %1 bricks as module '%2'").arg(bricks.size()).arg(name), 4000);
    });

    menuBar()->addMenu(tr("&Help"));
}

void MainWindow::updateTitle() {
    const QString name = currentFilePath_.isEmpty()
        ? tr("[untitled]")
        : QFileInfo(currentFilePath_).fileName();
    const bool dirty = !mapView_->undoStack()->isClean();
    setWindowTitle(tr("%1%2 — Collaborative Layout Designer")
                       .arg(name, dirty ? QStringLiteral(" *") : QString()));
}

bool MainWindow::openFile(const QString& path) {
    if (!maybeSave()) return false;
    auto result = saveload::readBbm(path);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("Open failed"),
            tr("%1\n\n%2").arg(path, result.error));
        return false;
    }
    if (!result.error.isEmpty()) {
        statusBar()->showMessage(tr("Loaded with warnings: %1").arg(result.error), 5000);
    }

    // Sidecar: optional. Hash-check the .bbm bytes to flag drift (user edited
    // the .bbm in vanilla BlueBrick between our writes).
    const QString sidecarPath = saveload::sidecarPathFor(path);
    if (QFile::exists(sidecarPath)) {
        QFile bf(path);
        QByteArray bbmBytes;
        if (bf.open(QIODevice::ReadOnly)) bbmBytes = bf.readAll();
        auto sres = saveload::readSidecar(sidecarPath, bbmBytes, result.map->sidecar);
        if (sres.ok && sres.hashMismatch) {
            statusBar()->showMessage(
                tr("Sidecar hash mismatch — .bbm was modified externally. Fork-only metadata preserved but may drift."), 8000);
        } else if (!sres.ok) {
            statusBar()->showMessage(tr("Sidecar load failed: %1").arg(sres.error), 5000);
        }
    }

    const int layerCount = static_cast<int>(result.map->layers().size());
    const int nbItems = result.map->nbItems;
    mapView_->loadMap(std::move(result.map));
    layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    modulesPanel_->setMap(mapView_->currentMap());
    currentFilePath_ = path;
    mapView_->undoStack()->setClean();
    cleanUndoIndex_ = 0;
    updateTitle();
    statusBar()->showMessage(tr("Opened %1 — %2 layers, %3 items")
                                 .arg(path).arg(layerCount).arg(nbItems));
    return true;
}

void MainWindow::onOpen() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open BlueBrick map"), {},
        tr("BlueBrick map (*.bbm);;All files (*)"));
    if (!path.isEmpty()) openFile(path);
}

bool MainWindow::writeMapTo(const QString& path) {
    auto* map = mapView_->currentMap();
    if (!map) return false;
    auto res = saveload::writeBbm(*map, path);
    if (!res.ok) {
        QMessageBox::warning(this, tr("Save failed"), res.error);
        return false;
    }

    // Sidecar: always write when the map carries fork-only metadata. Clean up
    // any stale sidecar if the map no longer has any.
    const QString sidecarPath = saveload::sidecarPathFor(path);
    if (!map->sidecar.isEmpty()) {
        QFile bf(path);
        if (bf.open(QIODevice::ReadOnly)) {
            const QByteArray bbmBytes = bf.readAll();
            QString err;
            if (!saveload::writeSidecar(sidecarPath, bbmBytes, map->sidecar, &err)) {
                QMessageBox::warning(this, tr("Sidecar save failed"),
                    tr("The .bbm saved successfully but the sidecar failed:\n%1").arg(err));
            }
        }
    } else if (QFile::exists(sidecarPath)) {
        QFile::remove(sidecarPath);
    }

    currentFilePath_ = path;
    mapView_->undoStack()->setClean();
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
    return true;
}

bool MainWindow::onSave() {
    if (!mapView_->currentMap()) return false;
    if (currentFilePath_.isEmpty()) return onSaveAs();
    return writeMapTo(currentFilePath_);
}

bool MainWindow::onSaveAs() {
    if (!mapView_->currentMap()) return false;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save BlueBrick map"), currentFilePath_,
        tr("BlueBrick map (*.bbm)"));
    if (path.isEmpty()) return false;
    return writeMapTo(path);
}

bool MainWindow::maybeSave() {
    if (!mapView_->currentMap() || mapView_->undoStack()->isClean()) return true;
    const auto btn = QMessageBox::question(
        this, tr("Unsaved changes"),
        tr("The current layout has unsaved changes. Save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (btn == QMessageBox::Save)    return onSave();
    if (btn == QMessageBox::Discard) return true;
    return false;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (maybeSave()) e->accept();
    else             e->ignore();
}

void MainWindow::onManageLibraries() {
    QStringList current;
    const QString vendored = defaultVendoredPartsRoot();
    if (!vendored.isEmpty() && QDir(vendored).exists()) current << vendored;
    current += loadUserLibraryPaths();

    LibraryPathsDialog dlg(current, this);
    if (dlg.exec() != QDialog::Accepted) return;

    QStringList newPaths = dlg.paths();
    // Persist the user additions — filter out the vendored path so it isn't
    // duplicated on next launch.
    QStringList userOnly;
    for (const QString& p : newPaths) {
        if (p != vendored) userOnly << p;
    }
    saveUserLibraryPaths(userOnly);
    rescanLibrary(newPaths);
    partsBrowser_->rebuild();
    statusBar()->showMessage(
        tr("Reloaded library: %1 parts across %2 path(s)")
            .arg(parts_.partCount()).arg(newPaths.size()), 4000);
}

void MainWindow::rescanLibrary(const QStringList& paths) {
    parts_.clear();
    for (const QString& p : paths) parts_.addSearchPath(p);
    parts_.scan();
}

QStringList MainWindow::loadUserLibraryPaths() const {
    QSettings s;
    s.beginGroup(LibraryPathsDialog::kSettingsGroup);
    const QStringList v = s.value(LibraryPathsDialog::kSettingsKey).toStringList();
    s.endGroup();
    return v;
}

void MainWindow::saveUserLibraryPaths(const QStringList& paths) {
    QSettings s;
    s.beginGroup(LibraryPathsDialog::kSettingsGroup);
    s.setValue(LibraryPathsDialog::kSettingsKey, paths);
    s.endGroup();
}

QString MainWindow::defaultVendoredPartsRoot() const {
    const QString exeDir = QCoreApplication::applicationDirPath();
    for (const QString& rel : { QStringLiteral("/../../../parts/BlueBrickParts/parts"),
                                 QStringLiteral("/parts/BlueBrickParts/parts"),
                                 QStringLiteral("/BlueBrickParts/parts") }) {
        if (QDir(exeDir + rel).exists()) return QDir(exeDir + rel).absolutePath();
    }
    return {};
}

void MainWindow::onZoomIn()  { mapView_->scale(1.2, 1.2); }
void MainWindow::onZoomOut() { mapView_->scale(1 / 1.2, 1 / 1.2); }

void MainWindow::onFitToView() {
    if (!mapView_->currentMap() || mapView_->scene()->itemsBoundingRect().isEmpty()) return;
    mapView_->fitInView(mapView_->scene()->itemsBoundingRect().adjusted(-50, -50, 50, 50),
                         Qt::KeepAspectRatio);
}

}
