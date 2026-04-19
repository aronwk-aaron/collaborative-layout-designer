#include "edit/AreaCommands.h"
#include "core/LayerArea.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QUndoStack>

using namespace cld;

namespace {

core::Map makeMapWithAreaLayer() {
    core::Map m;
    auto L = std::make_unique<core::LayerArea>();
    L->areaCellSizeInStud = 32;
    m.layers().push_back(std::move(L));
    return m;
}

const core::LayerArea& areaOf(const core::Map& m) {
    return static_cast<const core::LayerArea&>(*m.layers()[0]);
}

}

TEST(AreaCommands, PaintSingleCellAndUndo) {
    core::Map m = makeMapWithAreaLayer();
    QUndoStack stack;

    std::vector<edit::PaintAreaCellsCommand::Change> changes = {
        { 1, 2, QColor(0, 128, 0) }
    };
    stack.push(new edit::PaintAreaCellsCommand(m, 0, changes));

    const auto& L = areaOf(m);
    ASSERT_EQ(L.cells.size(), 1u);
    EXPECT_EQ(L.cells[0].x, 1);
    EXPECT_EQ(L.cells[0].y, 2);
    EXPECT_EQ(L.cells[0].color.rgba(), QColor(0, 128, 0).rgba());

    stack.undo();
    EXPECT_TRUE(L.cells.empty());
    stack.redo();
    ASSERT_EQ(L.cells.size(), 1u);
}

TEST(AreaCommands, PaintOverwritesExistingCell) {
    core::Map m = makeMapWithAreaLayer();
    auto& L = static_cast<core::LayerArea&>(*m.layers()[0]);
    L.cells.push_back({ 5, 5, QColor(200, 0, 0) });

    QUndoStack stack;
    std::vector<edit::PaintAreaCellsCommand::Change> changes = {
        { 5, 5, QColor(0, 0, 200) }
    };
    stack.push(new edit::PaintAreaCellsCommand(m, 0, changes));

    ASSERT_EQ(L.cells.size(), 1u);
    EXPECT_EQ(L.cells[0].color.rgba(), QColor(0, 0, 200).rgba());
    stack.undo();
    EXPECT_EQ(L.cells[0].color.rgba(), QColor(200, 0, 0).rgba());
}

TEST(AreaCommands, EraseExistingCell) {
    core::Map m = makeMapWithAreaLayer();
    auto& L = static_cast<core::LayerArea&>(*m.layers()[0]);
    L.cells.push_back({ 0, 0, QColor(0, 255, 0) });

    QUndoStack stack;
    std::vector<edit::PaintAreaCellsCommand::Change> changes = {
        { 0, 0, std::nullopt }
    };
    stack.push(new edit::PaintAreaCellsCommand(m, 0, changes));

    EXPECT_TRUE(L.cells.empty());
    stack.undo();
    ASSERT_EQ(L.cells.size(), 1u);
    EXPECT_EQ(L.cells[0].color.rgba(), QColor(0, 255, 0).rgba());
}

TEST(AreaCommands, MultiCellBatch) {
    core::Map m = makeMapWithAreaLayer();
    QUndoStack stack;
    std::vector<edit::PaintAreaCellsCommand::Change> changes = {
        { 0, 0, QColor(255, 0, 0) },
        { 1, 0, QColor(0, 255, 0) },
        { 0, 1, QColor(0, 0, 255) },
    };
    stack.push(new edit::PaintAreaCellsCommand(m, 0, changes));
    EXPECT_EQ(areaOf(m).cells.size(), 3u);
    stack.undo();
    EXPECT_TRUE(areaOf(m).cells.empty());
}
