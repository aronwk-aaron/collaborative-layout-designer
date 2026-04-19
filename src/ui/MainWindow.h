#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

class QAction;
class QComboBox;

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
    void onNew();
    void onOpen();
    bool onSave();
    bool onSaveAs();
    void onZoomIn();
    void onZoomOut();
    void onFitToView();
    void onManageLibraries();
    void onReloadLibrary();
    void onExportPartList();
    void onAbout();
    void onCreateModuleFromSelection();
    void onImportBbmAsModule();
    void onSaveSelectionAsModule();
    void onImportModuleFromLibraryPath(const QString& bbmPath);
    void rebuildRecentMenu();
    void pushRecentFile(const QString& path);

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
    class ModuleLibraryPanel* moduleLibraryPanel_ = nullptr;

    QString currentFilePath_;
    int     cleanUndoIndex_ = 0;   // index at which the stack is "clean"
    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;

    QComboBox* snapCombo_ = nullptr;
    QComboBox* rotCombo_  = nullptr;

    class QMenu* recentMenu_ = nullptr;

    // Auto-save: flushes the current map to a sidecar file every N seconds if
    // the undo stack is dirty. On startup, if an autosave file is newer than
    // the last-opened file, we offer to restore it (see restoreAutosaveIfAny).
    class QTimer* autosaveTimer_ = nullptr;
    void performAutosave();
public:
    // Returns the path where the autosave file lives for the current session
    // (AppDataLocation/autosave.bbm). Public so main.cpp can check on startup.
    static QString autosavePath();
    // Offers to restore the autosave if it exists and is newer than lastFile.
    // Called from main.cpp before openFile()ing the recent file. Returns true
    // if the user accepted the restore (in which case main.cpp should skip
    // reopening lastFile).
    bool restoreAutosaveIfAny(const QString& lastFile);
};

}
