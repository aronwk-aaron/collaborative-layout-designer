#pragma once

#include <QDialog>

class QUndoStack;

namespace cld::core { class Map; }
namespace cld::ui {

class MapView;

// Find & Replace dialog modelled on upstream FindForm.cs. Supports two modes:
//   - Text: find/replace inside TextCells' .text
//   - Part: find/replace part numbers across every brick layer
// The dialog is modeless so the user can keep it open while browsing matches.
class FindDialog : public QDialog {
    Q_OBJECT
public:
    FindDialog(MapView& view, QWidget* parent = nullptr);

private:
    MapView& view_;
};

}
