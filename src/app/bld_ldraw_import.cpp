#include "import/ldraw/LDrawReader.h"
#include "saveload/BbmWriter.h"
#include "core/LayerBrick.h"
#include "core/Map.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

// Reads an LDraw .ldr/.dat/.mpd file and writes an equivalent .bbm containing
// one brick layer. Part dimensions default to 2x2 studs until the BlueBrick
// parts library is consulted to fill in the real footprints — run this
// through the main app afterwards to verify rendering.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr,
            "Usage: bld-ldraw-import <input.ldr> <output.bbm>\n");
        return 1;
    }
    const QString inPath  = QString::fromLocal8Bit(argv[1]);
    const QString outPath = QString::fromLocal8Bit(argv[2]);

    auto read = bld::import::readLDraw(inPath);
    if (!read.ok) {
        std::fprintf(stderr, "Parse failed: %s\n", read.error.toUtf8().constData());
        return 2;
    }
    std::printf("%s: %zu part references, title=%s\n",
                inPath.toUtf8().constData(),
                read.parts.size(),
                read.title.toUtf8().constData());

    auto map = bld::import::toBlueBrickMap(read);
    auto r = bld::saveload::writeBbm(*map, outPath);
    if (!r.ok) {
        std::fprintf(stderr, "Write failed: %s\n", r.error.toUtf8().constData());
        return 3;
    }
    std::printf("Wrote %s (%zu bricks)\n", outPath.toUtf8().constData(),
                static_cast<const bld::core::LayerBrick*>(map->layers()[0].get())->bricks.size());
    return 0;
}
