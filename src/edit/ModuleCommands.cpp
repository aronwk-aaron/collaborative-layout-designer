#include "ModuleCommands.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/Sidecar.h"

#include <QDateTime>
#include <QObject>
#include <QUuid>

namespace cld::edit {

namespace {

core::LayerBrick* brickLayer(core::Map& map, int idx) {
    if (idx < 0 || idx >= static_cast<int>(map.layers().size())) return nullptr;
    auto* L = map.layers()[idx].get();
    return (L && L->kind() == core::LayerKind::Brick)
        ? static_cast<core::LayerBrick*>(L)
        : nullptr;
}

int findModuleIndex(const core::Map& map, const QString& id) {
    for (int i = 0; i < static_cast<int>(map.sidecar.modules.size()); ++i) {
        if (map.sidecar.modules[i].id == id) return i;
    }
    return -1;
}

}

// ----- CreateModuleCommand -----

CreateModuleCommand::CreateModuleCommand(core::Map& map, QString name,
                                         std::vector<Member> members,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      moduleId_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      name_(std::move(name)), members_(std::move(members)) {
    setText(QObject::tr("Create module %1 (%2 members)").arg(name_).arg(members_.size()));
}

void CreateModuleCommand::redo() {
    core::Module m;
    m.id = moduleId_;
    m.name = name_;
    for (const auto& mem : members_) m.memberIds.insert(mem.guid);
    map_.sidecar.modules.push_back(std::move(m));
}

void CreateModuleCommand::undo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

// ----- DeleteModuleCommand -----

DeleteModuleCommand::DeleteModuleCommand(core::Map& map, QString moduleId, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)) {
    setText(QObject::tr("Delete module"));
}

void DeleteModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) { removed_.reset(); insertIndex_ = -1; return; }
    removed_ = map_.sidecar.modules[i];
    insertIndex_ = i;
    map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

void DeleteModuleCommand::undo() {
    if (!removed_) return;
    const int idx = std::min<int>(insertIndex_, static_cast<int>(map_.sidecar.modules.size()));
    map_.sidecar.modules.insert(map_.sidecar.modules.begin() + idx, *removed_);
}

// ----- MoveModuleCommand -----

MoveModuleCommand::MoveModuleCommand(core::Map& map, QString moduleId, QPointF deltaStuds,
                                     QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)), delta_(deltaStuds) {
    setText(QObject::tr("Move module"));
}

void MoveModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) return;
    const auto& mod = map_.sidecar.modules[i];
    for (auto& layerPtr : map_.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (auto& b : static_cast<core::LayerBrick&>(*layerPtr).bricks) {
            if (mod.memberIds.contains(b.guid)) {
                b.displayArea.translate(delta_);
            }
        }
    }
}

void MoveModuleCommand::undo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) return;
    const auto& mod = map_.sidecar.modules[i];
    for (auto& layerPtr : map_.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (auto& b : static_cast<core::LayerBrick&>(*layerPtr).bricks) {
            if (mod.memberIds.contains(b.guid)) {
                b.displayArea.translate(-delta_);
            }
        }
    }
}

// ----- ImportBbmAsModuleCommand -----

ImportBbmAsModuleCommand::ImportBbmAsModuleCommand(core::Map& map, int targetLayerIndex,
                                                   QString sourcePath, QString moduleName,
                                                   std::vector<core::Brick> bricks,
                                                   QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(targetLayerIndex),
      sourcePath_(std::move(sourcePath)),
      moduleId_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      name_(std::move(moduleName)), bricks_(std::move(bricks)) {
    setText(QObject::tr("Import module from %1").arg(sourcePath_));
}

void ImportBbmAsModuleCommand::redo() {
    auto* L = brickLayer(map_, layerIndex_);
    if (!L) return;

    // Remap each brick to a fresh guid before inserting so import is
    // idempotent across multiple imports of the same source file.
    core::Module mod;
    mod.id = moduleId_;
    mod.name = name_;
    mod.sourceFile = sourcePath_;
    mod.importedAt = QDateTime::currentDateTimeUtc();
    for (auto& b : bricks_) {
        if (b.guid.isEmpty()) b.guid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        mod.memberIds.insert(b.guid);
        L->bricks.push_back(b);
    }
    map_.sidecar.modules.push_back(std::move(mod));
}

void ImportBbmAsModuleCommand::undo() {
    auto* L = brickLayer(map_, layerIndex_);
    if (!L) return;
    QSet<QString> toRemove;
    for (const auto& b : bricks_) toRemove.insert(b.guid);
    L->bricks.erase(
        std::remove_if(L->bricks.begin(), L->bricks.end(),
                       [&](const core::Brick& b) { return toRemove.contains(b.guid); }),
        L->bricks.end());
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

}
