#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>

#include "map_table_editor_dialog.h"
#include "res_archive_document.h"

namespace {
bool loadArchive(const QString& path, CResArchiveDocument* archive)
{
    QFile file(path);
    QStringList errors;
    return archive && file.open(QIODevice::ReadOnly) &&
        archive->loadBytes(file.readAll(), path, &errors);
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName("EI Map Table Editor");
    if(argc >= 4 && QString::fromLocal8Bit(argv[1]) == "--validate-derived-res")
    {
        CResArchiveDocument baseline;
        CResArchiveDocument candidate;
        const QString baselinePath = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
        const QString candidatePath = QFileInfo(QString::fromLocal8Bit(argv[3])).absoluteFilePath();
        QStringList errors;
        if(!loadArchive(baselinePath, &baseline) || !loadArchive(candidatePath, &candidate))
            return 3;
        return CResArchiveDocument::validateReplacement(
            baseline, candidate, "map.txt", candidate.memberPayload("map.txt"), &errors) ? 0 : 4;
    }
    CMapTableEditorDialog editor;
    if(argc >= 3 && QString::fromLocal8Bit(argv[1]) == "--validate-archive")
        return editor.openArchive(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath())
            ? 0 : 3;
    if(argc >= 2 && QString::fromLocal8Bit(argv[1]) == "--ui-widget-check")
    {
        return editor.findChild<QListWidget*>("mapZoneList") &&
               editor.findChild<QLineEdit*>("mapMinimapTexture") &&
               editor.findChild<QTableWidget*>("mapEntranceTable") &&
               editor.findChild<QPlainTextEdit*>("mapRawReview") &&
               editor.findChild<QPushButton*>("mapImportText") &&
               editor.findChild<QPushButton*>("mapExportText")
            ? 0 : 2;
    }
    if(argc >= 2)
        editor.openArchive(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath());
    editor.show();
    return application.exec();
}
