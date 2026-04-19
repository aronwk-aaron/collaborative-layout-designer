#pragma once

#include "../core/LayerRuler.h"

#include <QUndoCommand>

namespace cld::core { class Map; }

namespace cld::edit {

// Append a ruler item (linear or circular) to a LayerRuler at a given index.
class AddRulerItemCommand : public QUndoCommand {
public:
    AddRulerItemCommand(core::Map& map, int layerIndex, core::LayerRuler::AnyRuler ruler,
                        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    core::LayerRuler::AnyRuler ruler_;
    // The guid carried by the stored ruler (linear.guid or circular.guid)
    // — used as the identity for undo removal.
    QString guidForUndo_;
};

}
