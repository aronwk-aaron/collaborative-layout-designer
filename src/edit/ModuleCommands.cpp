#include "ModuleCommands.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/Sidecar.h"

#include <QDateTime>
#include <QObject>
#include <QUuid>

#include <cmath>

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
      moduleId_(core::newBbmId()),
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

// ----- RotateModuleCommand -----

RotateModuleCommand::RotateModuleCommand(core::Map& map, QString moduleId, double degrees,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)), degrees_(degrees) {
    setText(QObject::tr("Rotate module %1°").arg(degrees_, 0, 'f', 1));
}

void RotateModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) return;
    const auto& mod = map_.sidecar.modules[i];

    // Snapshot every member brick's pre-state the first time we redo, so
    // undo restores exact positions even for fractional angles.
    if (!captured_) {
        before_.clear();
        for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
            auto* L = brickLayer(map_, li);
            if (!L) continue;
            for (const auto& b : L->bricks) {
                if (mod.memberIds.contains(b.guid)) {
                    before_.push_back({ li, b.guid, b.displayArea.topLeft(), b.orientation });
                }
            }
        }
        captured_ = true;
    }
    if (before_.empty()) return;

    // Module centre: centroid of member displayArea centres (stud coords).
    QPointF centroid(0, 0);
    int count = 0;
    for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
        auto* L = brickLayer(map_, li);
        if (!L) continue;
        for (const auto& b : L->bricks) {
            if (mod.memberIds.contains(b.guid)) {
                centroid += b.displayArea.center();
                ++count;
            }
        }
    }
    if (count == 0) return;
    centroid /= count;

    const double rad = degrees_ * M_PI / 180.0;
    const double c = std::cos(rad), s = std::sin(rad);

    for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
        auto* L = brickLayer(map_, li);
        if (!L) continue;
        for (auto& b : L->bricks) {
            if (!mod.memberIds.contains(b.guid)) continue;
            // Rotate the brick's centre around the module centroid.
            const QPointF centre = b.displayArea.center();
            const QPointF rel = centre - centroid;
            const QPointF rotated(rel.x() * c - rel.y() * s, rel.x() * s + rel.y() * c);
            const QPointF newCentre = centroid + rotated;
            const QPointF newTopLeft = newCentre - QPointF(b.displayArea.width() / 2.0,
                                                            b.displayArea.height() / 2.0);
            b.displayArea.moveTo(newTopLeft);
            b.orientation = std::fmod(b.orientation + static_cast<float>(degrees_), 360.0f);
        }
    }
}

void RotateModuleCommand::undo() {
    for (const auto& s : before_) {
        auto* L = brickLayer(map_, s.layerIndex);
        if (!L) continue;
        for (auto& b : L->bricks) {
            if (b.guid == s.guid) {
                b.displayArea.moveTo(s.topLeft);
                b.orientation = s.orientation;
                break;
            }
        }
    }
}

// ----- FlattenModuleCommand -----

FlattenModuleCommand::FlattenModuleCommand(core::Map& map, QString moduleId, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)) {
    setText(QObject::tr("Flatten module"));
}

void FlattenModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) { removed_.reset(); insertIndex_ = -1; return; }
    removed_ = map_.sidecar.modules[i];
    insertIndex_ = i;
    map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

void FlattenModuleCommand::undo() {
    if (!removed_) return;
    const int idx = std::min<int>(insertIndex_, static_cast<int>(map_.sidecar.modules.size()));
    map_.sidecar.modules.insert(map_.sidecar.modules.begin() + idx, *removed_);
}

// ----- RescanModuleCommand -----

RescanModuleCommand::RescanModuleCommand(core::Map& map, int targetLayerIndex, QString moduleId,
                                         std::vector<core::Brick> freshBricks,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(targetLayerIndex),
      moduleId_(std::move(moduleId)), freshBricks_(std::move(freshBricks)) {
    setText(QObject::tr("Re-scan module (%1 bricks)").arg(freshBricks_.size()));
}

void RescanModuleCommand::redo() {
    const int mi = findModuleIndex(map_, moduleId_);
    if (mi < 0) return;
    auto* L = brickLayer(map_, layerIndex_);
    if (!L) return;

    // Snapshot the pre-state once: remove old members from the layer and
    // stash them so undo reinstates.
    if (!captured_) {
        oldMemberIds_ = map_.sidecar.modules[mi].memberIds;
        oldBricks_.clear();
        auto it = L->bricks.begin();
        while (it != L->bricks.end()) {
            if (oldMemberIds_.contains(it->guid)) {
                oldBricks_.push_back(*it);
                it = L->bricks.erase(it);
            } else {
                ++it;
            }
        }
        captured_ = true;
    } else {
        // Re-apply on redo after undo: remove the same guids again.
        auto it = L->bricks.begin();
        while (it != L->bricks.end()) {
            if (oldMemberIds_.contains(it->guid)) it = L->bricks.erase(it);
            else                                   ++it;
        }
    }

    // Insert fresh bricks and update memberIds. Fresh guids have already been
    // minted by the caller (MainWindow reads the .bbm and clears guids).
    QSet<QString> newIds;
    for (auto& b : freshBricks_) {
        if (b.guid.isEmpty()) b.guid = core::newBbmId();
        newIds.insert(b.guid);
        L->bricks.push_back(b);
    }
    map_.sidecar.modules[mi].memberIds = newIds;
    map_.sidecar.modules[mi].importedAt = QDateTime::currentDateTimeUtc();
}

void RescanModuleCommand::undo() {
    const int mi = findModuleIndex(map_, moduleId_);
    if (mi < 0) return;
    auto* L = brickLayer(map_, layerIndex_);
    if (!L) return;
    // Remove fresh bricks.
    QSet<QString> freshIds;
    for (const auto& b : freshBricks_) freshIds.insert(b.guid);
    auto it = L->bricks.begin();
    while (it != L->bricks.end()) {
        if (freshIds.contains(it->guid)) it = L->bricks.erase(it);
        else                              ++it;
    }
    // Restore old bricks + memberIds.
    for (const auto& b : oldBricks_) L->bricks.push_back(b);
    map_.sidecar.modules[mi].memberIds = oldMemberIds_;
}

// ----- ImportBbmAsModuleCommand -----

ImportBbmAsModuleCommand::ImportBbmAsModuleCommand(core::Map& map, int targetLayerIndex,
                                                   QString sourcePath, QString moduleName,
                                                   std::vector<core::Brick> bricks,
                                                   QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(targetLayerIndex),
      sourcePath_(std::move(sourcePath)),
      moduleId_(core::newBbmId()),
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
        if (b.guid.isEmpty()) b.guid = core::newBbmId();
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
