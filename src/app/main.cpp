#include "../parts/PartsLibrary.h"
#include "../ui/MainWindow.h"

#include <QApplication>
#include <QDir>
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

    cld::parts::PartsLibrary lib;
    lib.addSearchPath(defaultPartsRoot());
    lib.scan();

    cld::ui::MainWindow window(lib);
    window.show();

    if (argc > 1) {
        window.openFile(QString::fromLocal8Bit(argv[1]));
    }

    return QApplication::exec();
}
