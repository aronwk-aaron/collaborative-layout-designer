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
#include "../import/ldd/LDDMaterials.h"
#include "../import/ldd/LDDMeshBuilder.h"
#include "../import/ldd/LDDReader.h"
#include "../import/lif/LifReader.h"
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

#include "BackgroundTask.h"
#include "DownloadCenterDialog.h"
#include "ImportPreviewDialog.h"

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
#include <memory>

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

        // Preview before commit. The legacy path doesn't track granular
        // resolved/unresolved counts the way the LDraw-library and LDD
        // paths do, so the stats panel will be sparse — that's fine,
        // the sprite preview is the part the user really cares about.
        ImportPreviewDialog::Stats st;
        st.ldrawResolved = static_cast<int>(read.parts.size());
        ImportPreviewDialog dlg(source, kindLabel, sprite,
                                wStud, hStud, st, {}, this);
        if (dlg.exec() != QDialog::Accepted) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return;
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
            dlg.partName(), sprite, wStud, hStud, libRoot, author,
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

        // Bake + rasterize on a worker thread so the UI stays
        // responsive while a multi-thousand-part LDraw / Studio
        // model is being processed (can take 30s+ for big MOCs).
        // Cancel checkpoint sits between bake and rasterize so the
        // user can bail at the largest natural boundary.
        import::LDrawMeshLoader loader(lib, palette);
        import::BakedModel baked;
        import::RasterizeResult rast;
        const bool ok = runBackground(this,
            tr("Importing %1...").arg(QFileInfo(source).fileName()),
            [&](CancelToken& cancel){
                baked = import::bakeMeshFromLDraw(read, loader, palette);
                if (cancel.requested()) return;
                if (!baked.mesh.tris.empty()) {
                    import::RasterizeOptions ropt;
                    ropt.pxPerStud = 8;
                    rast = import::rasterizeMeshTopDown(baked.mesh, ropt);
                }
            });
        if (!ok) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return true;
        }
        if (baked.mesh.tris.empty()) {
            QMessageBox::warning(this, kindLabel,
                tr("LDraw library at %1 couldn't resolve any geometry for %2. "
                   "Errors:\n%3").arg(libRoot, source, baked.errors.join('\n')));
            return true;  // we tried and failed; don't fall back
        }
        if (rast.image.isNull()) {
            QMessageBox::warning(this, kindLabel,
                tr("Rasterized sprite is empty."));
            return true;
        }

        const int wStud = std::max(1, static_cast<int>(
            std::round(rast.meshBoundsXZ.width())));
        const int hStud = std::max(1, static_cast<int>(
            std::round(rast.meshBoundsXZ.height())));

        // Show the preview dialog so the user sees what they're
        // about to add to the library. Cancelling here just returns
        // — no part files written, no library scan, nothing to undo.
        ImportPreviewDialog::Stats st;
        st.ldrawResolved = baked.resolvedRefs;
        st.unmapped      = baked.unresolvedRefs;
        ImportPreviewDialog dlg(source, kindLabel, rast.image,
                                wStud, hStud, st, baked.errors, this);
        if (dlg.exec() != QDialog::Accepted) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return true;
        }

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
            dlg.partName(), rast.image, wStud, hStud, modulesRoot, author,
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
            tr("Imported %1 as '%2' — %3 part(s) resolved, %4 unresolved")
                .arg(QFileInfo(source).fileName(), key)
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
        if (lddRoot.isEmpty()) return false;

        const QString xmlPath = QDir(lddRoot).absoluteFilePath(QStringLiteral("ldraw.xml"));
        import::LDDLDrawMapping mapping;
        if (!mapping.loadFromFile(xmlPath)) return false;

        // Materials.xml — needed both for the LDD-only fallback's
        // colours and for cross-checking. May live in
        // <lddRoot>/Assets/db/Materials.xml after the user has
        // extracted db.lif manually, or inside db.lif itself which
        // we mount via LifReader.
        import::LDDMaterials materials;
        const QString matPath = QDir(lddRoot).absoluteFilePath(
            QStringLiteral("Assets/db/Materials.xml"));
        if (!materials.loadFromFile(matPath)) {
            // Try inside db.lif at the same logical path.
            const QString dbLifPath = QDir(lddRoot).absoluteFilePath(
                QStringLiteral("Assets/db.lif"));
            import::LifReader lif;
            if (lif.open(dbLifPath)) {
                materials.loadFromBytes(lif.read(QStringLiteral("/db/Materials.xml")));
            }
        }

        // Keep the original LDD-flavoured ref list so the LDD-only
        // fallback path can render parts that have NO LDraw mapping
        // by loading their .g geometry directly.
        const auto originalRead = read;

        // ----- LDraw-mapped path ---------------------------------------
        int translated = 0, unmapped = 0;
        for (auto& ref : read.parts) {
            QString designId = ref.filename;
            if (designId.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive))
                designId.chop(4);
            const int dot = designId.indexOf(QChar('.'));
            if (dot > 0) designId = designId.left(dot);
            const QString ldraw = mapping.partFor(designId);
            if (ldraw.isEmpty()) { ++unmapped; ref.filename.clear(); continue; }
            ref.filename = ldraw;
            const int newColour = mapping.colourFor(ref.colorCode);
            if (newColour >= 0) ref.colorCode = newColour;

            // Apply per-part LDD↔LDraw frame correction from
            // <Transformation> if present. The correction is an
            // axis-angle rotation + translation in the LDraw frame
            // applied INSIDE the ref's own placement, so we
            // post-multiply: refMatrix * correction. This keeps the
            // ref's own translation/rotation as the outer transform
            // (where the LDD model placed the brick) while baking
            // the per-design coord-frame fixup just before the
            // mesh-load step picks up the geometry.
            const auto t = mapping.transformFor(ldraw);
            if (t.exists) {
                const double angle = t.angle;
                const double c = std::cos(angle);
                const double s = std::sin(angle);
                const double C = 1.0 - c;
                // Normalise axis (the file sometimes ships unnormalised).
                double ax = t.ax, ay = t.ay, az = t.az;
                const double mag = std::sqrt(ax*ax + ay*ay + az*az);
                if (mag > 1e-9) { ax /= mag; ay /= mag; az /= mag; }
                // Rodrigues' rotation matrix (3x3).
                const double r00 = c + ax*ax*C;
                const double r01 = ax*ay*C - az*s;
                const double r02 = ax*az*C + ay*s;
                const double r10 = ay*ax*C + az*s;
                const double r11 = c + ay*ay*C;
                const double r12 = ay*az*C - ax*s;
                const double r20 = az*ax*C - ay*s;
                const double r21 = az*ay*C + ax*s;
                const double r22 = c + az*az*C;
                // Compose ref.m (3x3) with the correction:
                //   newM = oldM * R
                //   newT = oldM * tCorrection + oldT
                double oldM[9];
                for (int k = 0; k < 9; ++k) oldM[k] = ref.m[k];
                ref.m[0] = oldM[0]*r00 + oldM[1]*r10 + oldM[2]*r20;
                ref.m[1] = oldM[0]*r01 + oldM[1]*r11 + oldM[2]*r21;
                ref.m[2] = oldM[0]*r02 + oldM[1]*r12 + oldM[2]*r22;
                ref.m[3] = oldM[3]*r00 + oldM[4]*r10 + oldM[5]*r20;
                ref.m[4] = oldM[3]*r01 + oldM[4]*r11 + oldM[5]*r21;
                ref.m[5] = oldM[3]*r02 + oldM[4]*r12 + oldM[5]*r22;
                ref.m[6] = oldM[6]*r00 + oldM[7]*r10 + oldM[8]*r20;
                ref.m[7] = oldM[6]*r01 + oldM[7]*r11 + oldM[8]*r21;
                ref.m[8] = oldM[6]*r02 + oldM[7]*r12 + oldM[8]*r22;
                ref.x += oldM[0]*t.tx + oldM[1]*t.ty + oldM[2]*t.tz;
                ref.y += oldM[3]*t.tx + oldM[4]*t.ty + oldM[5]*t.tz;
                ref.z += oldM[6]*t.tx + oldM[7]*t.ty + oldM[8]*t.tz;
            }

            ++translated;
        }
        read.parts.erase(std::remove_if(read.parts.begin(), read.parts.end(),
            [](const auto& r){ return r.filename.isEmpty(); }), read.parts.end());

        // Bake LDraw-translated subset + LDD-only-fallback subset on
        // a worker thread; both stages can each take 10s+ on dense
        // models. Final rasterize joins them.
        geom::Mesh combined;
        int ldrawResolved = 0;
        QStringList allErrors;
        import::LDDLDrawBakedModel lddBaked;
        import::RasterizeResult rast;

        // Open db.lif up front (still on UI thread — small map).
        std::unique_ptr<import::LifReader> dbLif;
        const QString dbLifPath = QDir(lddRoot).absoluteFilePath(
            QStringLiteral("Assets/db.lif"));
        if (QFileInfo::exists(dbLifPath)) {
            dbLif = std::make_unique<import::LifReader>();
            if (!dbLif->open(dbLifPath)) dbLif.reset();
        }
        const bool ok = runBackground(this,
            tr("Importing %1...").arg(QFileInfo(source).fileName()),
            [&](CancelToken& cancel){
                if (!ldrawRoot.isEmpty()) {
                    import::LDrawLibrary lib(ldrawRoot);
                    if (lib.looksValid()) {
                        import::LDrawPalette palette;
                        palette.loadFromLDConfig(QDir(ldrawRoot).absoluteFilePath(
                            QStringLiteral("LDConfig.ldr")));
                        import::LDrawMeshLoader loader(lib, palette);
                        auto baked = import::bakeMeshFromLDraw(read, loader, palette);
                        ldrawResolved = baked.resolvedRefs;
                        allErrors += baked.errors;
                        for (const auto& tri : baked.mesh.tris) combined.tris.push_back(tri);
                    }
                }
                if (cancel.requested()) return;

                import::LDDMeshBuilder lddBuilder;
                lddBuilder.setOnDiskRoot(lddRoot);
                lddBuilder.setMapping(&mapping);
                lddBuilder.setMaterials(&materials);
                if (dbLif) lddBuilder.setLifReader(dbLif.get());
                lddBaked = lddBuilder.bake(originalRead);
                for (const auto& tri : lddBaked.mesh.tris) combined.tris.push_back(tri);
                allErrors += lddBaked.errors;
                if (cancel.requested()) return;

                if (!combined.tris.empty()) {
                    import::RasterizeOptions ropt;
                    ropt.pxPerStud = 8;
                    rast = import::rasterizeMeshTopDown(combined, ropt);
                }
            });
        if (!ok) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return true;
        }

        if (combined.tris.empty()) {
            QMessageBox::warning(this, kindLabel,
                tr("LDD model couldn't be rendered: %1 parts translated to LDraw, "
                   "%2 unmapped, %3 LDD-rendered, %4 skipped.\n\n%5")
                    .arg(translated).arg(unmapped)
                    .arg(lddBaked.rendered).arg(lddBaked.skipped)
                    .arg(allErrors.join('\n')));
            return true;
        }
        if (rast.image.isNull()) return true;

        // Preview before commit. Same dialog the LDraw path uses.
        ImportPreviewDialog::Stats st;
        st.ldrawResolved = ldrawResolved;
        st.lddRendered   = lddBaked.rendered;
        st.translated    = translated;
        st.unmapped      = unmapped;
        st.skipped       = lddBaked.skipped;
        ImportPreviewDialog dlg(source, kindLabel, rast.image,
                                static_cast<int>(std::round(rast.meshBoundsXZ.width())),
                                static_cast<int>(std::round(rast.meshBoundsXZ.height())),
                                st, allErrors, this);
        if (dlg.exec() != QDialog::Accepted) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return true;
        }

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
            dlg.partName(), rast.image, wStud, hStud, modulesRoot, author,
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
            tr("Imported %1 as '%2' — %3 LDraw-rendered + %4 LDD-rendered (%5 translated, %6 unmapped, %7 skipped)")
                .arg(QFileInfo(source).fileName(), key)
                .arg(ldrawResolved).arg(lddBaked.rendered)
                .arg(translated).arg(unmapped).arg(lddBaked.skipped), 10000);
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
