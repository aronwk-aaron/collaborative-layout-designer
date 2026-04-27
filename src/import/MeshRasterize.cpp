#include "MeshRasterize.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>

namespace cld::import {

namespace {

// Sort triangles by their max Y ascending so QPainter paints higher
// pieces (LDraw Y-up = closer to camera in top-down) on top of lower
// ones. This is the orthographic equivalent of a depth sort and gives
// correct fill ordering for the common case where triangles don't
// interpenetrate. It does NOT handle interpenetration — for the
// top-down sprite use case nothing relevant interpenetrates.
struct Sortable {
    const geom::Triangle* tri;
    double maxY;
};

}  // namespace

RasterizeResult rasterizeMeshTopDown(const geom::Mesh& mesh,
                                      const RasterizeOptions& opt) {
    RasterizeResult out;
    if (mesh.tris.empty()) return out;

    // Compute bounding box in stud space. Mesh is in LDU; convert
    // when projecting.
    double xmin =  std::numeric_limits<double>::infinity();
    double xmax = -std::numeric_limits<double>::infinity();
    double zmin =  std::numeric_limits<double>::infinity();
    double zmax = -std::numeric_limits<double>::infinity();
    for (const auto& t : mesh.tris) {
        for (const auto& v : t.v) {
            const double xs = v.x * opt.studsPerLdu;
            const double zs = v.z * opt.studsPerLdu;
            xmin = std::min(xmin, xs); xmax = std::max(xmax, xs);
            zmin = std::min(zmin, zs); zmax = std::max(zmax, zs);
        }
    }
    out.meshBoundsXZ = QRectF(QPointF(xmin, zmin), QPointF(xmax, zmax));

    // Pixel canvas size with margin so anti-aliased edges don't clip.
    const double widthPx  = (xmax - xmin) * opt.pxPerStud + 2.0 * opt.marginPx;
    const double heightPx = (zmax - zmin) * opt.pxPerStud + 2.0 * opt.marginPx;
    if (widthPx <= 0.0 || heightPx <= 0.0) return out;

    QImage img(static_cast<int>(std::ceil(widthPx)),
               static_cast<int>(std::ceil(heightPx)),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    // Origin offset: image (0,0) corresponds to stud (xmin - margin, zmin - margin).
    out.imageOriginInStuds = QPointF(xmin - opt.marginPx / static_cast<double>(opt.pxPerStud),
                                      zmin - opt.marginPx / static_cast<double>(opt.pxPerStud));

    // Build sortable list, then stable sort by max Y ascending.
    std::vector<Sortable> sorted;
    sorted.reserve(mesh.tris.size());
    for (const auto& t : mesh.tris) {
        const double maxY = std::max({ t.v[0].y, t.v[1].y, t.v[2].y });
        sorted.push_back({ &t, maxY });
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Sortable& a, const Sortable& b){ return a.maxY < b.maxY; });

    QPainter p(&img);
    if (opt.antialias) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
    }
    p.setPen(Qt::NoPen);

    for (const auto& s : sorted) {
        const auto& t = *s.tri;
        QPolygonF poly;
        for (int k = 0; k < 3; ++k) {
            const double xs = t.v[k].x * opt.studsPerLdu;
            const double zs = t.v[k].z * opt.studsPerLdu;
            const double xPx = (xs - xmin) * opt.pxPerStud + opt.marginPx;
            const double yPx = (zs - zmin) * opt.pxPerStud + opt.marginPx;
            poly << QPointF(xPx, yPx);
        }
        p.setBrush(t.color);
        p.drawPolygon(poly);
    }
    p.end();

    out.image = std::move(img);
    return out;
}

}  // namespace cld::import
