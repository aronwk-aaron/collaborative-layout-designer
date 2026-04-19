#pragma once

#include <QDockWidget>
#include <QString>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace cld::parts { class PartsLibrary; }

namespace cld::ui {

// Dock panel listing all parts in the library, grouped by subfolder category,
// with a live filter field. Double-clicking a part emits partActivated(key),
// which MainWindow handles by pushing an AddBrickCommand at the view centre.
class PartsBrowser : public QDockWidget {
    Q_OBJECT
public:
    explicit PartsBrowser(parts::PartsLibrary& lib, QWidget* parent = nullptr);

    void rebuild();   // re-read the library and repopulate the tree

signals:
    void partActivated(const QString& key);

private:
    void applyFilter(const QString& text);
    QString categoryForPath(const QString& absPath) const;

    parts::PartsLibrary& lib_;
    QLineEdit* filter_ = nullptr;
    QTreeWidget* tree_ = nullptr;
};

}
