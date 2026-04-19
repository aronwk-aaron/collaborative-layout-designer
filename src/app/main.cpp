#include "../parts/PartsLibrary.h"
#include "../ui/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString defaultPartsRoot() {
    // Preference order:
    //   1. Embedded submodule next to the executable (deployment default)
    //   2. Repo-relative fallback when running from a build tree
    //   3. User data directory (e.g. ~/.local/share/.../parts) for deploys
    const QString exeDir = QCoreApplication::applicationDirPath();
    for (const QString& rel : { QStringLiteral("/../../../parts/BlueBrickParts/parts"),
                                 QStringLiteral("/parts/BlueBrickParts/parts"),
                                 QStringLiteral("/BlueBrickParts/parts") }) {
        if (QDir(exeDir + rel).exists()) return exeDir + rel;
    }
    const QString userData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (QDir(userData + QStringLiteral("/parts")).exists()) {
        return userData + QStringLiteral("/parts");
    }
    return QDir(exeDir + QStringLiteral("/parts")).absolutePath();
}

}

int main(int argc, char** argv) {
    QApplication::setOrganizationName(QStringLiteral("CollaborativeLayoutDesigner"));
    QApplication::setApplicationName(QStringLiteral("Collaborative Layout Designer"));
    QApplication::setApplicationVersion(QStringLiteral("0.0.1"));

    QApplication app(argc, argv);

    // MainWindow scans the library during construction using paths from
    // QSettings (plus the vendored submodule if present); main only owns
    // the empty PartsLibrary.
    (void)defaultPartsRoot;  // kept for backward compat — unused now
    cld::parts::PartsLibrary lib;
    cld::ui::MainWindow window(lib);
    window.show();

    if (argc > 1) {
        window.openFile(QString::fromLocal8Bit(argv[1]));
    } else {
        // No file argument: reopen whatever the user had open last session —
        // but first check for a crash-recovery autosave.
        const QString last = QSettings().value(QStringLiteral("recent/lastFile")).toString();
        const bool restored = window.restoreAutosaveIfAny(last);
        if (!restored && !last.isEmpty() && QFile::exists(last)) {
            window.openFile(last);
        }
    }

    return QApplication::exec();
}
