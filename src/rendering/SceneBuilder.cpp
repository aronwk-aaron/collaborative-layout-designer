#include "SceneBuilder.h"

#include "../core/AnchoredLabel.h"
#include "../core/Layer.h"
#include "../core/LayerArea.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"
#include "../core/Map.h"
#include "../core/Sidecar.h"
#include "../core/Venue.h"
#include "../parts/PartsLibrary.h"

#include <QSettings>

#include <QBrush>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsEllipseItem>
#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>

namespace cld::rendering {

namespace {

constexpr int kPx = SceneBuilder::kPixelsPerStud;

double studToPx(double s) { return s * kPx; }

// Sink passed to each addXxxLayer() helper. We add items directly to the
// scene here (no QGraphicsItemGroup parent) so hit-testing, selection, and
// event dispatch can't be intercepted by a container. The sink also
// remembers every item it spawns so visibility toggles can iterate them,
// and applies a per-layer baseZ so layer ordering is preserved.
struct LayerSink {
    QGraphicsScene& scene;
    QList<QGraphicsItem*>& items;
    double baseZ = 0.0;
    bool   visible = true;

    void add(QGraphicsItem* it) {
        if (!it) return;
        // Only set zValue if the caller hasn't already set one (pixmap
        // items use brick.altitude for z so they self-layer within the
        // same layer). We bias everything by baseZ so higher layers
        // outrank lower layers regardless of per-item z.
        if (it->zValue() == 0.0) it->setZValue(baseZ);
        else                     it->setZValue(baseZ + it->zValue());
        it->setVisible(visible);
        scene.addItem(it);
        items.append(it);
    }
};

// Metadata keys attached to each brick QGraphicsItem via setData(). Used by
// the edit pipeline to look up the mutated brick on mouse release / key press.
constexpr int kBrickDataLayerIndex = 0;
constexpr int kBrickDataGuid       = 1;
constexpr int kBrickDataKind       = 2;  // value: "brick" to distinguish from other items

// Live snap during drag: when set to >0, QGraphicsPixmapItem / QGraphicsRectItem
// subclasses round their pos() to multiples of this scene-pixel value on every
// ItemPositionChange. SceneBuilder exposes a setter (called from MainWindow
// whenever the snap toolbar changes); the per-item override reads the static.
double gSnapPx = 0.0;

// Selection is now painted from MapView::drawForeground so every item kind
// (pixmap, rect, line, ellipse) gets a consistent, unmistakable highlight
// regardless of subclass-specific paint overrides. This helper stays only
// to hide connection-point dots on selection changes below.

// Palette matching BlueBrick's ConnectionType.Color-ish defaults: rail (cyan),
// road (orange), monorail (purple), and a fallback green. Type 0 means "no
// type" and never rendered.
QColor colorForConnectionType(int type) {
    switch (type) {
        case 1:  return QColor(0, 180, 220);   // rail
        case 2:  return QColor(220, 140, 0);   // road
        case 3:  return QColor(160, 0, 200);   // monorail std
        case 4:  return QColor(200, 0, 160);   // monorail short curve
        default: return QColor(30, 200, 60);   // all other types
    }
}

// Toggles visibility of every connection-point marker child so they only
// appear while the parent brick is selected. Children are tagged via
// setData(kBrickDataKind, "connDot") by addBrickLayer so we pick them out
// without disturbing anchored-label children etc.
void setConnectionDotsVisible(QGraphicsItem* parent, bool visible) {
    for (QGraphicsItem* child : parent->childItems()) {
        if (child->data(kBrickDataKind).toString() == QStringLiteral("connDot")) {
            child->setVisible(visible);
        }
    }
}

class SnappingPixmap : public QGraphicsPixmapItem {
public:
    using QGraphicsPixmapItem::QGraphicsPixmapItem;
protected:
    QVariant itemChange(GraphicsItemChange c, const QVariant& v) override {
        if (c == ItemPositionChange && gSnapPx > 0.0 && (flags() & ItemIsMovable)) {
            QPointF p = v.toPointF();
            p.setX(std::round(p.x() / gSnapPx) * gSnapPx);
            p.setY(std::round(p.y() / gSnapPx) * gSnapPx);
            return p;
        }
        if (c == ItemSelectedChange) {
            setConnectionDotsVisible(this, v.toBool());
        }
        return QGraphicsPixmapItem::itemChange(c, v);
    }
};

class SnappingRect : public QGraphicsRectItem {
public:
    using QGraphicsRectItem::QGraphicsRectItem;
protected:
    QVariant itemChange(GraphicsItemChange c, const QVariant& v) override {
        if (c == ItemPositionChange && gSnapPx > 0.0 && (flags() & ItemIsMovable)) {
            QPointF p = v.toPointF();
            p.setX(std::round(p.x() / gSnapPx) * gSnapPx);
            p.setY(std::round(p.y() / gSnapPx) * gSnapPx);
            return p;
        }
        return QGraphicsRectItem::itemChange(c, v);
    }
};

void addBrickLayer(const core::LayerBrick& L, LayerSink& sink, parts::PartsLibrary& lib,
                   int layerIndex, QHash<QString, QGraphicsItem*>& brickByGuid) {
    // Read scene-level render toggles once per layer; all bricks in the same
    // layer share these decisions. Stored under view/ in QSettings.
    QSettings settings;
    const bool alwaysShowConns = settings.value(QStringLiteral("view/connectionPoints"), false).toBool();
    const bool displayHulls    = settings.value(QStringLiteral("view/brickHulls"), false).toBool()
                                 || L.hull.displayHulls;
    const bool displayElev     = settings.value(QStringLiteral("view/brickElevation"), false).toBool();
    const bool displayElectric = settings.value(QStringLiteral("view/electricCircuits"), false).toBool();
    for (const auto& brick : L.bricks) {
        // BlueBrick bakes the color suffix into the PartNumber string itself
        // (e.g. "3811.1" for a blue 32x32 baseplate, or just "TABLE96X190"
        // for an uncolored composite). Lookup is case-insensitive because
        // upstream stores the part number upper-cased in .bbm but the vendored
        // library uses mixed case on disk.
        std::optional<parts::PartMetadata> meta = lib.metadata(brick.partNumber);
        if (!meta) {
            // Fallback: match by prefix so a .bbm referencing "3811" picks up
            // any color-specific variant.
            const QString needle = brick.partNumber.toLower() + QLatin1Char('.');
            for (const QString& key : lib.keys()) {
                if (key.toLower().startsWith(needle)) {
                    meta = lib.metadata(key);
                    break;
                }
            }
        }

        const QRectF areaPx(studToPx(brick.displayArea.x()),
                            studToPx(brick.displayArea.y()),
                            studToPx(brick.displayArea.width()),
                            studToPx(brick.displayArea.height()));
        const QPointF centerPx = areaPx.center();

        QGraphicsItem* item = nullptr;
        if (meta && !meta->gifFilePath.isEmpty()) {
            QPixmap pm(meta->gifFilePath);
            if (!pm.isNull()) {
                auto* p = new SnappingPixmap(pm);
                p->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
                p->setTransformationMode(Qt::SmoothTransformation);
                // Use the full bounding rect as the clickable / selection
                // shape, not the pixmap alpha mask. BlueBrick parts are GIFs
                // with transparent backgrounds — the default MaskShape makes
                // clicks on transparent pixels miss entirely, so the user
                // can click on what looks like the brick and get no
                // selection because the cursor landed on a transparent
                // pixel of the sprite.
                p->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
                p->setOffset(-pm.width() / 2.0, -pm.height() / 2.0);
                p->setTransformOriginPoint(0, 0);
                p->setRotation(brick.orientation);
                p->setPos(centerPx);
                p->setZValue(brick.altitude);
                item = p;
            }
        }
        if (!item) {
            // Fallback placeholder: dashed rectangle centred on origin so
            // drag + snap act on the brick's centre (consistent with pixmap
            // items above).
            auto* r = new SnappingRect(QRectF(-areaPx.width() / 2.0, -areaPx.height() / 2.0,
                                               areaPx.width(), areaPx.height()));
            r->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
            r->setPos(centerPx);
            QPen pen(QColor(200, 80, 80));
            pen.setStyle(Qt::DashLine);
            r->setPen(pen);
            r->setBrush(QBrush(QColor(255, 200, 200, 80)));
            item = r;
        }

        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setFlag(QGraphicsItem::ItemIsMovable,    true);
        item->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        item->setData(kBrickDataLayerIndex, layerIndex);
        item->setData(kBrickDataGuid,       brick.guid);
        item->setData(kBrickDataKind,       QStringLiteral("brick"));

        // Connection-point markers: child ellipses in the brick's local coord
        // system so they transform with the brick for free. Hidden by default;
        // shown on selection via the ItemSelectedChange hook above (or
        // unconditionally if view/connectionPoints is on).
        if (meta && !meta->connections.isEmpty()) {
            constexpr double kDotRadiusPx = 3.0;
            for (const auto& c : meta->connections) {
                if (c.type == 0) continue;
                const QPointF localPx(c.position.x() * kPx, c.position.y() * kPx);
                auto* dot = new QGraphicsEllipseItem(
                    localPx.x() - kDotRadiusPx, localPx.y() - kDotRadiusPx,
                    kDotRadiusPx * 2, kDotRadiusPx * 2, item);
                QColor col = colorForConnectionType(c.type);
                QPen pen(col.darker(160));
                pen.setWidthF(1.0);
                pen.setCosmetic(true);
                dot->setPen(pen);
                dot->setBrush(QBrush(col));
                dot->setZValue(1000);
                dot->setData(kBrickDataKind, QStringLiteral("connDot"));
                dot->setVisible(alwaysShowConns);
            }
        }

        // Hull outline — vanilla's DisplayHulls renders the brick's selection
        // polygon (we approximate with the displayArea rect for now). Drawn
        // as a sibling polygon in scene coords so it can't be occluded by
        // the parent pixmap. Picks up the layer's configured hull color +
        // thickness; falls back to a sensible default if never configured.
        if (displayHulls) {
            auto* hull = new QGraphicsRectItem(areaPx);
            QPen p(L.hull.color.color.isValid() ? L.hull.color.color : QColor(0, 0, 0));
            p.setWidthF(L.hull.thickness > 0 ? L.hull.thickness : 1);
            p.setCosmetic(true);
            hull->setPen(p);
            hull->setBrush(Qt::NoBrush);
            hull->setZValue(brick.altitude + 0.5);
            sink.add(hull);
        }

        // Altitude label centered on the brick — matches vanilla's
        // DisplayBrickElevation (LayerBrick.cs ~line 806).
        if (displayElev && std::abs(brick.altitude) > 0.001f) {
            auto* alt = new QGraphicsSimpleTextItem(QString::number(brick.altitude, 'f', 1));
            QFont f(QStringLiteral("Sans"));
            f.setPixelSize(10);
            alt->setFont(f);
            alt->setBrush(QBrush(QColor(40, 40, 40)));
            const QRectF bb = alt->boundingRect();
            alt->setPos(centerPx.x() - bb.width() / 2.0,
                        centerPx.y() - bb.height() / 2.0);
            alt->setZValue(brick.altitude + 0.6);
            sink.add(alt);
        }

        // Electric circuits — draw a thin coloured line between each pair
        // of connected connection points. We don't parse BrickLibrary's
        // ElectricCircuit list; approximate by colouring each active
        // LinkedTo connection. Cheap visual hint rather than upstream's
        // polarity-aware rendering.
        if (displayElectric && meta) {
            for (const auto& c : brick.connections) {
                if (c.linkedToId.isEmpty()) continue;
                // Find the local position of this connection on this brick.
                const auto& conns = meta->connections;
                const int idx = &c - &brick.connections[0];
                if (idx < 0 || idx >= conns.size()) continue;
                const auto& cm = conns[idx];
                const QPointF localPx(cm.position.x() * kPx, cm.position.y() * kPx);
                auto* marker = new QGraphicsEllipseItem(
                    localPx.x() - 2.5, localPx.y() - 2.5, 5.0, 5.0, item);
                QPen ep(QColor(220, 140, 0));
                ep.setWidthF(1.5);
                ep.setCosmetic(true);
                marker->setPen(ep);
                marker->setBrush(QBrush(QColor(255, 200, 50, 160)));
                marker->setZValue(999);
            }
        }

        brickByGuid.insert(brick.guid, item);
        sink.add(item);
    }
}

void addTextLayer(const core::LayerText& L, LayerSink& sink, int layerIndex) {
    for (const auto& cell : L.textCells) {
        auto* t = new QGraphicsSimpleTextItem(cell.text);
        QFont f(cell.font.familyName);
        f.setBold(cell.font.styleString.contains(QStringLiteral("Bold")));
        f.setItalic(cell.font.styleString.contains(QStringLiteral("Italic")));

        // Upstream BlueBrick stores the text's *pixel* bounding box (converted
        // to studs) in displayArea. The nominal Font.Size field is the
        // typographic size used to render that pixmap, not the scene-scale
        // size. We ignore font.sizePt and instead pick a pixel font size so
        // the rendered text fills the displayArea's short axis. For rotated
        // text (orientation != 0/180) the short axis is displayArea.width.
        const float orient = std::fmod(cell.orientation, 360.0f);
        const bool rot90 = (std::abs(std::abs(orient) - 90.0f)  < 1.0f ||
                            std::abs(std::abs(orient) - 270.0f) < 1.0f);
        // Unrotated target box in pixels. For 90° rotations the displayArea
        // AABB's width is the rendered height and vice versa.
        const double boxWpx = (rot90 ? cell.displayArea.height()
                                     : cell.displayArea.width()) * kPx;
        const double boxHpx = (rot90 ? cell.displayArea.width()
                                     : cell.displayArea.height()) * kPx;

        // Probe at a reference size, measure, then pick the pixel size that
        // makes the rendered bounding box fit entirely inside (boxWpx, boxHpx).
        // This preserves vanilla's per-label aspect without letting text
        // overflow into neighbours when labels are tightly packed.
        constexpr int kProbe = 100;
        f.setPixelSize(kProbe);
        t->setFont(f);
        const QRectF probeRect = t->boundingRect();
        if (probeRect.width() > 0 && probeRect.height() > 0) {
            const double scaleW = boxWpx / probeRect.width();
            const double scaleH = boxHpx / probeRect.height();
            const double scale  = std::min(scaleW, scaleH);
            const int finalPx = std::max(1, static_cast<int>(kProbe * scale));
            f.setPixelSize(finalPx);
            t->setFont(f);
        }
        t->setBrush(QBrush(cell.fontColor.color));

        // Centre the text on the displayArea centre, rotated in place. We
        // translate by centre, rotate, then offset by -bbox.center so the
        // item's local centre lands on the scene centre.
        const QRectF localBbox = t->boundingRect();
        const QPointF centerPx(studToPx(cell.displayArea.center().x()),
                                studToPx(cell.displayArea.center().y()));
        QTransform tr;
        tr.translate(centerPx.x(), centerPx.y());
        tr.rotate(cell.orientation);
        tr.translate(-localBbox.width() / 2.0, -localBbox.height() / 2.0);
        t->setTransform(tr);
        t->setFlag(QGraphicsItem::ItemIsSelectable, true);
        t->setData(kBrickDataLayerIndex, layerIndex);
        t->setData(kBrickDataGuid,       cell.guid);
        t->setData(kBrickDataKind,       QStringLiteral("text"));
        sink.add(t);
    }
}

void addAreaLayer(const core::LayerArea& L, LayerSink& sink) {
    const double sizePx = studToPx(L.areaCellSizeInStud);
    for (const auto& cell : L.cells) {
        auto* r = new QGraphicsRectItem(cell.x * sizePx, cell.y * sizePx, sizePx, sizePx);
        r->setPen(Qt::NoPen);
        r->setBrush(QBrush(cell.color));
        sink.add(r);
    }
}

QString formatDistance(double studs, int unit) {
    // Upstream Tools/Distance.cs Unit enum:
    //   0 STUD, 1 LDU, 2 STRAIGHT_TRACK, 3 MODULE, 4 METER, 5 FEET.
    // 1 stud = 20 LDU = 1/16 track = 1/96 AFOL module = 8 mm = 0.026248 ft.
    switch (unit) {
        case 1:  return QStringLiteral("%1 LDU").arg(studs * 20.0, 0, 'f', 0);
        case 2:  return QStringLiteral("%1 tracks").arg(studs / 16.0, 0, 'f', 2);
        case 3:  return QStringLiteral("%1 mod").arg(studs / 96.0, 0, 'f', 2);
        case 4:  return QStringLiteral("%1 m").arg(studs * 0.008, 0, 'f', 3);
        case 5:  return QStringLiteral("%1 ft").arg(studs * 0.026248, 0, 'f', 2);
        default: return QStringLiteral("%1 studs").arg(studs, 0, 'f', 2);
    }
}

// Add an ephemeral text label (measurement readout) to the ruler group at
// the given scene-pixel position, rotated to `rotationDeg`, in the chosen
// font + colour.
void addRulerLabel(LayerSink& sink, const QString& text,
                   QPointF scenePosPx, double rotationDeg,
                   const core::FontSpec& fontSpec, const core::ColorSpec& colorSpec) {
    if (text.isEmpty()) return;
    auto* t = new QGraphicsSimpleTextItem(text);
    QFont f(fontSpec.familyName);
    f.setBold(fontSpec.styleString.contains(QStringLiteral("Bold")));
    f.setItalic(fontSpec.styleString.contains(QStringLiteral("Italic")));
    f.setPixelSize(std::max(6, static_cast<int>(fontSpec.sizePt * 1.6)));
    t->setFont(f);
    t->setBrush(QBrush(colorSpec.color));
    const QRectF bb = t->boundingRect();
    QTransform tr;
    tr.translate(scenePosPx.x(), scenePosPx.y());
    tr.rotate(rotationDeg);
    tr.translate(-bb.width() / 2.0, -bb.height() - 4.0);  // sit just above the line
    t->setTransform(tr);
    sink.add(t);
}

void addRulerLayer(const core::LayerRuler& L, LayerSink& sink, int layerIndex) {
    for (const auto& any : L.rulers) {
        if (any.kind == core::RulerKind::Linear) {
            const auto& r = any.linear;
            const QPointF p1px(studToPx(r.point1.x()), studToPx(r.point1.y()));
            const QPointF p2px(studToPx(r.point2.x()), studToPx(r.point2.y()));

            // Guideline dash pattern: the stored list is in LDraw-style length
            // pairs (dash, gap, dash, gap, ...). Qt uses the same convention,
            // but expects multiples of pen width on non-cosmetic pens — we
            // keep them cosmetic for a stable on-screen look.
            if (r.guidelineThickness > 0 && !r.guidelineDashPattern.empty()) {
                // Perpendicular tick marks at each endpoint (upstream convention).
                const QPointF dir = p2px - p1px;
                const double len = std::hypot(dir.x(), dir.y());
                if (len > 0.001) {
                    const QPointF nrm(-dir.y() / len, dir.x() / len);  // perpendicular unit
                    constexpr double kTickStuds = 4.0;
                    const double t = studToPx(kTickStuds);
                    QPen gpen(r.guidelineColor.color);
                    gpen.setWidthF(r.guidelineThickness);
                    gpen.setCosmetic(true);
                    QList<qreal> dashes;
                    for (float d : r.guidelineDashPattern) dashes.append(d);
                    if (dashes.size() < 2) { dashes = { 4.0, 4.0 }; }
                    gpen.setDashPattern(dashes);

                    for (const QPointF& end : { p1px, p2px }) {
                        auto* g = new QGraphicsLineItem(
                            end.x() - nrm.x() * t, end.y() - nrm.y() * t,
                            end.x() + nrm.x() * t, end.y() + nrm.y() * t);
                        g->setPen(gpen);
                        sink.add(g);
                    }
                }
            }

            // Main line — selectable so the user can Edit Ruler...
            auto* line = new QGraphicsLineItem(p1px.x(), p1px.y(), p2px.x(), p2px.y());
            QPen pen(r.color.color);
            pen.setWidthF(r.lineThickness);
            line->setPen(pen);
            line->setFlag(QGraphicsItem::ItemIsSelectable, true);
            line->setData(kBrickDataLayerIndex, layerIndex);
            line->setData(kBrickDataGuid,       r.guid);
            line->setData(kBrickDataKind,       QStringLiteral("ruler"));
            sink.add(line);

            // Offset line + connector ticks. Vanilla BlueBrick draws an
            // auxiliary parallel line at `offsetDistance` studs from the main
            // line when AllowOffset is true. The sign of offsetDistance
            // controls which side the offset sits on.
            if (r.allowOffset && std::abs(r.offsetDistance) > 0.001f) {
                const QPointF dir = p2px - p1px;
                const double len = std::hypot(dir.x(), dir.y());
                if (len > 0.001) {
                    const QPointF nrm(-dir.y() / len, dir.x() / len);
                    const double off = studToPx(r.offsetDistance);
                    const QPointF o1 = p1px + nrm * off;
                    const QPointF o2 = p2px + nrm * off;
                    auto* off1 = new QGraphicsLineItem(p1px.x(), p1px.y(), o1.x(), o1.y());
                    auto* off2 = new QGraphicsLineItem(p2px.x(), p2px.y(), o2.x(), o2.y());
                    auto* mid  = new QGraphicsLineItem(o1.x(), o1.y(), o2.x(), o2.y());
                    QPen opn(r.color.color);
                    opn.setWidthF(r.lineThickness);
                    opn.setStyle(Qt::DashLine);
                    off1->setPen(opn); off2->setPen(opn); mid->setPen(opn);
                    sink.add(off1);
                    sink.add(off2);
                    sink.add(mid);
                }
            }

            // Distance label at the line midpoint.
            if (r.displayDistance) {
                const QPointF delta = r.point2 - r.point1;
                const double distStuds = std::hypot(delta.x(), delta.y());
                const QString text = r.displayUnit
                    ? formatDistance(distStuds, r.unit)
                    : QString::number(distStuds, 'f', 2);
                const QPointF mid = (p1px + p2px) / 2.0;
                const double angle = std::atan2(p2px.y() - p1px.y(), p2px.x() - p1px.x())
                                     * 180.0 / M_PI;
                addRulerLabel(sink, text, mid, angle, r.measureFont, r.measureFontColor);
            }
        } else {
            const auto& r = any.circular;
            const double rPx = studToPx(r.radius);
            const QPointF cPx(studToPx(r.center.x()), studToPx(r.center.y()));

            auto* el = new QGraphicsEllipseItem(
                cPx.x() - rPx, cPx.y() - rPx, 2 * rPx, 2 * rPx);
            QPen pen(r.color.color);
            pen.setWidthF(r.lineThickness);
            el->setPen(pen);
            el->setBrush(Qt::NoBrush);
            el->setFlag(QGraphicsItem::ItemIsSelectable, true);
            el->setData(kBrickDataLayerIndex, layerIndex);
            el->setData(kBrickDataGuid,       r.guid);
            el->setData(kBrickDataKind,       QStringLiteral("ruler"));
            sink.add(el);

            // Radius label to the right of centre.
            if (r.displayDistance) {
                const QString text = r.displayUnit
                    ? formatDistance(r.radius, r.unit)
                    : QString::number(r.radius, 'f', 2);
                addRulerLabel(sink, text, QPointF(cPx.x() + rPx, cPx.y()), 0.0,
                              r.measureFont, r.measureFontColor);
            }
        }
    }
}

}

void SceneBuilder::setLiveSnapStepStuds(double snapStepStuds) {
    gSnapPx = snapStepStuds * kPixelsPerStud;
}

SceneBuilder::SceneBuilder(QGraphicsScene& scene, parts::PartsLibrary& parts)
    : scene_(scene), parts_(parts) {}

void SceneBuilder::clear() {
    for (auto& list : itemsByLayer_) {
        for (auto* it : list) { scene_.removeItem(it); delete it; }
    }
    itemsByLayer_.clear();
    brickByGuid_.clear();
    for (auto* it : venueItems_)      { scene_.removeItem(it); delete it; }
    for (auto* it : worldLabelItems_) { scene_.removeItem(it); delete it; }
    venueItems_.clear();
    worldLabelItems_.clear();
}

void SceneBuilder::build(const core::Map& map) {
    clear();
    addVenue(map);  // z = -100 so it sits beneath every layer
    for (size_t i = 0; i < map.layers().size(); ++i) {
        addLayer(*map.layers()[i], static_cast<int>(i));
    }
    addAnchoredLabels(map);
}

void SceneBuilder::addLayer(const core::Layer& L, int layerIndex) {
    // Each per-layer item goes DIRECTLY into the scene — no parent group —
    // so hit-testing / selection / mouse events aren't intercepted by any
    // container. Per-layer ordering is preserved via baseZ in LayerSink.
    auto& list = itemsByLayer_[layerIndex];
    LayerSink sink{ scene_, list, static_cast<double>(layerIndex) * 1000.0, L.visible };
    const double opacity = std::clamp(L.transparency, 0, 100) / 100.0;

    switch (L.kind()) {
        case core::LayerKind::Grid:
            // Grid lines are painted by MapView::drawBackground, not as scene
            // items. Nothing to add here.
            break;
        case core::LayerKind::Brick:
            addBrickLayer(static_cast<const core::LayerBrick&>(L), sink, parts_, layerIndex, brickByGuid_);
            break;
        case core::LayerKind::Text:
            addTextLayer(static_cast<const core::LayerText&>(L), sink, layerIndex);
            break;
        case core::LayerKind::Area:
            addAreaLayer(static_cast<const core::LayerArea&>(L), sink);
            break;
        case core::LayerKind::Ruler:
            addRulerLayer(static_cast<const core::LayerRuler&>(L), sink, layerIndex);
            break;
        case core::LayerKind::AnchoredText:
            break;
    }

    // Apply per-layer transparency by scaling each item's opacity.
    if (opacity < 1.0) {
        for (auto* it : list) it->setOpacity(opacity);
    }
}

void SceneBuilder::addVenue(const core::Map& map) {
    if (!map.sidecar.venue || !map.sidecar.venue->enabled) return;
    const auto& v = *map.sidecar.venue;

    // Venue items live directly in the scene with a fixed low z so they
    // render beneath every layer. Tracked in venueItems_ for cleanup.
    LayerSink sink{ scene_, venueItems_, -100000.0, true };

    for (const auto& edge : v.edges) {
        if (edge.polyline.size() < 2) continue;
        QPainterPath path;
        path.moveTo(edge.polyline[0] * kPx);
        for (int i = 1; i < edge.polyline.size(); ++i) {
            path.lineTo(edge.polyline[i] * kPx);
        }
        auto* item = new QGraphicsPathItem(path);
        QPen pen;
        pen.setCosmetic(true);
        switch (edge.kind) {
            case core::EdgeKind::Wall:
                pen.setColor(QColor(40, 40, 40));
                pen.setWidthF(3.0);
                break;
            case core::EdgeKind::Door:
                pen.setColor(QColor(0, 150, 0));
                pen.setStyle(Qt::DashLine);
                pen.setWidthF(2.0);
                break;
            case core::EdgeKind::Open:
                pen.setColor(QColor(0, 0, 180));
                pen.setStyle(Qt::DotLine);
                pen.setWidthF(1.5);
                break;
        }
        item->setPen(pen);
        sink.add(item);
    }
    for (const auto& ob : v.obstacles) {
        if (ob.polygon.size() < 3) continue;
        QPolygonF poly;
        for (const auto& p : ob.polygon) poly << p * kPx;
        auto* item = new QGraphicsPolygonItem(poly);
        QPen pen(QColor(90, 90, 90));
        pen.setWidthF(1.0);
        item->setPen(pen);
        item->setBrush(QBrush(QColor(120, 120, 120, 100), Qt::BDiagPattern));
        sink.add(item);
    }
}

void SceneBuilder::addAnchoredLabels(const core::Map& map) {
    if (map.sidecar.anchoredLabels.empty()) return;

    // World-anchored labels go directly to the scene; brick/group/module
    // anchors become children of their target so Qt transform inheritance
    // moves them for free.
    LayerSink sink{ scene_, worldLabelItems_, 100000.0, true };

    for (const auto& lbl : map.sidecar.anchoredLabels) {
        auto* t = new QGraphicsSimpleTextItem(lbl.text);
        QFont f(lbl.font.familyName, static_cast<int>(lbl.font.sizePt));
        f.setBold(lbl.font.styleString.contains(QStringLiteral("Bold")));
        f.setItalic(lbl.font.styleString.contains(QStringLiteral("Italic")));
        t->setFont(f);
        t->setBrush(QBrush(lbl.color.color));
        t->setRotation(lbl.offsetRotation);

        if (lbl.kind == core::AnchorKind::Brick) {
            auto it = brickByGuid_.constFind(lbl.targetId);
            if (it != brickByGuid_.constEnd()) {
                t->setParentItem(*it);
                t->setPos(lbl.offset * kPx);
                continue;
            }
            // fall through to world-positioned if anchor not found
        }
        // World (or unresolved): position in scene coords.
        t->setPos(lbl.offset * kPx);
        sink.add(t);
    }
}

bool SceneBuilder::setLayerVisible(int layerIndex, bool visible) {
    auto it = itemsByLayer_.find(layerIndex);
    if (it == itemsByLayer_.end()) return false;
    for (auto* item : *it) item->setVisible(visible);
    return true;
}

}
