#include "EditCommands.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"

#include <QSet>

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

// ----- ReorderBricksCommand -----

ReorderBricksCommand::ReorderBricksCommand(core::Map& map, std::vector<Target> targets,
                                           Direction dir, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), targets_(std::move(targets)), dir_(dir) {
    setText(dir_ == ToFront
        ? QObject::tr("Bring %1 brick(s) to front").arg(targets_.size())
        : QObject::tr("Send %1 brick(s) to back").arg(targets_.size()));
}

void ReorderBricksCommand::redo() {
    // Snapshot the current per-layer order on first redo so undo can restore.
    if (!haveSnapshot_) {
        QHash<int, std::vector<QString>> snapshot;
        for (const auto& t : targets_) {
            if (snapshot.contains(t.layerIndex)) continue;
            if (auto* L = brickLayer(map_, t.layerIndex)) {
                std::vector<QString>& ord = snapshot[t.layerIndex];
                ord.reserve(L->bricks.size());
                for (const auto& b : L->bricks) ord.push_back(b.guid);
            }
        }
        originalOrder_ = std::move(snapshot);
        haveSnapshot_ = true;
    }

    // Group targets by layer so each layer reorders once.
    QHash<int, std::vector<QString>> byLayer;
    for (const auto& t : targets_) byLayer[t.layerIndex].push_back(t.guid);

    for (auto it = byLayer.begin(); it != byLayer.end(); ++it) {
        auto* L = brickLayer(map_, it.key());
        if (!L) continue;
        const std::vector<QString>& movingGuids = it.value();
        QSet<QString> movingSet(movingGuids.begin(), movingGuids.end());

        // Partition: keep relative order for both moving and non-moving sets.
        std::vector<core::Brick> keep;     // non-moving, in original order
        std::vector<core::Brick> moving;   // moving, in original order
        keep.reserve(L->bricks.size());
        moving.reserve(movingGuids.size());
        for (auto& b : L->bricks) {
            if (movingSet.contains(b.guid)) moving.push_back(std::move(b));
            else                            keep.push_back(std::move(b));
        }

        L->bricks.clear();
        L->bricks.reserve(keep.size() + moving.size());
        if (dir_ == ToFront) {
            for (auto& b : keep)   L->bricks.push_back(std::move(b));
            for (auto& b : moving) L->bricks.push_back(std::move(b));
        } else {
            for (auto& b : moving) L->bricks.push_back(std::move(b));
            for (auto& b : keep)   L->bricks.push_back(std::move(b));
        }
    }
}

void ReorderBricksCommand::undo() {
    for (auto it = originalOrder_.constBegin(); it != originalOrder_.constEnd(); ++it) {
        auto* L = brickLayer(map_, it.key());
        if (!L) continue;
        QHash<QString, core::Brick> byGuid;
        for (auto& b : L->bricks) byGuid.insert(b.guid, std::move(b));
        L->bricks.clear();
        L->bricks.reserve(it.value().size());
        for (const QString& guid : it.value()) {
            auto found = byGuid.find(guid);
            if (found != byGuid.end()) L->bricks.push_back(std::move(*found));
        }
    }
}

// ----- AddBricksCommand -----

AddBricksCommand::AddBricksCommand(core::Map& map, int layerIndex, std::vector<core::Brick> bricks,
                                   QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex), bricks_(std::move(bricks)) {
    setText(QObject::tr("Add %1 brick(s)").arg(bricks_.size()));
}

void AddBricksCommand::redo() {
    if (auto* L = brickLayer(map_, layerIndex_)) {
        for (const auto& b : bricks_) L->bricks.push_back(b);
    }
}

void AddBricksCommand::undo() {
    auto* L = brickLayer(map_, layerIndex_);
    if (!L) return;
    // Guids are unique within our bricks_ list; remove each from the layer.
    QSet<QString> toRemove;
    for (const auto& b : bricks_) toRemove.insert(b.guid);
    L->bricks.erase(
        std::remove_if(L->bricks.begin(), L->bricks.end(),
                       [&](const core::Brick& b) { return toRemove.contains(b.guid); }),
        L->bricks.end());
}

}
