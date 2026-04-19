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
// Connection-priority hook installed by MapView. See SceneBuilder.h.
SceneBuilder::LiveSnapHook gLiveSnapHook;

// Selection is now painted from MapView::drawForeground so every item kind
// (pixmap, rect, line, ellipse) gets a consistent, unmistakable highlight
// regardless of subclass-specific paint overrides. This helper stays only
// to hide connection-point dots on selection changes below.

// Palette matching BlueBrick's ConnectionType.Color-ish defaults: rail (cyan),
// road (orange), monorail (purple), and a fallback green. Type 0 means "no
// type" and never rendered.
QColor colorForConnectionType(const QString& /*type*/) {
    // User prefers all connection markers in a single bright red so they
    // stand out consistently against every brick colour (green baseplates,
    // grey road, blue sky, etc.). Per-type colour palettes can come back
    // later if users ask for them.
    return QColor(230, 40, 40);
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
        if (c == ItemPositionChange && (flags() & ItemIsMovable)) {
            QPointF p = v.toPointF();
            // Try connection snap FIRST (matches vanilla's getMovedSnapPoint
            // connection-priority behavior). Only fall back to the grid snap
            // if no free connection is close enough.
            if (gLiveSnapHook) {
                if (auto snapped = gLiveSnapHook(this, p)) {
                    return *snapped;
                }
            }
            if (gSnapPx > 0.0) {
                p.setX(std::round(p.x() / gSnapPx) * gSnapPx);
                p.setY(std::round(p.y() / gSnapPx) * gSnapPx);
                return p;
            }
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
        // unconditionally if view/connectionPoints is on). Radius bumped to
        // 6px with a white ring + type-coloured fill so they stand out
        // clearly on any brick colour — user asked for larger/more obvious
        // markers.
        if (meta && !meta->connections.isEmpty()) {
            // Radius 10px with a 2.5px white ring + red fill so the
            // markers are unmistakable at any zoom. User asked for
            // "bigger and more obvious" — this is big.
            constexpr double kDotRadiusPx = 10.0;
            for (const auto& c : meta->connections) {
                if (c.type.isEmpty()) continue;
                const QPointF localPx(c.position.x() * kPx, c.position.y() * kPx);
                auto* dot = new QGraphicsEllipseItem(
                    localPx.x() - kDotRadiusPx, localPx.y() - kDotRadiusPx,
                    kDotRadiusPx * 2, kDotRadiusPx * 2, item);
                QColor col = colorForConnectionType(c.type);
                QPen pen(QColor(255, 255, 255));
                pen.setWidthF(2.5);
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
    // Vanilla BlueBrick applies the area layer's transparency on top of
    // each cell's own colour. When the .bbm doesn't specify a transparency
    // (field defaults to 100), users still expect the cells to be
    // semi-transparent so underlying bricks show through. Match BlueBrick's
    // typical default by dropping our rendered alpha to 50% when the file
    // provides a "fully opaque" area layer.
    const double alpha = (L.transparency >= 100) ? 0.5 : L.transparency / 100.0;
    for (const auto& cell : L.cells) {
        auto* r = new QGraphicsRectItem(cell.x * sizePx, cell.y * sizePx, sizePx, sizePx);
        r->setPen(Qt::NoPen);
        QColor c = cell.color;
        c.setAlpha(static_cast<int>(c.alpha() * alpha));
        r->setBrush(QBrush(c));
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
            // Anchor points (where the ruler attaches conceptually).
            const QPointF anchor1(studToPx(r.point1.x()), studToPx(r.point1.y()));
            const QPointF anchor2(studToPx(r.point2.x()), studToPx(r.point2.y()));

            // The MAIN drawn line is between the *offsetted* points when
            // AllowOffset is on — vanilla moves the visible measure line
            // away from the anchors. When AllowOffset is off, offsetP == anchor.
            QPointF offsetP1 = anchor1, offsetP2 = anchor2;
            const QPointF dir = anchor2 - anchor1;
            const double  len = std::hypot(dir.x(), dir.y());
            // Perpendicular-offset vector: vanilla BlueBrick computes this
            // as (Uy, -Ux) where U is the unit vector along the ruler line
            // (LayerRulerItem.cs updateDisplayData line 877). Our previous
            // (-Uy, Ux) was opposite sign, so an OffsetDistance of -48 on a
            // left-to-right ruler moved the line upward instead of downward
            // and the red Nikki-area ruler ended up above the map.
            QPointF nrm;
            if (len > 0.001) nrm = QPointF(dir.y() / len, -dir.x() / len);
            const bool needOffset = r.allowOffset && std::abs(r.offsetDistance) > 0.001f && len > 0.001;
            if (needOffset) {
                const double off = studToPx(r.offsetDistance);
                offsetP1 += nrm * off;
                offsetP2 += nrm * off;
            }

            QPen mainPen(r.color.color);
            mainPen.setWidthF(r.lineThickness);
            mainPen.setCapStyle(Qt::FlatCap);

            // Measurement label + segmented main line (split around the text)
            // when displayDistance is on; otherwise one solid line.
            QGraphicsLineItem* selectableLine = nullptr;
            if (r.displayDistance) {
                const QPointF delta = r.point2 - r.point1;
                const double distStuds = std::hypot(delta.x(), delta.y());
                const QString text = r.displayUnit
                    ? formatDistance(distStuds, r.unit)
                    : QString::number(distStuds, 'f', 2);
                const QPointF midPx = (offsetP1 + offsetP2) / 2.0;

                // Keep the label readable: if the ruler's line angle would
                // rotate the text upside-down (90° < |angle| ≤ 180°), flip
                // by 180° so it stays right-reading. Upstream uses the same
                // "always readable" convention.
                double angleDeg = std::atan2(offsetP2.y() - offsetP1.y(),
                                              offsetP2.x() - offsetP1.x()) * 180.0 / M_PI;
                if (angleDeg > 90.0 || angleDeg < -90.0) angleDeg += 180.0;

                // Build label to measure its width, then split the line around it.
                QFont f(r.measureFont.familyName);
                f.setBold(r.measureFont.styleString.contains(QStringLiteral("Bold")));
                f.setItalic(r.measureFont.styleString.contains(QStringLiteral("Italic")));
                // Upstream's font is pt-sized and renders at screen size; we
                // bump the pixel size generously so it's readable at normal
                // zoom. Matches the visual weight of vanilla's "36.12 ft"
                // label instead of the tiny clipped fragment we had before.
                const int pxSize = std::max(16, static_cast<int>(r.measureFont.sizePt * 3.5));
                f.setPixelSize(pxSize);
                QFontMetricsF fm(f);
                const double halfText = fm.horizontalAdvance(text) / 2.0 + 6.0;

                // Unit vector along the ruler line (in offset-line coords).
                QPointF unit(dir.x() / len, dir.y() / len);
                const QPointF mid1 = midPx - unit * halfText;
                const QPointF mid2 = midPx + unit * halfText;

                auto* seg1 = new QGraphicsLineItem(offsetP1.x(), offsetP1.y(), mid1.x(), mid1.y());
                auto* seg2 = new QGraphicsLineItem(offsetP2.x(), offsetP2.y(), mid2.x(), mid2.y());
                seg1->setPen(mainPen);
                seg2->setPen(mainPen);
                sink.add(seg1);
                sink.add(seg2);
                // Both segments AND the label tag with the same ruler guid
                // so clicking any piece selects the logical ruler — and
                // so refreshSelectionOverlay can union all pieces into one
                // highlight rather than outlining each segment separately.
                seg2->setFlag(QGraphicsItem::ItemIsSelectable, true);
                seg2->setFlag(QGraphicsItem::ItemIsMovable,    true);
                seg2->setData(kBrickDataLayerIndex, layerIndex);
                seg2->setData(kBrickDataGuid,       r.guid);
                seg2->setData(kBrickDataKind,       QStringLiteral("ruler"));
                selectableLine = seg1;

                // Label centred on midPx, rotated to match the (possibly
                // flipped) line angle so text always reads left-to-right.
                auto* t = new QGraphicsSimpleTextItem(text);
                t->setFont(f);
                t->setBrush(QBrush(r.measureFontColor.color));
                const QRectF bb = t->boundingRect();
                QTransform tr;
                tr.translate(midPx.x(), midPx.y());
                tr.rotate(angleDeg);
                tr.translate(-bb.width() / 2.0, -bb.height() / 2.0);
                t->setTransform(tr);
                t->setFlag(QGraphicsItem::ItemIsSelectable, true);
                t->setFlag(QGraphicsItem::ItemIsMovable,    true);
                t->setData(kBrickDataLayerIndex, layerIndex);
                t->setData(kBrickDataGuid,       r.guid);
                t->setData(kBrickDataKind,       QStringLiteral("ruler"));
                sink.add(t);
            } else {
                // Single solid line from offsetP1 to offsetP2.
                auto* line = new QGraphicsLineItem(offsetP1.x(), offsetP1.y(),
                                                    offsetP2.x(), offsetP2.y());
                line->setPen(mainPen);
                sink.add(line);
                selectableLine = line;
            }

            // Tag the first main-line segment as the ruler's selectable
            // proxy so right-click / double-click → Properties works.
            if (selectableLine) {
                selectableLine->setFlag(QGraphicsItem::ItemIsSelectable, true);
                selectableLine->setFlag(QGraphicsItem::ItemIsMovable,    true);
                selectableLine->setData(kBrickDataLayerIndex, layerIndex);
                selectableLine->setData(kBrickDataGuid,       r.guid);
                selectableLine->setData(kBrickDataKind,       QStringLiteral("ruler"));
            }

            // Dashed guidelines from the anchor points to the offset line's
            // matching endpoints — only when allowOffset actually moves the
            // line away from the anchors. Matches vanilla's penForGuideline.
            if (needOffset) {
                QPen gpen(r.guidelineColor.color);
                gpen.setWidthF(std::max(0.5f, r.guidelineThickness));
                gpen.setCosmetic(true);
                QList<qreal> dashes;
                for (float d : r.guidelineDashPattern) if (d > 0) dashes.append(d);
                if (dashes.size() >= 2) gpen.setDashPattern(dashes);
                else                     gpen.setStyle(Qt::DashLine);

                auto* g1 = new QGraphicsLineItem(anchor1.x(), anchor1.y(), offsetP1.x(), offsetP1.y());
                auto* g2 = new QGraphicsLineItem(anchor2.x(), anchor2.y(), offsetP2.x(), offsetP2.y());
                g1->setPen(gpen);
                g2->setPen(gpen);
                sink.add(g1);
                sink.add(g2);

                // Small filled circles at the actual anchor points so the
                // user can see where the ruler is anchored when it's been
                // offset away from the visible measure line. Coloured to
                // match whatever the ruler is attached to: orange = free,
                // green = attached to a brick.
                constexpr double kAnchorRadiusPx = 4.0;
                auto addAnchorMarker = [&](QPointF p, bool attached) {
                    auto* dot = new QGraphicsEllipseItem(
                        p.x() - kAnchorRadiusPx, p.y() - kAnchorRadiusPx,
                        kAnchorRadiusPx * 2, kAnchorRadiusPx * 2);
                    QPen pen(QColor(40, 40, 40));
                    pen.setWidthF(1.5);
                    pen.setCosmetic(true);
                    dot->setPen(pen);
                    dot->setBrush(QBrush(attached ? QColor(30, 180, 60)
                                                   : QColor(240, 140, 30)));
                    dot->setData(kBrickDataLayerIndex, layerIndex);
                    dot->setData(kBrickDataGuid,       r.guid);
                    dot->setData(kBrickDataKind,       QStringLiteral("ruler"));
                    sink.add(dot);
                };
                addAnchorMarker(anchor1, !r.attachedBrick1Id.isEmpty());
                addAnchorMarker(anchor2, !r.attachedBrick2Id.isEmpty());
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
            el->setFlag(QGraphicsItem::ItemIsMovable,    true);
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

void SceneBuilder::setLiveConnectionSnapHook(LiveSnapHook hook) {
    gLiveSnapHook = std::move(hook);
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

    // Walkway buffer: draw a translucent band on the INSIDE of every
    // non-Wall edge (Door + Open). Walls are solid barriers so bricks
    // can butt right up against them — no buffer needed. The band is
    // `minWalkwayStuds` thick, on the left-hand side of each segment
    // (the inside of a counter-clockwise polygon). Drawn BEFORE the
    // edges so edge strokes render on top.
    const double walkPx = v.minWalkwayStuds * kPx;
    if (walkPx > 0.001) {
        for (const auto& edge : v.edges) {
            if (edge.kind == core::EdgeKind::Wall) continue;
            if (edge.polyline.size() < 2) continue;
            QPainterPath band;
            // For each pair of consecutive points, add a thin parallel
            // rectangle (segment + perpendicular offset into the venue).
            for (int i = 1; i < edge.polyline.size(); ++i) {
                const QPointF a = edge.polyline[i - 1] * kPx;
                const QPointF b = edge.polyline[i]     * kPx;
                const QPointF d = b - a;
                const double len = std::hypot(d.x(), d.y());
                if (len < 0.001) continue;
                const QPointF nIn(-d.y() / len, d.x() / len);  // left-hand normal
                const QPointF aIn = a + nIn * walkPx;
                const QPointF bIn = b + nIn * walkPx;
                QPolygonF quad; quad << a << b << bIn << aIn;
                band.addPolygon(quad);
            }
            auto* bandItem = new QGraphicsPathItem(band);
            bandItem->setPen(Qt::NoPen);
            bandItem->setBrush(QBrush(QColor(255, 170, 0, 60),
                                       Qt::BDiagPattern));
            sink.add(bandItem);
        }
    }

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
        // Beefier venue outline strokes — walls especially need to read as
        // a bold border against bricks / grid, not a hairline.
        switch (edge.kind) {
            case core::EdgeKind::Wall:
                pen.setColor(QColor(30, 30, 30));
                pen.setWidthF(7.0);
                break;
            case core::EdgeKind::Door:
                pen.setColor(QColor(0, 160, 0));
                pen.setStyle(Qt::DashLine);
                pen.setWidthF(5.0);
                break;
            case core::EdgeKind::Open:
                pen.setColor(QColor(0, 0, 200));
                pen.setStyle(Qt::DotLine);
                pen.setWidthF(4.0);
                break;
        }
        item->setPen(pen);
        // Tag the edge as a venue item so MapView can surface edit /
        // delete actions when the user clicks it.
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setData(kBrickDataLayerIndex, -1);
        item->setData(kBrickDataGuid,       QStringLiteral("venue"));
        item->setData(kBrickDataKind,       QStringLiteral("venue"));
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
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setData(kBrickDataLayerIndex, -1);
        item->setData(kBrickDataGuid,       QStringLiteral("venue"));
        item->setData(kBrickDataKind,       QStringLiteral("venue"));
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
        // Tag the label so MapView can select / edit / delete it. layer
        // index is always -1 for anchored labels (they live in the
        // sidecar, not in a Layer).
        t->setFlag(QGraphicsItem::ItemIsSelectable, true);
        t->setFlag(QGraphicsItem::ItemIsMovable,    true);
        t->setData(kBrickDataLayerIndex, -1);
        t->setData(kBrickDataGuid,       lbl.id);
        t->setData(kBrickDataKind,       QStringLiteral("label"));

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
