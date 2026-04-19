#include "PartsLibrary.h"

namespace cld::parts {

void PartsLibrary::addSearchPath(const QString& path) {
    if (!searchPaths_.contains(path)) searchPaths_.push_back(path);
}

int PartsLibrary::scan() {
    // Phase 1: walk search paths, pair GIFs with XML, populate index_.
    return 0;
}

std::optional<PartMetadata> PartsLibrary::metadata(const QString& key) const {
    if (auto it = index_.find(key.toStdString()); it != index_.end()) return it->second;
    return std::nullopt;
}

QPixmap PartsLibrary::pixmap(const QString& /*key*/) {
    return {};
}

}
