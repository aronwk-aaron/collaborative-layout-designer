#include "RulerCommands.h"

#include "../core/Layer.h"
#include "../core/LayerRuler.h"
#include "../core/Map.h"

#include <QObject>
#include <QUuid>

namespace cld::edit {

namespace {

core::LayerRuler* rulerLayer(core::Map& m, int idx) {
    if (idx < 0 || idx >= static_cast<int>(m.layers().size())) return nullptr;
    auto* L = m.layers()[idx].get();
    return (L && L->kind() == core::LayerKind::Ruler)
        ? static_cast<core::LayerRuler*>(L)
        : nullptr;
}

QString& guidOf(core::LayerRuler::AnyRuler& r) {
    return r.kind == core::RulerKind::Linear ? r.linear.guid : r.circular.guid;
}

}

AddRulerItemCommand::AddRulerItemCommand(core::Map& map, int layerIndex,
                                          core::LayerRuler::AnyRuler ruler,
                                          QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex), ruler_(std::move(ruler)) {
    if (guidOf(ruler_).isEmpty()) {
        guidOf(ruler_) = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    guidForUndo_ = guidOf(ruler_);
    setText(ruler_.kind == core::RulerKind::Linear
                ? QObject::tr("Add linear ruler")
                : QObject::tr("Add circular ruler"));
}

void AddRulerItemCommand::redo() {
    if (auto* L = rulerLayer(map_, layerIndex_)) L->rulers.push_back(ruler_);
}

void AddRulerItemCommand::undo() {
    auto* L = rulerLayer(map_, layerIndex_);
    if (!L) return;
    for (auto it = L->rulers.begin(); it != L->rulers.end(); ++it) {
        const QString& g = (it->kind == core::RulerKind::Linear)
                               ? it->linear.guid : it->circular.guid;
        if (g == guidForUndo_) { L->rulers.erase(it); break; }
    }
}

}
