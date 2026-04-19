#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"
#include "saveload/XmlPrimitives.h"

#include "core/Map.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QByteArray>

using namespace cld;

namespace {

core::Map makeSampleMap() {
    // We can't use the default constructor-copy because Map is non-copyable,
    // so callers that need a map build one directly.
    core::Map m;
    m.author = QStringLiteral("Ada Lovelace");
    m.lug = QStringLiteral("Analytic Engine LUG");
    m.event = QStringLiteral("Spring 2026 Exhibition");
    m.date = QDate(2026, 4, 18);
    m.comment = QStringLiteral("Line 1\nLine 2 with UTF-8: café");
    m.backgroundColor = QColor(240, 240, 240);
    m.exportInfo.exportPath = QStringLiteral("export/out.png");
    m.exportInfo.fileTypeIndex = 4;
    m.exportInfo.area = QRectF(0.0, 0.0, 1024.0, 768.0);
    m.exportInfo.scale = 2.5;
    m.exportInfo.watermark = true;
    m.exportInfo.electricCircuit = false;
    m.exportInfo.connectionPoints = true;
    m.selectedLayerIndex = -1;
    return m;
}

}

TEST(RoundTrip, MapHeaderInMemory) {
    const core::Map original = makeSampleMap();

    QByteArray buf;
    {
        QBuffer out(&buf);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        const auto w = saveload::writeBbm(original, out);
        ASSERT_TRUE(w.ok) << w.error.toStdString();
    }

    QBuffer in(&buf);
    ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    auto result = saveload::readBbm(in);
    ASSERT_TRUE(result.ok()) << result.error.toStdString();
    const auto& round = *result.map;

    EXPECT_EQ(round.dataVersion, core::Map::kCurrentDataVersion);
    EXPECT_EQ(round.author, original.author);
    EXPECT_EQ(round.lug, original.lug);
    EXPECT_EQ(round.event, original.event);
    EXPECT_EQ(round.date, original.date);
    EXPECT_EQ(round.comment, original.comment);
    EXPECT_EQ(round.backgroundColor.rgba(), original.backgroundColor.rgba());
    EXPECT_EQ(round.exportInfo.exportPath, original.exportInfo.exportPath);
    EXPECT_EQ(round.exportInfo.fileTypeIndex, original.exportInfo.fileTypeIndex);
    EXPECT_EQ(round.exportInfo.area, original.exportInfo.area);
    EXPECT_DOUBLE_EQ(round.exportInfo.scale, original.exportInfo.scale);
    EXPECT_EQ(round.exportInfo.watermark, original.exportInfo.watermark);
    EXPECT_EQ(round.exportInfo.electricCircuit, original.exportInfo.electricCircuit);
    EXPECT_EQ(round.exportInfo.connectionPoints, original.exportInfo.connectionPoints);
    EXPECT_EQ(round.selectedLayerIndex, original.selectedLayerIndex);
    EXPECT_TRUE(round.layers().empty());
}

TEST(Primitives, BoolFormatting) {
    EXPECT_EQ(saveload::xml::formatBool(true),  QStringLiteral("true"));
    EXPECT_EQ(saveload::xml::formatBool(false), QStringLiteral("false"));
}

TEST(Primitives, InvariantCultureFloatFormatting) {
    // No locale-specific thousands separators or commas. BlueBrick targets
    // .NET Framework 4.8 whose default double.ToString() is G15 (trims trailing
    // zeros) -> (0.1+0.2).ToString() == "0.3". .NET Core's round-trip default
    // would emit "0.30000000000000004" but that is NOT what vanilla emits.
    EXPECT_EQ(saveload::xml::formatFloat(1234.5f), QStringLiteral("1234.5"));
    EXPECT_EQ(saveload::xml::formatFloat(0.5f),    QStringLiteral("0.5"));
    EXPECT_EQ(saveload::xml::formatDouble(0.1 + 0.2), QStringLiteral("0.3"));
}

TEST(RoundTrip, MissingMapRootErrors) {
    QByteArray junk = "<?xml version=\"1.0\"?><NotAMap/>";
    QBuffer in(&junk);
    ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    auto result = saveload::readBbm(in);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.error.isEmpty());
}
