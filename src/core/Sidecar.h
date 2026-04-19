#pragma once

#include "AnchoredLabel.h"
#include "Module.h"
#include "Venue.h"

#include <QByteArray>
#include <QString>

#include <optional>
#include <vector>

namespace cld::core {

// Root container for fork-only metadata, serialized to a .bbm.cld sidecar
// beside the .bbm. Vanilla BlueBrick ignores this file; our writer always
// writes both atomically on save.
struct Sidecar {
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;

    // SHA-256 of the sibling .bbm bytes at the time of sidecar write. On load,
    // we compare against the current .bbm; drift (e.g. vanilla edited it in
    // between) is flagged so the UI can offer re-link by proximity.
    QByteArray bbmContentHashSha256;

    std::vector<AnchoredLabel> anchoredLabels;
    std::vector<Module>        modules;
    std::optional<Venue>       venue;

    bool isEmpty() const {
        return anchoredLabels.empty() && modules.empty() && !venue.has_value();
    }
};

}
