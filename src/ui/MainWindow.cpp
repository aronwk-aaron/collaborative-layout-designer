#include "MainWindow.h"

#include "LayerPanel.h"
#include "MapView.h"

#include "../core/Map.h"
#include "../parts/PartsLibrary.h"
#include "../saveload/BbmReader.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

namespace cld::ui {

MainWindow::MainWindow(parts::PartsLibrary& parts, QWidget* parent)
    : QMainWindow(parent), parts_(parts) {
    setWindowTitle(tr("Collaborative Layout Designer"));
    resize(1400, 900);

    mapView_ = new MapView(parts_, this);
    setCentralWidget(mapView_);

    layerPanel_ = new LayerPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, layerPanel_);

    setupMenus();

    statusBar()->showMessage(
        tr("Parts library: %1 parts indexed").arg(parts_.partCount()));
}

MainWindow::~MainWindow() = default;

void MainWindow::setupMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* openAct = file->addAction(tr("&Open..."));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpen);

    file->addSeparator();
    auto* quit = file->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

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

    menuBar()->addMenu(tr("&Edit"));
    menuBar()->addMenu(tr("&Layers"));
    menuBar()->addMenu(tr("&Modules"));
    menuBar()->addMenu(tr("&Help"));
}

bool MainWindow::openFile(const QString& path) {
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
    setWindowTitle(tr("%1 — Collaborative Layout Designer").arg(path));
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

void MainWindow::onZoomIn()  { mapView_->scale(1.2, 1.2); }
void MainWindow::onZoomOut() { mapView_->scale(1 / 1.2, 1 / 1.2); }

void MainWindow::onFitToView() {
    if (!mapView_->currentMap() || mapView_->scene()->itemsBoundingRect().isEmpty()) return;
    mapView_->fitInView(mapView_->scene()->itemsBoundingRect().adjusted(-50, -50, 50, 50),
                         Qt::KeepAspectRatio);
}

}
