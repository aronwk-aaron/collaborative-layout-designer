#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"
#include "saveload/XmlPrimitives.h"

#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QByteArray>

using namespace bld;

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
    m.backgroundColor = core::ColorSpec::fromArgb(QColor(240, 240, 240));
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
    EXPECT_EQ(round.backgroundColor, original.backgroundColor);
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
    // Numeric guid — vanilla BlueBrick requires ulong-parsable ids; round-trip
    // tests mirror real .bbm content by using decimal numeric strings.
    grid->guid = QStringLiteral("100");
    grid->name = QStringLiteral("My Grid");
    grid->visible = false;
    grid->transparency = 75;
    grid->hull.displayHulls = true;
    grid->hull.color = core::ColorSpec::fromArgb(QColor(200, 100, 50));
    grid->hull.thickness = 3;
    grid->gridColor = core::ColorSpec::fromArgb(QColor(10, 20, 30, 255));
    grid->gridThickness = 2.5f;
    grid->subGridColor = core::ColorSpec::fromArgb(QColor(100, 110, 120, 180));
    grid->subGridThickness = 0.75f;
    grid->gridSizeInStud = 16;
    grid->subDivisionNumber = 8;
    grid->displayGrid = false;
    grid->displaySubGrid = true;
    grid->displayCellIndex = true;
    grid->cellIndexFont.familyName = QStringLiteral("Courier New");
    grid->cellIndexFont.sizePt = 12.0f;
    grid->cellIndexFont.styleString = QStringLiteral("Bold, Italic");
    grid->cellIndexColor = core::ColorSpec::fromArgb(QColor(255, 128, 0));
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
    EXPECT_EQ(parsed->hull.color.color.rgba(), src.hull.color.color.rgba());
    EXPECT_EQ(parsed->hull.thickness, src.hull.thickness);
    EXPECT_EQ(parsed->gridColor.color.rgba(), src.gridColor.color.rgba());
    EXPECT_FLOAT_EQ(parsed->gridThickness, src.gridThickness);
    EXPECT_EQ(parsed->subGridColor.color.rgba(), src.subGridColor.color.rgba());
    EXPECT_FLOAT_EQ(parsed->subGridThickness, src.subGridThickness);
    EXPECT_EQ(parsed->gridSizeInStud, src.gridSizeInStud);
    EXPECT_EQ(parsed->subDivisionNumber, src.subDivisionNumber);
    EXPECT_EQ(parsed->displayGrid, src.displayGrid);
    EXPECT_EQ(parsed->displaySubGrid, src.displaySubGrid);
    EXPECT_EQ(parsed->displayCellIndex, src.displayCellIndex);
    EXPECT_EQ(parsed->cellIndexFont.familyName, src.cellIndexFont.familyName);
    EXPECT_FLOAT_EQ(parsed->cellIndexFont.sizePt, src.cellIndexFont.sizePt);
    EXPECT_EQ(parsed->cellIndexFont.styleString, src.cellIndexFont.styleString);
    EXPECT_EQ(parsed->cellIndexColor.color.rgba(), src.cellIndexColor.color.rgba());
    EXPECT_EQ(parsed->cellIndexColumnType, src.cellIndexColumnType);
    EXPECT_EQ(parsed->cellIndexRowType, src.cellIndexRowType);
    EXPECT_EQ(parsed->cellIndexCorner, src.cellIndexCorner);
}

TEST(RoundTrip, MapWithLayerBrick) {
    core::Map original = makeSampleMap();

    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = QStringLiteral("200");
    layer->name = QStringLiteral("Track");
    layer->visible = true;
    layer->transparency = 90;
    layer->hull.color = core::ColorSpec::fromArgb(QColor(50, 80, 120));
    layer->hull.thickness = 2;
    layer->displayBrickElevation = true;

    core::Brick b;
    b.guid = QStringLiteral("201");
    b.displayArea = QRectF(10.0, 20.0, 32.0, 16.0);
    b.myGroupId = QStringLiteral("202");
    b.partNumber = QStringLiteral("2875");
    b.orientation = 90.5f;
    b.activeConnectionPointIndex = 1;
    b.altitude = 0.5f;
    core::ConnectionPoint cp0; cp0.guid = QStringLiteral("203"); cp0.linkedToId = QStringLiteral("204");
    core::ConnectionPoint cp1; cp1.guid = QStringLiteral("205");
    b.connections = { cp0, cp1 };
    layer->bricks.push_back(b);

    core::Group g;
    g.guid = QStringLiteral("202");
    g.partNumber = QStringLiteral("CrossingGate");
    layer->groups.push_back(g);

    original.layers().push_back(std::move(layer));

    QByteArray buf;
    {
        QBuffer out(&buf);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        ASSERT_TRUE(saveload::writeBbm(original, out).ok);
    }

    QBuffer in(&buf);
    ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    auto result = saveload::readBbm(in);
    ASSERT_TRUE(result.ok()) << result.error.toStdString();
    ASSERT_EQ(result.map->layers().size(), 1u);

    const auto* parsed = dynamic_cast<const core::LayerBrick*>(result.map->layers()[0].get());
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->guid, QStringLiteral("200"));
    EXPECT_TRUE(parsed->displayBrickElevation);
    ASSERT_EQ(parsed->bricks.size(), 1u);
    EXPECT_EQ(parsed->bricks[0].guid, QStringLiteral("201"));
    EXPECT_EQ(parsed->bricks[0].partNumber, QStringLiteral("2875"));
    EXPECT_FLOAT_EQ(parsed->bricks[0].orientation, 90.5f);
    EXPECT_EQ(parsed->bricks[0].activeConnectionPointIndex, 1);
    EXPECT_FLOAT_EQ(parsed->bricks[0].altitude, 0.5f);
    EXPECT_EQ(parsed->bricks[0].myGroupId, QStringLiteral("202"));
    EXPECT_EQ(parsed->bricks[0].displayArea, QRectF(10.0, 20.0, 32.0, 16.0));
    ASSERT_EQ(parsed->bricks[0].connections.size(), 2u);
    EXPECT_EQ(parsed->bricks[0].connections[0].guid, QStringLiteral("203"));
    EXPECT_EQ(parsed->bricks[0].connections[0].linkedToId, QStringLiteral("204"));
    EXPECT_EQ(parsed->bricks[0].connections[1].guid, QStringLiteral("205"));
    EXPECT_EQ(parsed->bricks[0].connections[1].linkedToId, QString());
    ASSERT_EQ(parsed->groups.size(), 1u);
    EXPECT_EQ(parsed->groups[0].guid, QStringLiteral("202"));
    EXPECT_EQ(parsed->groups[0].partNumber, QStringLiteral("CrossingGate"));
}

TEST(RoundTrip, MapWithLayerText) {
    core::Map original = makeSampleMap();
    auto layer = std::make_unique<core::LayerText>();
    layer->guid = QStringLiteral("300");
    layer->name = QStringLiteral("Labels");

    core::TextCell c;
    c.guid = QStringLiteral("301");
    c.displayArea = QRectF(0, 0, 100, 20);
    c.text = QStringLiteral("Hello\nworld");
    c.orientation = 45.0f;
    c.fontColor = core::ColorSpec::fromArgb(QColor(128, 0, 128));
    c.font.familyName = QStringLiteral("Arial");
    c.font.sizePt = 14.0f;
    c.font.styleString = QStringLiteral("Italic");
    c.alignment = core::TextAlignment::Far;
    layer->textCells.push_back(c);
    original.layers().push_back(std::move(layer));

    QByteArray buf;
    { QBuffer out(&buf); ASSERT_TRUE(out.open(QIODevice::WriteOnly)); ASSERT_TRUE(saveload::writeBbm(original, out).ok); }
    QBuffer in(&buf); ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    auto result = saveload::readBbm(in);
    ASSERT_TRUE(result.ok()) << result.error.toStdString();

    const auto* parsed = dynamic_cast<const core::LayerText*>(result.map->layers()[0].get());
    ASSERT_NE(parsed, nullptr);
    ASSERT_EQ(parsed->textCells.size(), 1u);
    EXPECT_EQ(parsed->textCells[0].text, QStringLiteral("Hello\nworld"));
    EXPECT_FLOAT_EQ(parsed->textCells[0].orientation, 45.0f);
    EXPECT_EQ(parsed->textCells[0].font.familyName, QStringLiteral("Arial"));
    EXPECT_FLOAT_EQ(parsed->textCells[0].font.sizePt, 14.0f);
    EXPECT_EQ(parsed->textCells[0].font.styleString, QStringLiteral("Italic"));
    EXPECT_EQ(parsed->textCells[0].alignment, core::TextAlignment::Far);
}

TEST(RoundTrip, MapWithLayerArea) {
    core::Map original = makeSampleMap();
    auto layer = std::make_unique<core::LayerArea>();
    layer->guid = QStringLiteral("area-layer");
    layer->name = QStringLiteral("Grass");
    layer->areaCellSizeInStud = 16;
    layer->cells = {
        core::AreaCell{ 0, 0, QColor(0, 128, 0) },
        core::AreaCell{ 1, 0, QColor(0, 128, 0, 200) },
        core::AreaCell{ -5, 3, QColor(128, 128, 0) },
    };
    original.layers().push_back(std::move(layer));

    QByteArray buf;
    { QBuffer out(&buf); ASSERT_TRUE(out.open(QIODevice::WriteOnly)); ASSERT_TRUE(saveload::writeBbm(original, out).ok); }
    QBuffer in(&buf); ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    auto result = saveload::readBbm(in);
    ASSERT_TRUE(result.ok()) << result.error.toStdString();

    const auto* parsed = dynamic_cast<const core::LayerArea*>(result.map->layers()[0].get());
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->areaCellSizeInStud, 16);
    ASSERT_EQ(parsed->cells.size(), 3u);
    EXPECT_EQ(parsed->cells[0].x, 0);
    EXPECT_EQ(parsed->cells[0].y, 0);
    EXPECT_EQ(parsed->cells[0].color.rgba(), QColor(0, 128, 0).rgba());
    EXPECT_EQ(parsed->cells[1].color.alpha(), 200);
    EXPECT_EQ(parsed->cells[2].x, -5);
    EXPECT_EQ(parsed->cells[2].y, 3);
}

TEST(RoundTrip, MapWithLayerRuler) {
    core::Map original = makeSampleMap();
    auto layer = std::make_unique<core::LayerRuler>();
    layer->guid = QStringLiteral("400");
    layer->name = QStringLiteral("Measurements");

    core::LayerRuler::AnyRuler lin;
    lin.kind = core::RulerKind::Linear;
    lin.linear.guid = QStringLiteral("ruler-1");
    lin.linear.displayArea = QRectF(0, 0, 100, 10);
    lin.linear.color = core::ColorSpec::fromArgb(QColor(255, 0, 0));
    lin.linear.lineThickness = 1.5f;
    lin.linear.displayDistance = true;
    lin.linear.displayUnit = false;
    lin.linear.point1 = QPointF(10, 20);
    lin.linear.point2 = QPointF(110, 20);
    lin.linear.attachedBrick1Id = QStringLiteral("500");
    lin.linear.attachedBrick2Id = QString();
    lin.linear.offsetDistance = 5.0f;
    lin.linear.allowOffset = true;
    layer->rulers.push_back(lin);

    core::LayerRuler::AnyRuler cir;
    cir.kind = core::RulerKind::Circular;
    cir.circular.guid = QStringLiteral("ruler-2");
    cir.circular.displayArea = QRectF(-20, -20, 40, 40);
    cir.circular.color = core::ColorSpec::fromArgb(QColor(0, 255, 0));
    cir.circular.lineThickness = 2.0f;
    cir.circular.displayDistance = false;
    cir.circular.displayUnit = true;
    cir.circular.center = QPointF(0, 0);
    cir.circular.radius = 20.0f;
    cir.circular.attachedBrickId = QStringLiteral("501");
    layer->rulers.push_back(cir);
    original.layers().push_back(std::move(layer));

    QByteArray buf;
    { QBuffer out(&buf); ASSERT_TRUE(out.open(QIODevice::WriteOnly)); ASSERT_TRUE(saveload::writeBbm(original, out).ok); }
    QBuffer in(&buf); ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    auto result = saveload::readBbm(in);
    ASSERT_TRUE(result.ok()) << result.error.toStdString();

    const auto* parsed = dynamic_cast<const core::LayerRuler*>(result.map->layers()[0].get());
    ASSERT_NE(parsed, nullptr);
    ASSERT_EQ(parsed->rulers.size(), 2u);
    ASSERT_EQ(parsed->rulers[0].kind, core::RulerKind::Linear);
    // Ruler items don't carry an id attribute in the serialized XML
    // (vanilla parity), so writer emits no id and reader sees empty.
    // migrateNonNumericIds then mints a fresh numeric id on load so
    // every ruler has a unique, non-empty guid — otherwise in-app
    // selection / sidecar references collapse every ruler into one.
    EXPECT_FALSE(parsed->rulers[0].linear.guid.isEmpty());
    EXPECT_EQ(parsed->rulers[0].linear.point1, QPointF(10, 20));
    EXPECT_EQ(parsed->rulers[0].linear.point2, QPointF(110, 20));
    EXPECT_EQ(parsed->rulers[0].linear.attachedBrick1Id, QStringLiteral("500"));
    EXPECT_FLOAT_EQ(parsed->rulers[0].linear.offsetDistance, 5.0f);
    EXPECT_TRUE(parsed->rulers[0].linear.allowOffset);
    EXPECT_TRUE(parsed->rulers[0].linear.displayDistance);
    EXPECT_FALSE(parsed->rulers[0].linear.displayUnit);
    ASSERT_EQ(parsed->rulers[1].kind, core::RulerKind::Circular);
    EXPECT_FALSE(parsed->rulers[1].circular.guid.isEmpty());
    EXPECT_NE(parsed->rulers[0].linear.guid, parsed->rulers[1].circular.guid);
    EXPECT_EQ(parsed->rulers[1].circular.center, QPointF(0, 0));
    EXPECT_FLOAT_EQ(parsed->rulers[1].circular.radius, 20.0f);
    EXPECT_EQ(parsed->rulers[1].circular.attachedBrickId, QStringLiteral("501"));
}

TEST(RoundTrip, UnknownLayerTypeWarnsNotFails) {
    // A future/unknown layer type must surface as a warning without blocking the load.
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
        saveload::xml::writeColor(w, QStringLiteral("BackgroundColor"),
            core::ColorSpec::fromKnown(QColor(Qt::white), QStringLiteral("White")));
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
        w.writeAttribute(QStringLiteral("type"), QStringLiteral("futuretype"));
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
