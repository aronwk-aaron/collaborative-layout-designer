#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"
#include "saveload/XmlPrimitives.h"

#include "core/LayerGrid.h"
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

TEST(RoundTrip, MapWithLayerGrid) {
    core::Map original = makeSampleMap();

    auto grid = std::make_unique<core::LayerGrid>();
    grid->guid = QStringLiteral("12345678-1234-5678-1234-567812345678");
    grid->name = QStringLiteral("My Grid");
    grid->visible = false;
    grid->transparency = 75;
    grid->hull.displayHulls = true;
    grid->hull.color = QColor(200, 100, 50);
    grid->hull.thickness = 3;
    grid->gridColor = QColor(10, 20, 30, 255);
    grid->gridThickness = 2.5f;
    grid->subGridColor = QColor(100, 110, 120, 180);
    grid->subGridThickness = 0.75f;
    grid->gridSizeInStud = 16;
    grid->subDivisionNumber = 8;
    grid->displayGrid = false;
    grid->displaySubGrid = true;
    grid->displayCellIndex = true;
    grid->cellIndexFont.familyName = QStringLiteral("Courier New");
    grid->cellIndexFont.sizePt = 12.0f;
    grid->cellIndexFont.styleString = QStringLiteral("Bold, Italic");
    grid->cellIndexColor = QColor(255, 128, 0);
    grid->cellIndexColumnType = core::CellIndexType::Numbers;
    grid->cellIndexRowType = core::CellIndexType::Letters;
    grid->cellIndexCorner = QPoint(10, -5);
    original.layers().push_back(std::move(grid));

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
    ASSERT_EQ(result.map->layers().size(), 1u);

    const auto* parsed = dynamic_cast<const core::LayerGrid*>(result.map->layers()[0].get());
    ASSERT_NE(parsed, nullptr);

    const auto& src = static_cast<const core::LayerGrid&>(*original.layers()[0]);
    EXPECT_EQ(parsed->guid, src.guid);
    EXPECT_EQ(parsed->name, src.name);
    EXPECT_EQ(parsed->visible, src.visible);
    EXPECT_EQ(parsed->transparency, src.transparency);
    EXPECT_EQ(parsed->hull.displayHulls, src.hull.displayHulls);
    EXPECT_EQ(parsed->hull.color.rgba(), src.hull.color.rgba());
    EXPECT_EQ(parsed->hull.thickness, src.hull.thickness);
    EXPECT_EQ(parsed->gridColor.rgba(), src.gridColor.rgba());
    EXPECT_FLOAT_EQ(parsed->gridThickness, src.gridThickness);
    EXPECT_EQ(parsed->subGridColor.rgba(), src.subGridColor.rgba());
    EXPECT_FLOAT_EQ(parsed->subGridThickness, src.subGridThickness);
    EXPECT_EQ(parsed->gridSizeInStud, src.gridSizeInStud);
    EXPECT_EQ(parsed->subDivisionNumber, src.subDivisionNumber);
    EXPECT_EQ(parsed->displayGrid, src.displayGrid);
    EXPECT_EQ(parsed->displaySubGrid, src.displaySubGrid);
    EXPECT_EQ(parsed->displayCellIndex, src.displayCellIndex);
    EXPECT_EQ(parsed->cellIndexFont.familyName, src.cellIndexFont.familyName);
    EXPECT_FLOAT_EQ(parsed->cellIndexFont.sizePt, src.cellIndexFont.sizePt);
    EXPECT_EQ(parsed->cellIndexFont.styleString, src.cellIndexFont.styleString);
    EXPECT_EQ(parsed->cellIndexColor.rgba(), src.cellIndexColor.rgba());
    EXPECT_EQ(parsed->cellIndexColumnType, src.cellIndexColumnType);
    EXPECT_EQ(parsed->cellIndexRowType, src.cellIndexRowType);
    EXPECT_EQ(parsed->cellIndexCorner, src.cellIndexCorner);
}

TEST(RoundTrip, UnknownLayerTypeWarnsNotFails) {
    // Vanilla .bbm with a <Layer type="brick"> (not yet implemented) should load
    // as a valid map with 0 layers and a non-empty warning.
    QByteArray bbm;
    {
        QBuffer out(&bbm);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        QXmlStreamWriter w(&out);
        w.setAutoFormatting(false);
        w.writeStartDocument(QStringLiteral("1.0"));
        w.writeStartElement(QStringLiteral("Map"));
        saveload::xml::writeIntElement(w, QStringLiteral("Version"), 9);
        saveload::xml::writeIntElement(w, QStringLiteral("nbItems"), 0);
        saveload::xml::writeColor(w, QStringLiteral("BackgroundColor"), Qt::white);
        saveload::xml::writeTextElement(w, QStringLiteral("Author"), QString());
        saveload::xml::writeTextElement(w, QStringLiteral("LUG"), QString());
        saveload::xml::writeTextElement(w, QStringLiteral("Event"), QString());
        w.writeStartElement(QStringLiteral("Date"));
        saveload::xml::writeIntElement(w, QStringLiteral("Day"), 1);
        saveload::xml::writeIntElement(w, QStringLiteral("Month"), 1);
        saveload::xml::writeIntElement(w, QStringLiteral("Year"), 2026);
        w.writeEndElement();
        saveload::xml::writeTextElement(w, QStringLiteral("Comment"), QString());
        w.writeStartElement(QStringLiteral("ExportInfo"));
        saveload::xml::writeTextElement(w, QStringLiteral("ExportPath"), QString());
        saveload::xml::writeIntElement(w, QStringLiteral("ExportFileType"), 0);
        saveload::xml::writeRectF(w, QStringLiteral("ExportArea"), QRectF());
        saveload::xml::writeDoubleElement(w, QStringLiteral("ExportScale"), 0.0);
        saveload::xml::writeBoolElement(w, QStringLiteral("ExportWatermark"), false);
        saveload::xml::writeBoolElement(w, QStringLiteral("ExportElectricCircuit"), false);
        saveload::xml::writeBoolElement(w, QStringLiteral("ExportConnectionPoints"), false);
        w.writeEndElement(); // ExportInfo
        saveload::xml::writeIntElement(w, QStringLiteral("SelectedLayerIndex"), -1);
        w.writeStartElement(QStringLiteral("Layers"));
        w.writeStartElement(QStringLiteral("Layer"));
        w.writeAttribute(QStringLiteral("type"), QStringLiteral("brick"));
        w.writeAttribute(QStringLiteral("id"), QStringLiteral("dummy"));
        w.writeEndElement();
        w.writeEndElement(); // Layers
        w.writeEndElement(); // Map
        w.writeEndDocument();
    }

    QBuffer in(&bbm);
    ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    auto result = saveload::readBbm(in);
    ASSERT_TRUE(result.ok()) << result.error.toStdString();
    EXPECT_TRUE(result.map->layers().empty());
    EXPECT_FALSE(result.error.isEmpty());  // non-fatal warning populated
}

TEST(RoundTrip, MissingMapRootErrors) {
    QByteArray junk = "<?xml version=\"1.0\"?><NotAMap/>";
    QBuffer in(&junk);
    ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    auto result = saveload::readBbm(in);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.error.isEmpty());
}
