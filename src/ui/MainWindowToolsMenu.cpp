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
#include "../import/ldd/LDDLDrawMapping.h"
#include "../import/ldd/LDDReader.h"
#include "../import/ldraw/LDrawLibrary.h"
#include "../import/ldraw/LDrawMeshBuilder.h"
#include "../import/ldraw/LDrawMeshLoader.h"
#include "../import/ldraw/LDrawPalette.h"
#include "../import/ldraw/LDrawRasterize.h"
#include "../import/ldraw/LDrawReader.h"
#include "../import/mesh/MeshRasterize.h"
#include "../import/studio/StudioReader.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"

#include "DownloadCenterDialog.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
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

#include <algorithm>
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
        const bool hasRefs = modelMap && !modelMap->layers().empty();
        if (!hasRefs && read.primitives.empty()) {
            QMessageBox::warning(this, kindLabel,
                tr("No usable parts or primitives in %1").arg(source));
            return;
        }

        // Rebuild the imported map's connectivity so two bricks that
        // touch at matching connection points get their linkedToId
        // populated — we use those to discriminate "external" (free)
        // ends from internal joints when emitting the composite
        // part's ConnexionList below.
        if (hasRefs) edit::rebuildConnectivity(*modelMap, parts_);

        // Render the imported map into a QImage. Use a dedicated
        // scene + SceneBuilder so we don't disturb the user's current
        // view. When no library parts resolved (or the scene is
        // empty after compositing) we fall back to rasterizing the
        // file's inline primitives directly — type-3/4 fills + type-2
        // outlines at BlueBrick's 8 px/stud scale via
        // import::rasterizeTopDown.
        QGraphicsScene renderScene;
        renderScene.setBackgroundBrush(Qt::transparent);
        rendering::SceneBuilder renderer(renderScene, parts_);
        if (hasRefs) renderer.build(*modelMap);
        QRectF bounds = renderScene.itemsBoundingRect().adjusted(-4, -4, 4, 4);
        if (bounds.isEmpty() && !read.primitives.empty()) {
            // Bootstrap a scene from the primitive raster. We can
            // place the rasterized image as a single pixmap item and
            // treat it like any other library part from here on.
            const QImage primImg = import::rasterizeTopDown(read);
            if (!primImg.isNull()) {
                auto* item = renderScene.addPixmap(QPixmap::fromImage(primImg));
                item->setOffset(-primImg.width() / 2.0, -primImg.height() / 2.0);
                bounds = item->boundingRect().translated(item->pos())
                            .adjusted(-4, -4, 4, 4);
            }
        }
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

    // Newer LDraw / Studio import path: bake the model's geometry
    // through a user-pointed LDraw library + LDConfig.ldr palette,
    // rasterize top-down, and emit a sprite. Used when the user has
    // configured `import/ldrawLibraryPath` in Preferences. Falls back
    // to the legacy BlueBrickParts-rendering closure when unset or
    // invalid so existing users don't lose the feature mid-rollout.
    auto importViaLDrawLibrary = [this](const QString& source,
                                          const import::LDrawReadResult& read,
                                          const QString& kindLabel) -> bool {
        if (!read.ok) return false;
        const QString libRoot = QSettings().value(
            QStringLiteral("import/ldrawLibraryPath")).toString();
        if (libRoot.isEmpty()) return false;
        import::LDrawLibrary lib(libRoot);
        if (!lib.looksValid()) return false;

        import::LDrawPalette palette;
        palette.loadFromLDConfig(QDir(libRoot).absoluteFilePath(
            QStringLiteral("LDConfig.ldr")));

        import::LDrawMeshLoader loader(lib, palette);
        const auto baked = import::bakeMeshFromLDraw(read, loader, palette);
        if (baked.mesh.tris.empty()) {
            QMessageBox::warning(this, kindLabel,
                tr("LDraw library at %1 couldn't resolve any geometry for %2. "
                   "Errors:\n%3").arg(libRoot, source, baked.errors.join('\n')));
            return true;  // we tried and failed; don't fall back
        }

        // Rasterize the baked mesh into a top-down sprite at the
        // BlueBrick stock 8 px/stud sample rate.
        import::RasterizeOptions ropt;
        ropt.pxPerStud = 8;
        const auto rast = import::rasterizeMeshTopDown(baked.mesh, ropt);
        if (rast.image.isNull()) {
            QMessageBox::warning(this, kindLabel,
                tr("Rasterized sprite is empty."));
            return true;
        }

        const int wStud = std::max(1, static_cast<int>(
            std::round(rast.meshBoundsXZ.width())));
        const int hStud = std::max(1, static_cast<int>(
            std::round(rast.meshBoundsXZ.height())));

        // Emit as a new BlueBrick library part with no ConnexionList
        // for now — full connection-list bake from real LDraw geometry
        // requires picking up the studs from p/stud.dat refs and
        // is a follow-up. The part is still placeable; users can
        // manually edit connections via Properties.
        QString modulesRoot = QSettings().value(
            QStringLiteral("modules/libraryPath")).toString();
        if (modulesRoot.isEmpty()) {
            modulesRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + QStringLiteral("/imports");
        } else {
            modulesRoot += QStringLiteral("/imports");
        }
        QString err;
        const QString author = mapView_->currentMap()
                                   ? mapView_->currentMap()->author : QString();
        const QString key = import::writeImportedModelAsLibraryPart(
            source, rast.image, wStud, hStud, modulesRoot, author,
            {}, &err);
        if (key.isEmpty()) {
            QMessageBox::warning(this, kindLabel,
                tr("Could not write library part: %1").arg(err));
            return true;
        }
        rescanLibrary(loadUserLibraryPaths() << modulesRoot);
        partsBrowser_->rebuild();
        mapView_->addPartAtViewCenter(key);
        statusBar()->showMessage(
            tr("Imported %1 via LDraw library — %2 part(s) resolved, %3 unresolved")
                .arg(QFileInfo(source).fileName())
                .arg(baked.resolvedRefs).arg(baked.unresolvedRefs), 8000);
        return true;
    };

    auto* ldrawAct = importMenu->addAction(tr("&LDraw (.ldr / .dat / .mpd)..."));
    connect(ldrawAct, &QAction::triggered, this, [this, importAsPart, importViaLDrawLibrary]{
        const QString in = QFileDialog::getOpenFileName(this,
            tr("Import LDraw file"), {},
            tr("LDraw (*.ldr *.dat *.mpd);;All files (*)"));
        if (in.isEmpty()) return;
        const auto read = import::readLDraw(in);
        if (importViaLDrawLibrary(in, read, tr("LDraw import"))) return;
        importAsPart(in, read, tr("LDraw import"));
    });
    auto* studioAct = importMenu->addAction(tr("&Studio (.io)..."));
    connect(studioAct, &QAction::triggered, this, [this, importAsPart, importViaLDrawLibrary]{
        const QString in = QFileDialog::getOpenFileName(this,
            tr("Import Studio .io"), {},
            tr("Studio (*.io);;All files (*)"));
        if (in.isEmpty()) return;
        const auto read = import::readStudioIo(in);
        if (importViaLDrawLibrary(in, read, tr("Studio import"))) return;
        importAsPart(in, read, tr("Studio import"));
    });
    // LDD imports route through ldraw.xml when an LDD install path is
    // configured: rewrite each LDD designID → LDraw .dat filename and
    // each LDD materialID → LDraw colour code so the model bakes
    // through the same LDraw pipeline as native .ldr imports. Parts
    // without an LDraw mapping are dropped (LDD-only decorations
    // would need .g geometry from db.lif — handled in a follow-up).
    auto importLDDViaLDrawMapping = [this](const QString& source,
                                            import::LDrawReadResult read,
                                            const QString& kindLabel) -> bool {
        if (!read.ok) return false;
        const QString lddRoot = QSettings().value(
            QStringLiteral("import/lddInstallPath")).toString();
        const QString ldrawRoot = QSettings().value(
            QStringLiteral("import/ldrawLibraryPath")).toString();
        if (lddRoot.isEmpty() || ldrawRoot.isEmpty()) return false;

        const QString xmlPath = QDir(lddRoot).absoluteFilePath(QStringLiteral("ldraw.xml"));
        import::LDDLDrawMapping mapping;
        if (!mapping.loadFromFile(xmlPath)) return false;

        // Rewrite each LDDReader-emitted ref so its filename + colour
        // are LDraw-side. LDDReader currently produces filenames like
        // "<designID>.dat" or "<designID>.<matId>.dat" — strip back
        // to the bare designID, look up the LDraw .dat, and overwrite.
        // ColourCode = LDD materialID, which we translate through the
        // mapping to an LDraw code.
        int translated = 0, dropped = 0;
        for (auto& ref : read.parts) {
            QString designId = ref.filename;
            if (designId.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive))
                designId.chop(4);
            // Strip optional ".<matId>" decoration LDDReader appends.
            const int dot = designId.indexOf(QChar('.'));
            if (dot > 0) designId = designId.left(dot);
            const QString ldraw = mapping.partFor(designId);
            if (ldraw.isEmpty()) { ++dropped; ref.filename.clear(); continue; }
            ref.filename = ldraw;
            const int newColour = mapping.colourFor(ref.colorCode);
            if (newColour >= 0) ref.colorCode = newColour;
            ++translated;
        }
        // Drop refs whose filename got cleared (no mapping).
        read.parts.erase(std::remove_if(read.parts.begin(), read.parts.end(),
            [](const auto& r){ return r.filename.isEmpty(); }), read.parts.end());

        // Hand off to the LDraw pipeline: load library + palette,
        // bake mesh, rasterize, emit as a library part.
        import::LDrawLibrary lib(ldrawRoot);
        if (!lib.looksValid()) return false;
        import::LDrawPalette palette;
        palette.loadFromLDConfig(QDir(ldrawRoot).absoluteFilePath(
            QStringLiteral("LDConfig.ldr")));
        import::LDrawMeshLoader loader(lib, palette);
        const auto baked = import::bakeMeshFromLDraw(read, loader, palette);
        if (baked.mesh.tris.empty()) {
            QMessageBox::warning(this, kindLabel,
                tr("LDD model couldn't be rendered: %1 parts translated to LDraw, "
                   "%2 dropped (no mapping), and the LDraw library returned no "
                   "geometry.\n\n%3")
                    .arg(translated).arg(dropped)
                    .arg(baked.errors.join('\n')));
            return true;
        }

        import::RasterizeOptions ropt;
        ropt.pxPerStud = 8;
        const auto rast = import::rasterizeMeshTopDown(baked.mesh, ropt);
        if (rast.image.isNull()) return true;

        const int wStud = std::max(1, static_cast<int>(
            std::round(rast.meshBoundsXZ.width())));
        const int hStud = std::max(1, static_cast<int>(
            std::round(rast.meshBoundsXZ.height())));
        QString modulesRoot = QSettings().value(
            QStringLiteral("modules/libraryPath")).toString();
        if (modulesRoot.isEmpty()) {
            modulesRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + QStringLiteral("/imports");
        } else {
            modulesRoot += QStringLiteral("/imports");
        }
        QString err;
        const QString author = mapView_->currentMap()
                                   ? mapView_->currentMap()->author : QString();
        const QString key = import::writeImportedModelAsLibraryPart(
            source, rast.image, wStud, hStud, modulesRoot, author,
            {}, &err);
        if (key.isEmpty()) {
            QMessageBox::warning(this, kindLabel,
                tr("Could not write library part: %1").arg(err));
            return true;
        }
        rescanLibrary(loadUserLibraryPaths() << modulesRoot);
        partsBrowser_->rebuild();
        mapView_->addPartAtViewCenter(key);
        statusBar()->showMessage(
            tr("Imported %1 via LDD→LDraw mapping — %2 parts translated, %3 dropped, %4 resolved")
                .arg(QFileInfo(source).fileName())
                .arg(translated).arg(dropped).arg(baked.resolvedRefs), 8000);
        return true;
    };

    auto* lddAct = importMenu->addAction(tr("L&DD (.lxf / .lxfml)..."));
    connect(lddAct, &QAction::triggered, this, [this, importAsPart, importLDDViaLDrawMapping]{
        const QString in = QFileDialog::getOpenFileName(this,
            tr("Import LDD file"), {},
            tr("LDD (*.lxf *.lxfml);;All files (*)"));
        if (in.isEmpty()) return;
        const auto read = import::readLDD(in);
        if (importLDDViaLDrawMapping(in, read, tr("LDD import"))) return;
        importAsPart(in, read, tr("LDD import"));
    });

    auto* partListAct = tools->addAction(tr("Export &Part List (CSV)..."));
    connect(partListAct, &QAction::triggered, this, &MainWindow::onExportPartList);

    tools->addSeparator();
    auto* dlAct = tools->addAction(tr("&Download Additional Parts..."));
    dlAct->setToolTip(tr("Search the official + community part-package servers and install zip archives into your library"));
    connect(dlAct, &QAction::triggered, this, [this]{
        // Pick a default install root the same way the simple download
        // helper used to: first configured user library path, or the
        // app-data fallback. The dialog uses this as the extraction
        // destination AND as the source for the "already installed"
        // version comparison.
        QStringList userPaths = loadUserLibraryPaths();
        QString destRoot = userPaths.isEmpty()
            ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("parts"))
            : userPaths.first();
        DownloadCenterDialog dlg(destRoot, this);
        if (dlg.exec() == QDialog::Accepted && dlg.installedCount() > 0) {
            const QString root = dlg.libraryRoot();
            if (!userPaths.contains(root)) {
                userPaths.append(root);
                saveUserLibraryPaths(userPaths);
            }
            rescanLibrary(userPaths);
            statusBar()->showMessage(
                tr("Installed %1 package(s); library reloaded.")
                    .arg(dlg.installedCount()), 5000);
        }
    });

    // Preferences moved to Edit menu (BlueBrick parity).
}

}  // namespace cld::ui
