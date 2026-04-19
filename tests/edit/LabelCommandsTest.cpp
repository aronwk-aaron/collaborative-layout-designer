#include "edit/LabelCommands.h"
#include "core/Map.h"
#include "core/Sidecar.h"

#include <gtest/gtest.h>

#include <QUndoStack>

using namespace cld;

TEST(LabelCommands, AddUndoRedo) {
    core::Map m;
    core::AnchoredLabel L;
    L.id = QStringLiteral("lbl-1");
    L.text = QStringLiteral("Hello");
    L.kind = core::AnchorKind::World;

    QUndoStack stack;
    stack.push(new edit::AddAnchoredLabelCommand(m, L));
    ASSERT_EQ(m.sidecar.anchoredLabels.size(), 1u);

    stack.undo();
    EXPECT_TRUE(m.sidecar.anchoredLabels.empty());
    stack.redo();
    ASSERT_EQ(m.sidecar.anchoredLabels.size(), 1u);
    EXPECT_EQ(m.sidecar.anchoredLabels[0].text, QStringLiteral("Hello"));
}

TEST(LabelCommands, EditTextRestoresOnUndo) {
    core::Map m;
    core::AnchoredLabel L;
    L.id = QStringLiteral("lbl-1");
    L.text = QStringLiteral("Original");
    m.sidecar.anchoredLabels.push_back(L);

    QUndoStack stack;
    stack.push(new edit::EditAnchoredLabelTextCommand(m, L.id, QStringLiteral("Modified")));
    EXPECT_EQ(m.sidecar.anchoredLabels[0].text, QStringLiteral("Modified"));
    stack.undo();
    EXPECT_EQ(m.sidecar.anchoredLabels[0].text, QStringLiteral("Original"));
}

TEST(LabelCommands, DeleteRestoresPosition) {
    core::Map m;
    for (int i = 0; i < 3; ++i) {
        core::AnchoredLabel L;
        L.id = QStringLiteral("lbl-%1").arg(i);
        L.text = QString::number(i);
        m.sidecar.anchoredLabels.push_back(L);
    }
    QUndoStack stack;
    stack.push(new edit::DeleteAnchoredLabelCommand(m, QStringLiteral("lbl-1")));
    ASSERT_EQ(m.sidecar.anchoredLabels.size(), 2u);
    EXPECT_EQ(m.sidecar.anchoredLabels[1].id, QStringLiteral("lbl-2"));
    stack.undo();
    ASSERT_EQ(m.sidecar.anchoredLabels.size(), 3u);
    EXPECT_EQ(m.sidecar.anchoredLabels[1].id, QStringLiteral("lbl-1"));
}
