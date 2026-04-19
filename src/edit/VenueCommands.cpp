#include "VenueCommands.h"

#include "../core/Map.h"
#include "../core/Sidecar.h"

#include <QObject>

namespace cld::edit {

SetVenueCommand::SetVenueCommand(core::Map& map, std::optional<core::Venue> next,
                                 QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      before_(map.sidecar.venue), after_(std::move(next)) {
    setText(after_ ? QObject::tr("Update venue") : QObject::tr("Clear venue"));
}

void SetVenueCommand::redo() { map_.sidecar.venue = after_; }
void SetVenueCommand::undo() { map_.sidecar.venue = before_; }

}
