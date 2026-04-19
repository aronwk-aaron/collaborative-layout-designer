#pragma once

#include <QDockWidget>

class QListWidget;

namespace cld::core { class Map; }

namespace cld::ui {

// Read-only list of modules (fork-only cross-layer groups). Phase 3.3 ships
// this as a status surface; edit operations (create from selection, import,
// flatten) land in a follow-up.
class ModulesPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit ModulesPanel(QWidget* parent = nullptr);
    void setMap(const core::Map* map);

private:
    QListWidget* list_ = nullptr;
};

}
