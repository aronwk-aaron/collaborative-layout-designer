#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "parts/PartsLibrary.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>

namespace {

class QtFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!app_) {
            static int argc = 1;
            static char name[] = "cld_rendering_tests";
            static char* argv[] = { name, nullptr };
            app_ = new QApplication(argc, argv);
        }
    }
    void TearDown() override { /* don't delete; keeps QPixmap alive */ }
private:
    QApplication* app_ = nullptr;
};

::testing::Environment* const kEnv = ::testing::AddGlobalTestEnvironment(new QtFixture());

QString corpusDir()     { return QString::fromUtf8(CLD_BBM_CORPUS_DIR); }
QString partsLibRoot()  { return QString::fromUtf8(CLD_PARTS_LIBRARY_ROOT); }

}

TEST(RenderSmoke, LoadAndRasterizeTightCorner) {
    const QString path = corpusDir() + QStringLiteral("/tight-corner.bbm");
    if (!QFile::exists(path)) GTEST_SKIP() << "tight-corner.bbm missing";
    if (!QDir(partsLibRoot()).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";

    cld::parts::PartsLibrary lib;
    lib.addSearchPath(partsLibRoot());
    ASSERT_GT(lib.scan(), 100);

    auto loaded = cld::saveload::readBbm(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error.toStdString();

    QGraphicsScene scene;
    cld::rendering::SceneBuilder builder(scene, lib);
    builder.build(*loaded.map);

    const QRectF bounds = scene.itemsBoundingRect();
    ASSERT_FALSE(bounds.isEmpty()) << "scene produced no graphics items";

    // Rasterize to a fixed-size image and verify it is not entirely blank.
    QImage img(512, 512, QImage::Format_ARGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        scene.render(&p, QRectF(0, 0, 512, 512), bounds, Qt::KeepAspectRatio);
    }

    // Any non-white pixel tells us the pipeline drew *something* real
    // (bricks, text, areas, or rulers).
    int nonWhite = 0;
    for (int y = 0; y < img.height(); y += 4) {
        for (int x = 0; x < img.width(); x += 4) {
            if (img.pixel(x, y) != qRgb(255, 255, 255)) ++nonWhite;
        }
    }
    EXPECT_GT(nonWhite, 50) << "raster looks empty; render pipeline likely broken";
}

TEST(RenderSmoke, LayerVisibilityToggle) {
    const QString path = corpusDir() + QStringLiteral("/tight-corner.bbm");
    if (!QFile::exists(path)) GTEST_SKIP();
    if (!QDir(partsLibRoot()).exists()) GTEST_SKIP();

    cld::parts::PartsLibrary lib;
    lib.addSearchPath(partsLibRoot());
    lib.scan();

    auto loaded = cld::saveload::readBbm(path);
    ASSERT_TRUE(loaded.ok());

    QGraphicsScene scene;
    cld::rendering::SceneBuilder builder(scene, lib);
    builder.build(*loaded.map);

    EXPECT_TRUE(builder.setLayerVisible(0, false));
    EXPECT_FALSE(builder.setLayerVisible(9999, false));  // out of range
}
