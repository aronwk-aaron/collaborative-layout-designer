#include "VenueDimensionsDialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cmath>

namespace cld::ui {

VenueDimensionsDialog::VenueDimensionsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Draw venue outline by dimensions"));
    resize(560, 520);

    // All length input in feet (vanilla BlueBrick's Distance.FEET: 1 stud
    // = 0.026248 ft ⇒ 1 ft = 38.09814081 studs). Stored internally in
    // studs; conversion happens in the OK handler / preset.
    constexpr double kStudsPerFoot = 38.09814081;

    auto* vbox = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    auto* originX = new QDoubleSpinBox(this);
    originX->setRange(-1e5, 1e5); originX->setDecimals(2); originX->setSuffix(tr(" ft"));
    auto* originY = new QDoubleSpinBox(this);
    originY->setRange(-1e5, 1e5); originY->setDecimals(2); originY->setSuffix(tr(" ft"));
    form->addRow(tr("Start X:"), originX);
    form->addRow(tr("Start Y:"), originY);
    vbox->addLayout(form);

    vbox->addWidget(new QLabel(tr(
        "<b>Segments</b> — each row adds a new vertex at the given distance "
        "and angle from the previous point. Lengths are in <b>feet</b>. "
        "Angle: 0° east, 90° south, 180° west, 270° north. The polygon is "
        "closed automatically.")));

    auto* table = new QTableWidget(0, 2, this);
    table->setHorizontalHeaderLabels({ tr("Length (ft)"), tr("Angle (°)") });
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(true);
    vbox->addWidget(table, 1);

    auto* btnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(tr("Add segment"), this);
    auto* remBtn = new QPushButton(tr("Remove last"), this);
    auto* preset = new QPushButton(tr("Rectangle preset…"), this);
    preset->setToolTip(tr("Quickly fill four segments for a width × depth rectangle."));
    btnRow->addWidget(addBtn);
    btnRow->addWidget(remBtn);
    btnRow->addStretch();
    btnRow->addWidget(preset);
    vbox->addLayout(btnRow);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    vbox->addWidget(bb);

    // Convenient default: one row ready for editing. Default length 10 ft
    // is a reasonable starter for typical venue walls.
    auto addRow = [table](double lengthFt = 10.0, double angle = 0.0) {
        const int r = table->rowCount();
        table->insertRow(r);
        auto* lenItem = new QTableWidgetItem(QString::number(lengthFt, 'f', 2));
        auto* angItem = new QTableWidgetItem(QString::number(angle,    'f', 2));
        table->setItem(r, 0, lenItem);
        table->setItem(r, 1, angItem);
    };
    addRow();

    connect(addBtn, &QPushButton::clicked, this, [addRow]{ addRow(); });
    connect(remBtn, &QPushButton::clicked, this, [table]{
        if (table->rowCount() > 0) table->removeRow(table->rowCount() - 1);
    });
    connect(preset, &QPushButton::clicked, this, [this, table]{
        // Ask for width + depth (in feet) and replace the segment list
        // with a 4-row rectangle going east, south, west, north.
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Rectangle preset"));
        auto* f = new QFormLayout(&dlg);
        auto* w = new QDoubleSpinBox(&dlg);  w->setRange(0, 10000); w->setValue(30);
        auto* d = new QDoubleSpinBox(&dlg);  d->setRange(0, 10000); d->setValue(20);
        w->setSuffix(tr(" ft")); d->setSuffix(tr(" ft"));
        w->setDecimals(2);      d->setDecimals(2);
        f->addRow(tr("Width (east-west):"),  w);
        f->addRow(tr("Depth (north-south):"), d);
        auto* db = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        f->addRow(db);
        connect(db, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(db, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        table->setRowCount(0);
        const double widthFt = w->value();
        const double depthFt = d->value();
        auto append = [table](double lenFt, double ang) {
            const int r = table->rowCount();
            table->insertRow(r);
            table->setItem(r, 0, new QTableWidgetItem(QString::number(lenFt, 'f', 2)));
            table->setItem(r, 1, new QTableWidgetItem(QString::number(ang,   'f', 2)));
        };
        append(widthFt, 0.0);
        append(depthFt, 90.0);
        append(widthFt, 180.0);
        append(depthFt, 270.0);
    });

    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, [this, table, originX, originY, kStudsPerFoot]{
        QVector<QPointF> pts;
        // Convert origin from feet to studs for the internal data model.
        QPointF cur(originX->value() * kStudsPerFoot,
                    originY->value() * kStudsPerFoot);
        pts.push_back(cur);
        for (int r = 0; r < table->rowCount(); ++r) {
            auto* lenItem = table->item(r, 0);
            auto* angItem = table->item(r, 1);
            if (!lenItem || !angItem) continue;
            const double lengthFt = lenItem->text().toDouble();
            const double angDeg   = angItem->text().toDouble();
            if (lengthFt <= 0.0) continue;
            const double lengthStuds = lengthFt * kStudsPerFoot;
            const double rad = angDeg * M_PI / 180.0;
            cur += QPointF(std::cos(rad) * lengthStuds, std::sin(rad) * lengthStuds);
            pts.push_back(cur);
        }
        if (pts.size() < 3) {
            QMessageBox::information(this, tr("Venue outline"),
                tr("Need at least three non-zero segments to build a polygon."));
            return;
        }
        // Drop the closing vertex if it duplicates the origin (common when
        // the user manually entered the full loop).
        const QPointF delta = pts.last() - pts.first();
        if (std::hypot(delta.x(), delta.y()) < 0.5) pts.removeLast();
        polygon_ = std::move(pts);
        accept();
    });
}

}
