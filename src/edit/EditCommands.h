#pragma once

#include "../core/Brick.h"

#include <QPointF>
#include <QUndoCommand>

#include <vector>

namespace cld::core { class Layer; class LayerBrick; class Map; }

namespace cld::edit {

// Identity for a brick in the edit pipeline: (layer index, brick guid).
// Layer index + guid is stable across vector reshuffles (unlike raw pointers)
// so commands undo/redo correctly even after concurrent mutations.
struct BrickRef {
    int     layerIndex = -1;
    QString guid;
};

// Move one or more bricks by per-brick deltas (in studs). `before/after`
// hold the brick's displayArea position before/after; undo restores before.
class MoveBricksCommand : public QUndoCommand {
public:
    struct Entry {
        BrickRef ref;
        QPointF  beforeTopLeft;   // in studs
        QPointF  afterTopLeft;    // in studs
    };

    MoveBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    const std::vector<Entry>& entries() const { return entries_; }

private:
    core::Map& map_;
    std::vector<Entry> entries_;
};

// Rotate selected bricks in place. Orientation in degrees. `beforeOrient` is
// stored so undo is exact even for fractional rotations.
class RotateBricksCommand : public QUndoCommand {
public:
    struct Entry {
        BrickRef ref;
        float beforeOrientation = 0.0f;
        float afterOrientation  = 0.0f;
    };

    RotateBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    std::vector<Entry> entries_;
};

// Delete one or more bricks from their layers. Redo removes; undo restores
// at their original index.
class DeleteBricksCommand : public QUndoCommand {
public:
    struct Entry {
        int layerIndex = -1;
        int indexInLayer = -1;       // insertion index for undo
        core::Brick brick;           // full deep copy so undo can restore
    };

    DeleteBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    std::vector<Entry> entries_;
};

// Add a single brick to the given layer at the given index (or append if -1).
class AddBrickCommand : public QUndoCommand {
public:
    AddBrickCommand(core::Map& map, int layerIndex, core::Brick brick, int insertAt = -1,
                    QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    int         layerIndex_;
    core::Brick brick_;
    int         insertAt_;
};

// Append a batch of bricks to the given layer as a single undoable step. Used
// by Paste / Duplicate so the user doesn't see N individual undo entries for
// a single clipboard operation.
class AddBricksCommand : public QUndoCommand {
public:
    AddBricksCommand(core::Map& map, int layerIndex, std::vector<core::Brick> bricks,
                     QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    const std::vector<core::Brick>& bricks() const { return bricks_; }

private:
    core::Map& map_;
    int layerIndex_;
    std::vector<core::Brick> bricks_;
};

}
