#pragma once

#include "../core/AnchoredLabel.h"

#include <QUndoCommand>
#include <QString>

namespace cld::core { class Map; }

namespace cld::edit {

// Append an AnchoredLabel to sidecar.anchoredLabels. Redo pushes; undo removes
// by matching id. Label's id is expected to be unique within the sidecar.
class AddAnchoredLabelCommand : public QUndoCommand {
public:
    AddAnchoredLabelCommand(core::Map& map, core::AnchoredLabel label,
                            QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    const QString& labelId() const { return label_.id; }
private:
    core::Map& map_;
    core::AnchoredLabel label_;
};

class DeleteAnchoredLabelCommand : public QUndoCommand {
public:
    DeleteAnchoredLabelCommand(core::Map& map, QString labelId,
                               QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    QString labelId_;
    std::optional<core::AnchoredLabel> removed_;
    int insertIndex_ = -1;
};

class EditAnchoredLabelTextCommand : public QUndoCommand {
public:
    EditAnchoredLabelTextCommand(core::Map& map, QString labelId, QString newText,
                                 QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    QString labelId_;
    QString newText_;
    QString oldText_;
};

// Translate an anchored label by a (dx, dy) stud delta applied to its
// offset. For brick-anchored labels this moves the label relative to the
// anchor brick; for world-anchored labels it moves in world coords.
class MoveAnchoredLabelCommand : public QUndoCommand {
public:
    MoveAnchoredLabelCommand(core::Map& map, QString labelId, QPointF deltaStuds,
                             QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    QString labelId_;
    QPointF delta_;
};

}
