#pragma once

#include "../core/Venue.h"

#include <QUndoCommand>

#include <optional>

namespace bld::core { class Map; }

namespace bld::edit {

// Replace the sidecar venue with a new value (or clear it by passing
// nullopt). Simple whole-venue swap so every drawing / dialog / clear
// action routes through a single undoable step.
class SetVenueCommand : public QUndoCommand {
public:
    SetVenueCommand(core::Map& map, std::optional<core::Venue> next,
                    QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    std::optional<core::Venue> before_;
    std::optional<core::Venue> after_;
};

}
