#pragma once

#include <QByteArray>
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

inline bool terrainPaintReferenceMatches(const QByteArray& reference,
                                         const QByteArray& profileReference)
{
    if(reference.isEmpty())
        return false;
    if(reference == profileReference)
        return true;
    // The Astral update only appended a separate atlas. All original paint
    // sources are byte-identical and remain valid for saved map profiles.
    return reference == "a93378623cb2002a3168bf4d3fd3dd0bf8005db38fd6c746e6e4d25e064d55f9"
        && profileReference == "50c285225fb1745ee70d280e57a214f346481e6d380a78d55475f730d11a81b2";
}
