#include "BbmWriter.h"

#include "../core/Map.h"

namespace cld::saveload {

WriteResult writeBbm(const core::Map& /*map*/, const QString& /*path*/) {
    return { false, QStringLiteral("BbmWriter::writeBbm is not implemented yet (Phase 1).") };
}

}
