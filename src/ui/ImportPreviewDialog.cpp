#include "ImportPreviewDialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

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

    // Sprite preview. Scale to fit the available area while keeping
    // aspect; checkered background helps verify transparency.
    auto* spriteFrame = new QFrame(this);
    spriteFrame->setFrameShape(QFrame::StyledPanel);
    spriteFrame->setMinimumHeight(280);
    spriteFrame->setStyleSheet(QStringLiteral(
        "QFrame { background: "
        "qconicalgradient(cx:0.5, cy:0.5, angle:0, "
        "stop:0 #cccccc, stop:0.25 #ffffff, stop:0.5 #cccccc, "
        "stop:0.75 #ffffff, stop:1 #cccccc); }"));
    auto* spriteFrameLayout = new QVBoxLayout(spriteFrame);
    spriteFrameLayout->setContentsMargins(8, 8, 8, 8);
    auto* spriteLabel = new QLabel(spriteFrame);
    spriteLabel->setAlignment(Qt::AlignCenter);
    if (!sprite.isNull()) {
        // Display at native size when small enough; scale up modestly
        // for very small parts so the user can see what they're
        // accepting. Cap max render to 480x320 px.
        QPixmap pm = QPixmap::fromImage(sprite);
        const QSize target(std::min(640, sprite.width() * 4),
                            std::min(380, sprite.height() * 4));
        if (pm.width() > target.width() || pm.height() > target.height()) {
            pm = pm.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        } else if (pm.width() < 64 && pm.height() < 64) {
            pm = pm.scaled(pm.width() * 4, pm.height() * 4,
                            Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        spriteLabel->setPixmap(pm);
    } else {
        spriteLabel->setText(tr("(empty sprite)"));
    }
    spriteFrameLayout->addWidget(spriteLabel, 1);
    root->addWidget(spriteFrame, 1);

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

}  // namespace cld::ui
