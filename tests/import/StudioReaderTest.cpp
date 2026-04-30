// Tests for StudioReader. Builds a synthetic `.io` (ZIP) in a temp
// directory using QZipWriter, then runs it through readStudioIo.
//
// Uses Qt's private QZipWriter — same API we already use for the
// reader side, so no extra deps.

#include "import/studio/StudioReader.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

#include <private/qzipwriter_p.h>

#include "core/Map.h"
#include "core/LayerBrick.h"

using namespace bld;

namespace {

QString writeIo(QTemporaryDir& dir, const QString& name,
                const QString& entryName, const QByteArray& entryData) {
    const QString path = dir.filePath(name);
    {
        QZipWriter zw(path);
        zw.setCompressionPolicy(QZipWriter::AlwaysCompress);
        zw.addFile(entryName, entryData);
    }
    return path;
}

}  // namespace

TEST(StudioReader, ReadsModelLdrFromValidIo) {
    QTemporaryDir dir;
    const QByteArray body =
        "0 Studio test\n"
        "1 16 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n"
        "1 4 40 0 0 1 0 0 0 1 0 0 0 1 3002.dat\n";
    const QString io = writeIo(dir, "test.io", "model.ldr", body);
    auto r = import::readStudioIo(io);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.title, QStringLiteral("Studio test"));
    ASSERT_EQ(r.parts.size(), 2u);
    EXPECT_EQ(r.parts[0].filename, QStringLiteral("3001.dat"));
    EXPECT_EQ(r.parts[1].filename, QStringLiteral("3002.dat"));
    EXPECT_DOUBLE_EQ(r.parts[1].x, 40.0);
}

TEST(StudioReader, AcceptsMixedCaseEntryName) {
    // Real Studio 2.0 files sometimes capitalise "Model.ldr" or put it
    // under a nested path. Our reader should find it regardless.
    QTemporaryDir dir;
    const QString io = writeIo(dir, "capcase.io", "Model.LDR",
        QByteArray("0 mixed case\n"
                   "1 16 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n"));
    auto r = import::readStudioIo(io);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.title, QStringLiteral("mixed case"));
    ASSERT_EQ(r.parts.size(), 1u);
}

TEST(StudioReader, MissingModelLdrFails) {
    QTemporaryDir dir;
    const QString io = writeIo(dir, "no-model.io", "other.txt",
                               QByteArray("not the model"));
    auto r = import::readStudioIo(io);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.isEmpty());
}

TEST(StudioReader, InvalidZipFails) {
    QTemporaryDir dir;
    const QString path = dir.filePath("bogus.io");
    {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("this is not a zip");
    }
    auto r = import::readStudioIo(path);
    EXPECT_FALSE(r.ok);
}

TEST(StudioReader, ProducesPlaceableBrickMap) {
    // End-to-end: io → readStudioIo → toBlueBrickMap gives us a
    // usable map with one brick layer. This is the path the UI uses
    // when the user imports a .io as a library part.
    QTemporaryDir dir;
    const QString io = writeIo(dir, "e2e.io", "model.ldr",
        QByteArray("0 end to end\n"
                   "1 4 20 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n"));
    auto r = import::readStudioIo(io);
    ASSERT_TRUE(r.ok);
    auto m = import::toBlueBrickMap(r);
    ASSERT_NE(m, nullptr);
    ASSERT_FALSE(m->layers().empty());
    const auto* L = static_cast<const core::LayerBrick*>(m->layers()[0].get());
    ASSERT_EQ(L->bricks.size(), 1u);
    // 20 LDU / 20 = 1 stud on the X axis.
    EXPECT_NEAR(L->bricks[0].displayArea.center().x(), 1.0, 1e-6);
}
