#include "LDDLDrawMapping.h"

#include <QFile>
#include <QXmlStreamReader>

namespace cld::import {

bool LDDLDrawMapping::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QXmlStreamReader r(&f);
    int added = 0;

    // ldraw.xml is a flat list of mapping elements; no nesting. Scan
    // every start element and dispatch on tag name. The <LDrawMapping>
    // root just holds the version and is otherwise empty.
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) continue;
        const auto attrs = r.attributes();
        const QStringView name = r.name();
        if (name == QStringLiteral("Material")) {
            bool ok1 = false, ok2 = false;
            const int ldraw = attrs.value(QStringLiteral("ldraw")).toInt(&ok1);
            const int lego  = attrs.value(QStringLiteral("lego")).toInt(&ok2);
            if (ok1 && ok2) {
                materialToLdraw_.insert(lego, ldraw);
                ++added;
            }
        } else if (name == QStringLiteral("Brick") ||
                   name == QStringLiteral("Assembly")) {
            // <Brick ldraw="X.dat" lego="Y" />            — single-part
            // <Assembly ldraw="X.dat" lego="Y" tx ty tz>  — multi-part;
            //     children are <Part ldraw="...">, but the assembly's
            //     own ldraw= still names the canonical LDraw .dat the
            //     assembly maps to. Treat assemblies the same as
            //     bricks for top-level lookups.
            const QString ldraw = attrs.value(QStringLiteral("ldraw")).toString().trimmed();
            const QString lego  = attrs.value(QStringLiteral("lego")).toString().trimmed();
            if (!ldraw.isEmpty() && !lego.isEmpty()) {
                brickToLdraw_.insert(lego, ldraw);
                ++added;
            }
        } else if (name == QStringLiteral("Transformation")) {
            const QString ldraw = attrs.value(QStringLiteral("ldraw")).toString().trimmed();
            if (ldraw.isEmpty()) continue;
            Transformation t;
            t.exists = true;
            t.tx = attrs.value(QStringLiteral("tx")).toDouble();
            t.ty = attrs.value(QStringLiteral("ty")).toDouble();
            t.tz = attrs.value(QStringLiteral("tz")).toDouble();
            t.ax = attrs.value(QStringLiteral("ax")).toDouble();
            t.ay = attrs.value(QStringLiteral("ay")).toDouble();
            t.az = attrs.value(QStringLiteral("az")).toDouble();
            t.angle = attrs.value(QStringLiteral("angle")).toDouble();
            transformations_.insert(ldraw, t);
            ++added;
        }
    }
    return added > 0 && !r.hasError();
}

}  // namespace cld::import
