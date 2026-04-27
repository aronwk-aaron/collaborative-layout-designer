#include "import/LDrawLibrary.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace cld;

namespace {

// Create a minimal LDraw root with the tree layout the resolver
// expects, plus a couple of stub .dat files so resolve() can return
// real paths.
struct LDrawTree {
    QTemporaryDir dir;
    QString root;

    LDrawTree() {
        if (!dir.isValid()) ADD_FAILURE() << "tmp dir failed";
        root = dir.path();
        QDir d(root);
        d.mkpath(QStringLiteral("parts"));
        d.mkpath(QStringLiteral("parts/s"));
        d.mkpath(QStringLiteral("p"));
        d.mkpath(QStringLiteral("p/48"));
        d.mkpath(QStringLiteral("p/8"));
        write(QStringLiteral("LDConfig.ldr"), QByteArray("0 // stub palette\n"));
        write(QStringLiteral("parts/3001.dat"),       QByteArray("0 Brick 2x4\n"));
        write(QStringLiteral("parts/s/3001s01.dat"),  QByteArray("0 Sub of 3001\n"));
        write(QStringLiteral("p/box.dat"),            QByteArray("0 Box primitive\n"));
        write(QStringLiteral("p/48/4-4cyli.dat"),     QByteArray("0 Hi-res cyl\n"));
    }

    void write(const QString& rel, const QByteArray& body) {
        QFile f(QDir(root).absoluteFilePath(rel));
        if (!f.open(QIODevice::WriteOnly)) {
            ADD_FAILURE() << "couldn't write " << rel.toStdString();
            return;
        }
        f.write(body);
    }
};

}

TEST(LDrawLibrary, LooksValidRequiresLDConfigAndParts) {
    import::LDrawLibrary empty;
    EXPECT_FALSE(empty.looksValid());

    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    EXPECT_TRUE(lib.looksValid());
}

TEST(LDrawLibrary, ResolvesPartsBeforePrimitives) {
    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    const QString hit = lib.resolve(QStringLiteral("3001.dat"));
    ASSERT_FALSE(hit.isEmpty());
    EXPECT_TRUE(hit.endsWith(QStringLiteral("/parts/3001.dat")));
}

TEST(LDrawLibrary, ResolvesPrimitiveFromP) {
    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    const QString hit = lib.resolve(QStringLiteral("box.dat"));
    ASSERT_FALSE(hit.isEmpty());
    EXPECT_TRUE(hit.endsWith(QStringLiteral("/p/box.dat")));
}

TEST(LDrawLibrary, ResolvesSubPartViaSubdirHint) {
    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    // Author tools write subparts as "s\3001s01.dat" or "s/3001s01.dat";
    // resolver should split the prefix and find parts/s/.
    const QString hit = lib.resolve(QStringLiteral("s\\3001s01.dat"));
    ASSERT_FALSE(hit.isEmpty());
    EXPECT_TRUE(hit.endsWith(QStringLiteral("/parts/s/3001s01.dat")));
}

TEST(LDrawLibrary, Resolves48HighResPrimitive) {
    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    const QString hit = lib.resolve(QStringLiteral("4-4cyli.dat"));
    ASSERT_FALSE(hit.isEmpty());
    // Bare-name lookup walks parts/ → p/ → p/48 → p/8, so this lands at p/48.
    EXPECT_TRUE(hit.endsWith(QStringLiteral("/p/48/4-4cyli.dat")));
}

TEST(LDrawLibrary, ReturnsEmptyForUnknown) {
    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    EXPECT_TRUE(lib.resolve(QStringLiteral("does-not-exist.dat")).isEmpty());
}
