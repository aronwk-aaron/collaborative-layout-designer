#include "BbmReader.h"

#include "../core/Map.h"

namespace cld::saveload {

LoadResult readBbm(const QString& /*path*/) {
    LoadResult r;
    r.error = QStringLiteral("BbmReader::readBbm is not implemented yet (Phase 1).");
    return r;
}

}
