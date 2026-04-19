#include "edit/ModuleCommands.h"
#include "core/Brick.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/Sidecar.h"

#include <gtest/gtest.h>

#include <QUndoStack>
#include <QUuid>

using namespace cld;

namespace {

core::Map makeMapWithBrickLayer() {
    core::Map m;
    auto L = std::make_unique<core::LayerBrick>();
    L->guid = QStringLiteral("L1");
    m.layers().push_back(std::move(L));
    return m;
}

core::Brick makeBrick(const QString& guid, QRectF area = QRectF(0, 0, 16, 16)) {
    core::Brick b;
    b.guid = guid;
    b.partNumber = QStringLiteral("3001.1");
    b.displayArea = area;
    return b;
}

}

TEST(ModuleCommands, CreateAddsToSidecarAndUndo) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    L->bricks.push_back(makeBrick(QStringLiteral("a")));
    L->bricks.push_back(makeBrick(QStringLiteral("b")));

    QUndoStack stack;
    stack.push(new edit::CreateModuleCommand(
        m, QStringLiteral("Station A"),
        { { 0, QStringLiteral("a") }, { 0, QStringLiteral("b") } }));

    ASSERT_EQ(m.sidecar.modules.size(), 1u);
    EXPECT_EQ(m.sidecar.modules[0].name, QStringLiteral("Station A"));
    EXPECT_EQ(m.sidecar.modules[0].memberIds.size(), 2);

    stack.undo();
    EXPECT_EQ(m.sidecar.modules.size(), 0u);
}

TEST(ModuleCommands, DeleteRestoresOnUndo) {
    core::Map m;
    core::Module mod;
    mod.id = QStringLiteral("mod-1");
    mod.name = QStringLiteral("X");
    mod.memberIds.insert(QStringLiteral("a"));
    m.sidecar.modules.push_back(mod);

    QUndoStack stack;
    stack.push(new edit::DeleteModuleCommand(m, QStringLiteral("mod-1")));
    EXPECT_TRUE(m.sidecar.modules.empty());
    stack.undo();
    ASSERT_EQ(m.sidecar.modules.size(), 1u);
    EXPECT_EQ(m.sidecar.modules[0].id, QStringLiteral("mod-1"));
}

TEST(ModuleCommands, MoveAppliesDeltaToAllMemberBricks) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    L->bricks.push_back(makeBrick(QStringLiteral("a"), QRectF(10, 10, 16, 16)));
    L->bricks.push_back(makeBrick(QStringLiteral("b"), QRectF(50, 50, 16, 16)));
    L->bricks.push_back(makeBrick(QStringLiteral("c"), QRectF(100, 100, 16, 16)));

    core::Module mod;
    mod.id = QStringLiteral("mod-1");
    mod.memberIds.insert(QStringLiteral("a"));
    mod.memberIds.insert(QStringLiteral("b"));
    m.sidecar.modules.push_back(mod);

    QUndoStack stack;
    stack.push(new edit::MoveModuleCommand(m, QStringLiteral("mod-1"), QPointF(5, 7)));

    EXPECT_EQ(L->bricks[0].displayArea.topLeft(), QPointF(15, 17));
    EXPECT_EQ(L->bricks[1].displayArea.topLeft(), QPointF(55, 57));
    EXPECT_EQ(L->bricks[2].displayArea.topLeft(), QPointF(100, 100)); // not in module

    stack.undo();
    EXPECT_EQ(L->bricks[0].displayArea.topLeft(), QPointF(10, 10));
    EXPECT_EQ(L->bricks[1].displayArea.topLeft(), QPointF(50, 50));
}

TEST(ModuleCommands, ImportBbmAsModuleInsertsAndUndo) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    L->bricks.push_back(makeBrick(QStringLiteral("existing")));

    std::vector<core::Brick> imports;
    imports.push_back(makeBrick(QStringLiteral(""), QRectF(0, 0, 16, 16)));
    imports.push_back(makeBrick(QStringLiteral(""), QRectF(16, 0, 16, 16)));

    QUndoStack stack;
    stack.push(new edit::ImportBbmAsModuleCommand(
        m, 0, QStringLiteral("/tmp/src.bbm"), QStringLiteral("Imported"),
        imports));

    ASSERT_EQ(L->bricks.size(), 3u);
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("existing"));
    ASSERT_EQ(m.sidecar.modules.size(), 1u);
    EXPECT_EQ(m.sidecar.modules[0].sourceFile, QStringLiteral("/tmp/src.bbm"));
    EXPECT_EQ(m.sidecar.modules[0].memberIds.size(), 2);
    // Imported bricks should have been assigned fresh GUIDs.
    for (const auto& id : m.sidecar.modules[0].memberIds) {
        EXPECT_FALSE(id.isEmpty());
        EXPECT_NE(id, QStringLiteral("existing"));
    }

    stack.undo();
    EXPECT_EQ(L->bricks.size(), 1u);
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("existing"));
    EXPECT_TRUE(m.sidecar.modules.empty());
}
