#include "EditCommands.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"

namespace cld::edit {

namespace {

core::LayerBrick* brickLayer(core::Map& map, int idx) {
    if (idx < 0 || idx >= static_cast<int>(map.layers().size())) return nullptr;
    auto* L = map.layers()[idx].get();
    return (L && L->kind() == core::LayerKind::Brick)
        ? static_cast<core::LayerBrick*>(L)
        : nullptr;
}

core::Brick* findBrick(core::Map& map, const BrickRef& ref) {
    auto* L = brickLayer(map, ref.layerIndex);
    if (!L) return nullptr;
    for (auto& b : L->bricks) if (b.guid == ref.guid) return &b;
    return nullptr;
}

}

// ----- MoveBricksCommand -----

MoveBricksCommand::MoveBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), entries_(std::move(entries)) {
    setText(QObject::tr("Move %1 brick(s)").arg(entries_.size()));
}

void MoveBricksCommand::redo() {
    for (const auto& e : entries_) {
        if (auto* b = findBrick(map_, e.ref)) {
            b->displayArea.moveTo(e.afterTopLeft);
        }
    }
}

void MoveBricksCommand::undo() {
    for (const auto& e : entries_) {
        if (auto* b = findBrick(map_, e.ref)) {
            b->displayArea.moveTo(e.beforeTopLeft);
        }
    }
}

// ----- RotateBricksCommand -----

RotateBricksCommand::RotateBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), entries_(std::move(entries)) {
    setText(QObject::tr("Rotate %1 brick(s)").arg(entries_.size()));
}

void RotateBricksCommand::redo() {
    for (const auto& e : entries_) {
        if (auto* b = findBrick(map_, e.ref)) b->orientation = e.afterOrientation;
    }
}

void RotateBricksCommand::undo() {
    for (const auto& e : entries_) {
        if (auto* b = findBrick(map_, e.ref)) b->orientation = e.beforeOrientation;
    }
}

// ----- DeleteBricksCommand -----

DeleteBricksCommand::DeleteBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), entries_(std::move(entries)) {
    setText(QObject::tr("Delete %1 brick(s)").arg(entries_.size()));
}

void DeleteBricksCommand::redo() {
    for (const auto& e : entries_) {
        if (auto* L = brickLayer(map_, e.layerIndex)) {
            for (auto it = L->bricks.begin(); it != L->bricks.end(); ++it) {
                if (it->guid == e.brick.guid) { L->bricks.erase(it); break; }
            }
        }
    }
}

void DeleteBricksCommand::undo() {
    // Restore in original order (forward iteration; insertAt indices were
    // captured before deletion and remain valid if we insert in order).
    for (const auto& e : entries_) {
        if (auto* L = brickLayer(map_, e.layerIndex)) {
            const int idx = std::min<int>(e.indexInLayer, static_cast<int>(L->bricks.size()));
            L->bricks.insert(L->bricks.begin() + idx, e.brick);
        }
    }
}

// ----- AddBrickCommand -----

AddBrickCommand::AddBrickCommand(core::Map& map, int layerIndex, core::Brick brick, int insertAt,
                                 QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex),
      brick_(std::move(brick)), insertAt_(insertAt) {
    setText(QObject::tr("Add brick %1").arg(brick_.partNumber));
}

void AddBrickCommand::redo() {
    if (auto* L = brickLayer(map_, layerIndex_)) {
        if (insertAt_ < 0 || insertAt_ > static_cast<int>(L->bricks.size())) {
            L->bricks.push_back(brick_);
        } else {
            L->bricks.insert(L->bricks.begin() + insertAt_, brick_);
        }
    }
}

void AddBrickCommand::undo() {
    if (auto* L = brickLayer(map_, layerIndex_)) {
        for (auto it = L->bricks.begin(); it != L->bricks.end(); ++it) {
            if (it->guid == brick_.guid) { L->bricks.erase(it); break; }
        }
    }
}

}
