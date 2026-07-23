#include <QApplication>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTableWidget>

#include "map_table_editor_dialog.h"

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName("EI Map Table Editor");
    CMapTableEditorDialog editor;
    if(argc >= 2 && QString::fromLocal8Bit(argv[1]) == "--ui-widget-check")
    {
        return editor.findChild<QListWidget*>("mapZoneList") &&
               editor.findChild<QLineEdit*>("mapMinimapTexture") &&
               editor.findChild<QTableWidget*>("mapEntranceTable") &&
               editor.findChild<QPlainTextEdit*>("mapRawReview")
            ? 0 : 2;
    }
    if(argc >= 2)
        editor.openArchive(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath());
    editor.show();
    return application.exec();
}
