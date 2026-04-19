#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"

#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>

using namespace cld;

namespace {

QString corpusDir() { return QString::fromUtf8(CLD_BBM_CORPUS_DIR); }

bool corpusAvailable() { return QDir(corpusDir()).exists(); }

}

TEST(RealFixture, TightCornerLoads) {
    if (!corpusAvailable()) GTEST_SKIP() << "corpus dir missing: " << corpusDir().toStdString();

    const QString path = corpusDir() + QStringLiteral("/tight-corner.bbm");
    if (!QFile::exists(path)) GTEST_SKIP() << "tight-corner.bbm not present";

    auto result = saveload::readBbm(path);
    ASSERT_TRUE(result.ok()) << result.error.toStdString();
    EXPECT_EQ(result.map->dataVersion, core::Map::kCurrentDataVersion);
    EXPECT_FALSE(result.map->layers().empty()) << "real project should have layers";

    // Count kinds to make sure dispatch actually populated subclasses.
    int nGrid = 0, nBrick = 0, nText = 0, nArea = 0, nRuler = 0;
    for (const auto& L : result.map->layers()) {
        switch (L->kind()) {
            case core::LayerKind::Grid:  ++nGrid;  break;
            case core::LayerKind::Brick: ++nBrick; break;
            case core::LayerKind::Text:  ++nText;  break;
            case core::LayerKind::Area:  ++nArea;  break;
            case core::LayerKind::Ruler: ++nRuler; break;
            case core::LayerKind::AnchoredText: break;
        }
    }
    // A typical train-club layout has at least a grid + a brick layer.
    EXPECT_GE(nGrid,  1);
    EXPECT_GE(nBrick, 1);
}

TEST(RealFixture, TightCornerSemanticRoundTrip) {
    // Load -> save to an in-memory buffer -> load again -> compare field-level
    // identity (not byte-exact; byte-exact is a later gate).
    if (!corpusAvailable()) GTEST_SKIP();
    const QString path = corpusDir() + QStringLiteral("/tight-corner.bbm");
    if (!QFile::exists(path)) GTEST_SKIP();

    auto first = saveload::readBbm(path);
    ASSERT_TRUE(first.ok()) << first.error.toStdString();

    QByteArray roundBuf;
    {
        QBuffer out(&roundBuf);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        ASSERT_TRUE(saveload::writeBbm(*first.map, out).ok);
    }

    QBuffer in(&roundBuf);
    ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    auto second = saveload::readBbm(in);
    ASSERT_TRUE(second.ok()) << second.error.toStdString();

    // Basic semantic equality at map-header + layer-count level.
    EXPECT_EQ(second.map->author, first.map->author);
    EXPECT_EQ(second.map->lug,    first.map->lug);
    EXPECT_EQ(second.map->event,  first.map->event);
    EXPECT_EQ(second.map->date,   first.map->date);
    EXPECT_EQ(second.map->backgroundColor.rgba(), first.map->backgroundColor.rgba());
    ASSERT_EQ(second.map->layers().size(), first.map->layers().size());

    // Compare brick counts per layer — catches item-list truncation.
    for (size_t i = 0; i < first.map->layers().size(); ++i) {
        if (first.map->layers()[i]->kind() != core::LayerKind::Brick) continue;
        const auto* a = static_cast<const core::LayerBrick*>(first.map->layers()[i].get());
        const auto* b = static_cast<const core::LayerBrick*>(second.map->layers()[i].get());
        EXPECT_EQ(a->bricks.size(), b->bricks.size()) << "brick count changed on round-trip (layer " << i << ")";
        EXPECT_EQ(a->groups.size(), b->groups.size()) << "group count changed (layer " << i << ")";
    }
}
