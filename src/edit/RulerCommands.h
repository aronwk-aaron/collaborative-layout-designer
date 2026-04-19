#pragma once

#include "../core/LayerRuler.h"

#include <QUndoCommand>

#include <optional>
#include <vector>

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

// Delete a ruler item by guid. Remembers its original position for undo.
class DeleteRulerItemCommand : public QUndoCommand {
public:
    DeleteRulerItemCommand(core::Map& map, int layerIndex, QString guid,
                           QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    QString guid_;
    std::optional<core::LayerRuler::AnyRuler> removed_;
    int index_ = -1;
};

// Edit an existing ruler's base properties (color, line thickness, display
// toggles, guideline color/thickness/dash, unit, measure font/colour). The
// ruler's geometry (endpoints / centre / radius / attachments) is not
// touched.
class EditRulerItemCommand : public QUndoCommand {
public:
    struct BaseProps {
        core::ColorSpec color;
        float lineThickness = 1.0f;
        bool  displayDistance = true;
        bool  displayUnit = true;
        core::ColorSpec guidelineColor;
        float guidelineThickness = 1.0f;
        std::vector<float> guidelineDashPattern;
        int   unit = 0;
        core::FontSpec measureFont;
        core::ColorSpec measureFontColor;
    };

    EditRulerItemCommand(core::Map& map, int layerIndex, QString guid, BaseProps next,
                         QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    QString guid_;
    BaseProps before_;
    BaseProps after_;
};

}
