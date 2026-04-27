#include "LDDMaterials.h"

#include <QFile>
#include <QXmlStreamReader>

namespace cld::import {

namespace {

bool parseFromReader(QXmlStreamReader& r, QHash<int, QColor>& out) {
    int added = 0;
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) continue;
        if (r.name() != QStringLiteral("Material")) continue;
        const auto attrs = r.attributes();
        bool ok = false;
        const int matId = attrs.value(QStringLiteral("MatID")).toInt(&ok);
        if (!ok) continue;
        const int red   = attrs.value(QStringLiteral("Red")).toInt();
        const int green = attrs.value(QStringLiteral("Green")).toInt();
        const int blue  = attrs.value(QStringLiteral("Blue")).toInt();
        // Alpha defaults to 255 when unspecified — matches LDD's
        // shinyPlastic material default.
        const int alpha = attrs.hasAttribute(QStringLiteral("Alpha"))
                              ? attrs.value(QStringLiteral("Alpha")).toInt()
                              : 255;
        out.insert(matId, QColor::fromRgb(
            std::clamp(red, 0, 255),
            std::clamp(green, 0, 255),
            std::clamp(blue, 0, 255),
            std::clamp(alpha, 0, 255)));
        ++added;
    }
    return added > 0 && !r.hasError();
}

}  // namespace

bool LDDMaterials::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QXmlStreamReader r(&f);
    return parseFromReader(r, colors_);
}

bool LDDMaterials::loadFromBytes(const QByteArray& bytes) {
    QXmlStreamReader r(bytes);
    return parseFromReader(r, colors_);
}

}  // namespace cld::import
