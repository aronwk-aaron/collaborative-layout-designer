#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>

namespace cld::ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Collaborative Layout Designer"));
    resize(1280, 800);
    setupMenus();
    setupCentralPlaceholder();
    statusBar()->showMessage(tr("Phase 0 — bootstrap"));
}

MainWindow::~MainWindow() = default;

void MainWindow::setupMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* open = file->addAction(tr("&Open..."));
    open->setShortcut(QKeySequence::Open);
    open->setEnabled(false);

    auto* save = file->addAction(tr("&Save"));
    save->setShortcut(QKeySequence::Save);
    save->setEnabled(false);

    file->addSeparator();

    auto* quit = file->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    menuBar()->addMenu(tr("&Edit"));
    menuBar()->addMenu(tr("&Layers"));
    menuBar()->addMenu(tr("&Modules"));
    menuBar()->addMenu(tr("&Help"));
}

void MainWindow::setupCentralPlaceholder() {
    auto* placeholder = new QLabel(
        tr("Map canvas not implemented yet.\n\nPhase 1 will bring load/save parity with BlueBrick .bbm files."),
        this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
}

}
