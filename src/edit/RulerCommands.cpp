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
        guidOf(ruler_) = core::newBbmId();
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

// ----- DeleteRulerItemCommand -----

DeleteRulerItemCommand::DeleteRulerItemCommand(core::Map& map, int layerIndex, QString guid,
                                                QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex), guid_(std::move(guid)) {
    setText(QObject::tr("Delete ruler"));
}

void DeleteRulerItemCommand::redo() {
    auto* L = rulerLayer(map_, layerIndex_);
    if (!L) { removed_.reset(); return; }
    for (int i = 0; i < static_cast<int>(L->rulers.size()); ++i) {
        const QString& g = (L->rulers[i].kind == core::RulerKind::Linear)
                               ? L->rulers[i].linear.guid : L->rulers[i].circular.guid;
        if (g == guid_) {
            removed_ = L->rulers[i];
            index_ = i;
            L->rulers.erase(L->rulers.begin() + i);
            return;
        }
    }
}

void DeleteRulerItemCommand::undo() {
    if (!removed_) return;
    auto* L = rulerLayer(map_, layerIndex_);
    if (!L) return;
    const int i = std::min<int>(index_, static_cast<int>(L->rulers.size()));
    L->rulers.insert(L->rulers.begin() + i, *removed_);
}

// ----- EditRulerItemCommand -----

namespace {
core::LayerRuler::AnyRuler* findRuler(core::Map& map, int layerIndex, const QString& guid) {
    auto* L = rulerLayer(map, layerIndex);
    if (!L) return nullptr;
    for (auto& r : L->rulers) {
        const QString& g = (r.kind == core::RulerKind::Linear) ? r.linear.guid : r.circular.guid;
        if (g == guid) return &r;
    }
    return nullptr;
}

EditRulerItemCommand::BaseProps snapshot(const core::LayerRuler::AnyRuler& r) {
    const auto& b = (r.kind == core::RulerKind::Linear)
                        ? static_cast<const core::RulerItemBase&>(r.linear)
                        : static_cast<const core::RulerItemBase&>(r.circular);
    EditRulerItemCommand::BaseProps p;
    p.color = b.color;
    p.lineThickness = b.lineThickness;
    p.displayDistance = b.displayDistance;
    p.displayUnit = b.displayUnit;
    p.guidelineColor = b.guidelineColor;
    p.guidelineThickness = b.guidelineThickness;
    p.guidelineDashPattern = b.guidelineDashPattern;
    p.unit = b.unit;
    p.measureFont = b.measureFont;
    p.measureFontColor = b.measureFontColor;
    return p;
}

void apply(core::LayerRuler::AnyRuler& r, const EditRulerItemCommand::BaseProps& p) {
    auto& b = (r.kind == core::RulerKind::Linear)
                  ? static_cast<core::RulerItemBase&>(r.linear)
                  : static_cast<core::RulerItemBase&>(r.circular);
    b.color = p.color;
    b.lineThickness = p.lineThickness;
    b.displayDistance = p.displayDistance;
    b.displayUnit = p.displayUnit;
    b.guidelineColor = p.guidelineColor;
    b.guidelineThickness = p.guidelineThickness;
    b.guidelineDashPattern = p.guidelineDashPattern;
    b.unit = p.unit;
    b.measureFont = p.measureFont;
    b.measureFontColor = p.measureFontColor;
}
}

EditRulerItemCommand::EditRulerItemCommand(core::Map& map, int layerIndex, QString guid,
                                            BaseProps next, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex),
      guid_(std::move(guid)), after_(std::move(next)) {
    if (auto* r = findRuler(map, layerIndex, guid_)) {
        before_ = snapshot(*r);
    }
    setText(QObject::tr("Edit ruler"));
}

void EditRulerItemCommand::redo() {
    if (auto* r = findRuler(map_, layerIndex_, guid_)) apply(*r, after_);
}
void EditRulerItemCommand::undo() {
    if (auto* r = findRuler(map_, layerIndex_, guid_)) apply(*r, before_);
}

// ----- AttachRulerCommand -----

namespace {
core::LayerRuler* attachLookup(core::Map& m, int idx) {
    if (idx < 0 || idx >= static_cast<int>(m.layers().size())) return nullptr;
    auto* L = m.layers()[idx].get();
    return (L && L->kind() == core::LayerKind::Ruler)
        ? static_cast<core::LayerRuler*>(L) : nullptr;
}
const QString& attachGuidOf(const core::LayerRuler::AnyRuler& r) {
    return r.kind == core::RulerKind::Linear ? r.linear.guid : r.circular.guid;
}
}

AttachRulerCommand::AttachRulerCommand(core::Map& map, int layerIndex, QString rulerGuid,
                                       int endpointIndex, QString brickGuid,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex),
      rulerGuid_(std::move(rulerGuid)), endpointIndex_(endpointIndex), after_(std::move(brickGuid)) {
    // Snapshot the pre-existing attachment so undo restores it.
    if (auto* L = attachLookup(map, layerIndex)) {
        for (auto& any : L->rulers) {
            const QString& g = attachGuidOf(any);
            if (g != rulerGuid_) continue;
            if (any.kind == core::RulerKind::Linear) {
                before_ = (endpointIndex_ == 0) ? any.linear.attachedBrick1Id
                                                : any.linear.attachedBrick2Id;
            } else {
                before_ = any.circular.attachedBrickId;
            }
            break;
        }
    }
    setText(after_.isEmpty() ? QObject::tr("Detach ruler")
                             : QObject::tr("Attach ruler"));
}

namespace {
void applyAttachment(core::LayerRuler::AnyRuler& any, int endpointIndex,
                     const QString& brickGuid) {
    if (any.kind == core::RulerKind::Linear) {
        if (endpointIndex == 0) any.linear.attachedBrick1Id = brickGuid;
        else                    any.linear.attachedBrick2Id = brickGuid;
    } else {
        any.circular.attachedBrickId = brickGuid;
    }
}
}

void AttachRulerCommand::redo() {
    if (auto* L = attachLookup(map_, layerIndex_)) {
        for (auto& any : L->rulers) {
            if (attachGuidOf(any) == rulerGuid_) { applyAttachment(any, endpointIndex_, after_); break; }
        }
    }
}
void AttachRulerCommand::undo() {
    if (auto* L = attachLookup(map_, layerIndex_)) {
        for (auto& any : L->rulers) {
            if (attachGuidOf(any) == rulerGuid_) { applyAttachment(any, endpointIndex_, before_); break; }
        }
    }
}

}
