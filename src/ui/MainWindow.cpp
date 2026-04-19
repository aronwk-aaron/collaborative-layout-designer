#include "MainWindow.h"

#include "LayerPanel.h"
#include "MapView.h"
#include "PartsBrowser.h"

#include "../core/Map.h"
#include "../parts/PartsLibrary.h"
#include "../saveload/BbmReader.h"
#include "../saveload/BbmWriter.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QUndoStack>

namespace cld::ui {

MainWindow::MainWindow(parts::PartsLibrary& parts, QWidget* parent)
    : QMainWindow(parent), parts_(parts) {
    resize(1400, 900);

    mapView_ = new MapView(parts_, this);
    setCentralWidget(mapView_);

    layerPanel_ = new LayerPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, layerPanel_);

    partsBrowser_ = new PartsBrowser(parts_, this);
    addDockWidget(Qt::LeftDockWidgetArea, partsBrowser_);
    connect(partsBrowser_, &PartsBrowser::partActivated,
            mapView_, &MapView::addPartAtViewCenter);

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

    menuBar()->addMenu(tr("&Layers"));
    menuBar()->addMenu(tr("&Modules"));
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
    const int layerCount = static_cast<int>(result.map->layers().size());
    const int nbItems = result.map->nbItems;
    mapView_->loadMap(std::move(result.map));
    layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
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
    if (!mapView_->currentMap()) return false;
    auto res = saveload::writeBbm(*mapView_->currentMap(), path);
    if (!res.ok) {
        QMessageBox::warning(this, tr("Save failed"), res.error);
        return false;
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

void MainWindow::onZoomIn()  { mapView_->scale(1.2, 1.2); }
void MainWindow::onZoomOut() { mapView_->scale(1 / 1.2, 1 / 1.2); }

void MainWindow::onFitToView() {
    if (!mapView_->currentMap() || mapView_->scene()->itemsBoundingRect().isEmpty()) return;
    mapView_->fitInView(mapView_->scene()->itemsBoundingRect().adjusted(-50, -50, 50, 50),
                         Qt::KeepAspectRatio);
}

}
