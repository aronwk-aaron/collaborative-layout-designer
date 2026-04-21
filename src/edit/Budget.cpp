#include "Budget.h"

#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"

#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace cld::edit {

BudgetLimits readBudgetFile(const QString& path) {
    BudgetLimits limits;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return limits;
    QXmlStreamReader r(&f);
    while (r.readNextStartElement()) {
        if (r.name() == QStringLiteral("Budget")) {
            while (r.readNextStartElement()) {
                if (r.name() == QStringLiteral("BudgetEntry")) {
                    QString part;
                    int     limit = -1;
                    while (r.readNextStartElement()) {
                        if      (r.name() == QStringLiteral("PartNumber")) part = r.readElementText().trimmed();
                        else if (r.name() == QStringLiteral("Limit"))      limit = r.readElementText().toInt();
                        else r.skipCurrentElement();
                    }
                    if (!part.isEmpty() && limit >= 0) limits.insert(part, limit);
                } else {
                    r.skipCurrentElement();
                }
            }
        } else {
            r.skipCurrentElement();
        }
    }
    return limits;
}

bool writeBudgetFile(const QString& path, const BudgetLimits& limits) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QXmlStreamWriter w(&f);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    w.writeStartDocument(QStringLiteral("1.0"));
    w.writeStartElement(QStringLiteral("Budget"));
    w.writeTextElement(QStringLiteral("Version"), QStringLiteral("1"));
    QStringList parts = limits.keys();
    parts.sort();
    for (const QString& part : parts) {
        w.writeStartElement(QStringLiteral("BudgetEntry"));
        w.writeTextElement(QStringLiteral("PartNumber"), part);
        w.writeTextElement(QStringLiteral("Limit"), QString::number(limits.value(part)));
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndDocument();
    return f.commit();
}

QHash<QString, int> countPartUsage(const core::Map& map) {
    QHash<QString, int> usage;
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
            ++usage[b.partNumber];
        }
    }
    return usage;
}

QVector<BudgetViolation> checkBudget(const core::Map& map, const BudgetLimits& limits) {
    QVector<BudgetViolation> out;
    if (limits.isEmpty()) return out;
    const auto usage = countPartUsage(map);
    for (auto it = limits.constBegin(); it != limits.constEnd(); ++it) {
        const int used = usage.value(it.key(), 0);
        if (used > it.value()) {
            out.append({ it.key(), used, it.value(), used - it.value() });
        }
    }
    return out;
}

}  // namespace cld::edit
