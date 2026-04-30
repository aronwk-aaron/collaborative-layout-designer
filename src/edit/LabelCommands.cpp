#include "LabelCommands.h"

#include "../core/Map.h"
#include "../core/Sidecar.h"

#include <QObject>

namespace bld::edit {

namespace {

int findLabelIndex(const core::Map& map, const QString& id) {
    for (int i = 0; i < static_cast<int>(map.sidecar.anchoredLabels.size()); ++i) {
        if (map.sidecar.anchoredLabels[i].id == id) return i;
    }
    return -1;
}

}

AddAnchoredLabelCommand::AddAnchoredLabelCommand(core::Map& map, core::AnchoredLabel label,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), label_(std::move(label)) {
    setText(QObject::tr("Add label '%1'").arg(label_.text));
}

void AddAnchoredLabelCommand::redo() { map_.sidecar.anchoredLabels.push_back(label_); }

void AddAnchoredLabelCommand::undo() {
    const int i = findLabelIndex(map_, label_.id);
    if (i >= 0) map_.sidecar.anchoredLabels.erase(map_.sidecar.anchoredLabels.begin() + i);
}

DeleteAnchoredLabelCommand::DeleteAnchoredLabelCommand(core::Map& map, QString labelId,
                                                        QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), labelId_(std::move(labelId)) {
    setText(QObject::tr("Delete label"));
}

void DeleteAnchoredLabelCommand::redo() {
    const int i = findLabelIndex(map_, labelId_);
    if (i < 0) { removed_.reset(); insertIndex_ = -1; return; }
    removed_ = map_.sidecar.anchoredLabels[i];
    insertIndex_ = i;
    map_.sidecar.anchoredLabels.erase(map_.sidecar.anchoredLabels.begin() + i);
}

void DeleteAnchoredLabelCommand::undo() {
    if (!removed_) return;
    const int idx = std::min<int>(insertIndex_, static_cast<int>(map_.sidecar.anchoredLabels.size()));
    map_.sidecar.anchoredLabels.insert(map_.sidecar.anchoredLabels.begin() + idx, *removed_);
}

EditAnchoredLabelTextCommand::EditAnchoredLabelTextCommand(core::Map& map, QString labelId,
                                                            QString newText,
                                                            QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), labelId_(std::move(labelId)),
      newText_(std::move(newText)) {
    const int i = findLabelIndex(map, labelId_);
    if (i >= 0) oldText_ = map.sidecar.anchoredLabels[i].text;
    setText(QObject::tr("Edit label text"));
}

void EditAnchoredLabelTextCommand::redo() {
    const int i = findLabelIndex(map_, labelId_);
    if (i >= 0) map_.sidecar.anchoredLabels[i].text = newText_;
}

void EditAnchoredLabelTextCommand::undo() {
    const int i = findLabelIndex(map_, labelId_);
    if (i >= 0) map_.sidecar.anchoredLabels[i].text = oldText_;
}

// ----- MoveAnchoredLabelCommand -----

MoveAnchoredLabelCommand::MoveAnchoredLabelCommand(core::Map& map, QString labelId,
                                                   QPointF deltaStuds, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), labelId_(std::move(labelId)), delta_(deltaStuds) {
    setText(QObject::tr("Move label"));
}

void MoveAnchoredLabelCommand::redo() {
    const int i = findLabelIndex(map_, labelId_);
    if (i >= 0) map_.sidecar.anchoredLabels[i].offset += delta_;
}

void MoveAnchoredLabelCommand::undo() {
    const int i = findLabelIndex(map_, labelId_);
    if (i >= 0) map_.sidecar.anchoredLabels[i].offset -= delta_;
}

}
