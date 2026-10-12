#include "verified_clipboard.h"

#include <QApplication>
#include <QCoreApplication>

int runNeighborPerformanceCheck();

int main(int argc,char** argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication app(argc,argv);
    ei::TestClipboardSession clipboard(true);
    if(!clipboard.prepared())return 2;
    if(!clipboard.install())return 2;
    QCoreApplication::setOrganizationName(QStringLiteral("Ataman151Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("crossmap-world-brush"));
    return runNeighborPerformanceCheck();
}
