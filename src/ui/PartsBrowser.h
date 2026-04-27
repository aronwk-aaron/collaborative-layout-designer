#pragma once

#include <QDockWidget>
#include <QString>

class QComboBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace cld::parts { class PartsLibrary; }

namespace cld::ui {

// Dock panel showing parts in a thumbnail grid (QListView::IconMode).
// Top strip: category dropdown + live text filter. Double-click (or
// Enter) on a thumbnail activates the part.
class PartsBrowser : public QDockWidget {
    Q_OBJECT
public:
    explicit PartsBrowser(parts::PartsLibrary& lib, QWidget* parent = nullptr);

    void rebuild();   // re-read the library and repopulate

    // MIME type used when a thumbnail is dragged out of this panel. MapView
    // recognises the same string in its drop handler.
    static constexpr const char* kPartMimeType = "application/x-cld-part";

signals:
    void partActivated(const QString& key);

    // Emitted when the user deletes an imported part from disk via
    // the right-click menu. MainWindow listens to trigger a parts-
    // library rescan so the deleted entry stops appearing in the
    // grid.
    void partDeleted();

private:
    void applyFilter();
    QString categoryForPath(const QString& absPath) const;

    parts::PartsLibrary& lib_;
    QComboBox*    category_ = nullptr;
    QLineEdit*    filter_   = nullptr;
    QListWidget*  grid_     = nullptr;
};

}
