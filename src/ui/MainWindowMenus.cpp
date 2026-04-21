// Menu-bar setup pulled out of MainWindow.cpp so the latter can focus on
// construction, docks, file I/O, and high-level coordination. One class,
// multiple translation units — MOC only needs the header.

#include "MainWindow.h"

#include "BudgetDialog.h"
#include "FindDialog.h"
#include "LayerPanel.h"
#include "MapView.h"
#include "ModuleLibraryPanel.h"
#include "ModulesPanel.h"
#include "PartsBrowser.h"
#include "PartUsagePanel.h"
#include "PreferencesDialog.h"
#include "VenueDialog.h"
#include "VenueDimensionsDialog.h"
#include "../core/AnchoredLabel.h"
#include "../core/ColorSpec.h"
#include "../core/Ids.h"
#include "../core/Map.h"
#include "../edit/EditCommands.h"
#include "../edit/LabelCommands.h"
#include "../edit/LayerCommands.h"
#include "../edit/VenueCommands.h"
#include "../import/LDrawReader.h"
#include "../import/StudioReader.h"
#include "../import/LDDReader.h"
#include "../import/ImportToPart.h"
#include "../edit/Connectivity.h"
#include "../core/LayerBrick.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"
#include "../saveload/VenueIO.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
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
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUndoStack>

namespace cld::ui {

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
        // ExportImageForm.cs parity: width, keep-aspect/height, watermark,
        // background fill, transparent background, antialias, path. Each
        // setting is persisted under export/* so the next export remembers
        // the user's preferences.
        QSettings s;
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Export image"));
        auto* form = new QFormLayout(&dlg);
        auto* pathEdit = new QLineEdit(
            currentFilePath_.isEmpty()
                ? s.value(QStringLiteral("export/path")).toString()
                : QFileInfo(currentFilePath_).baseName() + QStringLiteral(".png"), &dlg);
        auto* browseBtn = new QPushButton(tr("..."), &dlg);
        auto* pathRow = new QHBoxLayout(); pathRow->addWidget(pathEdit); pathRow->addWidget(browseBtn);
        auto* pathWrap = new QWidget(&dlg); pathWrap->setLayout(pathRow);
        form->addRow(tr("Output file:"), pathWrap);
        connect(browseBtn, &QPushButton::clicked, &dlg, [pathEdit, &dlg]{
            const QString p = QFileDialog::getSaveFileName(&dlg, tr("Export map as image"),
                pathEdit->text(), tr("PNG (*.png);;JPEG (*.jpg *.jpeg)"));
            if (!p.isEmpty()) pathEdit->setText(p);
        });

        auto* widthSpin = new QSpinBox(&dlg);
        widthSpin->setRange(128, 16384);
        widthSpin->setValue(s.value(QStringLiteral("export/width"), 1600).toInt());
        form->addRow(tr("Width (px):"), widthSpin);
        auto* keepAspect = new QCheckBox(tr("Keep aspect ratio (height auto)"), &dlg);
        keepAspect->setChecked(s.value(QStringLiteral("export/keepAspect"), true).toBool());
        form->addRow(keepAspect);
        auto* heightSpin = new QSpinBox(&dlg);
        heightSpin->setRange(64, 16384);
        heightSpin->setValue(s.value(QStringLiteral("export/height"), 1200).toInt());
        heightSpin->setEnabled(!keepAspect->isChecked());
        connect(keepAspect, &QCheckBox::toggled, heightSpin, [heightSpin](bool on){
            heightSpin->setEnabled(!on);
        });
        form->addRow(tr("Height (px):"), heightSpin);

        auto* watermarkChk = new QCheckBox(tr("Embed general-info watermark"), &dlg);
        watermarkChk->setChecked(s.value(QStringLiteral("export/watermark"), false).toBool());
        form->addRow(watermarkChk);
        auto* transparentChk = new QCheckBox(tr("Transparent background"), &dlg);
        transparentChk->setChecked(s.value(QStringLiteral("export/transparent"), false).toBool());
        form->addRow(transparentChk);
        auto* antialiasChk = new QCheckBox(tr("Antialias"), &dlg);
        antialiasChk->setChecked(s.value(QStringLiteral("export/antialias"), true).toBool());
        form->addRow(antialiasChk);

        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        const QString path = pathEdit->text();
        if (path.isEmpty()) return;

        const int width  = widthSpin->value();
        const int height = keepAspect->isChecked()
            ? std::max(64, static_cast<int>(width * (bounds.height() / bounds.width())))
            : heightSpin->value();
        QImage img(width, height,
                   transparentChk->isChecked() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
        img.fill(transparentChk->isChecked()
                     ? Qt::transparent
                     : mapView_->currentMap()->backgroundColor.color);
        {
            QPainter p(&img);
            if (antialiasChk->isChecked()) {
                p.setRenderHint(QPainter::Antialiasing);
                p.setRenderHint(QPainter::SmoothPixmapTransform);
            }
            scene->render(&p, QRectF(0, 0, width, height), bounds,
                          keepAspect->isChecked() ? Qt::KeepAspectRatio : Qt::IgnoreAspectRatio);
            if (watermarkChk->isChecked()) {
                const auto* m = mapView_->currentMap();
                const QString stamp = tr("%1 / %2 / %3").arg(m->author, m->lug, m->event);
                QFont f; f.setPointSize(std::max(8, height / 60));
                p.setFont(f);
                p.setPen(QColor(0, 0, 0, 140));
                p.drawText(QRectF(0, 0, width, height).adjusted(10, 0, -10, -10),
                           Qt::AlignRight | Qt::AlignBottom, stamp);
            }
        }
        if (!img.save(path)) {
            QMessageBox::warning(this, tr("Export failed"), tr("Could not write %1").arg(path));
            return;
        }
        s.setValue(QStringLiteral("export/path"),        path);
        s.setValue(QStringLiteral("export/width"),       width);
        s.setValue(QStringLiteral("export/height"),      height);
        s.setValue(QStringLiteral("export/keepAspect"),  keepAspect->isChecked());
        s.setValue(QStringLiteral("export/watermark"),   watermarkChk->isChecked());
        s.setValue(QStringLiteral("export/transparent"), transparentChk->isChecked());
        s.setValue(QStringLiteral("export/antialias"),   antialiasChk->isChecked());
        statusBar()->showMessage(tr("Exported %1x%2 to %3").arg(width).arg(height).arg(path), 5000);
    });

    auto* pdfAct = file->addAction(tr("Export as P&DF..."));
    connect(pdfAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap()) return;
        auto* scene = mapView_->scene();
        const QRectF bounds = scene->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        if (bounds.isEmpty()) {
            QMessageBox::information(this, tr("Export"), tr("The map is empty."));
            return;
        }
        const QString suggested = currentFilePath_.isEmpty()
            ? QStringLiteral("layout.pdf")
            : QFileInfo(currentFilePath_).baseName() + QStringLiteral(".pdf");
        const QString path = QFileDialog::getSaveFileName(this,
            tr("Export map as PDF"), suggested, tr("PDF (*.pdf)"));
        if (path.isEmpty()) return;

        // A3 at 300dpi is plenty for typical BlueBrick layouts; the
        // QPrinter::HighResolution preset gives us 1200dpi though which
        // keeps sprites crisp when users scale the PDF up further.
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        // Fit the layout to a sensible page. Portrait vs landscape is
        // chosen to match the layout's aspect ratio so content fills
        // the page rather than leaving big margins.
        QPageLayout layout(
            QPageSize(QPageSize::A3),
            bounds.width() >= bounds.height()
                ? QPageLayout::Landscape
                : QPageLayout::Portrait,
            QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
        printer.setPageLayout(layout);

        QPainter p(&printer);
        if (!p.isActive()) {
            QMessageBox::warning(this, tr("Export failed"),
                tr("Could not open the PDF for writing."));
            return;
        }
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        scene->render(&p, QRectF(printer.pageLayout().paintRectPixels(printer.resolution())),
                      bounds, Qt::KeepAspectRatio);
        p.end();
        statusBar()->showMessage(tr("Exported PDF to %1").arg(path), 5000);
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
        QString targetId;
        core::AnchorKind kind = core::AnchorKind::World;
        QPointF offsetStuds;
        auto sel = mapView_->scene()->selectedItems();
        if (sel.size() == 1 && sel[0]->data(2).toString() == QStringLiteral("brick")) {
            targetId = sel[0]->data(1).toString();
            kind = core::AnchorKind::Brick;
            offsetStuds = QPointF(2.0, -2.0);
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
    addDockToggle(partUsagePanel_,     tr("&Used Parts Panel"));

    view->addSeparator();
    auto* statusToggle = view->addAction(tr("&Status Bar"));
    statusToggle->setCheckable(true);
    statusToggle->setChecked(true);
    connect(statusToggle, &QAction::toggled, statusBar(), &QStatusBar::setVisible);

    auto* scrollToggle = view->addAction(tr("Map &Scroll Bars"));
    scrollToggle->setCheckable(true);
    // Off by default — middle-button pan is the primary navigation, and
    // visible scrollbars steal wheel events. User can flip them back on
    // per-session if they want the affordance.
    scrollToggle->setChecked(false);
    connect(scrollToggle, &QAction::toggled, this, [this](bool on){
        const Qt::ScrollBarPolicy p = on ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
        mapView_->setHorizontalScrollBarPolicy(p);
        mapView_->setVerticalScrollBarPolicy(p);
    });

    view->addSeparator();
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
    addRenderToggle(tr("&Electric Circuits"),      QStringLiteral("view/electricCircuits"),    false);
    addRenderToggle(tr("Connection &Points"),      QStringLiteral("view/connectionPoints"),    false);
    addRenderToggle(tr("Ruler Attach P&oints"),     QStringLiteral("view/rulerAttachPoints"),   false);
    addRenderToggle(tr("&Watermark"),               QStringLiteral("view/watermark"),           true);
    addRenderToggle(tr("Brick &Hulls"),             QStringLiteral("view/brickHulls"),          false);
    addRenderToggle(tr("Brick E&levation Labels"),  QStringLiteral("view/brickElevation"),      false);
    addRenderToggle(tr("&Module Names"),             QStringLiteral("view/moduleNames"),         true);

    auto* tools = menuBar()->addMenu(tr("&Tools"));
    auto* libAct = tools->addAction(tr("Manage Parts &Libraries..."));
    connect(libAct, &QAction::triggered, this, &MainWindow::onManageLibraries);
    auto* reloadAct = tools->addAction(tr("&Reload Parts Library"));
    connect(reloadAct, &QAction::triggered, this, &MainWindow::onReloadLibrary);
    tools->addSeparator();

    auto* importMenu = tools->addMenu(tr("&Import"));
    // Shared helper: parse an LDraw-style read result, composite it to a
    // flat top-down sprite using whatever BlueBrick library parts
    // resolve, save as a new library part, rescan the parts panel, and
    // place the fresh part at the view centre so the user can work
    // with it right away. The goal (per user spec) is NOT to load the
    // imported file as a whole map — it's to turn the imported model
    // into a single composite part.
    auto importAsPart = [this](const QString& source,
                               const import::LDrawReadResult& read,
                               const QString& kindLabel) {
        if (!read.ok) {
            QMessageBox::warning(this, kindLabel,
                tr("Parse failed: %1").arg(read.error));
            return;
        }
        auto modelMap = import::toBlueBrickMap(read);
        if (!modelMap || modelMap->layers().empty()) {
            QMessageBox::warning(this, kindLabel,
                tr("No usable parts found in %1").arg(source));
            return;
        }

        // Rebuild the imported map's connectivity so two bricks that
        // touch at matching connection points get their linkedToId
        // populated — we use those to discriminate "external" (free)
        // ends from internal joints when emitting the composite part's
        // ConnexionList below.
        edit::rebuildConnectivity(*modelMap, parts_);

        // Render the imported map into a QImage. Use a dedicated scene
        // + SceneBuilder so we don't disturb the user's current view.
        QGraphicsScene renderScene;
        renderScene.setBackgroundBrush(Qt::transparent);
        rendering::SceneBuilder renderer(renderScene, parts_);
        renderer.build(*modelMap);
        const QRectF bounds = renderScene.itemsBoundingRect().adjusted(-4, -4, 4, 4);
        if (bounds.isEmpty()) {
            QMessageBox::warning(this, kindLabel,
                tr("Rendered model is empty."));
            return;
        }
        constexpr double kPxPerStud = 8.0;
        const int wPx = std::max(8, static_cast<int>(std::ceil(bounds.width())));
        const int hPx = std::max(8, static_cast<int>(std::ceil(bounds.height())));
        QImage sprite(wPx, hPx, QImage::Format_ARGB32);
        sprite.fill(Qt::transparent);
        {
            QPainter p(&sprite);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            renderScene.render(&p, QRectF(0, 0, wPx, hPx), bounds, Qt::KeepAspectRatio);
        }
        const int wStud = std::max(1, static_cast<int>(std::round(wPx / kPxPerStud)));
        const int hStud = std::max(1, static_cast<int>(std::round(hPx / kPxPerStud)));

        // Destination library: the user-library path configured under
        // modules/libraryPath, with /imports subdir so these don't
        // collide with module .bbm files.
        // Gather every free (linkedToId empty) connection from every
        // brick in the imported model, converted to sprite-local studs
        // (origin = sprite centre). These become the <ConnexionList>
        // of the composite part so it snaps like a real track tile.
        QVector<import::ImportedConnection> externalConns;
        {
            const auto rotate = [](QPointF p, double deg) {
                const double r = deg * M_PI / 180.0;
                const double c = std::cos(r), s = std::sin(r);
                return QPointF(p.x() * c - p.y() * s, p.x() * s + p.y() * c);
            };
            const QPointF spriteCentreStuds(
                (bounds.left() + bounds.right()) * 0.5 / kPxPerStud,
                (bounds.top()  + bounds.bottom()) * 0.5 / kPxPerStud);
            for (const auto& layerPtr : modelMap->layers()) {
                if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
                const auto& BL = static_cast<const core::LayerBrick&>(*layerPtr);
                for (const auto& brick : BL.bricks) {
                    auto meta = parts_.metadata(brick.partNumber);
                    if (!meta) continue;
                    const QPointF brickCentre = brick.displayArea.center();
                    for (int ci = 0; ci < meta->connections.size(); ++ci) {
                        const auto& c = meta->connections[ci];
                        if (c.type.isEmpty()) continue;
                        if (ci < static_cast<int>(brick.connections.size()) &&
                            !brick.connections[ci].linkedToId.isEmpty()) continue;
                        // World stud coords of this free connection.
                        const QPointF wStuds = brickCentre
                            + rotate(c.position, brick.orientation);
                        import::ImportedConnection ic;
                        ic.type = c.type;
                        // Translate from world to sprite-local (centre of sprite = origin).
                        ic.xStuds   = wStuds.x() - spriteCentreStuds.x();
                        ic.yStuds   = wStuds.y() - spriteCentreStuds.y();
                        ic.angleDeg = c.angleDegrees + brick.orientation;
                        externalConns.append(ic);
                    }
                }
            }
        }

        QString libRoot = QSettings().value(QStringLiteral("modules/libraryPath")).toString();
        if (libRoot.isEmpty()) {
            libRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + QStringLiteral("/imports");
        } else {
            libRoot += QStringLiteral("/imports");
        }
        QString err;
        const QString author = mapView_->currentMap()
                                   ? mapView_->currentMap()->author : QString();
        const QString key = import::writeImportedModelAsLibraryPart(
            source, sprite, wStud, hStud, libRoot, author,
            externalConns, &err);
        if (key.isEmpty()) {
            QMessageBox::warning(this, kindLabel,
                tr("Could not write library part: %1").arg(err));
            return;
        }

        // Rescan the parts library so the new part is available.
        rescanLibrary(loadUserLibraryPaths() << libRoot);
        partsBrowser_->rebuild();

        // Drop the new part at the view centre.
        mapView_->addPartAtViewCenter(key);

        statusBar()->showMessage(
            tr("Imported %1 as part '%2' (%3 × %4 studs, %5 source parts)")
                .arg(QFileInfo(source).fileName(), key)
                .arg(wStud).arg(hStud).arg(read.parts.size()), 6000);
    };

    auto* ldrawAct = importMenu->addAction(tr("&LDraw (.ldr / .dat / .mpd)..."));
    connect(ldrawAct, &QAction::triggered, this, [this, importAsPart]{
        const QString in = QFileDialog::getOpenFileName(this,
            tr("Import LDraw file"), {},
            tr("LDraw (*.ldr *.dat *.mpd);;All files (*)"));
        if (in.isEmpty()) return;
        importAsPart(in, import::readLDraw(in), tr("LDraw import"));
    });
    auto* studioAct = importMenu->addAction(tr("&Studio (.io)..."));
    connect(studioAct, &QAction::triggered, this, [this, importAsPart]{
        const QString in = QFileDialog::getOpenFileName(this,
            tr("Import Studio .io"), {},
            tr("Studio (*.io);;All files (*)"));
        if (in.isEmpty()) return;
        importAsPart(in, import::readStudioIo(in), tr("Studio import"));
    });
    auto* lddAct = importMenu->addAction(tr("L&DD (.lxf / .lxfml)..."));
    connect(lddAct, &QAction::triggered, this, [this, importAsPart]{
        const QString in = QFileDialog::getOpenFileName(this,
            tr("Import LDD file"), {},
            tr("LDD (*.lxf *.lxfml);;All files (*)"));
        if (in.isEmpty()) return;
        importAsPart(in, import::readLDD(in), tr("LDD import"));
    });

    auto* partListAct = tools->addAction(tr("Export &Part List (CSV)..."));
    connect(partListAct, &QAction::triggered, this, &MainWindow::onExportPartList);
    tools->addSeparator();
    auto* prefsAct = tools->addAction(tr("&Preferences..."));
    prefsAct->setShortcut(QKeySequence::Preferences);
    connect(prefsAct, &QAction::triggered, this, [this]{
        PreferencesDialog dlg(this);
        dlg.exec();
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        mapView_->setSnapStepStuds(s.value(QStringLiteral("snapStepStuds"), 0.0).toDouble());
        mapView_->setRotationStepDegrees(s.value(QStringLiteral("rotationStepDegrees"), 90.0).toDouble());
        s.endGroup();
        const QString libDir = QSettings().value(QStringLiteral("modules/libraryPath")).toString();
        if (!libDir.isEmpty() && libDir != moduleLibraryPanel_->libraryPath()) {
            moduleLibraryPanel_->setLibraryPath(libDir);
        }
        mapView_->rebuildScene();
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

    mapMenu->addSeparator();
    auto* venueMenu = mapMenu->addMenu(tr("&Venue"));
    auto* drawOutlineAct = venueMenu->addAction(tr("Draw &Outline..."));
    drawOutlineAct->setToolTip(tr("Click points on the map to build the venue outline. "
                                    "Right-click or Enter finishes; Escape cancels."));
    connect(drawOutlineAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap()) return;
        mapView_->setTool(MapView::Tool::DrawVenueOutline);
        statusBar()->showMessage(
            tr("Click points to outline the venue. Right-click / Enter to finish, Escape to cancel."),
            8000);
    });
    auto* drawByDimsAct = venueMenu->addAction(tr("Draw Outline by &Dimensions..."));
    drawByDimsAct->setToolTip(tr("Build the venue outline by entering lengths + angles "
                                   "instead of clicking points on the map."));
    connect(drawByDimsAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        VenueDimensionsDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) return;
        const auto poly  = dlg.polygon();
        const auto metas = dlg.segments();
        if (poly.size() < 3) return;
        core::Venue v = m->sidecar.venue.value_or(core::Venue{});
        v.enabled = true;
        v.edges.clear();
        for (int i = 0; i < poly.size(); ++i) {
            core::VenueEdge e;
            e.polyline = { poly[i], poly[(i + 1) % poly.size()] };
            if (i < metas.size()) {
                e.kind  = metas[i].kind;
                e.label = metas[i].label;
            } else {
                e.kind  = core::EdgeKind::Wall;
            }
            v.edges.push_back(e);
        }
        mapView_->undoStack()->push(new edit::SetVenueCommand(*m, std::make_optional(v)));
        mapView_->rebuildScene();
        statusBar()->showMessage(
            tr("Venue outline built from %1 segments").arg(poly.size()), 3000);
    });

    auto* drawObstacleAct = venueMenu->addAction(tr("Add &Obstacle..."));
    drawObstacleAct->setToolTip(tr("Click points to add an obstacle polygon (pillar, column)."));
    connect(drawObstacleAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap() || !mapView_->currentMap()->sidecar.venue) {
            QMessageBox::information(this, tr("Add obstacle"),
                tr("Draw the venue outline first."));
            return;
        }
        mapView_->setTool(MapView::Tool::DrawVenueObstacle);
        statusBar()->showMessage(
            tr("Click points to outline an obstacle. Right-click / Enter to finish, Escape to cancel."),
            8000);
    });
    venueMenu->addSeparator();
    auto* editVenueAct = venueMenu->addAction(tr("&Edit Venue Properties..."));
    connect(editVenueAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        VenueDialog dlg(m->sidecar.venue, this);
        if (dlg.exec() != QDialog::Accepted) return;
        if (dlg.cleared()) {
            mapView_->undoStack()->push(new edit::SetVenueCommand(*m, std::nullopt));
        } else {
            mapView_->undoStack()->push(new edit::SetVenueCommand(*m, dlg.result()));
        }
        mapView_->rebuildScene();
    });
    auto* clearVenueAct = venueMenu->addAction(tr("&Clear Venue"));
    connect(clearVenueAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m || !m->sidecar.venue) return;
        const auto btn = QMessageBox::question(this, tr("Clear venue"),
            tr("Remove the venue from this project?"));
        if (btn != QMessageBox::Yes) return;
        mapView_->undoStack()->push(new edit::SetVenueCommand(*m, std::nullopt));
        mapView_->rebuildScene();
    });

    venueMenu->addSeparator();

    // Save / Load library. Venues are per-project (Map::sidecar.venue is
    // one optional), but we let the user stash a venue as a standalone
    // .cld-venue file in a library folder so it can be reused across
    // projects. List shows every .cld-venue in the configured folder.
    auto venueLibraryFolder = []() -> QString {
        QString dir = QSettings().value(QStringLiteral("venue/libraryPath")).toString();
        if (dir.isEmpty()) {
            dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + QStringLiteral("/venues");
            QSettings().setValue(QStringLiteral("venue/libraryPath"), dir);
        }
        QDir().mkpath(dir);
        return dir;
    };

    auto sanitizeFilename = [](QString n) -> QString {
        static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
        n.replace(bad, QStringLiteral("_"));
        while (n.startsWith(QLatin1Char('.')) || n.startsWith(QLatin1Char(' '))) n.remove(0, 1);
        while (n.endsWith(QLatin1Char('.'))  || n.endsWith(QLatin1Char(' ')))  n.chop(1);
        if (n.isEmpty()) n = QStringLiteral("Venue");
        return n;
    };

    auto* saveVenueAct = venueMenu->addAction(tr("&Save Venue to Library..."));
    connect(saveVenueAct, &QAction::triggered, this,
            [this, venueLibraryFolder, sanitizeFilename]{
        auto* m = mapView_->currentMap();
        if (!m || !m->sidecar.venue) {
            QMessageBox::information(this, tr("Save venue"),
                tr("There's no venue on this project yet."));
            return;
        }
        const QString dir = venueLibraryFolder();
        bool ok = false;
        const QString defName = m->sidecar.venue->name.isEmpty() ? tr("Venue")
                                                                   : m->sidecar.venue->name;
        const QString raw = QInputDialog::getText(this, tr("Save venue to library"),
            tr("Venue name (filename):"), QLineEdit::Normal, defName, &ok);
        if (!ok || raw.isEmpty()) return;
        const QString target = QDir(dir).filePath(sanitizeFilename(raw)
                                                    + QStringLiteral(".cld-venue"));
        if (QFile::exists(target)) {
            const auto btn = QMessageBox::question(this, tr("Save venue"),
                tr("%1 already exists. Overwrite?").arg(target));
            if (btn != QMessageBox::Yes) return;
        }
        QString err;
        if (!saveload::writeVenueFile(target, *m->sidecar.venue, &err)) {
            QMessageBox::warning(this, tr("Save venue"),
                tr("Could not write %1: %2").arg(target, err));
            return;
        }
        statusBar()->showMessage(tr("Saved venue to %1").arg(target), 4000);
    });

    auto* loadVenueAct = venueMenu->addAction(tr("Load Venue from &Library..."));
    connect(loadVenueAct, &QAction::triggered, this, [this, venueLibraryFolder]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        const QString dir = venueLibraryFolder();
        QDir d(dir);
        const QStringList files = d.entryList({ QStringLiteral("*.cld-venue") },
                                                QDir::Files, QDir::Name | QDir::IgnoreCase);
        if (files.isEmpty()) {
            QMessageBox::information(this, tr("Load venue"),
                tr("No saved venues in %1.").arg(dir));
            return;
        }
        QStringList displayNames;
        for (const QString& f : files) displayNames << QFileInfo(f).completeBaseName();
        bool ok = false;
        const QString picked = QInputDialog::getItem(this, tr("Load venue from library"),
            tr("Choose a saved venue:"), displayNames, 0, false, &ok);
        if (!ok || picked.isEmpty()) return;
        const QString path = d.filePath(picked + QStringLiteral(".cld-venue"));
        QString err;
        auto venue = saveload::readVenueFile(path, &err);
        if (!venue) {
            QMessageBox::warning(this, tr("Load venue"),
                tr("Could not read %1: %2").arg(path, err));
            return;
        }
        if (m->sidecar.venue) {
            const auto btn = QMessageBox::question(this, tr("Replace venue"),
                tr("This project already has a venue. Replace it?"));
            if (btn != QMessageBox::Yes) return;
        }
        mapView_->undoStack()->push(new edit::SetVenueCommand(*m, venue));
        mapView_->rebuildScene();
        statusBar()->showMessage(tr("Loaded venue '%1'").arg(picked), 3000);
    });

    auto* openVenueFileAct = venueMenu->addAction(tr("Load Venue from &File..."));
    connect(openVenueFileAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Load venue file"), {},
            tr("Venue (*.cld-venue);;All files (*)"));
        if (path.isEmpty()) return;
        QString err;
        auto venue = saveload::readVenueFile(path, &err);
        if (!venue) {
            QMessageBox::warning(this, tr("Load venue"),
                tr("Could not read %1: %2").arg(path, err));
            return;
        }
        if (m->sidecar.venue) {
            const auto btn = QMessageBox::question(this, tr("Replace venue"),
                tr("This project already has a venue. Replace it?"));
            if (btn != QMessageBox::Yes) return;
        }
        mapView_->undoStack()->push(new edit::SetVenueCommand(*m, venue));
        mapView_->rebuildScene();
    });

    menuBar()->addMenu(tr("&Layers"));

    auto* budgetMenu = menuBar()->addMenu(tr("&Budget"));
    auto* budgetDlg = budgetMenu->addAction(tr("Open Budget &Editor..."));
    connect(budgetDlg, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap()) return;
        auto* dlg = new BudgetDialog(*mapView_->currentMap(), this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

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

}  // namespace cld::ui
