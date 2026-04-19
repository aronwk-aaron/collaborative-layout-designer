#pragma once

#include <QString>

class QWidget;
class QUndoStack;

namespace cld::core {
class Map;
}
namespace cld::parts { class PartsLibrary; }

namespace cld::ui {

// Upstream-parity property editors: open the dialog, edit in place via a
// QUndoCommand when the user accepts, return true if anything actually
// changed. Each helper expects the caller to have already located the
// target item and its owning layer.

bool editBrickDialog(QWidget* parent, core::Map& map, int layerIndex,
                     const QString& brickGuid, parts::PartsLibrary& lib,
                     QUndoStack& undoStack);

bool editRulerDialog(QWidget* parent, core::Map& map, int layerIndex,
                     const QString& rulerGuid, QUndoStack& undoStack);

bool editTextDialog(QWidget* parent, core::Map& map, int layerIndex,
                    const QString& cellGuid, QUndoStack& undoStack);

}
