#include "resourcemanager.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include "mmp_codec.h"
#include <QDebug>
#include <QMessageBox>
#include <QtMath>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextCodec>
#include <QCryptographicHash>
#include <QHash>
#include <limits>

#include "res_file.h"
#include "utils.h"
#include "view.h"
#include "types.h"
#include "settings.h"
#include "log.h"
#include "resource_name.h"
#include "unit_appearance.h"
#include "tile.h"

CObjectList* CObjectList::m_pObjectContainer = nullptr;
CTextureList* CTextureList::m_pTextureContainer = nullptr;
CSessionDataManager* CSessionDataManager::m_pSessionDataManger = nullptr;

namespace {
ERenderMaterialClass classifyDxt1Alpha(const QByteArray& data, int offset, int width, int height)
{
    const int blocks = qMax(1, (width + 3) / 4) * qMax(1, (height + 3) / 4);
    for(int block = 0; block < blocks && offset + 8 <= data.size(); ++block, offset += 8)
    {
        const uchar* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
        const quint16 color0 = quint16(bytes[0]) | (quint16(bytes[1]) << 8);
        const quint16 color1 = quint16(bytes[2]) | (quint16(bytes[3]) << 8);
        if(color0 > color1)
            continue;
        const quint32 indices = quint32(bytes[4]) | (quint32(bytes[5]) << 8)
            | (quint32(bytes[6]) << 16) | (quint32(bytes[7]) << 24);
        for(int pixel = 0; pixel < 16; ++pixel)
            if(((indices >> (pixel * 2)) & 3u) == 3u)
                return ERenderMaterialClass::Cutout;
    }
    return ERenderMaterialClass::Opaque;
}

ERenderMaterialClass classifyDxt3Alpha(const QByteArray& data, int offset, int width, int height)
{
    bool hasZero = false;
    const int blocks = qMax(1, (width + 3) / 4) * qMax(1, (height + 3) / 4);
    for(int block = 0; block < blocks && offset + 16 <= data.size(); ++block, offset += 16)
    {
        for(int nibble = 0; nibble < 16; ++nibble)
        {
            const uchar byte = uchar(data.at(offset + nibble / 2));
            const int alpha = nibble & 1 ? byte >> 4 : byte & 15;
            if(alpha > 0 && alpha < 15)
                return ERenderMaterialClass::Transparent;
            hasZero = hasZero || alpha == 0;
        }
    }
    return hasZero ? ERenderMaterialClass::Cutout : ERenderMaterialClass::Opaque;
}

ERenderMaterialClass classifyDxt5Alpha(const QByteArray& data, int offset, int width, int height)
{
    bool hasZero = false;
    const int blocks = qMax(1, (width + 3) / 4) * qMax(1, (height + 3) / 4);
    for(int block = 0; block < blocks && offset + 16 <= data.size(); ++block, offset += 16)
    {
        const uchar* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
        int palette[8] = {bytes[0], bytes[1], 0, 0, 0, 0, 0, 0};
        if(palette[0] > palette[1])
            for(int i = 2; i < 8; ++i)
                palette[i] = ((8 - i) * palette[0] + (i - 1) * palette[1]) / 7;
        else
        {
            for(int i = 2; i < 6; ++i)
                palette[i] = ((6 - i) * palette[0] + (i - 1) * palette[1]) / 5;
            palette[6] = 0;
            palette[7] = 255;
        }
        quint64 indices = 0;
        for(int byte = 0; byte < 6; ++byte)
            indices |= quint64(bytes[2 + byte]) << (byte * 8);
        for(int pixel = 0; pixel < 16; ++pixel)
        {
            const int alpha = palette[(indices >> (pixel * 3)) & 7u];
            if(alpha > 0 && alpha < 255)
                return ERenderMaterialClass::Transparent;
            hasZero = hasZero || alpha == 0;
        }
    }
    return hasZero ? ERenderMaterialClass::Cutout : ERenderMaterialClass::Opaque;
}

struct SCachedArchiveIndex
{
    qint64 size{-1};
    QDateTime modified;
    QMap<QString, SResFileEntry> entries;
};

const SCachedArchiveIndex& archiveIndex(const QString& path)
{
    static QMap<QString, SCachedArchiveIndex> cache;
    const QFileInfo info(path);
    SCachedArchiveIndex& cached = cache[path];
    if(cached.size == info.size() && cached.modified == info.lastModified())
        return cached;

    cached = SCachedArchiveIndex();
    cached.size = info.size();
    cached.modified = info.lastModified();
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return cached;

    QDataStream stream(&file);
    util::formatStream(stream);
    SResFileHeader header;
    stream >> header;
    if(header.Signature != 0x019CE23C || header.TableSize > 10000000)
        return cached;

    stream.device()->seek(header.TableOffset);
    QVector<SResHashTable> table;
    table.resize(int(header.TableSize));
    for(auto& entry : table)
        stream >> entry;
    const QByteArray names = stream.device()->read(header.NamesLenght);
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    for(const auto& raw : table)
    {
        if(raw.NameOffset + raw.NameLength > uint(names.size()) || raw.DataOffset + raw.DataSize > quint64(file.size()))
            continue;
        SResFileEntry entry;
        entry.FileName = codec->toUnicode(names.mid(int(raw.NameOffset), int(raw.NameLength)));
        entry.Position = int(raw.DataOffset);
        entry.Size = int(raw.DataSize);
        entry.LastWriteTime = QDateTime::fromTime_t(raw.LastWriteTime);
        cached.entries.insert(entry.FileName.toLower(), entry);
    }
    ei::log(eLogInfo, QString("Indexed RES table: %1 entries:%2").arg(path).arg(cached.entries.size()));
    return cached;
}

bool readIndexedResource(const QVector<QString>& sources, const QString& requestedName, QByteArray& output, QString* sourcePath = nullptr)
{
    const QString normalized = requestedName.toLower();
    for(const QString& path : sources)
    {
        const auto& index = archiveIndex(path);
        auto found = index.entries.constFind(normalized);
        if(found == index.entries.constEnd())
            continue;
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly) || !file.seek(found->Position))
            continue;
        output = file.read(found->Size);
        if(output.size() != found->Size)
            continue;
        if(sourcePath)
            *sourcePath = path;
        return true;
    }
    return false;
}

QStringList indexedNames(const QVector<QString>& sources, const QString& suffix)
{
    QSet<QString> seen;
    QStringList result;
    for(const QString& path : sources)
    {
        const auto& index = archiveIndex(path);
        for(auto it = index.entries.constBegin(); it != index.entries.constEnd(); ++it)
        {
            if(!it.key().endsWith(suffix) || seen.contains(it.key()))
                continue;
            seen.insert(it.key());
            result.append(it.key());
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QMap<QString, QByteArray> normalizedResourceMap(const QMap<QString, QByteArray>& source)
{
    QMap<QString, QByteArray> result;
    for(auto it = source.constBegin(); it != source.constEnd(); ++it)
        result.insert(it.key().toLower(), it.value());
    return result;
}

QString resourceSourceFingerprint(const QVector<QString>& sources)
{
    QString result;
    for(const QString& path : sources)
    {
        const QFileInfo info(path);
        result += info.absoluteFilePath().toLower() + '|'
                + QString::number(info.size()) + '|'
                + QString::number(info.lastModified().toMSecsSinceEpoch()) + '\n';
    }
    return result;
}

bool readPreferredUnitDatabase(const QVector<QString>& sources, QByteArray& unitsDatabase,
                               QString* sourcePath)
{
    // Mod packages sometimes keep an original-unmodified database beside the
    // active database.res. Prefer the active conventional name in each source
    // tier so an alphabetically earlier backup cannot silently define unit
    // appearances.
    for(const QString& path : sources)
    {
        if(QFileInfo(path).fileName().compare("database.res", Qt::CaseInsensitive) != 0)
            continue;
        if(readIndexedResource({path}, "units.udb", unitsDatabase, sourcePath))
            return true;
    }
    for(const QString& path : sources)
    {
        const QString name = QFileInfo(path).fileName().toLower();
        if(name.contains("backup") || name.contains("original")
                || name.contains("unmodified") || name.contains(".old"))
            continue;
        if(readIndexedResource({path}, "units.udb", unitsDatabase, sourcePath))
            return true;
    }
    return readIndexedResource(sources, "units.udb", unitsDatabase, sourcePath);
}

QString databaseHumanoidTexture(const QVector<QString>& sources, const QString& prototypeName,
                                const QString& modelName, QString* databaseSource)
{
    static QMap<QString, QMap<QString, int>> skinIndexCache;
    static QMap<QString, QString> databaseSourceCache;
    const QString fingerprint = resourceSourceFingerprint(sources);
    if(!skinIndexCache.contains(fingerprint))
    {
        QByteArray unitsDatabase;
        QString source;
        if(readPreferredUnitDatabase(sources, unitsDatabase, &source))
        {
            skinIndexCache.insert(fingerprint, ei::unitPrototypeSkinIndices(unitsDatabase));
            databaseSourceCache.insert(fingerprint, source);
            ei::log(eLogInfo, QString("Indexed unit appearances: %1 prototypes from %2")
                    .arg(skinIndexCache.value(fingerprint).size()).arg(source));
        }
        else
        {
            skinIndexCache.insert(fingerprint, QMap<QString, int>());
            ei::log(eLogWarning, "No units.udb found in configured resource sources");
        }
    }
    if(databaseSource)
        *databaseSource = databaseSourceCache.value(fingerprint);
    const auto cached = skinIndexCache.constFind(fingerprint);
    return ei::humanoidTextureResourceName(cached.value(), prototypeName, modelName);
}

QVector<QString> configuredResourceSources(CSettings* settings)
{
    QVector<QString> explicitSources;
    if(settings)
    {
        auto roots = dynamic_cast<COptStringList*>(settings->opt(eOptSetResource, "resourceRoots"));
        if(roots && !roots->value().isEmpty())
        {
            explicitSources = roots->value();
        }
        else
        {
            for(QString optionName : {QString("figPaths"), QString("texPaths")})
            {
                auto option = dynamic_cast<COptStringList*>(settings->opt(eOptSetResource, optionName));
                if(option)
                    explicitSources.append(option->value());
            }
        }
    }

    static QMap<QString, QVector<QString>> cache;
    QString cacheKey;
    for(const QString& source : explicitSources)
        cacheKey += source + '\n';
    if(cache.contains(cacheKey))
        return cache.value(cacheKey);

    QVector<QString> directoryOrder;
    QMap<QString, QVector<QString>> explicitByDirectory;
    for(const QString& source : explicitSources)
    {
        const QFileInfo info(source);
        const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        const QString directoryKey = QDir::cleanPath(directory).toLower();
        if(!explicitByDirectory.contains(directoryKey))
            directoryOrder.append(directory);
        if(info.isFile())
            explicitByDirectory[directoryKey].append(info.absoluteFilePath());
    }

    QVector<QString> result;
    QSet<QString> seen;
    auto appendUnique = [&result, &seen](const QString& path)
    {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        const QString key = QDir::cleanPath(absolute).toLower();
        if(seen.contains(key))
            return;
        seen.insert(key);
        result.append(absolute);
    };

    for(const QString& directory : directoryOrder)
    {
        const QString directoryKey = QDir::cleanPath(directory).toLower();
        for(const QString& source : explicitByDirectory.value(directoryKey))
            appendUnique(source);

        const QDir dir(directory);
        for(const QFileInfo& archive : dir.entryInfoList({"*.res"}, QDir::Files, QDir::Name | QDir::IgnoreCase))
            appendUnique(archive.absoluteFilePath());
    }

    cache.insert(cacheKey, result);
    ei::log(eLogInfo, QString("Resource catalog expanded %1 configured archives to %2 generic sources")
            .arg(explicitSources.size()).arg(result.size()));
    return result;
}

int dxtLevelSize(int width, int height, int blockBytes)
{
    return qMax(1, (width + 3) / 4) * qMax(1, (height + 3) / 4) * blockBytes;
}
}

CObjectList *CObjectList::getInstance()
{
    if(nullptr == m_pObjectContainer)
        m_pObjectContainer = new CObjectList();
    return m_pObjectContainer;
}

CObjectList::CObjectList():
    m_pSettings(nullptr)
{
    //read figures for aux objects.
    //TODO: can be conflict with user model name. load aux figures in separate map
    auto auxFile = QFileInfo(":/auxData.res");

    CResFile res(auxFile.filePath());
    QMap<QString, QByteArray> aFile = res.bufferOfFiles();

    for(auto file : aFile.toStdMap())
    {
        if (file.first.toLower().endsWith(".mod"))
            readAssembly(aFile, file.first);
    }
    ei::log(eLogInfo, "aux objects loaded");
}

CObjectList::~CObjectList()
{
    ei::CFigure* pFig = nullptr;
    foreach (pFig, m_aFigure)
        pFig->~CFigure();
}


void CObjectList::readFigure(const QByteArray& file, const QString& name)
{
    QDataStream stream(file);
    util::formatStream(stream);
    ei::CFigure* fig = new ei::CFigure;
    fig->readData(stream);
    fig->setName(name);
    m_aFigure.insert(name, fig);
    //addItem(name);
}

//read *.mod files, create figure hierarchy with part offsets
//in: aFile, assemblyRoot
void CObjectList::readAssembly(const QMap<QString, QByteArray>& aFile, const QString& assemblyRoot)
{
    const QString normalizedRoot = assemblyRoot.toLower();
    const QMap<QString, QByteArray> normalizedFiles = normalizedResourceMap(aFile);
    const QByteArray modelData = normalizedFiles.value(normalizedRoot);
    if(modelData.isEmpty())
    {
        ei::log(eLogWarning, "Assembly archive is missing: " + normalizedRoot);
        return;
    }

    CResFile model(modelData);
    QMap<QString, QByteArray> aComponent = normalizedResourceMap(model.bufferOfFiles());
    const QString linkName = normalizedRoot.split(".mod").first();
    const QByteArray linkData = aComponent.value(linkName);
    if(linkData.isEmpty())
    {
        ei::log(eLogWarning, "Assembly link table is missing: " + normalizedRoot);
        return;
    }

    QDataStream lnkStream(linkData);
    util::formatStream(lnkStream);
    int nLink;
    lnkStream >> nLink;
    if(lnkStream.status() != QDataStream::Ok || nLink <= 0 || nLink > 10000)
    {
        ei::log(eLogWarning, "Invalid assembly link count: " + normalizedRoot);
        return;
    }

    struct SAssemblyLink
    {
        QString componentName;
        QString parentName;
        ei::CFigure* figure = nullptr;
    };

    auto readLinkName = [&lnkStream](QString& result, bool allowEmpty) -> bool
    {
        int length = 0;
        lnkStream >> length;
        if(lnkStream.status() != QDataStream::Ok || length < 0 || length > 4096 || (!allowEmpty && length == 0))
            return false;
        if(length == 0)
        {
            result.clear();
            return true;
        }

        QByteArray raw(length, Qt::Uninitialized);
        if(lnkStream.readRawData(raw.data(), length) != length)
            return false;
        while(!raw.isEmpty() && raw.endsWith('\0'))
            raw.chop(1);
        result = QString::fromLatin1(raw).toLower();
        return !result.isEmpty() || allowEmpty;
    };

    QVector<SAssemblyLink> links;
    links.reserve(nLink);
    QMap<QString, ei::CFigure*> aParent;
    for (int i(0); i<nLink; ++i)
    {
        SAssemblyLink link;
        if(!readLinkName(link.componentName, false) || !readLinkName(link.parentName, true))
        {
            ei::log(eLogWarning, QString("Invalid assembly link %1 in %2").arg(i).arg(normalizedRoot));
            break;
        }

        const QByteArray componentData = aComponent.value(link.componentName);
        if(componentData.isEmpty())
        {
            ei::log(eLogWarning, QString("Assembly %1 is missing component %2").arg(normalizedRoot, link.componentName));
            continue;
        }

        QDataStream compStream(componentData);
        util::formatStream(compStream);
        link.figure = new ei::CFigure;
        if(!link.figure->readData(compStream))
        {
            ei::log(eLogWarning, QString("Assembly %1 has an invalid component %2").arg(normalizedRoot, link.componentName));
            delete link.figure;
            continue;
        }
        link.figure->setName(link.componentName);
        aParent.insert(link.componentName, link.figure);
        links.append(link);
    }

    ei::CFigure* rootFigure = nullptr;
    for(const SAssemblyLink& link : links)
    {
        if(!link.parentName.isEmpty())
            continue;
        if(!rootFigure)
            rootFigure = link.figure;
        else
            rootFigure->addChild(link.figure);
    }

    for(const SAssemblyLink& link : links)
    {
        if(link.parentName.isEmpty())
            continue;

        ei::CFigure* parentFigure = aParent.value(link.parentName, nullptr);
        if(parentFigure)
            parentFigure->addChild(link.figure);
        else
        {
            ei::log(eLogWarning, QString("Assembly %1 component %2 references missing parent %3")
                    .arg(normalizedRoot, link.componentName, link.parentName));
            if(rootFigure)
                rootFigure->addChild(link.figure);
        }
    }

    if(!rootFigure)
    {
        ei::log(eLogWarning, "Assembly has no usable root: " + normalizedRoot);
        return;
    }
    m_aFigure.insert(normalizedRoot, rootFigure);

    //.bon file parse here
    QString bonFile(linkName);
    bonFile.append(".bon");
    const QByteArray positionData = normalizedFiles.value(bonFile);
    if(!positionData.isEmpty())
    {
        CResFile position(positionData);
        aComponent = normalizedResourceMap(position.bufferOfFiles());
        for (auto& fig: aParent.values())
        {
            const QByteArray offsetData = aComponent.value(fig->name().toLower());
            if(offsetData.isEmpty())
                continue;
            QDataStream bonStream(offsetData);
            util::formatStream(bonStream);
            fig->readAssemblyOffset(bonStream);
        }
    }
    rootFigure->applyAssemblyOffset();
}

void CObjectList::loadFigures(QSet<QString>& aFigure)
{
    const QVector<QString> sources = configuredResourceSources(m_pSettings);
    if(sources.isEmpty())
    {
        ei::log(eLogWarning, "No figure resources configured. Use Settings > Game and Mod Setup, or the advanced legacy resource settings.");
        return;
    }

    if(aFigure.isEmpty())
    {
        m_arrFigureForComboBox.clear();
        for(const QString& name : indexedNames(sources, ".mod"))
            m_arrFigureForComboBox.append(name.left(name.size() - 4));
        return;
    }

    for(const auto& requestedFig : aFigure)
    {
        const QString fig = requestedFig.toLower();
        if(m_aFigure.contains(fig) || m_missingFigureNames.contains(fig))
            continue;
        QByteArray figureData;
        if(!readIndexedResource(sources, fig, figureData))
        {
            m_missingFigureNames.insert(fig);
            ei::log(eLogWarning, "Missing figure (negative cached): " + fig);
            continue;
        }

        if(fig.endsWith(".mod"))
        {
            const QString bonName = fig.left(fig.size() - 4) + ".bon";
            QByteArray bonData;
            QMap<QString, QByteArray> assemblyFiles;
            assemblyFiles.insert(fig, figureData);
            if(readIndexedResource(sources, bonName, bonData))
                assemblyFiles.insert(bonName, bonData);
            readAssembly(assemblyFiles, fig);
        }
        else if(fig.endsWith(".fig"))
            readFigure(figureData, fig);
    }
}

ei::CFigure* CObjectList::getFigure(const QString& name)
{
    QString figureName = name.toLower() + ".mod";
    if(m_missingFigureNames.contains(figureName))
        return figureDefault();
    if(!m_aFigure.contains(figureName))
    {
        QSet<QString> figure;
        figure.insert(figureName);
        loadFigures(figure);
    }

    return m_aFigure.contains(figureName) ? m_aFigure[figureName] : figureDefault();
}

QStringList CObjectList::figurePartNames(const QString& name)
{
    QStringList result;
    ei::CFigure* figure = getFigure(name);
    if(figure)
        figure->getPartNames(result);
    return result;
}

QByteArray CObjectList::modelAnimation(const QString& name)
{
    const QString member = name.toLower() + ".anm";
    if(m_animationArchives.contains(member)) return m_animationArchives.value(member);
    if(m_missingAnimationNames.contains(member)) return {};
    QByteArray bytes;
    if(!readIndexedResource(configuredResourceSources(m_pSettings), member, bytes))
    {
        m_missingAnimationNames.insert(member);
        ei::log(eLogWarning, "Missing model animation archive: " + member);
        return {};
    }
    m_animationArchives.insert(member, bytes);
    return bytes;
}

QByteArray CObjectList::resourceData(const QString& name, QString* sourcePath) const
{
    QByteArray bytes;
    readIndexedResource(configuredResourceSources(m_pSettings), name, bytes, sourcePath);
    return bytes;
}

void CObjectList::initResource()
{
    QSet<QString> empty;
    loadFigures(empty);
}

void CObjectList::refreshConfiguredSources()
{
    m_missingFigureNames.clear();
    m_animationArchives.clear();
    m_missingAnimationNames.clear();
    QSet<QString> empty;
    loadFigures(empty);
}

ei::CFigure *CObjectList::figureDefault()
{
    const QString fallbackName("cannotdisplay.mod");
    ei::CFigure* fallback = m_aFigure.value(fallbackName, nullptr);
    if(!fallback)
        ei::log(eLogWarning, "Default placeholder figure is unavailable: " + fallbackName);
    return fallback;
}


CTextureList *CTextureList::getInstance()
{
    if(nullptr == m_pTextureContainer)
        m_pTextureContainer = new CTextureList();
    return m_pTextureContainer;
}

CTextureList::CTextureList():
    m_pSettings(nullptr)
{
}

CTextureList::~CTextureList()
{
//    for(auto& tex : m_aTexture)
//    {
//      //error with destroying :(
//        tex->destroy();
//    }
    m_aTexture.clear();
}



uchar extractColor(uint pixel, uint mask, uint shift)
{
    return uchar(0.5 + 255 * ((pixel & mask) >> shift) / (mask >> shift));
}

bool decompressPnt3(QImage& image, const QByteArray& data, uint compressedSize,
                    QString* diagnostic)
{
    const int offset = 76;
    if(compressedSize > uint(qMax(0, data.size() - offset)))
    {
        if(diagnostic) *diagnostic = "PNT3 compressed size exceeds its MMP payload.";
        return false;
    }
    // The original surface unpacker writes PNT3 pixels sequentially into the
    // destination atlas. Preserve that row-major order; atlas placement comes
    // from transparent run markers, not an image-space rotation or UV fixup.
    return ei::decodePnt3Image(data.mid(offset, int(compressedSize)),
                              image.width(), image.height(), &image, diagnostic);
}


//in. data - texture byte data
//in. name - texture name
void CTextureList::parse(QByteArray& data, const QString& name)
{
    QDataStream stream(data);
    util::formatStream(stream);

    SMmpHeader header;
    stream >> header;
    if(header.m_signature != 0x00504D4D)
    {
        Q_ASSERT("incorrect texture signature" && false);
        return;
    }

    // SMmpHeader serializes a fixed 76-byte header.  The uncompressed and
    // PNT3 decoders consume QDataStream directly, so leave it at the payload
    // boundary rather than immediately after the fields read by operator>>.
    // Compressed DXT uploads below use explicit byte offsets and are unchanged.
    stream.device()->seek(header.size());

    const QOpenGLContext* currentContext = QOpenGLContext::currentContext();
    const bool openGlesCompatibility = currentContext && currentContext->isOpenGLES();
    const int uploadedMipCount = openGlesCompatibility
        ? 1 : qMax(1, header.m_mipcount);
    QOpenGLTexture* texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    // GL_TEXTURE_MAG_FILTER only accepts NEAREST or LINEAR.  The historical
    // mipmapped value is rejected by ANGLE and poisoned all ordinary MMP
    // texture setup, while the separately composed humanoid RGBA path stayed
    // visible.  ES2 also requires a complete mip chain when a mip filter is
    // selected; shipped MMPs commonly stop at their last block-safe DXT level.
    texture->setMinificationFilter(openGlesCompatibility
        ? QOpenGLTexture::Nearest : QOpenGLTexture::NearestMipMapNearest);
    // Preserve the native renderer's smooth sky/model sampling. Only the
    // mipmapped MAG value was invalid; replacing it globally with NEAREST
    // made the cylindrical sky visibly pixelated on desktop GL.
    texture->setMagnificationFilter(QOpenGLTexture::Linear);
    texture->setWrapMode(QOpenGLTexture::Repeat);
    ERenderMaterialClass materialClass = ERenderMaterialClass::Conservative;
    switch (header.m_format)
    {
    case ETextureFormat::eMMP_DXT1:
    {
        materialClass = classifyDxt1Alpha(data, header.size(), header.m_width, header.m_height);
        if(openGlesCompatibility)
        {
            QImage image;
            QString diagnostic;
            const int size = dxtLevelSize(header.m_width, header.m_height, 8);
            if(!ei::decodeDxtImage(data.mid(header.size(), size), header.m_width,
                                   header.m_height, ei::DxtFormat::Dxt1,
                                   &image, &diagnostic))
            {
                ei::log(eLogWarning, "Invalid ANGLE DXT1 texture " + name + ": " + diagnostic);
                delete texture;
                return;
            }
            texture->setData(image);
            break;
        }
        texture->setFormat(QOpenGLTexture::TextureFormat::RGBA_DXT1);
        texture->setSize(int(header.m_width), int(header.m_height));
        texture->setMipLevels(uploadedMipCount);
        texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
        int offset = header.size();
        for(int i(0); i<uploadedMipCount; ++i)
        {
            const int size = dxtLevelSize(qMax(1, header.m_width >> i), qMax(1, header.m_height >> i), 8);
            texture->setCompressedData(i, 0, size, data.data() + offset);
            offset += size;
        }
        break;
    }
    case ETextureFormat::eMMP_DXT2:
    case ETextureFormat::eMMP_DXT3:
    {
        materialClass = classifyDxt3Alpha(data, header.size(), header.m_width, header.m_height);
        if(openGlesCompatibility)
        {
            QImage image;
            QString diagnostic;
            const int size = dxtLevelSize(header.m_width, header.m_height, 16);
            if(!ei::decodeDxtImage(data.mid(header.size(), size), header.m_width,
                                   header.m_height, ei::DxtFormat::Dxt3,
                                   &image, &diagnostic))
            {
                ei::log(eLogWarning, "Invalid ANGLE DXT3 texture " + name + ": " + diagnostic);
                delete texture;
                return;
            }
            texture->setData(image);
            break;
        }
        texture->setFormat(QOpenGLTexture::TextureFormat::RGBA_DXT3);
        texture->setSize(int(header.m_width), int(header.m_height));
        texture->setMipLevels(uploadedMipCount);
        texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
        int offset = header.size();
        for(int i(0); i<uploadedMipCount; ++i)
        {
            const int size = dxtLevelSize(qMax(1, header.m_width >> i), qMax(1, header.m_height >> i), 16);
            texture->setCompressedData(i, 0, size, data.data() + offset);
            offset += size;
        }
        break;
    }
    case ETextureFormat::eMMP_DXT4:
    case ETextureFormat::eMMP_DXT5:
    {
        materialClass = classifyDxt5Alpha(data, header.size(), header.m_width, header.m_height);
        if(openGlesCompatibility)
        {
            QImage image;
            QString diagnostic;
            const int size = dxtLevelSize(header.m_width, header.m_height, 16);
            if(!ei::decodeDxtImage(data.mid(header.size(), size), header.m_width,
                                   header.m_height, ei::DxtFormat::Dxt5,
                                   &image, &diagnostic))
            {
                ei::log(eLogWarning, "Invalid ANGLE DXT5 texture " + name + ": " + diagnostic);
                delete texture;
                return;
            }
            texture->setData(image);
            break;
        }
        texture->setFormat(QOpenGLTexture::TextureFormat::RGBA_DXT5);
        texture->setSize(int(header.m_width), int(header.m_height));
        texture->setMipLevels(uploadedMipCount);
        texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
        int offset = header.size();
        for(int i(0); i<uploadedMipCount; ++i)
        {
            const int size = dxtLevelSize(qMax(1, header.m_width >> i), qMax(1, header.m_height >> i), 16);
            texture->setCompressedData(i, 0, size, data.data() + offset);
            offset += size;
        }
        break;
    }
    case ETextureFormat::eMMP_5650    : // use the same direct draw reading algorithm
    case ETextureFormat::eMMP_5551    : // use the same direct draw reading algorithm
    case ETextureFormat::eMMP_4444    : // use the same direct draw reading algorithm
    case ETextureFormat::eMMP_8888    : // use the same direct draw reading algorithm
    {
        // Unlike DXT and PNT3, the DirectDraw formats keep a 56-byte pixel
        // descriptor between the five common MMP header dwords and the pixel
        // payload. Seeking
        // to byte 76 here made the first pixels masquerade as format masks and
        // could decode an otherwise valid 5551 texture as fully transparent
        // (JMV bat02 is a shipped example).
        QImage img;
        QString diagnostic;
        if(!ei::decodeDirectDrawImage(data.mid(5 * int(sizeof(quint32))),
                                      header.m_width, header.m_height, &img, &diagnostic))
        {
            ei::log(eLogWarning, "Invalid DirectDraw texture " + name + ": " + diagnostic);
            delete texture;
            return;
        }
        texture->setData(img);
        materialClass = classifyTextureAlpha(img);
        break;
    }
    case ETextureFormat::eMMP_PNT3    :
    {
        // JMV equipment/face atlases predominantly use sparse PNT3. Shipped
        // payloads omit trailing transparent pixels; decode into an exact,
        // zero-filled image instead of reading beyond the RLE output.
        QImage img(header.m_width, header.m_height, QImage::Format_RGBA64);
        QString diagnostic;
        if(!decompressPnt3(img, data, uint(header.m_mipcount), &diagnostic))
        {
            ei::log(eLogWarning, "Invalid PNT3 texture " + name + ": " + diagnostic);
            delete texture;
            return;
        }
        // PNT3 carries one sparse compressed base image; this header field is
        // its compressed byte count, not an authored mip count. Generated
        // mipmaps average small helmet/weapon/shield islands into transparent
        // atlas space and make them vanish with distance or oblique views.
        texture->setMinificationFilter(QOpenGLTexture::Nearest);
        texture->setData(img);
        materialClass = classifyTextureAlpha(img);
        break;
    }
    case ETextureFormat::eMMP_5550    :qDebug() << "eMMP_5550"  << name; delete texture; return;
    case ETextureFormat::eMMP_DXTN    :qDebug() << "eMMP_DXTN"  << name; delete texture; return;
    case ETextureFormat::eMMP_PAINT   :qDebug() << "eMMP_PAINT" << name; delete texture; return;
    case ETextureFormat::eMMP_PAINT32 :qDebug() << "eMMP_PNT32" << name; delete texture; return;
    default:
    {
        Q_ASSERT("unknown texture format" && false);
        delete texture;
        return;
    }
    }
    m_aTexture.insert(name.toLower(), texture);
    m_textureMaterialClasses.insert(texture, materialClass);
}

void CTextureList::initAuxTexture()
{
    //read textures for aux objects.
    //TODO: can be conflict with user tex name. load aux textures in separate map
    auto auxFile = QFileInfo(":/auxData.res");

    CResFile res(auxFile.filePath());
    QMap<QString, QByteArray> aFile = normalizedResourceMap(res.bufferOfFiles());

    QString texName;
    for (auto& packedTex : aFile.toStdMap())
    {
        if(!packedTex.first.toLower().endsWith(".mmp")) continue;
        texName = packedTex.first.split(".")[0];
        if(m_aTexture.contains(texName)) continue;
        parse(packedTex.second, texName);
    }
    ei::log(eLogInfo, "aux texture loaded");
}

void CTextureList::loadTexture(QSet<QString>& aName)
{
    const QVector<QString> sources = configuredResourceSources(m_pSettings);
    if(sources.isEmpty())
    {
        ei::log(eLogWarning, "No texture resources configured. Use Settings > Game and Mod Setup, or the advanced legacy resource settings.");
        return;
    }

    if(aName.isEmpty())
    {
        m_arrCellComboBox.clear();
        for(const QString& name : indexedNames(sources, ".mmp"))
            m_arrCellComboBox.append(name.left(name.size() - 4));
        return;
    }

    for(const auto& requestedTexture : aName)
    {
        const QString normalized = requestedTexture.toLower();
        if(m_aTexture.contains(normalized) || m_missingTextureNames.contains(normalized))
            continue;
        QByteArray textureData;
        if(readIndexedResource(sources, normalized + ".mmp", textureData))
            parse(textureData, normalized);
        if(!m_aTexture.contains(normalized))
        {
            m_missingTextureNames.insert(normalized);
            ei::log(eLogWarning, "Missing texture (negative cached): " + normalized);
        }
    }
}

QOpenGLTexture* CTextureList::loadTextureResource(const QString& name, const QString& modelName)
{
    const QString cacheKey = ei::textureCacheKey(name, modelName);
    if(m_aTexture.contains(cacheKey))
        return m_aTexture.value(cacheKey);
    if(m_missingTextureNames.contains(cacheKey))
        return textureDefault();

    const QVector<QString> sources = configuredResourceSources(m_pSettings);
    const QStringList candidates = ei::textureResourceCandidates(name, modelName);
    for(int candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex)
    {
        const QString& candidate = candidates.at(candidateIndex);
        QByteArray textureData;
        QString sourcePath;
        if(!readIndexedResource(sources, candidate, textureData, &sourcePath))
            continue;
        parse(textureData, cacheKey);
        if(m_aTexture.contains(cacheKey))
        {
            if(candidate != name.toLower() + ".mmp")
                ei::log(eLogInfo, QString("Resolved texture %1 %2 model:%3 -> %4 in %5")
                        .arg(candidateIndex >= 3 ? "fallback" : "alias", name, modelName,
                             candidate, sourcePath));
            return m_aTexture.value(cacheKey);
        }
    }


    QString databaseSource;
    const QString databaseCandidate = databaseHumanoidTexture(sources, name, modelName,
                                                               &databaseSource);
    if(!databaseCandidate.isEmpty())
    {
        QByteArray textureData;
        QString textureSource;
        if(readIndexedResource(sources, databaseCandidate, textureData, &textureSource))
        {
            parse(textureData, cacheKey);
            if(m_aTexture.contains(cacheKey))
            {
                ei::log(eLogInfo, QString("Resolved database unit texture %1 model:%2 -> %3 (database:%4 texture:%5)")
                        .arg(name, modelName, databaseCandidate, databaseSource, textureSource));
                return m_aTexture.value(cacheKey);
            }
        }
        ei::log(eLogWarning, QString("Database unit texture is missing: %1 model:%2 resolved:%3 database:%4")
                .arg(name, modelName, databaseCandidate, databaseSource));
    }

    m_missingTextureNames.insert(cacheKey);
    ei::log(eLogWarning, QString("Missing texture (negative cached): %1 model:%2 candidates:%3")
            .arg(name, modelName, candidates.join(", ")));
    return textureDefault();
}

QOpenGLTexture* CTextureList::texture(const QString& name)
{
    QString texName(name.toLower());
    if(m_missingTextureNames.contains(texName))
        return textureDefault();
    if(!m_aTexture.contains(texName))
    {
        QSet<QString> texture;
        texture.insert(texName);
        loadTexture(texture);
    }

    return m_aTexture.contains(texName) ? m_aTexture[texName] : textureDefault();
}

QOpenGLTexture* CTextureList::texture(const QString& name, const QString& modelName)
{
    return loadTextureResource(name.toLower(), modelName.toLower());
}

QImage CTextureList::loadTextureImageResource(const QString& name, const QString& modelName)
{
    const QVector<QString> sources = configuredResourceSources(m_pSettings);
    const QString key = resourceSourceFingerprint(sources) + '|'
        + ei::textureCacheKey(name, modelName);
    if(m_aTextureImages.contains(key))
        return m_aTextureImages.value(key);
    for(const QString& candidate : ei::textureResourceCandidates(name, modelName))
    {
        QByteArray data;
        if(!readIndexedResource(sources, candidate, data))
            continue;
        QDataStream stream(data);
        util::formatStream(stream);
        SMmpHeader header;
        stream >> header;
        if(header.m_signature != 0x00504D4D || header.m_format != ETextureFormat::eMMP_PNT3)
        {
            ei::log(eLogWarning, "Shared body-atlas composition currently requires PNT3: " + candidate);
            m_aTextureImages.insert(key, QImage());
            return QImage();
        }
        QImage image(header.m_width, header.m_height, QImage::Format_RGBA64);
        QString diagnostic;
        if(!decompressPnt3(image, data, uint(header.m_mipcount), &diagnostic))
        {
            ei::log(eLogWarning, "Invalid body-atlas layer " + candidate + ": " + diagnostic);
            m_aTextureImages.insert(key, QImage());
            return QImage();
        }
        m_aTextureImages.insert(key, image);
        return image;
    }
    ei::log(eLogWarning, QString("Missing body-atlas layer: %1 model:%2").arg(name, modelName));
    m_aTextureImages.insert(key, QImage());
    return QImage();
}

QImage CTextureList::textureImage(const QString& name, const QString& modelName)
{
    return loadTextureImageResource(name, modelName);
}

QImage CTextureList::layeredTextureImage(const QStringList& layerNames,
                                         const QString& modelName,
                                         QString* diagnostic)
{
    QVector<QImage> layers;
    for(const QString& name : layerNames)
    {
        const QImage image = loadTextureImageResource(name, modelName);
        if(image.isNull())
        {
            if(layers.isEmpty())
            {
                if(diagnostic)
                    *diagnostic = "Base body-atlas layer is unavailable: " + name;
                return QImage();
            }
            ei::log(eLogWarning, "Skipping unavailable humanoid body-atlas overlay: " + name);
            continue;
        }
        layers.append(image);
    }
    QImage composite;
    QString localDiagnostic;
    if(!ei::compositeTextureLayers(layers, &composite, &localDiagnostic))
    {
        if(diagnostic)
            *diagnostic = localDiagnostic;
        return QImage();
    }
    if(diagnostic)
        diagnostic->clear();
    return composite;
}

QOpenGLTexture* CTextureList::layeredTexture(const QStringList& layerNames,
                                             const QString& modelName)
{
    const QString cacheKey = "layers|"
        + resourceSourceFingerprint(configuredResourceSources(m_pSettings)) + '|'
        + modelName.toLower() + '|'
        + layerNames.join(';').toLower();
    if(m_aTexture.contains(cacheKey))
        return m_aTexture.value(cacheKey);
    QString diagnostic;
    const QImage composite = layeredTextureImage(layerNames, modelName, &diagnostic);
    if(composite.isNull())
    {
        ei::log(eLogWarning, "Cannot compose humanoid body atlas: " + diagnostic);
        return textureDefault();
    }
    // Match the established QImage upload path used by ordinary PNT3 MMPs.
    // The QOpenGLTexture(QImage) convenience constructor may select different
    // storage/orientation behavior even though the FIG UVs are unchanged.
    QOpenGLTexture* texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    texture->setMinificationFilter(QOpenGLTexture::Nearest);
    texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    texture->setWrapMode(QOpenGLTexture::Repeat);
    texture->setData(composite);
    m_aTexture.insert(cacheKey, texture);
    m_textureMaterialClasses.insert(texture, classifyTextureAlpha(composite));
    return texture;
}

QOpenGLTexture* CTextureList::textureDefault()
{
    Q_ASSERT(m_aTexture.contains("default"));
    return m_aTexture[QString("default")];
}

ERenderMaterialClass CTextureList::materialClass(QOpenGLTexture* texture) const
{
    return m_textureMaterialClasses.value(texture, ERenderMaterialClass::Conservative);
}

void CTextureList::initResource()
{

    QImage img(":/default0.png", "PNG");
    QOpenGLTexture* texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    texture->setMinificationFilter(QOpenGLTexture::Nearest);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);
    texture->setWrapMode(QOpenGLTexture::Repeat);
    texture->setSize(img.width(), img.height());
    texture->setFormat(QOpenGLTexture::TextureFormat::RGBA8_UNorm);
    texture->setData(img.mirrored());
    m_aTexture.insert(QString("default"), texture);
    m_textureMaterialClasses.insert(texture, classifyTextureAlpha(img));

    initAuxTexture();

    if(true)
    {
        QSet<QString> aEmpty;
        loadTexture(aEmpty);
    }
}

QImage convert_DXT(QDataStream& stream, int width, int height, bool DXT3 = false)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    uint16_t color[4][4] = {};

    for (int i = 0; i < height / 4; i++) {
        for (int j = 0; j < width / 4; j++) {
            if (DXT3) {
                for (int x = 0; x < 4; x++) {
                    uint16_t row;
                    stream >> row;
                    for (int y = 0; y < 4; y++) {
                        int alpha = (row & 15) * 17;
                        row >>= 4;
                        image.setPixel(j * 4 + y, i * 4 + x, QColor(0, 0, 0, alpha).rgba());
                    }
                }
            }

            uint16_t gen_c1, gen_c2;
            stream >> gen_c1;
            stream >> gen_c2;

            color[0][0] = extractColor(gen_c1, 63488, 11);
            color[0][1] = extractColor(gen_c1, 2016, 5);
            color[0][2] = extractColor(gen_c1, 31, 0);

            color[1][0] = extractColor(gen_c2, 63488, 11);
            color[1][1] = extractColor(gen_c2, 2016, 5);
            color[1][2] = extractColor(gen_c2, 31, 0);

            if (gen_c1 > gen_c2 || DXT3) {
                for (int k = 0; k < 3; k++) {
                    color[2][k] = (2 * color[0][k] + color[1][k]) / 3;
                    color[3][k] = (color[0][k] + 2 * color[1][k]) / 3;
                }
            } else {
                for (int k = 0; k < 3; k++) {
                    color[2][k] = (color[0][k] + color[1][k]) / 2;
                    color[3][k] = 0;
                }
            }

            for (int x = 0; x < 4; x++) {
                uint8_t row;
                stream >> row;
                for (int y = 0; y < 4; y++) {
                    int idx = row & 3;
                    QColor pixelColor(color[idx][0], color[idx][1], color[idx][2], (DXT3 ? image.pixelColor(j * 4 + y, i * 4 + x).alpha() : 255));
                    image.setPixel(j * 4 + y, i * 4 + x, pixelColor.rgba());
                    row >>= 2;
                }
            }
        }
    }

    return image;
}

int CTextureList::extractMmpToDxt1(QVector<QImage>& outArrImage, const QStringList& inArrTextureName)
{
    outArrImage.clear();
    outArrImage.resize(inArrTextureName.size());
    QVector<QString> sources = configuredResourceSources(m_pSettings);
    if(!m_mapLocalTerrainArchive.isEmpty())
        sources.prepend(m_mapLocalTerrainArchive);
    if(sources.isEmpty())
        return false;

    int nCount(0);
    // Expanded map-specific atlases may intentionally repeat authored MMP
    // pages under distinct physical page names. Preserve those physical
    // identities while sharing one immutable decoded QImage for byte-identical
    // payloads instead of allocating the same RGBA page hundreds of times.
    QHash<QByteArray, QImage> decodedPayloads;
    for(int index = 0; index < inArrTextureName.size(); ++index)
    {
        const QString texName = inArrTextureName.at(index).toLower();
        QByteArray textureData;
        QString sourcePath;
        if(!readIndexedResource(sources, texName, textureData, &sourcePath))
        {
            ei::log(eLogWarning, "Missing terrain texture: " + texName);
            continue;
        }

        const QByteArray payloadSha256 = QCryptographicHash::hash(
            textureData, QCryptographicHash::Sha256);
        const auto cached = decodedPayloads.constFind(payloadSha256);
        if(cached != decodedPayloads.constEnd())
        {
            outArrImage[index] = cached.value();
            ++nCount;
            ei::log(eLogInfo, QString("Resolved repeated terrain tile atlas %1 source:%2 sharedDecodedPixels:true")
                    .arg(texName, sourcePath));
            continue;
        }

        QDataStream stream(textureData);
        util::formatStream(stream);
        SMmpHeader header;
        stream >> header;
        if(header.m_signature != 0x00504D4D)
        {
            ei::log(eLogWarning, "Invalid terrain texture signature: " + texName + " source:" + sourcePath);
            continue;
        }
        if(header.m_format != ETextureFormat::eMMP_DXT1)
        {
            ei::log(eLogWarning, "Unsupported terrain texture format: " + texName + " source:" + sourcePath);
            continue;
        }

        QImage& img = outArrImage[index];
        const qint64 blockBytes = qint64(qMax(1, (header.m_width + 3) / 4))
            * qint64(qMax(1, (header.m_height + 3) / 4)) * 8;
        if(blockBytes > std::numeric_limits<int>::max()
           || header.size() + blockBytes > textureData.size())
        {
            ei::log(eLogWarning, "Truncated terrain texture payload: " + texName
                    + " source:" + sourcePath);
            continue;
        }
        QString decodeDiagnostic;
        if(!ei::decodeDxtImage(textureData.mid(header.size(), int(blockBytes)),
                               header.m_width, header.m_height,
                               ei::DxtFormat::Dxt1, &img, &decodeDiagnostic))
        {
            ei::log(eLogWarning, "Cannot decode terrain texture: " + texName
                    + " source:" + sourcePath + " diagnostic:" + decodeDiagnostic);
            continue;
        }
        img = img.mirrored(false, true);
        decodedPayloads.insert(payloadSha256, img);
        ++nCount;
        ei::log(eLogInfo, QString("Resolved terrain tile atlas %1 source:%2 size:%3x%4")
                .arg(texName, sourcePath).arg(header.m_width).arg(header.m_height));
    }
    return nCount == inArrTextureName.size();
}

int CTextureList::extractMmpToDxt1(QImage& outImage, const QString textureName)
{
    QVector<QImage> arrImage;
    QStringList arrTexName;
    arrTexName.append(textureName);
    int res = extractMmpToDxt1(arrImage, arrTexName);
    if(res != 0 && !arrImage.isEmpty())
        outImage = arrImage.front();
    else
        outImage = QImage();
    return res;
}

bool CTextureList::terrainMmpPayload(const QString& textureName,
                                     QByteArray* payload,
                                     QString* sourcePath) const
{
    if(payload)
        payload->clear();
    if(!payload)
        return false;
    QVector<QString> sources = configuredResourceSources(m_pSettings);
    if(!m_mapLocalTerrainArchive.isEmpty())
        sources.prepend(m_mapLocalTerrainArchive);
    return readIndexedResource(sources, textureName.toLower(), *payload,
                               sourcePath);
}

bool CTextureList::terrainMmpTile(const QString& textureName, int logicalSlot,
                                  QImage* tile, QString* sourcePath) const
{
    QByteArray payload;
    if(!tile || !terrainMmpPayload(textureName, &payload, sourcePath))
        return false;
    QString diagnostic;
    if(!ei::decodeMmpDxt1AtlasTile(payload, logicalSlot, tile, &diagnostic))
    {
        ei::log(eLogWarning, QString(
            "Cannot decode terrain atlas tile directly: %1 slot:%2 diagnostic:%3")
            .arg(textureName).arg(logicalSlot).arg(diagnostic));
        return false;
    }
    return true;
}

void CTextureList::setMapLocalTerrainArchive(const QString& path)
{
    QString resolved;
    if(!path.isEmpty())
    {
        const QFileInfo candidate(path);
        if(candidate.isFile())
            resolved = candidate.absoluteFilePath();
    }
    if(resolved == m_mapLocalTerrainArchive)
        return;

    m_mapLocalTerrainArchive = resolved;
    if(m_mapLocalTerrainArchive.isEmpty())
        ei::log(eLogInfo, "Map-local terrain atlas source cleared");
    else
        ei::log(eLogInfo, "Map-local terrain atlas source: " + m_mapLocalTerrainArchive);
}

struct STexSpecified
{
    SMmpHeader header;
    QByteArray data;
    int selectedMip{0};
};

namespace {
int dxt1MipBytes(int width, int height)
{
    return qMax(1, (width + 3) / 4) * qMax(1, (height + 3) / 4) * 8;
}

int dxt1MipOffset(const SMmpHeader& header, int mipLevel)
{
    int offset = header.size();
    int width = header.m_width;
    int height = header.m_height;
    for(int level = 0; level < mipLevel; ++level)
    {
        offset += dxt1MipBytes(width, height);
        width = qMax(1, width / 2);
        height = qMax(1, height / 2);
    }
    return offset;
}

QByteArray combineTerrainMip(const QVector<STexSpecified>& parts, int outputLevel,
                             int tileSize, int columns)
{
    const int blocksWide = qMax(1, (tileSize + 3) / 4);
    const int blocksHigh = qMax(1, (tileSize + 3) / 4);
    const int rowBytes = blocksWide * 8;
    const int rows = (parts.size() + columns - 1) / columns;
    QByteArray combined(rowBytes * columns * blocksHigh * rows, char(0));
    for(int partIndex = 0; partIndex < parts.size(); ++partIndex)
    {
        const STexSpecified& part = parts.at(partIndex);
        const int pageColumn = partIndex % columns;
        const int pageRow = partIndex / columns;
        for(int blockRow = 0; blockRow < blocksHigh; ++blockRow)
        {
            const int offset = dxt1MipOffset(part.header,
                part.selectedMip + outputLevel) + blockRow * rowBytes;
            const int destination = ((pageRow * blocksHigh + blockRow) * columns
                                     + pageColumn) * rowBytes;
            memcpy(combined.data() + destination,
                   part.data.constData() + offset, size_t(rowBytes));
        }
    }
    return combined;
}
}

QOpenGLTexture* CTextureList::buildLandTex(const QString& name,
                                           int expectedTexCount,
                                           int& texCount,
                                           int& atlasColumns)
{
    ei::setCrashContext("mpr/terrain-atlas/cache:" + name);
    QString archiveFingerprint;
    if(!m_mapLocalTerrainArchive.isEmpty())
    {
        const QFileInfo archive(m_mapLocalTerrainArchive);
        archiveFingerprint = QStringLiteral("|%1|%2|%3")
            .arg(archive.absoluteFilePath())
            .arg(archive.size())
            .arg(archive.lastModified().toMSecsSinceEpoch());
    }
    const QString cacheKey = name.toLower() + archiveFingerprint;
    if(m_aTexture.contains(cacheKey))
    {
        texCount = qMax(1, expectedTexCount);
        atlasColumns = CTile::preferredAtlasColumns(texCount);
        return m_aTexture[cacheKey];
    }

    QVector<STexSpecified> aPart;
    QVector<QString> sources = configuredResourceSources(m_pSettings);
    if(!m_mapLocalTerrainArchive.isEmpty())
        sources.prepend(m_mapLocalTerrainArchive);
    if(sources.isEmpty())
    {
        ei::log(eLogWarning, "Cannot build terrain textures because no resources are configured. Use Settings > Game and Mod Setup.");
    }

    const QStringList textureNames = ei::terrainTextureResourceNames(name, expectedTexCount);
    QStringList missingTextures;
    for(const QString& texName : textureNames)
    {
        ei::setCrashContext("mpr/terrain-atlas/resolve:" + texName);
        QByteArray textureData;
        QString sourcePath;
        if(!readIndexedResource(sources, texName, textureData, &sourcePath))
        {
            missingTextures.append(texName);
            continue;
        }

        STexSpecified part;
        part.data = textureData;
        QDataStream stream(part.data);
        util::formatStream(stream);
        stream >> part.header;
        if(part.header.m_signature != 0x00504D4D || part.header.m_format != ETextureFormat::eMMP_DXT1)
        {
            missingTextures.append(texName);
            ei::log(eLogWarning, "Invalid or unsupported terrain atlas: " + texName + " source:" + sourcePath);
            continue;
        }
        aPart.append(part);
        ei::log(eLogInfo, QString("Resolved terrain atlas %1 source:%2 size:%3x%4")
                .arg(texName, sourcePath).arg(part.header.m_width).arg(part.header.m_height));
    }

    if(expectedTexCount <= 0 || !missingTextures.isEmpty() || aPart.size() != expectedTexCount)
    {
        ei::log(ELogMessageType::eLogWarning,
                QString("Incomplete terrain atlas set map:%1 expected:%2 loaded:%3 missing:%4; loading defaults")
                .arg(name).arg(expectedTexCount).arg(aPart.size()).arg(missingTextures.join(", ")));
        aPart.clear();
        STexSpecified part;
        auto auxFile = QFileInfo(":/auxData.res");
        CResFile res(auxFile.filePath());
        QMap<QString, QByteArray> aFile = normalizedResourceMap(res.bufferOfFiles());
        part.data = aFile["default_zone.mmp"];
        QDataStream stream(part.data);
        util::formatStream(stream);
        stream >> part.header;
        for(int i(0); i<qMax(1, expectedTexCount); ++i)
        {
            aPart.append(part);
        }
    }
    //get min texture size
    ei::setCrashContext("mpr/terrain-atlas/combine:" + name);
    int minTexSize(aPart.first().header.m_width);
    for(auto& part: aPart)
    {
        Q_ASSERT(part.header.m_width == part.header.m_height);
        if(part.header.m_width < minTexSize)
            minTexSize = part.header.m_width;
    }

    texCount = aPart.size();
    atlasColumns = CTile::preferredAtlasColumns(texCount);
    const int atlasRows = (texCount + atlasColumns - 1) / atlasColumns;
    // Select the authored level matching the smallest source tile. Sources in
    // one map can have different base resolutions, but all output levels must
    // line up in one horizontal atlas.
    int outputMipCount = std::numeric_limits<int>::max();
    for(auto& part: aPart)
    {
        int size = part.header.m_width;
        while(size > minTexSize && part.selectedMip + 1 < part.header.m_mipcount)
        {
            size = qMax(1, size / 2);
            ++part.selectedMip;
        }
        Q_ASSERT(size == minTexSize);
        outputMipCount = qMin(outputMipCount,
            part.header.m_mipcount - part.selectedMip);
    }
    int blockSafeMipCount = 0;
    for(int size = minTexSize; size >= 4; size /= 2)
        ++blockSafeMipCount;
    outputMipCount = qMax(1, qMin(outputMipCount, blockSafeMipCount));
    QVector<QByteArray> combinedMips;
    combinedMips.reserve(outputMipCount);
    for(int level = 0; level < outputMipCount; ++level)
        combinedMips.append(combineTerrainMip(aPart, level,
            qMax(1, minTexSize >> level), atlasColumns));

    ei::log(eLogInfo, QString("Uploading terrain atlas map:%1 parts:%2 grid:%3x%4 dimensions:%5x%6 compressedBytes:%7")
            .arg(name).arg(texCount).arg(atlasColumns).arg(atlasRows)
            .arg(minTexSize * atlasColumns).arg(minTexSize * atlasRows)
            .arg(combinedMips.first().size()));
    ei::setCrashContext("mpr/terrain-atlas/opengl-create:" + name);
    const QOpenGLContext* currentContext = QOpenGLContext::currentContext();
    if(currentContext)
    {
        GLint maximumTextureSize = 0;
        currentContext->functions()->glGetIntegerv(
            GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
        if(maximumTextureSize > 0
           && (minTexSize * atlasColumns > maximumTextureSize
               || minTexSize * atlasRows > maximumTextureSize))
        {
            ei::log(eLogWarning, QString(
                "Terrain atlas grid exceeds this GPU map:%1 pages:%2 grid:%3x%4 page:%5 max:%6")
                .arg(name).arg(texCount).arg(atlasColumns).arg(atlasRows)
                .arg(minTexSize).arg(maximumTextureSize));
            return nullptr;
        }
    }
    const bool openGlesCompatibility = currentContext && currentContext->isOpenGLES();
    const int uploadedMipCount = openGlesCompatibility ? 1 : outputMipCount;
    QOpenGLTexture* texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    texture->setMinificationFilter(m_terrainMipmappingEnabled && !openGlesCompatibility
        ? QOpenGLTexture::NearestMipMapLinear : QOpenGLTexture::Nearest);
    texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    texture->setWrapMode(QOpenGLTexture::Repeat);
    if(openGlesCompatibility)
    {
        // ANGLE's ES2 compressed upload accepts neither the shipped DXT MMPs
        // nor the horizontally block-packed terrain atlas reliably. Decode
        // the selected authored mip for each tile, then compose the exact
        // atlas in ordinary RGBA memory. Desktop GL retains the zero-copy DXT
        // path below.
        QImage atlas(minTexSize * atlasColumns, minTexSize * atlasRows,
                     QImage::Format_ARGB32);
        atlas.fill(Qt::transparent);
        for(int partIndex = 0; partIndex < aPart.size(); ++partIndex)
        {
            const STexSpecified& part = aPart[partIndex];
            const int offset = dxt1MipOffset(part.header, part.selectedMip);
            const int bytes = dxt1MipBytes(minTexSize, minTexSize);
            QImage tile;
            QString diagnostic;
            if(!ei::decodeDxtImage(part.data.mid(offset, bytes), minTexSize,
                                   minTexSize, ei::DxtFormat::Dxt1,
                                   &tile, &diagnostic))
            {
                ei::log(eLogWarning, QString("Invalid ANGLE terrain tile %1 part:%2: %3")
                        .arg(name).arg(partIndex).arg(diagnostic));
                delete texture;
                return nullptr;
            }
            const int pageColumn = partIndex % atlasColumns;
            const int pageRow = partIndex / atlasColumns;
            for(int y = 0; y < minTexSize; ++y)
                memcpy(atlas.scanLine(pageRow * minTexSize + y)
                           + pageColumn * minTexSize * 4,
                       tile.constScanLine(y), size_t(minTexSize) * 4u);
        }
        ei::setCrashContext("mpr/terrain-atlas/opengl-upload-rgba:" + name);
        texture->setData(atlas);
        m_aTexture[cacheKey] = texture;
        m_terrainTextures.insert(texture);
        ei::log(eLogInfo, QString("Terrain atlas set ready map:%1 expected:%2 loaded:%3 ANGLE-RGBA")
                .arg(name).arg(expectedTexCount).arg(texCount));
        return texture;
    }
    texture->setFormat(QOpenGLTexture::TextureFormat::RGBA_DXT1); //todo: read texture format from file. EI can use both dxt3 and dxt1
    texture->setMipLevels(uploadedMipCount);
    texture->setSize(minTexSize * atlasColumns, minTexSize * atlasRows);
    ei::setCrashContext("mpr/terrain-atlas/opengl-allocate:" + name);
    texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    ei::setCrashContext("mpr/terrain-atlas/opengl-upload:" + name);
    for(int level = 0; level < uploadedMipCount; ++level)
        texture->setCompressedData(level, 0, combinedMips[level].size(),
                                   combinedMips[level].constData());
    m_aTexture[cacheKey] = texture;
    m_terrainTextures.insert(texture);
    ei::log(eLogInfo, QString("Terrain atlas set ready map:%1 expected:%2 loaded:%3 mips:%4")
            .arg(name).arg(expectedTexCount).arg(texCount).arg(uploadedMipCount));
    return texture;
}

void CTextureList::setTerrainMipmappingEnabled(bool enabled)
{
    if(m_terrainMipmappingEnabled == enabled)
        return;
    m_terrainMipmappingEnabled = enabled;
    for(QOpenGLTexture* texture : m_terrainTextures)
    {
        if(!texture || !texture->isCreated())
            continue;
        const QOpenGLContext* currentContext = QOpenGLContext::currentContext();
        const bool openGlesCompatibility = currentContext && currentContext->isOpenGLES();
        texture->setMinificationFilter(enabled && !openGlesCompatibility
            ? QOpenGLTexture::NearestMipMapLinear : QOpenGLTexture::Nearest);
        texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    }
}

void CTextureList::refreshConfiguredSources()
{
    m_missingTextureNames.clear();
    m_aTextureImages.clear();
    for(auto it = m_aTexture.begin(); it != m_aTexture.end();)
    {
        if(it.key().startsWith("layers|"))
            it = m_aTexture.erase(it);
        else
            ++it;
    }
    QSet<QString> empty;
    loadTexture(empty);
}

void CTextureList::clearTransientVisualCache()
{
    m_aTextureImages.clear();
    for(auto it = m_aTexture.begin(); it != m_aTexture.end();)
    {
        if(!it.key().startsWith("layers|"))
        {
            ++it;
            continue;
        }
        QOpenGLTexture* texture = it.value();
        it = m_aTexture.erase(it);
        if(texture)
        {
            m_textureMaterialClasses.remove(texture);
            if(texture->isCreated())
                texture->destroy();
            delete texture;
        }
    }
}


bool isDIfferent(const QString &value)
{
    {
        return value == "<different>";
    }
}

QString valueDifferent()
{
    return "<different>";
}

CResourceStringList* CResourceStringList::m_pResourceStringContainer = nullptr;

CResourceStringList* CResourceStringList::getInstance()
{
    if(nullptr == m_pResourceStringContainer)
        m_pResourceStringContainer = new CResourceStringList();
    return m_pResourceStringContainer;
}

CResourceStringList::CResourceStringList()
{
    initResourceString();
}

bool CResourceStringList::getPropList(QMap<uint, QString>& map, const EObjParam propType)
{
    map.clear();
    if(!m_propValueName.contains(propType))
        return false;

    map = m_propValueName[propType];
    return true;
}

const char* CResourceStringList::noLiquidIndexName()
{
    return "-1 (No liquid)";
}

//todo: move string outside hard code to localization file
void CResourceStringList::initResourceString()
{
    QMap<uint, QString> map;
    map[0] = "Not active";
    map[1] = "Hand";
    map[2] = "Picklock (unused)";
    map[4] = "Crowbar (unused)";
    map[5] = "Hand or Crowbar";
    map[8] = "Key";
    map[9] = "Hand or Key";
    m_propValueName[eObjParam_LEVER_SCIENCE_STATS_Type_Open] = map;

    map.clear();
    {
        for(int i(0); i < 32; ++i)
            map[i] = "Player-" + QString::number(i);
    }
    m_propValueName[eObjParam_PLAYER] = map;

    map.clear();
    map[0] = "False";
    map[1] = "True";
    m_propValueName[eObjParam_IS_SHADOW] = map;
    m_propValueName[eObjParam_LEVER_IS_CYCLED] = map;
    m_propValueName[eObjParam_LEVER_IS_DOOR] = map;
    m_propValueName[eObjParam_USE_IN_SCRIPT] = map;
    m_propValueName[eObjParam_LEVER_RECALC_GRAPH] = map;
    m_propValueName[eObjParam_UNIT_NEED_IMPORT] = map;
    m_propValueName[eObjParam_SOUND_AMBIENT] = map;
    m_propValueName[eObjParam_SOUND_IS_MUSIC] = map;
    m_propValueName[eObjParam_LIGHT_SHADOW] = map;
    m_propValueName[eObjParam_TRAP_CAST_ONCE] = map;
    m_propValueName[eObjParam_ALWAYS_ACTIVE] = map;

    map.clear();
    map[50] = "Human";
    map[51] = "Animal";
    map[52] = "Monstr";
    m_propValueName[eObjParam_TYPE] = map;

    map.clear();

    map[8192] = "Fireball";
    map[8193] = "Campfire";
    map[8194] = "Fireblast";
    map[8195] = "Fire";
    map[8196] = "Smoke";
    map[8197] = "Vulcansmoke";
    map[8198] = "Healing";
    map[8199] = "Poisonfog";
    map[8200] = "Aggressionfog";
    map[8201] = "Geyser";
    map[8202] = "Tornado";
    map[8203] = "Casting";
    map[8204] = "Nuke";
    map[8205] = "Bansheecasting";
    map[8206] = "Mushroom";
    map[8207] = "Blood1";
    map[8208] = "Firewall";
    map[8209] = "Firearrow";
    map[8210] = "Acidray";
    map[8211] = "Bluegas";
    map[8212] = "Link";
    map[8213] = "Sphereacid";
    map[8214] = "Sphereelectricity";
    map[8215] = "Spherefire";
    map[8216] = "Clayring";
    map[8217] = "Teleport";
    map[8218] = "Antimagic";
    map[8219] = "Modifier1";
    map[8220] = "Modifier2";
    map[8221] = "Modifier3";
    map[8222] = "Modifier4";
    map[8223] = "Modifier5";
    map[8224] = "Modifier6";
    map[8225] = "Modifier7";
    map[8226] = "Modifier8";
    map[8227] = "Modifier9";
    map[8228] = "Modifier10";
    map[8229] = "Modifier11";
    map[8230] = "Modifier12";
    map[8231] = "Castingfire";
    map[8232] = "Castingelectricity";
    map[8233] = "Castingacid";
    map[8234] = "Castingdivination";
    map[8235] = "Castingillusion";
    map[8236] = "Castingdomination";
    map[8237] = "Castingenchantment";
    map[8238] = "Castinghealing";
    map[8239] = "Castingfailed";
    map[8240] = "Lightningblast";
    map[8241] = "Blood2";
    map[8242] = "Blood3";
    map[8243] = "Blood4";
    map[8244] = "A4bloodred";
    map[8245] = "A4bloodgreen";
    map[8246] = "A4bloodblue";
    map[8247] = "A4bloodblack";
    map[8248] = "Zoneexit";
    map[8249] = "Path";
    map[8250] = "Pathdestination";
    map[8251] = "Pathfailed";
    map[8252] = "Moshka";
    map[8253] = "Portalstar";
    map[8254] = "Portal";
    map[8255] = "Cylinder1";
    map[8256] = "Cylinder2";
    map[8257] = "Firestar";
    map[8258] = "Acidstar";
    map[8259] = "Sparks";
    map[8260] = "Visionstar1";
    map[8261] = "Visionstar2";
    map[8262] = "Visionstar3";
    map[8263] = "Regeneration";
    map[8264] = "Silence";
    map[8265] = "Feeblemind";
    map[8266] = "Feetcloud1";
    map[8267] = "Feetcloud2";
    map[8268] = "Visionstar4";
    map[8269] = "Ballofstars";
    map[8270] = "Rickarrow";
    map[8271] = "Cursestars";
    map[8272] = "Curseholder";
    map[8273] = "Starttrans";
    map[8274] = "Transform";
    m_propValueName[eObjParam_PARTICL_TYPE] = map;

    map.clear();
    map[0] = "Idle";
    map[1] = "Guard radius";
    map[2] = "Path";
    map[3] = "Place";
    map[4] = "Briffing";
    map[5] = "Guard Alaram";
    m_propValueName[eObjParam_LOGIC_BEHAVIOUR] = map;

    map.clear();
    map[0] = "Attack";
    map[1] = "Revenge";
    map[2] = "Fear";
    map[3] = "Fear player";
    m_propValueName[eObjParam_AGRESSION_MODE] = map;

    map.clear();
    map[0] = "HP";
    map[1] = "Max HP";
    map[2] = "MP";
    map[3] = "Max MP";
    map[4] = "Tuning Move";
    map[5] = "Actions";
    map[6] = "Speed Run";
    map[7] = "Speed Walk";
    map[8] = "Speed Crouch";
    map[9] = "Speed Crawl";
    map[10] = "Vision Arc";
    map[11] = "Skills Peripherial";
    map[12] = "Peripherial Arc";
    map[13] = "Attack Distance";
    map[14] = "AI Class Stay";
    map[15] = "AI Class Lay";
    map[16] = "Reserved";
    map[17] = "Range";
    map[18] = "Attack";
    map[19] = "Defence";
    map[20] = "Weight";
    map[21] = "Damage Min";
    map[22] = "Damage Range";
    map[23] = "Armor Impalling";
    map[24] = "Armor Slashing";
    map[25] = "Armor Crushing";
    map[26] = "Armor Thermal";
    map[27] = "Armor Chemical";
    map[28] = "Armor Electrical";
    map[29] = "Armor General";
    map[30] = "Absorption";
    map[31] = "Sight";
    map[32] = "Night Sight";
    map[33] = "Sense Life";
    map[34] = "Sense Hear";
    map[35] = "Sense Smell";
    map[36] = "Sense Tracking";
    map[37] = "pSight";
    map[38] = "pNight Sight";
    map[39] = "pSense Life";
    map[40] = "pSense Hear";
    map[41] = "pSense Smell";
    map[42] = "pSense Tracking";
    map[43] = "Manual Skill Science";
    map[44] = "Manual Skill Stealing";
    map[45] = "Manual Skill Tame";
    map[46] = "Magical Skill 1";
    map[47] = "Magical Skill 2";
    map[48] = "Magical Skill 3";
    map[49] = "Reserved";
    map[50] = "Reserved";
    m_propValueName[eObjParam_UNIT_STATS] = map;

    m_tileType[ETileType::eGrass]     = "Grass";
    m_tileType[ETileType::eGround]    = "Ground";
    m_tileType[ETileType::eStone]     = "Stone";
    m_tileType[ETileType::eSand]      = "Sand";
    m_tileType[ETileType::eRock]      = "Rock";
    m_tileType[ETileType::eField]     = "Field";
    m_tileType[ETileType::eWater]     = "Water";
    m_tileType[ETileType::eRoad]      = "Road";
    m_tileType[ETileType::eUndefined] = "Undefined";
    m_tileType[ETileType::eSnow]      = "Snow";
    m_tileType[ETileType::eIce]       = "Ice";
    m_tileType[ETileType::eDrygrass]  = "Drygrass";
    m_tileType[ETileType::eSnowballs] = "Snowballs";
    m_tileType[ETileType::eLava]      = "Lava";
    m_tileType[ETileType::eSwamp]     = "Swamp";
    m_tileType[ETileType::eHighrock]  = "Highrock";

    m_materialType[ETerrainType::eTerrainNoWater] = "Liquid type 1";
    m_materialType[ETerrainType::eTerrainWater] = "Liquid type 3";

}

CNvttManager::CNvttManager(QString appDir):
    m_bInit(false)
{
    m_nvcompress = appDir + QDir::separator() + "nvtt" + QDir::separator() + "nvcompress.exe";
    if(!QFile::exists(m_nvcompress))
    {
        ei::log(eLogFatal, "Cannot find nvcompress.exe for dxt converting");
    }
    m_bInit = true;
}

int CNvttManager::dxtToBmp(QString pathToDxt, QString pathToBmp)
{
    if(!m_bInit)
    {
        ei::log(eLogWarning, "Cannot convert dxt data. CNvttManager not initialized. Check nvcompress.exe");
        return -1;
    }
    // nvcompress params
    QStringList arguments;
    arguments << "-d" << pathToDxt << pathToBmp;

    QProcess process;
    //process.start(m_nvcompress, arguments);
    QString cmd = QString("\"%1\" \"%3\" \"%4\"").arg(m_nvcompress, pathToDxt, pathToBmp);
    process.start(cmd);

    // check if process running
    if (!process.waitForStarted()) {
        qDebug() << "Cannot initialise process!";
        return -1;
    }

    if (!process.waitForFinished()) {
        qDebug() << "process suspended";
        return -1;
    }

    QString output = process.readAllStandardOutput();
    ei::log(eLogDebug, "NVTT conversion: " + output);

    QString errorOutput = process.readAllStandardError();
    if (!errorOutput.isEmpty()) {
        qDebug() << "conversion error: " << errorOutput;
    }
    return 0;
}

CSessionDataManager* CSessionDataManager::getInstance()
{
    if(nullptr == m_pSessionDataManger)
        m_pSessionDataManger = new CSessionDataManager();
    return m_pSessionDataManger;
}

void CSessionDataManager::getLastSession(QString& mprPath, QVector<QString>& arrMobPath)
{
    mprPath.clear();
    arrMobPath.clear();

    QFile sessionFile(sessionDataFile());
    if(!sessionFile.open(QIODevice::ReadOnly))
        return;

    if(sessionFile.size() == 0)
        return;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(sessionFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        ei::log(eLogWarning, "Error parsing last session file:" + parseError.errorString());
        sessionFile.close();
        return;
    }
    sessionFile.close();


    QJsonObject obj = doc.object();
    if(!obj.contains("Last Session"))
        return;

    QJsonObject lastSession = obj["Last Session"].toObject();
    if(!lastSession.contains("MPR"))
        return;

    mprPath = lastSession["MPR"].toString();

    if(!lastSession.contains("arrMOB"))
        return;

    QJsonArray arrMob = lastSession["arrMOB"].toArray();
    for(auto it = arrMob.begin(); it < arrMob.end(); ++it)
        arrMobPath.append(it->toString());

    return;
}

void CSessionDataManager::addZoneData(const QString& mprPath, const QVector<QString>& arrMobPath)
{
    if(mprPath.isEmpty())
        return;

    QJsonObject zone;
    zone.insert("mpr", mprPath);
    if(!arrMobPath.isEmpty())
    {
        QJsonArray arrMob;
        for(const auto& mobPath : arrMobPath)
            arrMob.append(mobPath);

        zone.insert("mob list", arrMob);
    }

    m_lastSession["zone"] = zone;
}

bool CSessionDataManager::getZoneData(QString& mprPath, QVector<QString>& arrMobPath)
{
    if(!m_lastSession.contains("zone"))
        return false;

    QJsonObject zone = m_lastSession["zone"].toObject();
    if(!zone.contains("mpr"))
        return false;

    mprPath = zone["mpr"].toString();

    if(!zone.contains("mob list"))
        return true;

    QJsonArray arrMob = zone["mob list"].toArray();
    for(auto it = arrMob.begin(); it < arrMob.end(); ++it)
        arrMobPath.append(it->toString());

    return true;
}

void CSessionDataManager::addWorkspaceIdentity(const QString& kind,
                                               const QString& identifier,
                                               const QString& displayName,
                                               const QString& mapZoneId)
{
    if(kind.isEmpty())
        return;
    QJsonObject identity;
    identity.insert("kind", kind);
    identity.insert("identifier", identifier);
    identity.insert("displayName", displayName);
    identity.insert("mapZoneId", mapZoneId);
    m_lastSession.insert("workspace identity", identity);
}

bool CSessionDataManager::getWorkspaceIdentity(QString& kind, QString& identifier,
                                               QString& displayName, QString& mapZoneId)
{
    kind.clear();
    identifier.clear();
    displayName.clear();
    mapZoneId.clear();
    const QJsonObject identity = m_lastSession.value("workspace identity").toObject();
    kind = identity.value("kind").toString();
    if(kind.isEmpty())
        return false;
    identifier = identity.value("identifier").toString();
    displayName = identity.value("displayName").toString();
    mapZoneId = identity.value("mapZoneId").toString();
    return true;
}

void CSessionDataManager::addCameraData(const QVector3D& position, const QVector3D& pivot, const QVector3D& rotation)
{
    QJsonObject camera;
    QJsonArray vec;
    vec.append(position.x());
    vec.append(position.y());
    vec.append(position.z());
    camera.insert("position", vec);

    QJsonArray piv;
    piv.append(pivot.x());
    piv.append(pivot.y());
    piv.append(pivot.z());
    camera.insert("pivot", piv);

    QJsonArray rot;
    rot.append(rotation.x());
    rot.append(rotation.y());
    rot.append(rotation.z());
    camera.insert("rotation", rot);
    m_lastSession["camera"] = camera;
}

bool CSessionDataManager::getCameraData(QVector3D& position, QVector3D& pivot, QVector3D& rotation)
{
    if(!m_lastSession.contains("camera"))
        return false;

    QJsonObject camera = m_lastSession["camera"].toObject();
    if(!camera.contains("position"))
        return false;

    QJsonArray pos = camera["position"].toArray();
    if(pos.size() != 3)
        return false;

    position.setX(pos[0].toDouble());
    position.setY(pos[1].toDouble());
    position.setZ(pos[2].toDouble());

    if(!camera.contains("pivot"))
        return false;

    QJsonArray piv = camera["pivot"].toArray();
    if(piv.size() != 3)
        return false;

    pivot.setX(piv[0].toDouble());
    pivot.setY(piv[1].toDouble());
    pivot.setZ(piv[2].toDouble());

    if(!camera.contains("rotation"))
        return false;
    QJsonArray rot = camera["rotation"].toArray();
    if(rot.size() != 3)
        return false;

    rotation.setX(rot[0].toDouble());
    rotation.setY(rot[1].toDouble());
    rotation.setZ(rot[2].toDouble());
    return true;
}

void CSessionDataManager::addQuickTileIndices(const QVector<int>& arrInd)
{
    QJsonArray arr;
    for(auto& ind: arrInd)
        arr.append(ind);
    m_lastSession["quick tile indices"] = arr;
}

bool CSessionDataManager::getdQuickTileIndices(QVector<int>& arrInd)
{
    if(!m_lastSession.contains("quick tile indices"))
        return false;

    QJsonArray arr = m_lastSession["quick tile indices"].toArray();
    arrInd.clear();
    for(int i(0); i<arr.size(); ++i)
        arrInd.append(arr[i].toInt());

    return true;
}

void CSessionDataManager::saveSession()
{
    QFile sessionFile(sessionDataFile());
    QJsonDocument jsonDdoc;
    jsonDdoc.setObject(m_lastSession);
    sessionFile.open(QIODevice::WriteOnly | QIODevice::Text);
    sessionFile.resize(0);
    sessionFile.write(jsonDdoc.toJson(QJsonDocument::Indented));
    sessionFile.close();
}

void CSessionDataManager::reset()
{
    m_lastSession = QJsonObject();
}

void CSessionDataManager::loadSession()
{
    QFile sessionFile(sessionDataFile());
    if(!sessionFile.open(QIODevice::ReadOnly))
        return;

    if(sessionFile.size() == 0)
        return;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(sessionFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        ei::log(eLogWarning, "Error parsing last session file:" + parseError.errorString());
        sessionFile.close();
        return;
    }
    sessionFile.close();


    m_lastSession = doc.object();
}

CSessionDataManager::CSessionDataManager()
{

}

QString CSessionDataManager::sessionDataFile()
{
    return QString("%1%2%3").arg(QDir::tempPath()).arg(QDir::separator()).arg("ei_mapper_endurance_session.json");
}
