// Tools menu — manage libraries, reload library, Import submenu
// (LDraw / Studio / LDD → composite library part), Export Part List,
// Preferences. Import has the heaviest payload (render to sprite,
// rebuild connectivity, write composite part XML + GIF, rescan
// library) — that's why this chunk warranted its own TU.
//
// All imports share one `importAsPart` closure so LDraw / Studio /
// LDD go through identical rendering + connectivity + library-
// persist code. Imports ALWAYS produce a composite library part,
// never a loose map — that's the user-confirmed intent.

#include "MainWindow.h"

#include "MapView.h"
#include "ModuleLibraryPanel.h"
#include "PartsBrowser.h"
#include "PreferencesDialog.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../edit/Connectivity.h"
#include "../import/ImportToPart.h"
#include "../import/LDDReader.h"
#include "../import/LDrawReader.h"
#include "../import/StudioReader.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QVector>

#include <cmath>

namespace cld::ui {

void MainWindow::setupToolsMenu() {
    auto* tools = menuBar()->addMenu(tr("&Tools"));
    auto* libAct = tools->addAction(tr("Manage Parts &Libraries..."));
    connect(libAct, &QAction::triggered, this, &MainWindow::onManageLibraries);
    auto* reloadAct = tools->addAction(tr("&Reload Parts Library"));
    connect(reloadAct, &QAction::triggered, this, &MainWindow::onReloadLibrary);
    tools->addSeparator();

    auto* importMenu = tools->addMenu(tr("&Import"));
    // Shared helper: parse an LDraw-style read result, composite it to
    // a flat top-down sprite using whatever BlueBrick library parts
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
        // ends from internal joints when emitting the composite
        // part's ConnexionList below.
        edit::rebuildConnectivity(*modelMap, parts_);

        // Render the imported map into a QImage. Use a dedicated
        // scene + SceneBuilder so we don't disturb the user's current
        // view.
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

        // Gather every free (linkedToId empty) connection from every
        // brick in the imported model, converted to sprite-local
        // studs (origin = sprite centre). These become the
        // <ConnexionList> of the composite part so it snaps like a
        // real track tile.
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
                        const QPointF wStuds = brickCentre
                            + rotate(c.position, brick.orientation);
                        import::ImportedConnection ic;
                        ic.type = c.type;
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

        rescanLibrary(loadUserLibraryPaths() << libRoot);
        partsBrowser_->rebuild();
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
}

}  // namespace cld::ui
