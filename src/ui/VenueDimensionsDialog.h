#pragma once

#include <QDialog>
#include <QPointF>
#include <QVector>

namespace cld::ui {

// Build a venue outline polygon by entering dimensions instead of clicking
// on the map. The user sets an origin (studs), then lists a sequence of
// segments each defined by a length (studs) and an absolute angle in
// degrees (0° = east, 90° = south to match Qt's Y-down scene convention).
// OK returns the resulting polygon; the caller pushes a SetVenueCommand
// whose outline uses it.
class VenueDimensionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit VenueDimensionsDialog(QWidget* parent = nullptr);

    // Polygon vertices in stud coords. Empty if the user cancelled.
    const QVector<QPointF>& polygon() const { return polygon_; }

private:
    QVector<QPointF> polygon_;
};

}
