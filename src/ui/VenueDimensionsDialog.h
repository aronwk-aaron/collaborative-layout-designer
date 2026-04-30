#pragma once

#include "../core/Venue.h"

#include <QDialog>
#include <QPointF>
#include <QString>
#include <QVector>

namespace bld::ui {

// Build a venue outline by entering dimensions instead of clicking on
// the map. Each segment row carries length (ft), angle (absolute
// degrees, 0° = east, 90° = south), a kind (Wall/Door/Open), and an
// optional label (e.g. "Main entrance"). OK emits the polygon vertex
// list (studs) plus per-segment kinds + labels so the caller can build
// a Venue with each edge classified correctly straight out of the
// dimensions flow.
class VenueDimensionsDialog : public QDialog {
    Q_OBJECT
public:
    struct SegmentMeta {
        core::EdgeKind kind = core::EdgeKind::Wall;
        QString label;
    };

    explicit VenueDimensionsDialog(QWidget* parent = nullptr);

    const QVector<QPointF>&    polygon() const  { return polygon_; }
    const QVector<SegmentMeta>& segments() const { return segments_; }

private:
    QVector<QPointF>     polygon_;
    QVector<SegmentMeta> segments_;
};

}
