#include "../parts/PartsLibrary.h"
#include "../ui/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

int main(int argc, char** argv) {
    QApplication::setOrganizationName(QStringLiteral("CollaborativeLayoutDesigner"));
    QApplication::setApplicationName(QStringLiteral("Collaborative Layout Designer"));
    QApplication::setApplicationVersion(QStringLiteral("0.0.1"));

    QApplication app(argc, argv);

    // Localization scaffolding: load the user-selected UI language's .qm file
    // from <appDir>/translations/cld_<code>.qm if it exists, and the matching
    // Qt-provided generic translations (qtbase_<code>.qm). No-op until we
    // actually ship compiled .qm files, but the wiring is in place.
    static QTranslator appTranslator;
    static QTranslator qtTranslator;
    QString langCode = QSettings().value(QStringLiteral("general/language")).toString();
    if (langCode.isEmpty()) langCode = QLocale::system().name().split('_').value(0);
    if (!langCode.isEmpty() && langCode != QStringLiteral("en")) {
        const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
        if (appTranslator.load(QStringLiteral("cld_") + langCode, dir)) {
            QCoreApplication::installTranslator(&appTranslator);
        }
        if (qtTranslator.load(QStringLiteral("qtbase_") + langCode,
                              QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            QCoreApplication::installTranslator(&qtTranslator);
        }
    }

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
    // Guarantee a working canvas. If every load path above failed (first
    // run, missing lastFile, autosave declined), seed a blank doc so the
    // layer panel etc. operate on a real Map instead of silently no-op'ing.
    window.ensureDocument();

    return QApplication::exec();
}
