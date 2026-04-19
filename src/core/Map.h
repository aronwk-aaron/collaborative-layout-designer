#pragma once

#include "Ids.h"

#include <QColor>
#include <QDate>
#include <QRectF>
#include <QString>

#include <memory>
#include <vector>

namespace cld::core {

class Layer;

// Mirrors the image-export metadata block in vanilla BlueBrick's <ExportInfo>.
struct ExportInfo {
    QString exportPath;          // saved relative to .bbm file; absolute path resolved after load
    int     fileTypeIndex = 1;
    QRectF  area;
    double  scale = 0.0;
    bool    watermark = false;
    bool    electricCircuit = false;
    bool    connectionPoints = false;
};

class Map {
public:
    Map();
    ~Map();
    Map(const Map&) = delete;
    Map& operator=(const Map&) = delete;
    Map(Map&&) noexcept;
    Map& operator=(Map&&) noexcept;

    // Current data version that Phase 1 reads/writes. Matches CURRENT_DATA_VERSION
    // in upstream MapData/Map.cs (v9 as of BlueBrick 1.9.2.0).
    static constexpr int kCurrentDataVersion = 9;

    // ---------- header ----------

    int     dataVersion = kCurrentDataVersion;

    QString author;
    QString lug;
    QString event;
    QDate   date = QDate::currentDate();
    QString comment;

    QColor  backgroundColor = Qt::white;

    ExportInfo exportInfo;

    int     selectedLayerIndex = -1;

    // ---------- content ----------

    const std::vector<std::unique_ptr<Layer>>& layers() const { return layers_; }
    std::vector<std::unique_ptr<Layer>>&       layers()       { return layers_; }

private:
    std::vector<std::unique_ptr<Layer>> layers_;
};

}
