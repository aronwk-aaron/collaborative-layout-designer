#pragma once

#include <QDockWidget>
#include <QString>

class QLabel;
class QListWidget;
class QListWidgetItem;

namespace cld::ui {

// A simple library of module .bbm files on disk. Backed by a QSettings folder
// path ("modules/libraryPath"). Lists every .bbm in that folder; double-click
// (or the right-click "Import into map" action) emits moduleImportRequested
// with the full file path. MainWindow turns that into the existing
// ImportBbmAsModuleCommand flow, so saving a module here and loading it into
// another project is symmetric.
class ModuleLibraryPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit ModuleLibraryPanel(QWidget* parent = nullptr);

    QString libraryPath() const;
    void setLibraryPath(const QString& dir);
    void refresh();

signals:
    void moduleImportRequested(const QString& bbmPath);

private slots:
    void onChooseFolder();
    void onActivated(QListWidgetItem* item);

private:
    QLabel*      header_ = nullptr;
    QListWidget* list_   = nullptr;
    QString      path_;
};

}
