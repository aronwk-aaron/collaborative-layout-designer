#include "BudgetDialog.h"

#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace bld::ui {

namespace {

// Upstream .bbb schema (Budget.cs IXmlSerializable):
//   <Budget><Version/> <BudgetEntry><PartNumber/><Limit/></BudgetEntry>... </Budget>
// Limits unset in the file default to -1 (infinite).
constexpr int kBudgetVersion = 1;

}

BudgetDialog::BudgetDialog(core::Map& map, QWidget* parent)
    : QDialog(parent), map_(map) {
    setWindowTitle(tr("Budget"));
    resize(620, 500);

    auto* vbox = new QVBoxLayout(this);
    auto* btnRow = new QHBoxLayout();
    auto* newBtn    = new QPushButton(tr("New"), this);
    auto* openBtn   = new QPushButton(tr("Open..."), this);
    auto* saveBtn   = new QPushButton(tr("Save..."), this);
    auto* refreshBtn= new QPushButton(tr("Refresh from map"), this);
    btnRow->addWidget(newBtn);
    btnRow->addWidget(openBtn);
    btnRow->addWidget(saveBtn);
    btnRow->addStretch();
    btnRow->addWidget(refreshBtn);
    vbox->addLayout(btnRow);

    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({ tr("Part"), tr("Used"), tr("Limit (blank = unlimited)") });
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    vbox->addWidget(table_);

    statusLabel_ = new QLabel(this);
    vbox->addWidget(statusLabel_);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    vbox->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::close);

    connect(newBtn, &QPushButton::clicked, this, [this]{
        limits_.clear();
        activePath_.clear();
        rebuildTable();
    });
    connect(openBtn, &QPushButton::clicked, this, [this]{
        const QString p = QFileDialog::getOpenFileName(this, tr("Open budget"),
            activePath_, tr("BlueBrick budget (*.bbb);;All files (*)"));
        if (!p.isEmpty()) loadBudgetFile(p);
    });
    connect(saveBtn, &QPushButton::clicked, this, [this]{
        const QString p = QFileDialog::getSaveFileName(this, tr("Save budget"),
            activePath_.isEmpty() ? QStringLiteral("budget.bbb") : activePath_,
            tr("BlueBrick budget (*.bbb)"));
        if (!p.isEmpty()) saveBudgetFile(p);
    });
    connect(refreshBtn, &QPushButton::clicked, this, [this]{ rebuildTable(); });

    connect(table_, &QTableWidget::cellChanged, this, [this](int row, int col){
        if (col != 2) return;
        auto* keyItem = table_->item(row, 0);
        auto* valItem = table_->item(row, 2);
        if (!keyItem || !valItem) return;
        const QString part = keyItem->text();
        const QString txt  = valItem->text().trimmed();
        if (txt.isEmpty()) {
            limits_.remove(part);
        } else {
            bool ok = false;
            const int v = txt.toInt(&ok);
            if (ok && v >= 0) limits_[part] = v;
        }
        // Recolour the row without a full rebuild.
        const int used = usage_.value(part, 0);
        const int limit = limits_.value(part, -1);
        const QColor row_color = (limit >= 0 && used > limit) ? QColor(255, 210, 210) : QColor(Qt::white);
        for (int c = 0; c < table_->columnCount(); ++c)
            if (auto* it = table_->item(row, c)) it->setBackground(row_color);
    });

    // Preload the last-used budget if there is one.
    const QString last = QSettings().value(QStringLiteral("budget/lastFile")).toString();
    if (!last.isEmpty() && QFile::exists(last)) loadBudgetFile(last);
    else rebuildTable();
}

void BudgetDialog::rebuildTable() {
    // Recount from the current map.
    usage_.clear();
    for (const auto& L : map_.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
            usage_[b.partNumber]++;
        }
    }

    // Union of parts that appear in either usage or the budget so a
    // budgeted-but-not-placed part still shows up with used=0.
    QStringList parts;
    parts.reserve(usage_.size() + limits_.size());
    for (auto it = usage_.constBegin();  it != usage_.constEnd();  ++it)  parts << it.key();
    for (auto it = limits_.constBegin(); it != limits_.constEnd(); ++it)
        if (!parts.contains(it.key())) parts << it.key();
    std::sort(parts.begin(), parts.end());

    table_->blockSignals(true);
    table_->setRowCount(parts.size());
    int overBudget = 0;
    for (int i = 0; i < parts.size(); ++i) {
        const QString& part = parts[i];
        const int used = usage_.value(part, 0);
        const bool hasLimit = limits_.contains(part);
        const int limit = limits_.value(part, -1);

        auto* nameItem  = new QTableWidgetItem(part);
        auto* usedItem  = new QTableWidgetItem(QString::number(used));
        auto* limitItem = new QTableWidgetItem(hasLimit ? QString::number(limit) : QString());
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        usedItem->setFlags(usedItem->flags() & ~Qt::ItemIsEditable);

        const QColor rowColor = (hasLimit && used > limit)
            ? QColor(255, 210, 210) : QColor(Qt::white);
        for (auto* it : { nameItem, usedItem, limitItem }) it->setBackground(rowColor);
        if (hasLimit && used > limit) ++overBudget;

        table_->setItem(i, 0, nameItem);
        table_->setItem(i, 1, usedItem);
        table_->setItem(i, 2, limitItem);
    }
    table_->blockSignals(false);

    statusLabel_->setText(overBudget > 0
        ? tr("⚠ %1 part(s) over budget").arg(overBudget)
        : tr("All parts within budget"));
}

void BudgetDialog::loadBudgetFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Open budget"),
            tr("Cannot open %1: %2").arg(path, f.errorString()));
        return;
    }
    limits_.clear();
    QXmlStreamReader r(&f);
    while (r.readNextStartElement()) {
        if (r.name() == QStringLiteral("Budget")) {
            while (r.readNextStartElement()) {
                if (r.name() == QStringLiteral("BudgetEntry")) {
                    QString part; int limit = -1;
                    while (r.readNextStartElement()) {
                        if (r.name() == QStringLiteral("PartNumber")) part = r.readElementText();
                        else if (r.name() == QStringLiteral("Limit")) limit = r.readElementText().toInt();
                        else r.skipCurrentElement();
                    }
                    if (!part.isEmpty()) limits_[part] = limit;
                } else {
                    r.skipCurrentElement();
                }
            }
        } else {
            r.skipCurrentElement();
        }
    }
    activePath_ = path;
    QSettings().setValue(QStringLiteral("budget/lastFile"), path);
    setWindowTitle(tr("Budget — %1").arg(QFileInfo(path).fileName()));
    rebuildTable();
}

void BudgetDialog::saveBudgetFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save budget"),
            tr("Cannot write %1: %2").arg(path, f.errorString()));
        return;
    }
    QXmlStreamWriter w(&f);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("Budget"));
    w.writeTextElement(QStringLiteral("Version"), QString::number(kBudgetVersion));
    QStringList keys = limits_.keys(); std::sort(keys.begin(), keys.end());
    for (const QString& part : keys) {
        w.writeStartElement(QStringLiteral("BudgetEntry"));
        w.writeTextElement(QStringLiteral("PartNumber"), part);
        w.writeTextElement(QStringLiteral("Limit"), QString::number(limits_.value(part, -1)));
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndDocument();
    activePath_ = path;
    QSettings().setValue(QStringLiteral("budget/lastFile"), path);
    setWindowTitle(tr("Budget — %1").arg(QFileInfo(path).fileName()));
}

}
