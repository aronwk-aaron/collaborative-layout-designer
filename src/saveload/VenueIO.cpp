#include "VenueIO.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace cld::saveload {

namespace {

QJsonObject encodePoint(QPointF p) {
    QJsonObject o;
    o[QStringLiteral("x")] = p.x();
    o[QStringLiteral("y")] = p.y();
    return o;
}

QPointF decodePoint(const QJsonObject& o) {
    return { o.value(QStringLiteral("x")).toDouble(),
             o.value(QStringLiteral("y")).toDouble() };
}

}

bool writeVenueFile(const QString& path, const core::Venue& v, QString* errOut) {
    QJsonObject root;
    root[QStringLiteral("schema")] = QStringLiteral("cld-venue/1");
    root[QStringLiteral("name")] = v.name;
    root[QStringLiteral("enabled")] = v.enabled;
    root[QStringLiteral("minWalkwayStuds")] = v.minWalkwayStuds;
    QJsonObject bounds;
    bounds[QStringLiteral("x")] = v.layoutBoundsStuds.x();
    bounds[QStringLiteral("y")] = v.layoutBoundsStuds.y();
    bounds[QStringLiteral("w")] = v.layoutBoundsStuds.width();
    bounds[QStringLiteral("h")] = v.layoutBoundsStuds.height();
    root[QStringLiteral("bounds")] = bounds;

    QJsonArray edges;
    for (const auto& e : v.edges) {
        QJsonObject eo;
        eo[QStringLiteral("kind")] = static_cast<int>(e.kind);
        eo[QStringLiteral("doorWidthStuds")] = e.doorWidthStuds;
        eo[QStringLiteral("label")] = e.label;
        QJsonArray pts;
        for (const auto& p : e.polyline) pts.append(encodePoint(p));
        eo[QStringLiteral("poly")] = pts;
        edges.append(eo);
    }
    root[QStringLiteral("edges")] = edges;

    QJsonArray obstacles;
    for (const auto& ob : v.obstacles) {
        QJsonObject oo;
        oo[QStringLiteral("label")] = ob.label;
        QJsonArray pts;
        for (const auto& p : ob.polygon) pts.append(encodePoint(p));
        oo[QStringLiteral("poly")] = pts;
        obstacles.append(oo);
    }
    root[QStringLiteral("obstacles")] = obstacles;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errOut) *errOut = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    return true;
}

std::optional<core::Venue> readVenueFile(const QString& path, QString* errOut) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errOut) *errOut = f.errorString();
        return std::nullopt;
    }
    QJsonParseError jerr;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &jerr);
    if (jerr.error != QJsonParseError::NoError) {
        if (errOut) *errOut = jerr.errorString();
        return std::nullopt;
    }
    const auto root = doc.object();
    core::Venue v;
    v.name = root.value(QStringLiteral("name")).toString();
    v.enabled = root.value(QStringLiteral("enabled")).toBool(true);
    v.minWalkwayStuds = root.value(QStringLiteral("minWalkwayStuds"))
                             .toDouble(v.minWalkwayStuds);
    const auto bounds = root.value(QStringLiteral("bounds")).toObject();
    v.layoutBoundsStuds = QRectF(
        bounds.value(QStringLiteral("x")).toDouble(),
        bounds.value(QStringLiteral("y")).toDouble(),
        bounds.value(QStringLiteral("w")).toDouble(),
        bounds.value(QStringLiteral("h")).toDouble());
    for (const auto& ev : root.value(QStringLiteral("edges")).toArray()) {
        const auto eo = ev.toObject();
        core::VenueEdge e;
        e.kind = static_cast<core::EdgeKind>(eo.value(QStringLiteral("kind")).toInt());
        e.doorWidthStuds = eo.value(QStringLiteral("doorWidthStuds")).toDouble();
        e.label = eo.value(QStringLiteral("label")).toString();
        for (const auto& p : eo.value(QStringLiteral("poly")).toArray()) {
            e.polyline.append(decodePoint(p.toObject()));
        }
        v.edges.append(e);
    }
    for (const auto& ov : root.value(QStringLiteral("obstacles")).toArray()) {
        const auto oo = ov.toObject();
        core::VenueObstacle ob;
        ob.label = oo.value(QStringLiteral("label")).toString();
        for (const auto& p : oo.value(QStringLiteral("poly")).toArray()) {
            ob.polygon.append(decodePoint(p.toObject()));
        }
        v.obstacles.append(ob);
    }
    return v;
}

}
