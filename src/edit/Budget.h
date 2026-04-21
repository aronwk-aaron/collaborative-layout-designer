#pragma once

#include <QHash>
#include <QString>
#include <QVector>

namespace cld::core { class Map; }

namespace cld::edit {

// Key-value pair of part number → maximum allowed count. Read from a
// `.bbb` XML file (upstream BlueBrick's budget format).
using BudgetLimits = QHash<QString, int>;

// Load a BlueBrick-style `.bbb` file. Returns an empty hash on read error
// (missing file, malformed XML, zero budget entries — any failure is
// silent; callers treat empty as "no budget active"). Exactly matches
// the parser in BudgetDialog.cpp so the dialog and the status-bar
// readout agree.
BudgetLimits readBudgetFile(const QString& path);

// Save `limits` to `path` in the `.bbb` schema. Returns false on I/O
// error; the caller should surface the problem to the user.
bool writeBudgetFile(const QString& path, const BudgetLimits& limits);

// Count every brick by partNumber across every brick layer. Same
// method BudgetDialog uses internally.
QHash<QString, int> countPartUsage(const core::Map& map);

// One violation record. `overBy` is `used - limit` (always > 0 in the
// returned list — parts within budget aren't reported).
struct BudgetViolation {
    QString partNumber;
    int     used = 0;
    int     limit = 0;
    int     overBy = 0;
};

// Compare current map usage against the budget limits and return every
// part that's over its limit. Empty list = project is within budget
// (or the budget is empty, which is also fine).
QVector<BudgetViolation> checkBudget(const core::Map& map, const BudgetLimits& limits);

}  // namespace cld::edit
