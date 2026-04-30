#pragma once

#include <QDialog>
#include <QHash>
#include <QString>

class QLabel;
class QTableWidget;

namespace bld::core { class Map; }

namespace bld::ui {

// Minimal Budget MVP parity with upstream BudgetDialog: loads/saves a .bbb
// XML file (PartNumber -> limit), counts actual usage from the current map,
// and warns in red when usage exceeds the limit. This is intentionally
// scope-capped — full upstream flow (import/merge, filter library to
// budgeted only, enforcement at placement time) can land incrementally.
class BudgetDialog : public QDialog {
    Q_OBJECT
public:
    explicit BudgetDialog(core::Map& map, QWidget* parent = nullptr);

private:
    void rebuildTable();
    void loadBudgetFile(const QString& path);
    void saveBudgetFile(const QString& path);

    core::Map& map_;
    QHash<QString, int> limits_;
    QHash<QString, int> usage_;
    QString activePath_;
    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

}
