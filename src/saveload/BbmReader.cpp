#include "BbmReader.h"

#include "LayerIO.h"
#include "XmlPrimitives.h"

#include "../core/Layer.h"
#include "../core/Map.h"

#include <QFile>
#include <QXmlStreamReader>

#include <vector>

namespace cld::saveload {

namespace {

void readDateElement(QXmlStreamReader& r, QDate& date) {
    int d = 1, m = 1, y = 2000;
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("Day"))   d = xml::readIntElement(r);
        else if (n == QStringLiteral("Month")) m = xml::readIntElement(r);
        else if (n == QStringLiteral("Year"))  y = xml::readIntElement(r);
        else r.skipCurrentElement();
    }
    const QDate parsed(y, m, d);
    date = parsed.isValid() ? parsed : QDate(2000, 1, 1);
}

void readExportInfo(QXmlStreamReader& r, core::ExportInfo& info, int dataVersion) {
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("ExportPath"))           info.exportPath = xml::readTextElement(r);
        else if (n == QStringLiteral("ExportFileType"))       info.fileTypeIndex = xml::readIntElement(r);
        else if (n == QStringLiteral("ExportArea"))           info.area = xml::readRectF(r);
        else if (n == QStringLiteral("ExportScale"))          info.scale = xml::readDoubleElement(r);
        else if (n == QStringLiteral("ExportWatermark"))      info.watermark = xml::readBoolElement(r);
        else if (n == QStringLiteral("ExportBrickHull"))      (void)xml::readBoolElement(r); // v8 only; dropped on write in v9
        else if (n == QStringLiteral("ExportElectricCircuit")) info.electricCircuit = xml::readBoolElement(r);
        else if (n == QStringLiteral("ExportConnectionPoints")) info.connectionPoints = xml::readBoolElement(r);
        else r.skipCurrentElement();
    }
    (void)dataVersion;
}

QString readLayersInto(QXmlStreamReader& r, core::Map& map, int dataVersion) {
    int skipped = 0;
    while (r.readNextStartElement()) {
        if (r.name() != QStringLiteral("Layer")) {
            r.skipCurrentElement();
            continue;
        }
        auto outcome = readLayer(r, dataVersion);
        if (outcome.layer) {
            map.layers().push_back(std::move(outcome.layer));
        } else {
            ++skipped;
        }
    }
    if (skipped == 0) return {};
    return QStringLiteral("%1 layer(s) skipped (unsupported type)").arg(skipped);
}

LoadResult readMapElement(QXmlStreamReader& r) {
    auto map = std::make_unique<core::Map>();
    QString warning;

    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (n == QStringLiteral("Version")) {
            map->dataVersion = xml::readIntElement(r);
            if (map->dataVersion > core::Map::kCurrentDataVersion) {
                return { nullptr, QStringLiteral(
                    "File data version %1 is newer than supported (%2).")
                        .arg(map->dataVersion).arg(core::Map::kCurrentDataVersion) };
            }
        } else if (n == QStringLiteral("nbItems")) {
            (void)xml::readIntElement(r); // recomputed on save
        } else if (n == QStringLiteral("BackgroundColor")) {
            map->backgroundColor = xml::readColor(r);
        } else if (n == QStringLiteral("Author")) {
            map->author = xml::readTextElement(r);
        } else if (n == QStringLiteral("LUG")) {
            map->lug = xml::readTextElement(r);
        } else if (n == QStringLiteral("Event")) {
            map->event = xml::readTextElement(r);
        } else if (n == QStringLiteral("Date")) {
            readDateElement(r, map->date);
        } else if (n == QStringLiteral("Comment")) {
            map->comment = xml::readTextElement(r);
        } else if (n == QStringLiteral("ExportInfo")) {
            readExportInfo(r, map->exportInfo, map->dataVersion);
        } else if (n == QStringLiteral("SelectedLayerIndex")) {
            map->selectedLayerIndex = xml::readIntElement(r);
        } else if (n == QStringLiteral("Layers")) {
            const auto w = readLayersInto(r, *map, map->dataVersion);
            if (!w.isEmpty()) warning = w;
        } else {
            r.skipCurrentElement();
        }
    }

    LoadResult out;
    out.map = std::move(map);
    out.error = warning; // non-fatal warning if layers were skipped
    return out;
}

}

LoadResult readBbm(QIODevice& input) {
    QXmlStreamReader r(&input);
    while (r.readNextStartElement()) {
        if (r.name() == QStringLiteral("Map")) {
            return readMapElement(r);
        }
        r.skipCurrentElement();
    }
    return { nullptr, QStringLiteral("No <Map> root element found.") };
}

LoadResult readBbm(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return { nullptr, QStringLiteral("Cannot open file: %1").arg(file.errorString()) };
    }
    return readBbm(file);
}

}
