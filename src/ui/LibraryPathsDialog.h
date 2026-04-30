#pragma once

#include <QDialog>
#include <QStringList>

class QListWidget;

namespace bld::ui {

// Manages the list of filesystem roots scanned by PartsLibrary. Backed by
// QSettings so choices persist across launches. User can add additional
// library roots (e.g. TrixBrix, BrickTracks community packs) alongside the
// vendored BlueBrickParts submodule.
class LibraryPathsDialog : public QDialog {
    Q_OBJECT
public:
    explicit LibraryPathsDialog(QStringList initial, QWidget* parent = nullptr);

    QStringList paths() const;

    // QSettings keys used for persistence (public so MainWindow can use them
    // at startup and also on reset).
    static constexpr const char* kSettingsGroup = "partsLibrary";
    static constexpr const char* kSettingsKey   = "userPaths";

private slots:
    void onAdd();
    void onRemove();
    void onMoveUp();
    void onMoveDown();

private:
    QListWidget* list_ = nullptr;
};

}
