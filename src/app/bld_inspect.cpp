#include "saveload/BbmReader.h"

#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QString>

#include <cstdio>

using namespace bld;

namespace {

const char* layerKindName(core::LayerKind k) {
    switch (k) {
        case core::LayerKind::Grid:  return "grid";
        case core::LayerKind::Brick: return "brick";
        case core::LayerKind::Text:  return "text";
        case core::LayerKind::Area:  return "area";
        case core::LayerKind::Ruler: return "ruler";
        case core::LayerKind::AnchoredText: return "anchored-text";
    }
    return "?";
}

void dumpLayer(const core::Layer& L, int i) {
    std::printf("  [%d] %-14s name=%s visible=%s transparency=%d",
                i, layerKindName(L.kind()),
                L.name.toUtf8().constData(), L.visible ? "true" : "false", L.transparency);
    switch (L.kind()) {
        case core::LayerKind::Brick: {
            const auto& B = static_cast<const core::LayerBrick&>(L);
            std::printf("  bricks=%zu groups=%zu", B.bricks.size(), B.groups.size());
            break;
        }
        case core::LayerKind::Text: {
            const auto& T = static_cast<const core::LayerText&>(L);
            std::printf("  cells=%zu groups=%zu", T.textCells.size(), T.groups.size());
            break;
        }
        case core::LayerKind::Area: {
            const auto& A = static_cast<const core::LayerArea&>(L);
            std::printf("  cells=%zu cellSize=%d", A.cells.size(), A.areaCellSizeInStud);
            break;
        }
        case core::LayerKind::Ruler: {
            const auto& R = static_cast<const core::LayerRuler&>(L);
            std::printf("  rulers=%zu groups=%zu", R.rulers.size(), R.groups.size());
            break;
        }
        case core::LayerKind::Grid: {
            const auto& G = static_cast<const core::LayerGrid&>(L);
            std::printf("  gridSize=%d sub=%d", G.gridSizeInStud, G.subDivisionNumber);
            break;
        }
        default: break;
    }
    std::printf("\n");
}

}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::fprintf(stderr, "Usage: bld-inspect <file.bbm>\n");
        return 1;
    }
    const QString path = QString::fromUtf8(argv[1]);
    auto result = saveload::readBbm(path);
    if (!result.ok()) {
        std::fprintf(stderr, "Failed: %s\n", result.error.toUtf8().constData());
        return 2;
    }
    if (!result.error.isEmpty()) {
        std::fprintf(stderr, "Warning: %s\n", result.error.toUtf8().constData());
    }

    const auto& m = *result.map;
    std::printf("File: %s\n", path.toUtf8().constData());
    std::printf("Version: %d\n", m.dataVersion);
    std::printf("Author: %s\n", m.author.toUtf8().constData());
    std::printf("LUG: %s\n",    m.lug.toUtf8().constData());
    std::printf("Event: %s\n",  m.event.toUtf8().constData());
    std::printf("Date: %s\n",   m.date.toString(Qt::ISODate).toUtf8().constData());
    std::printf("nbItems: %d\n", m.nbItems);
    std::printf("Background: %s (%s)\n",
                m.backgroundColor.knownName.isEmpty() ? m.backgroundColor.color.name().toUtf8().constData()
                                                      : m.backgroundColor.knownName.toUtf8().constData(),
                m.backgroundColor.isKnown() ? "known" : "argb");
    std::printf("Layers (%zu):\n", m.layers().size());
    for (size_t i = 0; i < m.layers().size(); ++i) dumpLayer(*m.layers()[i], static_cast<int>(i));
    return 0;
}
