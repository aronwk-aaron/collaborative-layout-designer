#include "geom/Mesh.h"
#include "import/mesh/MeshRasterize.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QImage>

using namespace cld;

TEST(MeshRasterize, EmptyMeshReturnsNullImage) {
    geom::Mesh m;
    const auto r = import::rasterizeMeshTopDown(m);
    EXPECT_TRUE(r.image.isNull());
}

TEST(MeshRasterize, SingleTriangleProducesColouredPixels) {
    geom::Mesh m;
    geom::Triangle t;
    // 1-stud (20 LDU) right triangle in the xz plane.
    t.v[0] = { 0,  0,  0 };
    t.v[1] = { 20, 0, 0 };
    t.v[2] = { 0,  0, 20 };
    t.color = QColor::fromRgb(255, 0, 0);
    m.tris.push_back(t);

    const auto r = import::rasterizeMeshTopDown(m);
    ASSERT_FALSE(r.image.isNull());
    EXPECT_EQ(r.meshBoundsXZ.width(),  1.0);
    EXPECT_EQ(r.meshBoundsXZ.height(), 1.0);

    // At default 8 px/stud + 2 px margin → ~12×12 image. Sample
    // somewhere clearly inside the triangle (a couple of px right /
    // down of the top-left corner) and verify it's red-ish.
    QImage img = r.image.convertToFormat(QImage::Format_ARGB32);
    const QColor px = img.pixelColor(3, 3);
    EXPECT_GT(px.red(), 200);
    EXPECT_LT(px.green(), 50);
    EXPECT_LT(px.blue(), 50);
    EXPECT_GT(px.alpha(), 200);
}

TEST(MeshRasterize, HigherYTrianglePaintsOver) {
    geom::Mesh m;
    geom::Triangle low, hi;
    // Two overlapping squares (split into 2 tris each); low one is
    // green at Y=0, high one is blue at Y=10. Full overlap so every
    // pixel of the visible result should be blue.
    low.color = QColor(0, 200, 0);
    hi.color  = QColor(0, 0, 200);
    low.v[0] = { 0,  0, 0 };
    low.v[1] = { 20, 0, 0 };
    low.v[2] = { 0,  0, 20 };
    hi.v[0] = { 0,  10, 0 };
    hi.v[1] = { 20, 10, 0 };
    hi.v[2] = { 0,  10, 20 };
    m.tris.push_back(low);
    m.tris.push_back(hi);

    const auto r = import::rasterizeMeshTopDown(m);
    ASSERT_FALSE(r.image.isNull());
    QImage img = r.image.convertToFormat(QImage::Format_ARGB32);
    const QColor px = img.pixelColor(3, 3);
    // Sort puts high-Y last → blue should win.
    EXPECT_LT(px.green(), 50);
    EXPECT_GT(px.blue(), 150);
}
