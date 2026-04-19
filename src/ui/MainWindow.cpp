#include "MainWindow.h"

#include "LayerPanel.h"
#include "FindDialog.h"
#include "LibraryPathsDialog.h"
#include "PreferencesDialog.h"
#include "MapView.h"
#include "ModuleLibraryPanel.h"
#include "ModulesPanel.h"
#include "PartsBrowser.h"

#include "../core/Map.h"
#include "../parts/PartsLibrary.h"
#include "../core/Brick.h"
#include "../core/Ids.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/AnchoredLabel.h"
#include "../core/ColorSpec.h"
#include "../edit/EditCommands.h"
#include "../edit/LabelCommands.h"
#include "../edit/LayerCommands.h"
#include "../edit/ModuleCommands.h"
#include "../saveload/BbmReader.h"
#include "../saveload/BbmWriter.h"
#include "../saveload/SidecarIO.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QInputDialog>
#include <QPainter>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QCheckBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTime>
#include <QTimer>
#include <QHBoxLayout>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUndoStack>
#include <QUuid>

namespace cld::ui {

namespace {
constexpr const char* kLastFileKey = "recent/lastFile";
}

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
    connect(layerPanel_, &LayerPanel::addLayerRequested, this, [this](core::LayerKind k){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(new edit::AddLayerCommand(*mapView_->currentMap(), k));
        layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
        mapView_->rebuildScene();
    });
    connect(layerPanel_, &LayerPanel::deleteLayerRequested, this, [this](int idx){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(new edit::DeleteLayerCommand(*mapView_->currentMap(), idx));
        layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
        mapView_->rebuildScene();
    });
    connect(layerPanel_, &LayerPanel::moveLayerRequested, this, [this](int idx, int delta){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(new edit::MoveLayerCommand(*mapView_->currentMap(), idx, delta));
        layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
        mapView_->rebuildScene();
    });
    connect(layerPanel_, &LayerPanel::renameLayerRequested, this, [this](int idx, const QString& name){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(new edit::RenameLayerCommand(*mapView_->currentMap(), idx, name));
        layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    });
    connect(layerPanel_, &LayerPanel::activeLayerChanged, this, [this](int){
        // Active-layer change is a soft UI state — no undo entry, no scene
        // rebuild. We don't need to do anything else here besides what
        // LayerPanel already did (set map_->selectedLayerIndex).
    });
    connect(layerPanel_, &LayerPanel::layerOptionsRequested, this, [this](int idx){
        auto* map = mapView_->currentMap();
        if (!map || idx < 0 || idx >= static_cast<int>(map->layers().size())) return;
        auto& layer = *map->layers()[idx];
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Layer options"));
        auto* form = new QFormLayout(&dlg);
        auto* nameE = new QLineEdit(layer.name, &dlg);
        form->addRow(tr("Name:"), nameE);
        auto* alphaSpin = new QSpinBox(&dlg);
        alphaSpin->setRange(0, 100); alphaSpin->setSuffix(QStringLiteral(" %"));
        alphaSpin->setValue(layer.transparency);
        form->addRow(tr("Transparency:"), alphaSpin);
        auto* visChk = new QCheckBox(tr("Visible"), &dlg);
        visChk->setChecked(layer.visible);
        form->addRow(visChk);

        // Hull section (vanilla's DisplayHulls on brick/text/ruler layers).
        auto* hullChk = new QCheckBox(tr("Display selection hulls"), &dlg);
        hullChk->setChecked(layer.hull.displayHulls);
        form->addRow(hullChk);
        auto* hullThick = new QSpinBox(&dlg);
        hullThick->setRange(1, 20);
        hullThick->setValue(layer.hull.thickness);
        form->addRow(tr("Hull thickness:"), hullThick);

        // Grid-layer specific section — mirrors LayerGridOptionForm.cs.
        QSpinBox* gridSize = nullptr;
        QSpinBox* gridThick = nullptr;
        QSpinBox* subDiv = nullptr;
        QCheckBox* showGrid = nullptr;
        QCheckBox* showSub = nullptr;
        QCheckBox* showCellIdx = nullptr;
        if (layer.kind() == core::LayerKind::Grid) {
            auto& G = static_cast<core::LayerGrid&>(layer);
            form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Grid layer")), &dlg));
            gridSize = new QSpinBox(&dlg);
            gridSize->setRange(1, 512); gridSize->setSuffix(tr(" studs"));
            gridSize->setValue(G.gridSizeInStud);
            form->addRow(tr("Cell size:"), gridSize);
            gridThick = new QSpinBox(&dlg);
            gridThick->setRange(1, 20);
            gridThick->setValue(static_cast<int>(G.gridThickness));
            form->addRow(tr("Grid line thickness:"), gridThick);
            subDiv = new QSpinBox(&dlg);
            subDiv->setRange(2, 32);
            subDiv->setValue(G.subDivisionNumber);
            form->addRow(tr("Sub-divisions per cell:"), subDiv);
            showGrid = new QCheckBox(tr("Display grid"), &dlg);
            showGrid->setChecked(G.displayGrid);
            form->addRow(showGrid);
            showSub = new QCheckBox(tr("Display sub-grid"), &dlg);
            showSub->setChecked(G.displaySubGrid);
            form->addRow(showSub);
            showCellIdx = new QCheckBox(tr("Display cell index labels"), &dlg);
            showCellIdx->setChecked(G.displayCellIndex);
            form->addRow(showCellIdx);
        }

        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        mapView_->undoStack()->beginMacro(tr("Layer options"));
        if (nameE->text() != layer.name) {
            mapView_->undoStack()->push(new edit::RenameLayerCommand(*map, idx, nameE->text()));
        }
        if (alphaSpin->value() != layer.transparency) {
            mapView_->undoStack()->push(new edit::SetLayerTransparencyCommand(*map, idx, alphaSpin->value()));
        }
        mapView_->undoStack()->endMacro();
        layer.visible = visChk->isChecked();
        layer.hull.displayHulls = hullChk->isChecked();
        layer.hull.thickness = hullThick->value();
        if (layer.kind() == core::LayerKind::Grid) {
            auto& G = static_cast<core::LayerGrid&>(layer);
            G.gridSizeInStud = gridSize->value();
            G.gridThickness = static_cast<float>(gridThick->value());
            G.subDivisionNumber = subDiv->value();
            G.displayGrid = showGrid->isChecked();
            G.displaySubGrid = showSub->isChecked();
            G.displayCellIndex = showCellIdx->isChecked();
        }
        mapView_->rebuildScene();
        layerPanel_->setMap(map, mapView_->builder());
    });

    partsBrowser_ = new PartsBrowser(parts_, this);
    addDockWidget(Qt::LeftDockWidgetArea, partsBrowser_);
    connect(partsBrowser_, &PartsBrowser::partActivated,
            mapView_, &MapView::addPartAtViewCenter);

    modulesPanel_ = new ModulesPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, modulesPanel_);
    moduleLibraryPanel_ = new ModuleLibraryPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, moduleLibraryPanel_);
    connect(moduleLibraryPanel_, &ModuleLibraryPanel::moduleImportRequested,
            this, &MainWindow::onImportModuleFromLibraryPath);
    connect(modulesPanel_, &ModulesPanel::moduleDeleteRequested, this, [this](const QString& id){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(
            new edit::DeleteModuleCommand(*mapView_->currentMap(), id));
        modulesPanel_->setMap(mapView_->currentMap());
    });
    connect(modulesPanel_, &ModulesPanel::createModuleRequested,
            this, &MainWindow::onCreateModuleFromSelection);
    connect(modulesPanel_, &ModulesPanel::importBbmRequested,
            this, &MainWindow::onImportBbmAsModule);

    setupMenus();

    // ----- Toolbar: snap + rotation step presets -----
    auto* toolbar = addToolBar(tr("Edit tools"));
    toolbar->setObjectName(QStringLiteral("toolbar.edit"));
    toolbar->setMovable(true);
    toolbar->addWidget(new QLabel(tr("  Snap: "), this));
    snapCombo_ = new QComboBox(this);
    // (display text, stud step). 0 = off.
    const std::vector<std::pair<QString, double>> snapOptions = {
        { tr("off"),    0.0 },
        { QStringLiteral("32"), 32.0 },
        { QStringLiteral("16"), 16.0 },
        { QStringLiteral("8"),   8.0 },
        { QStringLiteral("4"),   4.0 },
        { QStringLiteral("2"),   2.0 },
        { QStringLiteral("1"),   1.0 },
        { QStringLiteral("0.5"), 0.5 },
    };
    for (const auto& o : snapOptions) snapCombo_->addItem(o.first, o.second);
    toolbar->addWidget(snapCombo_);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("  Tool: "), this));
    auto* toolGroupHost = new QWidget(this);
    auto* toolRow = new QHBoxLayout(toolGroupHost);
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(1);
    auto* selectBtn = new QToolButton(toolGroupHost);
    selectBtn->setText(tr("Select"));
    selectBtn->setCheckable(true); selectBtn->setChecked(true);
    auto* paintBtn = new QToolButton(toolGroupHost);
    paintBtn->setText(tr("Paint")); paintBtn->setCheckable(true);
    auto* eraseBtn = new QToolButton(toolGroupHost);
    eraseBtn->setText(tr("Erase")); eraseBtn->setCheckable(true);
    auto* lineBtn = new QToolButton(toolGroupHost);
    lineBtn->setText(tr("Line")); lineBtn->setCheckable(true);
    lineBtn->setToolTip(tr("Draw a linear ruler (click-drag)"));
    auto* circleBtn = new QToolButton(toolGroupHost);
    circleBtn->setText(tr("Circle")); circleBtn->setCheckable(true);
    circleBtn->setToolTip(tr("Draw a circular ruler (click-drag)"));
    toolRow->addWidget(selectBtn);
    toolRow->addWidget(paintBtn);
    toolRow->addWidget(eraseBtn);
    toolRow->addWidget(lineBtn);
    toolRow->addWidget(circleBtn);
    toolbar->addWidget(toolGroupHost);

    auto* colorBtn = new QToolButton(this);
    colorBtn->setToolTip(tr("Paint colour"));
    auto refreshColorBtn = [this, colorBtn]{
        QPixmap pm(20, 20);
        pm.fill(mapView_->paintColor());
        colorBtn->setIcon(QIcon(pm));
    };
    {
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        const QColor savedColor(s.value(QStringLiteral("paintColor"),
                                          QColor(0, 128, 0).name()).toString());
        s.endGroup();
        if (savedColor.isValid()) mapView_->setPaintColor(savedColor);
    }
    refreshColorBtn();
    toolbar->addWidget(colorBtn);

    auto setTool = [this, selectBtn, paintBtn, eraseBtn, lineBtn, circleBtn](MapView::Tool t){
        selectBtn->setChecked(t == MapView::Tool::Select);
        paintBtn->setChecked(t == MapView::Tool::PaintArea);
        eraseBtn->setChecked(t == MapView::Tool::EraseArea);
        lineBtn->setChecked(t == MapView::Tool::DrawLinearRuler);
        circleBtn->setChecked(t == MapView::Tool::DrawCircularRuler);
        mapView_->setTool(t);
    };
    connect(selectBtn, &QToolButton::clicked, [setTool]{ setTool(MapView::Tool::Select); });
    connect(paintBtn,  &QToolButton::clicked, [setTool]{ setTool(MapView::Tool::PaintArea); });
    connect(eraseBtn,  &QToolButton::clicked, [setTool]{ setTool(MapView::Tool::EraseArea); });
    connect(lineBtn,   &QToolButton::clicked, [setTool]{ setTool(MapView::Tool::DrawLinearRuler); });
    connect(circleBtn, &QToolButton::clicked, [setTool]{ setTool(MapView::Tool::DrawCircularRuler); });
    connect(colorBtn, &QToolButton::clicked, [this, refreshColorBtn]{
        const QColor c = QColorDialog::getColor(mapView_->paintColor(), this,
                                                 tr("Paint colour"),
                                                 QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        mapView_->setPaintColor(c);
        refreshColorBtn();
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        s.setValue(QStringLiteral("paintColor"), c.name(QColor::HexArgb));
        s.endGroup();
    });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("  Rotate: "), this));
    rotCombo_ = new QComboBox(this);
    const std::vector<std::pair<QString, double>> rotOptions = {
        { QStringLiteral("90°"),    90.0 },
        { QStringLiteral("45°"),    45.0 },
        { QStringLiteral("22.5°"),  22.5 },
        { QStringLiteral("11.25°"), 11.25 },
        { QStringLiteral("5°"),     5.0 },
        { QStringLiteral("1°"),     1.0 },
    };
    for (const auto& o : rotOptions) rotCombo_->addItem(o.first, o.second);
    toolbar->addWidget(rotCombo_);

    // Restore from QSettings.
    {
        QSettings s;
        s.beginGroup(QStringLiteral("editing"));
        const double snap = s.value(QStringLiteral("snapStepStuds"), 0.0).toDouble();
        const double rot  = s.value(QStringLiteral("rotationStepDegrees"), 90.0).toDouble();
        s.endGroup();
        mapView_->setSnapStepStuds(snap);
        mapView_->setRotationStepDegrees(rot);
        for (int i = 0; i < snapCombo_->count(); ++i) {
            if (qFuzzyCompare(snapCombo_->itemData(i).toDouble() + 1.0, snap + 1.0)) {
                snapCombo_->setCurrentIndex(i); break;
            }
        }
        for (int i = 0; i < rotCombo_->count(); ++i) {
            if (qFuzzyCompare(rotCombo_->itemData(i).toDouble(), rot)) {
                rotCombo_->setCurrentIndex(i); break;
            }
        }
    }
    connect(snapCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){
        const double v = snapCombo_->currentData().toDouble();
        mapView_->setSnapStepStuds(v);
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        s.setValue(QStringLiteral("snapStepStuds"), v); s.endGroup();
    });
    connect(rotCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){
        const double v = rotCombo_->currentData().toDouble();
        mapView_->setRotationStepDegrees(v);
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        s.setValue(QStringLiteral("rotationStepDegrees"), v); s.endGroup();
    });

    updateTitle();

    connect(mapView_->undoStack(), &QUndoStack::indexChanged,
            this, [this](int){ updateTitle(); });
    connect(mapView_->undoStack(), &QUndoStack::cleanChanged,
            this, [this](bool){ updateTitle(); });

    // Permanent right-side status label showing map dimensions in studs,
    // modules (96 studs), and meters (1 stud = 8 mm). Upstream shows the same
    // trio in its status bar. Refreshed on every undo-stack change so it
    // stays live while the user edits.
    auto* dimsLabel = new QLabel(this);
    statusBar()->addPermanentWidget(dimsLabel);
    auto refreshDims = [this, dimsLabel]{
        if (!mapView_->currentMap()) { dimsLabel->clear(); return; }
        const QRectF bb = mapView_->scene()->itemsBoundingRect();
        if (bb.isEmpty()) { dimsLabel->setText(tr("empty")); return; }
        const double studsPerPx = 1.0 / 8.0;
        const double wStud = bb.width()  * studsPerPx;
        const double hStud = bb.height() * studsPerPx;
        dimsLabel->setText(tr("%1 × %2 studs  (%3 × %4 m)")
            .arg(wStud, 0, 'f', 0).arg(hStud, 0, 'f', 0)
            .arg(wStud * 0.008, 0, 'f', 2).arg(hStud * 0.008, 0, 'f', 2));
    };
    connect(mapView_->undoStack(), &QUndoStack::indexChanged, this, [refreshDims](int){ refreshDims(); });
    QTimer::singleShot(0, this, refreshDims);

    // Live selection readout — helps confirm that clicks and drag-select
    // are actually producing a selection. Updates whenever the scene's
    // selection set changes.
    auto* selLabel = new QLabel(tr("no selection"), this);
    statusBar()->addPermanentWidget(selLabel);
    connect(mapView_, &MapView::selectionChanged, this, [this, selLabel]{
        const int n = mapView_->scene()->selectedItems().size();
        selLabel->setText(n == 0 ? tr("no selection")
                                 : tr("selected: %1").arg(n));
    });

    // Auto-save: 60s tick, writes to AppDataLocation/autosave.bbm whenever
    // the undo stack is dirty. Cheap enough to run unconditionally; the
    // writeMapTo path is the same used for manual saves.
    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(60 * 1000);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::performAutosave);
    autosaveTimer_->start();

    statusBar()->showMessage(
        tr("Parts library: %1 parts indexed").arg(parts_.partCount()));

    // Restore the window geometry + dock layout the user left at last exit,
    // and assign stable object names so Qt can identify each dock on save/restore.
    layerPanel_->setObjectName(QStringLiteral("dock.layers"));
    partsBrowser_->setObjectName(QStringLiteral("dock.parts"));
    modulesPanel_->setObjectName(QStringLiteral("dock.modules"));
    moduleLibraryPanel_->setObjectName(QStringLiteral("dock.moduleLibrary"));
    QSettings s;
    s.beginGroup(QStringLiteral("ui"));
    const QByteArray geom = s.value(QStringLiteral("geometry")).toByteArray();
    const QByteArray state = s.value(QStringLiteral("state")).toByteArray();
    s.endGroup();
    if (!geom.isEmpty())  restoreGeometry(geom);
    if (!state.isEmpty()) restoreState(state);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* newAct = file->addAction(tr("&New"));
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &MainWindow::onNew);

    auto* openAct = file->addAction(tr("&Open..."));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpen);

    recentMenu_ = file->addMenu(tr("Open &Recent"));
    rebuildRecentMenu();

    auto* saveAct = file->addAction(tr("&Save"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::onSave);

    auto* saveAsAct = file->addAction(tr("Save &As..."));
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::onSaveAs);

    file->addSeparator();
    auto* exportAct = file->addAction(tr("Export as &Image..."));
    connect(exportAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap()) return;
        auto* scene = mapView_->scene();
        const QRectF bounds = scene->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        if (bounds.isEmpty()) {
            QMessageBox::information(this, tr("Export"), tr("The map is empty."));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export map as image"),
            currentFilePath_.isEmpty() ? QString() : QFileInfo(currentFilePath_).baseName() + ".png",
            tr("PNG (*.png);;JPEG (*.jpg *.jpeg)"));
        if (path.isEmpty()) return;
        bool ok = false;
        const int width = QInputDialog::getInt(
            this, tr("Image width"),
            tr("Output width in pixels (height auto-scales):"), 1600, 128, 16384, 1, &ok);
        if (!ok) return;
        const double aspect = bounds.height() / bounds.width();
        const int height = std::max(64, static_cast<int>(width * aspect));
        QImage img(width, height, QImage::Format_ARGB32);
        img.fill(mapView_->currentMap()->backgroundColor.color);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            scene->render(&p, QRectF(0, 0, width, height), bounds, Qt::KeepAspectRatio);
        }
        if (!img.save(path)) {
            QMessageBox::warning(this, tr("Export failed"), tr("Could not write %1").arg(path));
            return;
        }
        statusBar()->showMessage(tr("Exported %1x%2 to %3").arg(width).arg(height).arg(path), 5000);
    });

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
    auto* rotCCW = edit->addAction(tr("Rotate &CCW"));
    rotCCW->setShortcut(Qt::Key_R);
    connect(rotCCW, &QAction::triggered, [this]{
        mapView_->rotateSelected(static_cast<float>(-mapView_->rotationStepDegrees()));
    });
    auto* rotCW = edit->addAction(tr("Rotate C&W"));
    rotCW->setShortcut(QKeySequence(tr("Shift+R")));
    connect(rotCW, &QAction::triggered, [this]{
        mapView_->rotateSelected(static_cast<float>(mapView_->rotationStepDegrees()));
    });

    edit->addSeparator();
    auto* cutAct = edit->addAction(tr("Cu&t"));
    cutAct->setShortcut(QKeySequence::Cut);
    connect(cutAct, &QAction::triggered, [this]{ mapView_->cutSelection(); });

    auto* copyAct = edit->addAction(tr("&Copy"));
    copyAct->setShortcut(QKeySequence::Copy);
    connect(copyAct, &QAction::triggered, [this]{ mapView_->copySelection(); });

    auto* pasteAct = edit->addAction(tr("&Paste"));
    pasteAct->setShortcut(QKeySequence::Paste);
    connect(pasteAct, &QAction::triggered, [this]{ mapView_->pasteClipboard(); });

    auto* dupAct = edit->addAction(tr("&Duplicate"));
    dupAct->setShortcut(QKeySequence(tr("Ctrl+D")));
    connect(dupAct, &QAction::triggered, [this]{ mapView_->duplicateSelection(); });

    edit->addSeparator();
    auto* selAllAct = edit->addAction(tr("Select &All"));
    selAllAct->setShortcut(QKeySequence::SelectAll);
    connect(selAllAct, &QAction::triggered, [this]{ mapView_->selectAll(); });

    auto* selNoneAct = edit->addAction(tr("Deselect All"));
    selNoneAct->setShortcut(QKeySequence(tr("Ctrl+Shift+A")));
    connect(selNoneAct, &QAction::triggered, [this]{ mapView_->deselectAll(); });

    auto* findAct = edit->addAction(tr("&Find && Replace..."));
    findAct->setShortcut(QKeySequence::Find);
    connect(findAct, &QAction::triggered, this, [this]{
        // Modeless: construct, show, let the user close it later. The dialog
        // deletes itself on close via the WA_DeleteOnClose attribute.
        auto* dlg = new FindDialog(*mapView_, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

    auto* selPathAct = edit->addAction(tr("Select &Path"));
    selPathAct->setShortcut(QKeySequence(tr("Ctrl+P")));
    selPathAct->setToolTip(tr("Extend selection to every brick connected to current selection"));
    connect(selPathAct, &QAction::triggered, [this]{ mapView_->selectPath(); });

    edit->addSeparator();
    // Move Step submenu — nudge selection by the current snap step (or a
    // specific override). Mirrors BlueBrick's Edit > Transform > Move Step.
    auto* moveStepMenu = edit->addMenu(tr("&Move Step"));
    auto addNudge = [this, moveStepMenu](const QString& label, double dx, double dy){
        auto* a = moveStepMenu->addAction(label);
        connect(a, &QAction::triggered, this, [this, dx, dy]{
            mapView_->nudgeSelected(dx, dy);
        });
    };
    addNudge(tr("Up"),    0.0, -1.0);
    addNudge(tr("Down"),  0.0,  1.0);
    addNudge(tr("Left"), -1.0,  0.0);
    addNudge(tr("Right"), 1.0,  0.0);

    auto* groupAct = edit->addAction(tr("&Group"));
    groupAct->setShortcut(QKeySequence(tr("Ctrl+G")));
    connect(groupAct, &QAction::triggered, [this]{ mapView_->groupSelection(); });
    auto* ungroupAct = edit->addAction(tr("&Ungroup"));
    ungroupAct->setShortcut(QKeySequence(tr("Ctrl+Shift+G")));
    connect(ungroupAct, &QAction::triggered, [this]{ mapView_->ungroupSelection(); });

    edit->addSeparator();
    auto* addTextAct = edit->addAction(tr("Add &Text..."));
    addTextAct->setShortcut(QKeySequence(tr("Ctrl+T")));
    connect(addTextAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap()) return;
        bool ok = false;
        const QString text = QInputDialog::getText(
            this, tr("Add text"), tr("Label text:"),
            QLineEdit::Normal, {}, &ok);
        if (ok && !text.isEmpty()) mapView_->addTextAtViewCenter(text);
    });

    edit->addSeparator();
    auto* toFrontAct = edit->addAction(tr("Bring to &Front"));
    toFrontAct->setShortcut(QKeySequence(tr("Ctrl+Shift+]")));
    connect(toFrontAct, &QAction::triggered, [this]{ mapView_->bringSelectionToFront(); });

    auto* toBackAct = edit->addAction(tr("Send to &Back"));
    toBackAct->setShortcut(QKeySequence(tr("Ctrl+Shift+[")));
    connect(toBackAct, &QAction::triggered, [this]{ mapView_->sendSelectionToBack(); });

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
        L.id = core::newBbmId();
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

    view->addSeparator();
    // Dock / toolbar visibility toggles — upstream View menu parity.
    auto addDockToggle = [view](QDockWidget* d, const QString& label){
        auto* act = view->addAction(label);
        act->setCheckable(true);
        act->setChecked(d->isVisible());
        QObject::connect(act, &QAction::toggled, d, &QDockWidget::setVisible);
        QObject::connect(d, &QDockWidget::visibilityChanged, act, &QAction::setChecked);
    };
    addDockToggle(partsBrowser_,       tr("&Parts Panel"));
    addDockToggle(layerPanel_,         tr("&Layers Panel"));
    addDockToggle(modulesPanel_,       tr("&Modules Panel"));
    addDockToggle(moduleLibraryPanel_, tr("Module Li&brary Panel"));

    view->addSeparator();
    auto* statusToggle = view->addAction(tr("&Status Bar"));
    statusToggle->setCheckable(true);
    statusToggle->setChecked(true);
    connect(statusToggle, &QAction::toggled, statusBar(), &QStatusBar::setVisible);

    auto* scrollToggle = view->addAction(tr("Map &Scroll Bars"));
    scrollToggle->setCheckable(true);
    scrollToggle->setChecked(true);
    connect(scrollToggle, &QAction::toggled, this, [this](bool on){
        const Qt::ScrollBarPolicy p = on ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
        mapView_->setHorizontalScrollBarPolicy(p);
        mapView_->setVerticalScrollBarPolicy(p);
    });

    view->addSeparator();
    // Scene-level render toggles — each writes through QSettings so the
    // choice sticks across launches. SceneBuilder reads these on rebuild
    // to honour the current preference.
    auto addRenderToggle = [this, view](const QString& label, const QString& settingsKey, bool defaultOn){
        auto* act = view->addAction(label);
        act->setCheckable(true);
        QSettings s;
        act->setChecked(s.value(settingsKey, defaultOn).toBool());
        connect(act, &QAction::toggled, this, [this, settingsKey](bool on){
            QSettings().setValue(settingsKey, on);
            mapView_->rebuildScene();
        });
    };
    addRenderToggle(tr("&Electric Circuits"),       QStringLiteral("view/electricCircuits"),    false);
    addRenderToggle(tr("Connection &Points"),       QStringLiteral("view/connectionPoints"),    false);
    addRenderToggle(tr("Ruler Attach P&oints"),      QStringLiteral("view/rulerAttachPoints"),   false);
    addRenderToggle(tr("&Watermark"),                QStringLiteral("view/watermark"),           true);
    addRenderToggle(tr("Brick &Hulls"),              QStringLiteral("view/brickHulls"),          false);
    addRenderToggle(tr("Brick E&levation Labels"),   QStringLiteral("view/brickElevation"),      false);

    auto* tools = menuBar()->addMenu(tr("&Tools"));
    auto* libAct = tools->addAction(tr("Manage Parts &Libraries..."));
    connect(libAct, &QAction::triggered, this, &MainWindow::onManageLibraries);
    auto* reloadAct = tools->addAction(tr("&Reload Parts Library"));
    connect(reloadAct, &QAction::triggered, this, &MainWindow::onReloadLibrary);
    tools->addSeparator();
    auto* partListAct = tools->addAction(tr("Export &Part List (CSV)..."));
    connect(partListAct, &QAction::triggered, this, &MainWindow::onExportPartList);
    tools->addSeparator();
    auto* prefsAct = tools->addAction(tr("&Preferences..."));
    prefsAct->setShortcut(QKeySequence::Preferences);
    connect(prefsAct, &QAction::triggered, this, [this]{
        PreferencesDialog dlg(this);
        dlg.exec();
        // Re-apply editing-related settings the user may have changed.
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        mapView_->setSnapStepStuds(s.value(QStringLiteral("snapStepStuds"), 0.0).toDouble());
        mapView_->setRotationStepDegrees(s.value(QStringLiteral("rotationStepDegrees"), 90.0).toDouble());
        s.endGroup();
    });

    // ----- Map menu -----
    auto* mapMenu = menuBar()->addMenu(tr("&Map"));
    auto* bgAct = mapMenu->addAction(tr("Background &Colour..."));
    connect(bgAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        QColor init = m->backgroundColor.color;
        const QColor c = QColorDialog::getColor(init, this, tr("Background colour"),
                                                 QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        mapView_->undoStack()->push(new edit::ChangeBackgroundColorCommand(
            *m, core::ColorSpec::fromArgb(c)));
        mapView_->rebuildScene();
        mapView_->scene()->setBackgroundBrush(c);
    });
    auto* infoAct = mapMenu->addAction(tr("General &Info..."));
    connect(infoAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Map information"));
        auto* form = new QFormLayout(&dlg);
        auto* authorE = new QLineEdit(m->author, &dlg);
        auto* lugE    = new QLineEdit(m->lug, &dlg);
        auto* eventE  = new QLineEdit(m->event, &dlg);
        auto* dateE   = new QDateEdit(m->date, &dlg); dateE->setCalendarPopup(true);
        auto* commentE = new QPlainTextEdit(m->comment, &dlg);
        commentE->setMinimumHeight(100);
        form->addRow(tr("Author:"),  authorE);
        form->addRow(tr("LUG:"),     lugE);
        form->addRow(tr("Event:"),   eventE);
        form->addRow(tr("Date:"),    dateE);
        form->addRow(tr("Comment:"), commentE);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        edit::ChangeGeneralInfoCommand::Info next{
            authorE->text(), lugE->text(), eventE->text(),
            dateE->date(), commentE->toPlainText()
        };
        mapView_->undoStack()->push(new edit::ChangeGeneralInfoCommand(*m, std::move(next)));
    });

    menuBar()->addMenu(tr("&Layers"));

    auto* modules = menuBar()->addMenu(tr("&Modules"));
    auto* createModAct = modules->addAction(tr("Create from &Selection..."));
    connect(createModAct, &QAction::triggered, this, &MainWindow::onCreateModuleFromSelection);
    auto* importModAct = modules->addAction(tr("&Import .bbm as Module..."));
    connect(importModAct, &QAction::triggered, this, &MainWindow::onImportBbmAsModule);
    auto* saveModAct = modules->addAction(tr("&Save Selection as Module..."));
    connect(saveModAct, &QAction::triggered, this, &MainWindow::onSaveSelectionAsModule);

    auto* help = menuBar()->addMenu(tr("&Help"));
    auto* aboutAct = help->addAction(tr("&About CLD..."));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);
    auto* aboutQtAct = help->addAction(tr("About &Qt..."));
    connect(aboutQtAct, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::onCreateModuleFromSelection() {
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
}

void MainWindow::onSaveSelectionAsModule() {
    auto* map = mapView_->currentMap();
    if (!map) return;
    // Gather selected bricks by (layer, guid).
    struct PickedBrick { int layerIndex; core::Brick brick; };
    std::vector<PickedBrick> picks;
    for (QGraphicsItem* it : mapView_->scene()->selectedItems()) {
        if (it->data(2).toString() != QStringLiteral("brick")) continue;
        const int li = it->data(0).toInt();
        const QString guid = it->data(1).toString();
        if (li < 0 || li >= static_cast<int>(map->layers().size())) continue;
        auto* L = map->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
            if (b.guid == guid) { picks.push_back({ li, b }); break; }
        }
    }
    if (picks.empty()) {
        QMessageBox::information(this, tr("Save module"),
            tr("Select one or more bricks first."));
        return;
    }

    // Build a fresh Map containing just those bricks in a single brick layer
    // so it can be re-loaded into any other project later via the module
    // library panel (or Modules → Import .bbm as Module).
    core::Map module;
    module.author = map->author;
    module.lug = map->lug;
    module.event = QObject::tr("Module");
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    layer->name = QStringLiteral("Module");
    for (const auto& p : picks) layer->bricks.push_back(p.brick);
    module.nbItems = static_cast<int>(layer->bricks.size());
    module.layers().push_back(std::move(layer));

    // Pick target path: if module library path is configured, default there;
    // otherwise fall back to QFileDialog's default.
    QString startDir = moduleLibraryPanel_->libraryPath();
    QDir().mkpath(startDir);  // best-effort
    const QString defaultName = QInputDialog::getText(
        this, tr("Save module"), tr("Module name:"),
        QLineEdit::Normal, tr("New Module"));
    if (defaultName.isEmpty()) return;
    QString target = QFileDialog::getSaveFileName(
        this, tr("Save selection as module"),
        startDir.isEmpty() ? defaultName + ".bbm" : QDir(startDir).filePath(defaultName + ".bbm"),
        tr("BlueBrick map (*.bbm)"));
    if (target.isEmpty()) return;
    if (!target.endsWith(QStringLiteral(".bbm"), Qt::CaseInsensitive)) target += QStringLiteral(".bbm");

    auto r = saveload::writeBbm(module, target);
    if (!r.ok) {
        QMessageBox::warning(this, tr("Save module failed"), r.error);
        return;
    }
    statusBar()->showMessage(tr("Saved %1 bricks to %2").arg(picks.size()).arg(target), 4000);
    moduleLibraryPanel_->refresh();
}

void MainWindow::onImportModuleFromLibraryPath(const QString& bbmPath) {
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
    auto loaded = saveload::readBbm(bbmPath);
    if (!loaded.ok()) {
        QMessageBox::warning(this, tr("Import failed"), loaded.error);
        return;
    }
    std::vector<core::Brick> bricks;
    for (const auto& L : loaded.map->layers()) {
        if (L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
            core::Brick copy = b;
            copy.guid.clear();  // regenerated inside the command
            bricks.push_back(std::move(copy));
        }
    }
    if (bricks.empty()) return;
    const QString name = QFileInfo(bbmPath).baseName();
    const int count = static_cast<int>(bricks.size());
    mapView_->undoStack()->push(new edit::ImportBbmAsModuleCommand(
        *map, targetLayer, bbmPath, name, std::move(bricks)));
    mapView_->rebuildScene();
    modulesPanel_->setMap(map);
    statusBar()->showMessage(
        tr("Imported %1 bricks from module '%2'").arg(count).arg(name), 4000);
}

void MainWindow::onImportBbmAsModule() {
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
    const int imported = static_cast<int>(bricks.size());
    mapView_->undoStack()->push(new edit::ImportBbmAsModuleCommand(
        *map, targetLayer, path, name, std::move(bricks)));
    mapView_->rebuildScene();
    modulesPanel_->setMap(map);
    statusBar()->showMessage(
        tr("Imported %1 bricks as module '%2'").arg(imported).arg(name), 4000);
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
    QSettings().setValue(QString::fromLatin1(kLastFileKey), path);
    pushRecentFile(path);
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
    QSettings().setValue(QString::fromLatin1(kLastFileKey), path);
    pushRecentFile(path);
    // Manual save supersedes any autosave for the session.
    QFile::remove(autosavePath());
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
    if (!maybeSave()) { e->ignore(); return; }
    // Persist window geometry + dock layout for the next launch.
    QSettings s;
    s.beginGroup(QStringLiteral("ui"));
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("state"),    saveState());
    s.endGroup();
    e->accept();
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

void MainWindow::onNew() {
    if (!maybeSave()) return;
    auto blank = std::make_unique<core::Map>();
    // Seed with a single brick layer so drop-onto-map works out of the box.
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    layer->name = tr("Bricks");
    blank->layers().push_back(std::move(layer));
    blank->nbItems = 0;
    mapView_->loadMap(std::move(blank));
    layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    modulesPanel_->setMap(mapView_->currentMap());
    currentFilePath_.clear();
    mapView_->undoStack()->clear();
    mapView_->undoStack()->setClean();
    updateTitle();
    statusBar()->showMessage(tr("New layout"), 3000);
}

void MainWindow::onReloadLibrary() {
    QStringList allPaths;
    const QString vendored = defaultVendoredPartsRoot();
    if (!vendored.isEmpty() && QDir(vendored).exists()) allPaths << vendored;
    for (const QString& p : loadUserLibraryPaths()) {
        if (!allPaths.contains(p) && QDir(p).exists()) allPaths << p;
    }
    rescanLibrary(allPaths);
    partsBrowser_->rebuild();
    mapView_->rebuildScene();
    statusBar()->showMessage(
        tr("Reloaded library: %1 parts across %2 path(s)")
            .arg(parts_.partCount()).arg(allPaths.size()), 4000);
}

void MainWindow::onExportPartList() {
    auto* map = mapView_->currentMap();
    if (!map) return;
    // Aggregate part counts across every brick layer. Output format mirrors
    // vanilla's ExportPartList: "part number, count".
    QHash<QString, int> counts;
    for (const auto& L : map->layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
            counts[b.partNumber]++;
        }
    }
    if (counts.isEmpty()) {
        QMessageBox::information(this, tr("Export part list"),
            tr("The current layout contains no bricks."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export part list"),
        currentFilePath_.isEmpty()
            ? QStringLiteral("parts.csv")
            : QFileInfo(currentFilePath_).baseName() + ".csv",
        tr("CSV (*.csv);;Text (*.txt)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"),
            tr("Cannot write %1: %2").arg(path, f.errorString()));
        return;
    }
    // Sort alphabetically for a stable / diffable output.
    QStringList keys = counts.keys(); std::sort(keys.begin(), keys.end());
    f.write("PartNumber,Count\n");
    int total = 0;
    for (const QString& k : keys) {
        f.write(QStringLiteral("%1,%2\n").arg(k).arg(counts[k]).toUtf8());
        total += counts[k];
    }
    f.close();
    statusBar()->showMessage(tr("Exported %1 unique parts, %2 total, to %3")
        .arg(keys.size()).arg(total).arg(path), 5000);
}

void MainWindow::onAbout() {
    QMessageBox::about(this, tr("About Collaborative Layout Designer"),
        tr("<h3>Collaborative Layout Designer</h3>"
           "<p>Cross-platform C++/Qt 6 fork of <b>BlueBrick</b> by Alban Nanty and contributors.</p>"
           "<p>Adds cross-layer modules, anchored text labels, and event venues on top of the "
           "vanilla <i>.bbm</i> format (extra metadata stored in a sidecar <i>.bbm.cld</i> so "
           "vanilla BlueBrick 1.9.2 keeps opening our files).</p>"
           "<p>Licensed under GPL-3.0 — same as upstream BlueBrick.</p>"
           "<p>Parts library: %1 parts indexed.</p>")
        .arg(parts_.partCount()));
}

// Recent-files list: persisted as QStringList under recent/list, capped to 12.
namespace {
constexpr const char* kRecentListKey = "recent/list";
constexpr int kRecentMax = 12;
}

void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    const QStringList list = QSettings().value(kRecentListKey).toStringList();
    for (const QString& p : list) {
        QAction* act = recentMenu_->addAction(QFileInfo(p).fileName());
        act->setToolTip(p);
        connect(act, &QAction::triggered, this, [this, p]{ openFile(p); });
    }
    if (list.isEmpty()) {
        auto* empty = recentMenu_->addAction(tr("(no recent files)"));
        empty->setEnabled(false);
    } else {
        recentMenu_->addSeparator();
        auto* clear = recentMenu_->addAction(tr("Clear Menu"));
        connect(clear, &QAction::triggered, this, [this]{
            QSettings().remove(QString::fromLatin1(kRecentListKey));
            rebuildRecentMenu();
        });
    }
}

void MainWindow::pushRecentFile(const QString& path) {
    QSettings s;
    QStringList list = s.value(kRecentListKey).toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kRecentMax) list.removeLast();
    s.setValue(kRecentListKey, list);
    rebuildRecentMenu();
}

QString MainWindow::autosavePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/autosave.bbm");
}

void MainWindow::performAutosave() {
    if (!mapView_->currentMap() || mapView_->undoStack()->isClean()) return;
    const QString path = autosavePath();
    auto res = saveload::writeBbm(*mapView_->currentMap(), path);
    if (res.ok) {
        // Keep a note of what file the autosave corresponds to so the startup
        // prompt can mention the original filename.
        QSettings().setValue(QStringLiteral("autosave/sourceFile"), currentFilePath_);
        statusBar()->showMessage(tr("Autosaved (%1)").arg(QTime::currentTime().toString("HH:mm:ss")), 2000);
    }
}

bool MainWindow::restoreAutosaveIfAny(const QString& lastFile) {
    const QString path = autosavePath();
    if (!QFile::exists(path)) return false;
    const QFileInfo ai(path);
    if (!lastFile.isEmpty()) {
        const QFileInfo li(lastFile);
        if (li.exists() && li.lastModified() >= ai.lastModified()) return false;
    }
    const QString source = QSettings().value(QStringLiteral("autosave/sourceFile")).toString();
    const auto btn = QMessageBox::question(
        this, tr("Restore autosave?"),
        tr("An unsaved layout from the last session was recovered:\n%1\n\n"
           "Original file: %2\n"
           "Restore it?")
            .arg(path, source.isEmpty() ? tr("(new file)") : source),
        QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes) return false;
    openFile(path);
    currentFilePath_ = source;   // so a subsequent Save overwrites the original
    updateTitle();
    return true;
}

void MainWindow::onZoomIn()  { mapView_->scale(1.2, 1.2); }
void MainWindow::onZoomOut() { mapView_->scale(1 / 1.2, 1 / 1.2); }

void MainWindow::onFitToView() {
    if (!mapView_->currentMap() || mapView_->scene()->itemsBoundingRect().isEmpty()) return;
    mapView_->fitInView(mapView_->scene()->itemsBoundingRect().adjusted(-50, -50, 50, 50),
                         Qt::KeepAspectRatio);
}

}
