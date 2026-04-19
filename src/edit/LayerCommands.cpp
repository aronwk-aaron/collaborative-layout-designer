#include "LayerCommands.h"

#include "../core/ColorSpec.h"
#include "../core/LayerArea.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"
#include "../core/Map.h"

#include <QObject>
#include <QUuid>

namespace cld::edit {

namespace {

std::unique_ptr<core::Layer> makeLayer(core::LayerKind kind, const QString& name) {
    std::unique_ptr<core::Layer> layer;
    switch (kind) {
        case core::LayerKind::Grid:  layer = std::make_unique<core::LayerGrid>();  break;
        case core::LayerKind::Brick: layer = std::make_unique<core::LayerBrick>(); break;
        case core::LayerKind::Text:  layer = std::make_unique<core::LayerText>();  break;
        case core::LayerKind::Area:  layer = std::make_unique<core::LayerArea>();  break;
        case core::LayerKind::Ruler: layer = std::make_unique<core::LayerRuler>(); break;
        default: return nullptr;
    }
    layer->guid = core::newBbmId();
    layer->name = name;
    return layer;
}

}

// ----- AddLayerCommand -----

AddLayerCommand::AddLayerCommand(core::Map& map, core::LayerKind kind, int insertAt,
                                 QString name, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), kind_(kind), insertAt_(insertAt),
      name_(name.isEmpty() ? QObject::tr("New Layer") : std::move(name)) {
    setText(QObject::tr("Add layer '%1'").arg(name_));
}

AddLayerCommand::~AddLayerCommand() = default;

void AddLayerCommand::redo() {
    auto& layers = map_.layers();
    auto layer = detached_ ? std::move(detached_) : makeLayer(kind_, name_);
    if (!layer) return;
    int idx = (insertAt_ < 0 || insertAt_ > static_cast<int>(layers.size()))
                  ? static_cast<int>(layers.size()) : insertAt_;
    layers.insert(layers.begin() + idx, std::move(layer));
    insertedIndex_ = idx;
}

void AddLayerCommand::undo() {
    auto& layers = map_.layers();
    if (insertedIndex_ < 0 || insertedIndex_ >= static_cast<int>(layers.size())) return;
    detached_ = std::move(layers[insertedIndex_]);
    layers.erase(layers.begin() + insertedIndex_);
}

// ----- DeleteLayerCommand -----

DeleteLayerCommand::DeleteLayerCommand(core::Map& map, int index, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), index_(index) {
    setText(QObject::tr("Delete layer"));
}

DeleteLayerCommand::~DeleteLayerCommand() = default;

void DeleteLayerCommand::redo() {
    auto& layers = map_.layers();
    if (index_ < 0 || index_ >= static_cast<int>(layers.size())) return;
    removed_ = std::move(layers[index_]);
    layers.erase(layers.begin() + index_);
}

void DeleteLayerCommand::undo() {
    if (!removed_) return;
    auto& layers = map_.layers();
    const int idx = std::min<int>(index_, static_cast<int>(layers.size()));
    layers.insert(layers.begin() + idx, std::move(removed_));
}

// ----- MoveLayerCommand -----

MoveLayerCommand::MoveLayerCommand(core::Map& map, int index, int delta, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), index_(index), delta_(delta) {
    setText(delta > 0 ? QObject::tr("Move layer up") : QObject::tr("Move layer down"));
}

void MoveLayerCommand::redo() {
    auto& layers = map_.layers();
    const int n = static_cast<int>(layers.size());
    if (index_ < 0 || index_ >= n) return;
    const int target = index_ + delta_;
    if (target < 0 || target >= n) return;
    std::swap(layers[index_], layers[target]);
}

void MoveLayerCommand::undo() {
    // Swap is its own inverse.
    auto& layers = map_.layers();
    const int n = static_cast<int>(layers.size());
    if (index_ < 0 || index_ >= n) return;
    const int target = index_ + delta_;
    if (target < 0 || target >= n) return;
    std::swap(layers[index_], layers[target]);
}

// ----- RenameLayerCommand -----

RenameLayerCommand::RenameLayerCommand(core::Map& map, int index, QString newName,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), index_(index), newName_(std::move(newName)) {
    auto& layers = map_.layers();
    if (index_ >= 0 && index_ < static_cast<int>(layers.size())) {
        oldName_ = layers[index_]->name;
    }
    setText(QObject::tr("Rename layer"));
}

void RenameLayerCommand::redo() {
    auto& layers = map_.layers();
    if (index_ >= 0 && index_ < static_cast<int>(layers.size())) {
        layers[index_]->name = newName_;
    }
}

void RenameLayerCommand::undo() {
    auto& layers = map_.layers();
    if (index_ >= 0 && index_ < static_cast<int>(layers.size())) {
        layers[index_]->name = oldName_;
    }
}

// ----- ChangeBackgroundColorCommand -----

ChangeBackgroundColorCommand::ChangeBackgroundColorCommand(core::Map& map,
                                                            const core::ColorSpec& newColor,
                                                            QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), oldColor_(map.backgroundColor), newColor_(newColor) {
    setText(QObject::tr("Change background colour"));
}
void ChangeBackgroundColorCommand::redo() { map_.backgroundColor = newColor_; }
void ChangeBackgroundColorCommand::undo() { map_.backgroundColor = oldColor_; }

// ----- ChangeGeneralInfoCommand -----

ChangeGeneralInfoCommand::ChangeGeneralInfoCommand(core::Map& map, Info next, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      before_{ map.author, map.lug, map.event, map.date, map.comment },
      after_(std::move(next)) {
    setText(QObject::tr("Edit map information"));
}
void ChangeGeneralInfoCommand::redo() {
    map_.author = after_.author;
    map_.lug = after_.lug;
    map_.event = after_.event;
    map_.date = after_.date;
    map_.comment = after_.comment;
}
void ChangeGeneralInfoCommand::undo() {
    map_.author = before_.author;
    map_.lug = before_.lug;
    map_.event = before_.event;
    map_.date = before_.date;
    map_.comment = before_.comment;
}

}
