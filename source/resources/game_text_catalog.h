#ifndef GAME_TEXT_CATALOG_H
#define GAME_TEXT_CATALOG_H
#include <QMap>
#include <QString>
#include <QStringList>
namespace ei {
struct SGameTextCatalog
{
    QMap<QString, QString> entries;
    QString sourceArchive;
    QStringList diagnostics;
};
SGameTextCatalog loadGameTextCatalog(const QStringList& orderedResourceRoots);
}
#endif
