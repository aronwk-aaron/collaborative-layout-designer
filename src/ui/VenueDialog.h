#pragma once

#include "../core/Venue.h"

#include <QDialog>

#include <optional>

namespace cld::ui {

// Properties editor for the project's single venue. Name, walkway width,
// enabled toggle, per-edge kind (wall/door/open) + label, obstacle list.
// Geometry drawing (polygon points) happens in MapView via a dedicated
// drawing tool — this dialog only edits metadata of an existing venue.
// Returns the edited venue on accept (nullopt on cancel or clear).
class VenueDialog : public QDialog {
    Q_OBJECT
public:
    explicit VenueDialog(const std::optional<core::Venue>& current, QWidget* parent = nullptr);

    std::optional<core::Venue> result() const { return result_; }
    bool cleared() const { return cleared_; }

private:
    std::optional<core::Venue> result_;
    bool cleared_ = false;
};

}
