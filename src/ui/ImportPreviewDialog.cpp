#include "ImportPreviewDialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace cld::ui {

ImportPreviewDialog::ImportPreviewDialog(const QString& sourceFile,
                                          const QString& kindLabel,
                                          const QImage& sprite,
                                          int widthStuds,
                                          int heightStuds,
                                          const Stats& stats,
                                          const QStringList& errors,
                                          QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Preview: %1").arg(kindLabel));
    resize(720, 560);

    auto* root = new QVBoxLayout(this);

    // Header: source file + dimensions.
    {
        const QString name = QFileInfo(sourceFile).fileName();
        auto* hdr = new QLabel(
            tr("<b>%1</b> — %2 × %3 studs").arg(name).arg(widthStuds).arg(heightStuds), this);
        hdr->setTextFormat(Qt::RichText);
        root->addWidget(hdr);
    }

    // Sprite preview inside a scroll area so the user can pan around
    // a zoomed-in view. Toolbar above with zoom controls + a fit-to-
    // window button. Ctrl+wheel zooms; wheel alone scrolls.
    spriteImage_ = sprite;
    auto* zoomRow = new QHBoxLayout();
    auto* zoomOut = new QPushButton(tr("-"), this);
    auto* zoomIn  = new QPushButton(tr("+"), this);
    auto* fitBtn  = new QPushButton(tr("Fit"), this);
    auto* nativeBtn = new QPushButton(tr("1:1"), this);
    auto* zoomLabel = new QLabel(this);
    zoomOut->setFixedWidth(32);
    zoomIn->setFixedWidth(32);
    zoomLabel->setMinimumWidth(60);
    zoomLabel->setAlignment(Qt::AlignCenter);
    zoomRow->addWidget(zoomOut);
    zoomRow->addWidget(zoomIn);
    zoomRow->addWidget(zoomLabel);
    zoomRow->addStretch();
    zoomRow->addWidget(fitBtn);
    zoomRow->addWidget(nativeBtn);
    root->addLayout(zoomRow);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(false);
    scroll->setMinimumHeight(280);
    scroll->setBackgroundRole(QPalette::Mid);
    scroll->setAlignment(Qt::AlignCenter);
    spriteLabel_ = new QLabel(scroll);
    spriteLabel_->setAlignment(Qt::AlignCenter);
    spriteLabel_->setStyleSheet(QStringLiteral(
        "QLabel { background: "
        "qconicalgradient(cx:0.5, cy:0.5, angle:0, "
        "stop:0 #cccccc, stop:0.25 #ffffff, stop:0.5 #cccccc, "
        "stop:0.75 #ffffff, stop:1 #cccccc); }"));
    scroll->setWidget(spriteLabel_);
    scroll_ = scroll;
    root->addWidget(scroll, 1);

    auto applyZoom = [this, zoomLabel, scroll]{
        if (spriteImage_.isNull()) {
            spriteLabel_->setText(tr("(empty sprite)"));
            spriteLabel_->resize(scroll->viewport()->size());
            zoomLabel->setText(QStringLiteral("—"));
            return;
        }
        const int newW = std::max(1, static_cast<int>(spriteImage_.width()  * zoom_));
        const int newH = std::max(1, static_cast<int>(spriteImage_.height() * zoom_));
        QPixmap pm = QPixmap::fromImage(spriteImage_)
            .scaled(newW, newH, Qt::KeepAspectRatio,
                    zoom_ < 1.5 ? Qt::SmoothTransformation : Qt::FastTransformation);
        spriteLabel_->setPixmap(pm);
        spriteLabel_->resize(pm.size());
        zoomLabel->setText(QStringLiteral("%1%").arg(int(std::round(zoom_ * 100))));
    };
    auto fit = [this, applyZoom, scroll]{
        if (spriteImage_.isNull()) { applyZoom(); return; }
        const QSize avail = scroll->viewport()->size() - QSize(20, 20);
        if (avail.width() <= 0 || avail.height() <= 0) { applyZoom(); return; }
        const double zx = double(avail.width())  / spriteImage_.width();
        const double zy = double(avail.height()) / spriteImage_.height();
        zoom_ = std::clamp(std::min(zx, zy), 0.05, 32.0);
        applyZoom();
    };
    auto bumpZoom = [this, applyZoom](double factor){
        zoom_ = std::clamp(zoom_ * factor, 0.05, 32.0);
        applyZoom();
    };
    connect(zoomOut, &QPushButton::clicked, this, [bumpZoom]{ bumpZoom(1.0 / 1.25); });
    connect(zoomIn,  &QPushButton::clicked, this, [bumpZoom]{ bumpZoom(1.25); });
    connect(fitBtn,  &QPushButton::clicked, this, fit);
    connect(nativeBtn, &QPushButton::clicked, this, [this, applyZoom]{
        zoom_ = 1.0; applyZoom();
    });

    // Ctrl+wheel on the scroll area zooms; default scroll behaviour
    // applies otherwise. Install an event filter so the scroll area's
    // own wheel handling stays in place when Ctrl isn't held.
    scroll->viewport()->installEventFilter(this);
    bumpZoom_ = bumpZoom;

    // Initial: fit to viewport for big sprites, native + 4x for tiny.
    if (!sprite.isNull()) {
        if (sprite.width() < 64 && sprite.height() < 64) {
            zoom_ = 4.0;
            applyZoom();
        } else {
            // Defer fit() until the dialog is sized — the scroll
            // viewport doesn't know its real size at construction.
            QMetaObject::invokeMethod(this, fit, Qt::QueuedConnection);
        }
    } else {
        applyZoom();
    }

    // Stats.
    {
        auto* form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        if (stats.ldrawResolved > 0)
            form->addRow(tr("LDraw-rendered:"), new QLabel(QString::number(stats.ldrawResolved), this));
        if (stats.lddRendered > 0)
            form->addRow(tr("LDD-rendered:"),   new QLabel(QString::number(stats.lddRendered), this));
        if (stats.translated > 0)
            form->addRow(tr("LDD→LDraw mapped:"), new QLabel(QString::number(stats.translated), this));
        if (stats.unmapped > 0)
            form->addRow(tr("Unmapped refs:"),  new QLabel(QString::number(stats.unmapped), this));
        if (stats.skipped > 0)
            form->addRow(tr("Skipped (no .g):"), new QLabel(QString::number(stats.skipped), this));
        root->addLayout(form);
    }

    // Errors panel — collapsed by default, only shown when errors exist.
    if (!errors.isEmpty()) {
        auto* errLabel = new QLabel(tr("%1 warning(s)/error(s) during bake:").arg(errors.size()), this);
        root->addWidget(errLabel);
        auto* errEdit = new QPlainTextEdit(this);
        errEdit->setReadOnly(true);
        errEdit->setMaximumHeight(120);
        errEdit->setPlainText(errors.join(QChar('\n')));
        root->addWidget(errEdit);
    }

    // Name field + buttons.
    {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Save as:"), this));
        nameEdit_ = new QLineEdit(this);
        nameEdit_->setText(QFileInfo(sourceFile).completeBaseName());
        row->addWidget(nameEdit_, 1);
        root->addLayout(row);
    }
    {
        auto* bb = new QDialogButtonBox(this);
        // Custom labels: accept means "save it", reject means "abort".
        auto* acceptBtn = bb->addButton(tr("Save as Library Part"), QDialogButtonBox::AcceptRole);
        bb->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        // Enable Save only when the name is non-empty.
        connect(nameEdit_, &QLineEdit::textChanged, this,
                [acceptBtn](const QString& s){ acceptBtn->setEnabled(!s.trimmed().isEmpty()); });
        acceptBtn->setEnabled(!nameEdit_->text().trimmed().isEmpty());
        root->addWidget(bb);
    }
}

QString ImportPreviewDialog::partName() const {
    return nameEdit_ ? nameEdit_->text().trimmed() : QString();
}

bool ImportPreviewDialog::eventFilter(QObject* obj, QEvent* ev) {
    // Ctrl+wheel on the scroll viewport zooms; plain wheel falls
    // through to the QScrollArea so the user can scroll a zoomed
    // sprite.
    if (scroll_ && obj == scroll_->viewport() && ev->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(ev);
        if (we->modifiers().testFlag(Qt::ControlModifier)) {
            const int delta = we->angleDelta().y();
            if (delta != 0 && bumpZoom_) {
                bumpZoom_(delta > 0 ? 1.15 : 1.0 / 1.15);
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, ev);
}

}  // namespace cld::ui
