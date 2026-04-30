#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QLabel;

namespace bld::ui {

// Mirrors BlueBrick's two-step Download Center workflow:
//   Step 1 — LibraryPackageSourceForm: user ticks one or more sources
//            (Official, Non-LEGO, Custom URL), we fetch the HTML index
//            page for each and regex-parse `<a href="X.zip">X.zip</a>`
//            entries to build a candidate package list. Already-
//            installed packages (with a >= version in their About.txt)
//            are filtered out.
//   Step 2 — DownloadCenterForm: candidates appear in a checkbox list;
//            user selects which to install; we download each into a
//            temp file then unzip it into <libraryRoot>/. A library
//            rescan kicks off via the parent MainWindow on accept.
//
// All network work runs on the UI thread but is non-blocking from the
// user's POV — a QEventLoop drives reply completion while the dialog
// stays responsive. The download size cap (per-file) and overall sanity
// checks match what we used in the simpler URL-prompt version.
class DownloadCenterDialog : public QDialog {
    Q_OBJECT
public:
    // libraryRoot is where extracted packages should land; typically
    // the user's first configured library path or AppDataLocation/parts.
    explicit DownloadCenterDialog(QString libraryRoot, QWidget* parent = nullptr);

    // Set after exec() returns Accepted: list of (libraryRoot, count)
    // packages that downloaded + extracted successfully. Caller can
    // trigger a library rescan against libraryRoot.
    QString libraryRoot() const { return libraryRoot_; }
    int     installedCount() const { return installedCount_; }

private slots:
    void onSearchClicked();
    void onCustomToggled(bool on);
    void onDownloadClicked();

private:
    struct Package {
        QString name;          // basename without version, e.g. "LegoTrains"
        QString version;       // version part of filename (may be empty)
        QString sourceUrl;     // full http(s) URL to the .zip
        QString fileName;      // bare filename ("X.zip")
    };

    // Fetch one URL synchronously (with a per-request timeout) and
    // return its body as text. Sets `error` on failure, returns empty.
    QString fetchUrl(const QUrl& url, QString* error);

    // Walk libraryRoot_ for installed package folders; return a map
    // packageName -> version (from config/About.txt).
    QHash<QString, QString> readInstalledVersions() const;

    // Drop entries already installed at >= version.
    QVector<Package> filterAlreadyInstalled(QVector<Package> input) const;

    // Download one package's zip to tmp, extract into libraryRoot_/.
    // Returns true on success, sets `error` otherwise.
    bool downloadAndInstall(const Package& pkg, QString* error);

    QString          libraryRoot_;
    int              installedCount_ = 0;

    // ---- Source selection ----
    QCheckBox*  cbOfficial_   = nullptr;
    QCheckBox*  cbNonLego_    = nullptr;
    QCheckBox*  cbCustom_     = nullptr;
    QLineEdit*  customUrl_    = nullptr;
    QPushButton* searchBtn_   = nullptr;
    QLabel*     statusLabel_  = nullptr;

    // ---- Candidate list (populated after search) ----
    QListWidget* candidates_  = nullptr;
    QPushButton* downloadBtn_ = nullptr;
    QPushButton* closeBtn_    = nullptr;
    QProgressBar* progress_   = nullptr;
};

}  // namespace bld::ui
