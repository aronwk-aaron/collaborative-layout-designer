#include "../ui/MainWindow.h"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication::setOrganizationName(QStringLiteral("CollaborativeLayoutDesigner"));
    QApplication::setApplicationName(QStringLiteral("Collaborative Layout Designer"));
    QApplication::setApplicationVersion(QStringLiteral("0.0.1"));

    QApplication app(argc, argv);

    cld::ui::MainWindow window;
    window.show();

    return QApplication::exec();
}
