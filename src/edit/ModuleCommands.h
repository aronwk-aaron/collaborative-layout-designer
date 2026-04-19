#pragma once

#include "../core/Brick.h"
#include "../core/Module.h"

#include <QPointF>
#include <QString>
#include <QUndoCommand>

#include <vector>

namespace cld::core { class Map; }

namespace cld::edit {

// Create a sidecar module grouping the given (layer, guid) pairs. No brick data
// is mutated — this is pure sidecar bookkeeping. Redo appends to sidecar.modules;
// undo removes it.
class CreateModuleCommand : public QUndoCommand {
public:
    struct Member { int layerIndex = -1; QString guid; };
    CreateModuleCommand(core::Map& map, QString name, std::vector<Member> members,
                        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    const QString& moduleId() const { return moduleId_; }

private:
    core::Map&  map_;
    QString     moduleId_;
    QString     name_;
    std::vector<Member> members_;
};

// Remove a module from sidecar.modules. Keeps the member bricks untouched.
class DeleteModuleCommand : public QUndoCommand {
public:
    DeleteModuleCommand(core::Map& map, QString moduleId, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    QString    moduleId_;
    std::optional<core::Module> removed_;
    int insertIndex_ = -1;
};

// Translate every brick belonging to a module by a stud delta. Cross-layer,
// since module membership spans layers.
class MoveModuleCommand : public QUndoCommand {
public:
    MoveModuleCommand(core::Map& map, QString moduleId, QPointF deltaStuds,
                      QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    QString    moduleId_;
    QPointF    delta_;
};

// Load a .bbm, merge its brick layers into the current map (remapping guids
// to fresh ones to avoid collisions), and register the new bricks as a module.
// `targetLayerIndex` is the brick-layer in the host map that will receive the
// imported bricks. Undo removes both the bricks and the module entry.
class ImportBbmAsModuleCommand : public QUndoCommand {
public:
    struct Added { int layerIndex = -1; QString guid; };
    ImportBbmAsModuleCommand(core::Map& map, int targetLayerIndex,
                             QString sourcePath, QString moduleName,
                             std::vector<core::Brick> bricks,
                             QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    const QString& moduleId() const { return moduleId_; }
    int layerIndex() const { return layerIndex_; }

private:
    core::Map& map_;
    int layerIndex_;
    QString sourcePath_;
    QString moduleId_;
    QString name_;
    std::vector<core::Brick> bricks_;
};

}
