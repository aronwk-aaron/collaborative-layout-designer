#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

class QAction;

namespace cld::parts { class PartsLibrary; }

namespace cld::ui {

class MapView;
class LayerPanel;
class PartsBrowser;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(parts::PartsLibrary& parts, QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;

    // Rescans the parts library against all configured paths. Called at
    // startup and after the user edits the paths via onManageLibraries.
    void rescanLibrary(const QStringList& paths);

    // Load/save the user's custom library paths to QSettings. The vendored
    // submodule path is always prepended regardless of user state.
    QStringList loadUserLibraryPaths() const;
    void saveUserLibraryPaths(const QStringList& paths);

    QString defaultVendoredPartsRoot() const;

private slots:
    void onOpen();
    bool onSave();
    bool onSaveAs();
    void onZoomIn();
    void onZoomOut();
    void onFitToView();
    void onManageLibraries();

private:
    void setupMenus();
    void updateTitle();
    bool maybeSave();              // prompts on dirty close
    bool writeMapTo(const QString& path);

    parts::PartsLibrary& parts_;
    MapView*      mapView_     = nullptr;
    LayerPanel*   layerPanel_  = nullptr;
    PartsBrowser* partsBrowser_ = nullptr;
    class ModulesPanel* modulesPanel_ = nullptr;

    QString currentFilePath_;
    int     cleanUndoIndex_ = 0;   // index at which the stack is "clean"
    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;
};

}
