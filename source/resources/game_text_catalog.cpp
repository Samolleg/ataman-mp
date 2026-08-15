#include "game_text_catalog.h"
#include "res_archive_document.h"
#include <QDir>
#include <QFile>
#include <QTextCodec>

namespace {
QString findFile(const QString& root, const QString& wanted)
{
    const QFileInfoList files = QDir(root).entryInfoList(QDir::Files | QDir::Readable);
    for(const QFileInfo& file : files)
        if(file.fileName().compare(wanted, Qt::CaseInsensitive) == 0)
            return file.absoluteFilePath();
    return QString();
}
}
namespace ei {
SGameTextCatalog loadGameTextCatalog(const QStringList& orderedResourceRoots)
{
    SGameTextCatalog result;
    // Resource roots are ordered highest-precedence first. Compose from the
    // lowest layer upward so a sparse JMV texts.res overlays, rather than
    // erases, names that remain owned by Base.
    for(int rootIndex = orderedResourceRoots.size() - 1; rootIndex >= 0; --rootIndex)
    {
        const QString path = findFile(orderedResourceRoots.at(rootIndex), QStringLiteral("texts.res"));
        if(path.isEmpty()) continue;
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly))
        {
            result.diagnostics << QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
            continue;
        }
        CResArchiveDocument archive;
        QStringList errors;
        if(!archive.loadBytes(file.readAll(), path, &errors))
        {
            result.diagnostics += errors;
            continue;
        }
        QTextCodec* codec = QTextCodec::codecForName("CP1251");
        for(const SResArchiveMember& member : archive.members())
            result.entries.insert(member.name, codec ? codec->toUnicode(member.payload)
                                                      : QString::fromLocal8Bit(member.payload));
        result.sourceArchive = path;
    }
    if(result.entries.isEmpty())
        result.diagnostics << QStringLiteral("No readable texts.res was found in the active resource profile.");
    return result;
}
}
