#pragma once

#include <QDockWidget>

class QListWidget;

namespace cld::core { class Map; }

namespace cld::ui {

// List of modules (fork-only cross-layer groups). Right-click invokes Delete.
// moduleDeleteRequested is emitted with the module id so MainWindow can push
// an undoable DeleteModuleCommand.
class ModulesPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit ModulesPanel(QWidget* parent = nullptr);
    void setMap(const core::Map* map);

signals:
    void moduleDeleteRequested(const QString& moduleId);

private:
    QListWidget* list_ = nullptr;
};

}
