#pragma once

#include "../core/Venue.h"

#include <QString>

#include <optional>

namespace bld::saveload {

// Read / write a Venue as standalone JSON (.bld-venue). Separate from
// the map-sidecar encoding so users can save a venue to a library
// folder, share it, and load it into other projects.
bool writeVenueFile(const QString& path, const core::Venue& venue, QString* errOut = nullptr);
std::optional<core::Venue> readVenueFile(const QString& path, QString* errOut = nullptr);

}
