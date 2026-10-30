#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>

// Never replace a usable explicit reference. Resolve the packaged fallback
// against the executable directory, independently of the current directory.
inline QString terrainReferencePath(const QString& configured,
                                    const QString& applicationDirectory)
{
    const QString path = configured.trimmed();
    if(!path.isEmpty() && QFileInfo(path).isFile())
        return QFileInfo(path).absoluteFilePath();
    const QString bundled = QDir(applicationDirectory).filePath(
        QStringLiteral("terrain-reference/HDPack_Terrain-JamaisVu135-50c285225fb1745e.res"));
    if(QFileInfo(bundled).isFile())
        return QFileInfo(bundled).absoluteFilePath();
    return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}
