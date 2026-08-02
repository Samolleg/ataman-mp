#include <QFileInfo>
#include <QDir>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QMessageBox>

#include "landscape.h"
#include "map_object_placement_policy.h"

#include <cmath>
#include "render_performance.h"
#include "view_frustum.h"
#include "res_file.h"
#include "utils.h"
#include "resourcemanager.h"
#include "node.h"
#include "sector.h"
#include "backup_manager.h"
#include "staged_save_validator.h"
#include "log.h"
#include "mpr_document.h"
#include "terrain_edit_model.h"

CLandscape* CLandscape::m_pLand = nullptr;

CLandscape* CLandscape::getInstance()
{
    if(nullptr == m_pLand)
        m_pLand = new CLandscape();
    return m_pLand;
}

void CLandscape::unloadMpr()
{
    m_canonicalTerrain = nullptr;
    for (auto& xSec: m_aSector)
    {
        for(auto& ySec: xSec)
            delete ySec;
    }
    m_aSector.clear();
    m_aMaterial.clear();
    m_aTileTypes.clear();
    m_aAnimTile.clear();
    m_bDirty = false;
    ++m_shadowGeneration;
}

CLandscape::CLandscape():
  m_bDirty(false),
  m_canonicalTerrain(nullptr)
{
    m_aSector.clear();
    m_aAnimTile.clear();
    m_aMaterial.clear();
    m_aTileTypes.clear();
}

CLandscape::~CLandscape()
{
    unloadMpr();
}

bool CLandscape::readHeader(QDataStream& stream)
{
    const uint mapSignature = 0xce4af672;
    stream >> m_header;
    if (m_header.signature != mapSignature)
    {
        Q_ASSERT("Incorrect landscape signature" && false);
        return false;
    }

    SMaterial mat;
    for(uint i(0); i<m_header.nMaterial; ++i)
    {
        stream >> mat;
        m_aMaterial.append(mat);
    }

    int type;
    for(uint i(0); i<m_header.nTile; ++i)
    {
        stream >> type;
        if(type > 15 || type < 0)
        {
            ei::log(eLogWarning, "incorrect tile type at index: " + QString::number(i));
            m_arrIncorectTiles.append(i);
        }
        m_aTileTypes.append((ETileType)type);
    }

    SAnimTile animTile;
    for(uint i(0); i<m_header.nAnimTile; ++i)
    {
        stream >> animTile;
        m_aAnimTile.append(animTile);
    }
    return true;
}

bool CLandscape::serializeMpr(const QString& zoneName, CResFile& mprFile)
{
    QByteArray mpData;
    QDataStream mpStream(&mpData, QIODevice::WriteOnly);
    util::formatStream(mpStream);

    //write map header data (*.mp)
    m_header.nAnimTile = m_aAnimTile.size();
    m_header.nMaterial = m_aMaterial.size();
    mpStream << m_header;
    for(auto& mat: m_aMaterial)
    {
        mpStream << mat;
    }

    int type;
    for(auto& tileTyle: m_aTileTypes)
    {
        type = int(tileTyle);
        mpStream << type;
    }

    for(auto& animTile: m_aAnimTile)
    {
        mpStream << animTile;
    }
    // end of header data
    mprFile.addFiledataCaseInsensitive(zoneName + ".mp", mpData);

    for(int row(0); row < m_aSector.size(); ++row)
    {
        for(int col(0); col<m_aSector[row].size(); ++col)
        {
            QByteArray secData = m_aSector[row][col]->serializeSector();
            QString secName = QString("%1%2%3.sec").arg(zoneName).arg(col, 3, 10, QChar('0')).arg(row, 3, 10, QChar('0'));
            mprFile.addFiledataCaseInsensitive(secName, secData);
        }
    }
    return true;
}

// Generates sector suffix by number: 001002 - sector x:1 y:2
QString genSectorSuffix(int x, int y)
{
    QString name;
    name.append('0');
    if (x < 10)
    {
        name.append('0');
        name.append(QString::number(x));
    }
    else {
        name.append(QString::number(x));
    }
    name.append('0');
    if (y < 10)
    {
        name.append('0');
        name.append(QString::number(y));
    }
    else {
        name.append(QString::number(y));
    }
    return name;
}

void CLandscape::readMap(const QFileInfo& path)
{
    if (!path.exists())
        return;

    ei::setCrashContext("mpr/open/source:" + path.fileName());
    m_filePath = path;

    QFile source(path.filePath());
    if(source.open(QIODevice::ReadOnly))
        m_originalMprData = source.readAll();

    const SBackupResult openBackup = CBackupManager().snapshotOnOpen(path);
    if(!openBackup.success)
    {
        ei::log(eLogWarning, QString("MPR opened without recovery snapshot:%1 error:%2")
            .arg(path.absoluteFilePath(), openBackup.error));
        QMessageBox::warning(nullptr, "Endurance backup warning",
            QString("The MPR was opened, but its recovery snapshot could not be created.\n\n%1\n\n"
                    "Do not edit or save this file until the backup location is writable.")
                .arg(openBackup.error));
    }

    ei::log(eLogInfo, "Start read terrain: " + m_filePath.absoluteFilePath());
    ei::setCrashContext("mpr/archive/read:" + path.fileName());
    CResFile map(path.filePath());
    QMap<QString, QByteArray> aTmp =  map.bufferOfFiles();  // map contains DIfferentCaseName
    ei::log(eLogInfo, QString("MPR archive read complete file:%1 members:%2")
            .arg(path.fileName()).arg(aTmp.size()));
    ei::setCrashContext("mpr/archive/normalize:" + path.fileName());
    QMap<QString, QByteArray> aComponent;
    QString innerMapName;
    for (auto& file: aTmp.toStdMap())
    {
        QString fName = file.first.toLower();
        aComponent.insert(fName, file.second);
        if(fName.endsWith(".mp"))
            innerMapName = fName.left(fName.size() - 3);
    }
    m_innerMapName = innerMapName;

    QDataStream mpStream(aComponent[innerMapName + ".mp"]);
    util::formatStream(mpStream);
    ei::setCrashContext("mpr/header/read:" + path.fileName());
    if (!readHeader(mpStream))
    {
        ei::setCrashContext("idle");
        return;
    }

    ei::log(eLogInfo, QString("MPR namespace outer:%1 inner:%2 expectedTerrainAtlases:%3")
            .arg(path.baseName(), innerMapName).arg(m_header.nTexture));
    int texCount;
    ei::setCrashContext("mpr/terrain-atlas/build:" + path.fileName());
    m_texture = CTextureList::getInstance()->buildLandTex(innerMapName, int(m_header.nTexture), texCount);
    ei::log(eLogInfo, QString("MPR terrain atlas complete file:%1 textureCount:%2")
            .arg(path.fileName()).arg(texCount));

    // read sectors
    UI2 secIndex;
    for (uint y(0); y<m_header.nYSector; ++y)
    {
        QVector<CSector*> xSec;
        for (uint x(0); x<m_header.nXSector; ++x)
        {
            ei::setCrashContext(QString("mpr/sector/read:%1:%2,%3").arg(path.fileName()).arg(x).arg(y));
            secIndex.reset(x, y);
            QDataStream secStream(aComponent.take(innerMapName + genSectorSuffix(int(x), int(y)) + ".sec"));
            util::formatStream(secStream);
            CSector* sector = new CSector(secStream, m_header.maxZ, texCount, &m_aMaterial);
            sector->setIndex(secIndex);
            xSec.append(sector);
            if(sector->existsTileIndices(m_arrIncorectTiles))
            {
                qDebug() << "sector (" << x << "," << y << ")";
            }
        }
        m_aSector.append(xSec);
    }

    ei::log(eLogInfo, "End read terrain");
    ++m_shadowGeneration;
    ei::setCrashContext("idle");
}

bool CLandscape::save(const SaveProgress& progress)
{
    return saveMapAs(m_filePath, progress);
}

bool CLandscape::saveMapAs(const QFileInfo& path, const SaveProgress& progress)
{
    // TODO: check if source texture has correct textureSize and tileSize with map header

    if(m_bDirty)
    {
        ei::log(eLogFatal,
            QString("Refusing changed MPR save through the legacy rebuilding serializer path:%1")
                .arg(path.absoluteFilePath()));
        QMessageBox::critical(nullptr, "Endurance safe save",
            "Changed landscape saving remains disabled while the lossless MPR/SEC edit path is "
            "being validated. The destination was not created or replaced.");
        return false;
    }
    const QByteArray data = m_originalMprData;
    if(data.isEmpty())
    {
        QMessageBox::critical(nullptr, "Endurance safe save",
            "The original MPR bytes are unavailable. The destination was not created or replaced.");
        return false;
    }

    const QString sourcePath = QDir::cleanPath(m_filePath.absoluteFilePath());
    const QString destinationPath = QDir::cleanPath(path.absoluteFilePath());
    if(QString::compare(sourcePath, destinationPath, Qt::CaseInsensitive) == 0)
    {
        const SExistingFileComparison comparison = compareExistingFile(path, data);
        if(comparison.exists && comparison.readable && comparison.equal)
        {
            ei::log(eLogInfo, QString("Unchanged MPR save is a no-op; source bytes and timestamp preserved:%1")
                .arg(destinationPath));
            return true;
        }
        if(comparison.exists)
        {
            const QString detail = comparison.readable
                ? QString("The loaded MPR changed on disk after it was opened.")
                : QString("The loaded MPR could not be read: %1").arg(comparison.error);
            ei::log(eLogFatal, QString("Refusing unchanged MPR overwrite:%1 error:%2")
                .arg(destinationPath, detail));
            QMessageBox::critical(nullptr, "Endurance safe save",
                detail + "\n\nEndurance did not overwrite the file. Reopen it before saving.");
            return false;
        }
    }

    if(progress) progress("Freshly validating the staged MPR...", 1, 4);
    const SStagedSaveValidation validation = validateStagedSave(data, path);
    if(!validation.success)
    {
        ei::log(eLogFatal, QString("Refusing MPR save because staged validation failed path:%1 error:%2")
            .arg(path.absoluteFilePath(), validation.error));
        QMessageBox::critical(nullptr, "Endurance safe save",
            QString("The staged MPR failed fresh validation, so the destination was not replaced.\n\n%1")
                .arg(validation.error));
        return false;
    }
    ei::log(eLogInfo, QString("Staged MPR validation passed path:%1 size:%2 sha256:%3")
        .arg(path.absoluteFilePath()).arg(validation.size).arg(validation.sha256));

    if(progress) progress("Creating the pre-save backup...", 2, 4);
    if(path.exists())
    {
        const SBackupResult saveBackup = CBackupManager().snapshotBeforeSave(path);
        if(!saveBackup.success)
        {
            ei::log(eLogFatal, QString("Refusing MPR save because pre-save backup failed:%1 error:%2")
                .arg(path.absoluteFilePath(), saveBackup.error));
            QMessageBox::critical(nullptr, "Endurance safe save",
                QString("The pre-save backup failed, so the source file was not replaced.\n\n%1")
                    .arg(saveBackup.error));
            return false;
        }
    }

    if(progress) progress("Atomically replacing the destination...", 3, 4);
    QSaveFile out(path.filePath());
    if(!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
    {
        ei::log(eLogFatal, QString("Atomic MPR save failed:%1").arg(path.filePath()));
        QMessageBox::critical(nullptr, "Endurance safe save",
            "Atomic MPR save failed. The destination was not replaced.");
        return false;
    }
    m_originalMprData = data;
    m_bDirty = false;
    if(progress) progress("MPR save complete.", 4, 4);
    return true;
}

bool CLandscape::adoptCanonicalSave(const QFileInfo& path, const QByteArray& bytes)
{
    const SExistingFileComparison comparison = compareExistingFile(path, bytes);
    if(!comparison.exists || !comparison.readable || !comparison.equal)
        return false;
    m_filePath = QFileInfo(path.absoluteFilePath());
    m_originalMprData = bytes;
    m_bDirty = false;
    return true;
}

void CLandscape::draw(QOpenGLShaderProgram* program, const CViewFrustum* frustum)
{
    m_texture->bind(0);
    program->setUniformValue("qt_Texture0", 0);
    for (auto& xSec: m_aSector)
        for(auto& ySec: xSec)
        {
            RenderPerformanceContext& performance = RenderPerformanceContext::instance();
            if(performance.collecting())
                ++performance.counters().terrainSectorsTested;
            if(frustum && !ySec->intersectsFrustum(*frustum))
            {
                if(performance.collecting())
                    ++performance.counters().terrainSectorsCulled;
                continue;
            }
            if(performance.collecting())
                ++performance.counters().terrainSectorsVisible;
            ySec->draw(program);
        }
}

void CLandscape::drawShadow(QOpenGLShaderProgram* program,
                            const CViewFrustum* frustum)
{
    for(auto& row : m_aSector)
        for(CSector* sector : row)
        {
            if(!sector)
                continue;
            RenderPerformanceContext& performance = RenderPerformanceContext::instance();
            if(performance.collecting())
            {
                ++performance.counters().terrainShadowSectorsTested;
                ++performance.counters().shadowCandidates;
            }
            if(frustum && !sector->intersectsFrustum(*frustum))
            {
                if(performance.collecting())
                    ++performance.counters().terrainShadowSectorsCulled;
                continue;
            }
            if(performance.collecting())
            {
                ++performance.counters().terrainShadowSectorsVisible;
                ++performance.counters().shadowItems;
            }
            sector->drawShadow(program);
        }
}

void CLandscape::drawWater(QOpenGLShaderProgram *program, const CViewFrustum* frustum)
{
    m_texture->bind(0);
    program->setUniformValue("qt_Texture0", 0);
    for (auto& xSec: m_aSector)
        for(auto& ySec: xSec)
        {
            RenderPerformanceContext& performance = RenderPerformanceContext::instance();
            if(performance.collecting())
                ++performance.counters().terrainSectorsTested;
            if(frustum && !ySec->intersectsFrustum(*frustum))
            {
                if(performance.collecting())
                    ++performance.counters().terrainSectorsCulled;
                continue;
            }
            if(performance.collecting())
                ++performance.counters().terrainSectorsVisible;
            ySec->drawWater(program);
        }
}

bool CLandscape::projectPt(QVector3D& point, bool land)
{
    int xIndex = int(point.x()/32.0f);
    int yIndex = int(point.y()/32.0f);
    if(m_aSector.isEmpty() || yIndex < 0 || yIndex >= m_aSector.size()
        || m_aSector.at(yIndex).isEmpty() || xIndex < 0 || xIndex >= m_aSector.at(yIndex).size()
        || m_aSector.at(yIndex).at(xIndex) == nullptr)
    {
//        point.setX(0.0f);
//        point.setY(0.0f);
        point.setZ(0.0f);
        return false;
    }
    //Q_ASSERT(yIndex < m_aSector.size() && xIndex < m_aSector.first().size());
    return m_aSector[yIndex][xIndex]->projectPt(point, land);
}

bool CLandscape::projectPt(QVector<QVector3D>& aPoint)
{
    int projectedPt(0);
    for(auto& pt: aPoint)
    {
        projectPt(pt);
        ++projectedPt;
    }

    return projectedPt == aPoint.size();
}

void CLandscape::projectPosition(CNode* pNode)
{
    if(!isMprLoad())
    {
        // Standalone MOB mode: stored coordinates are also display coordinates.
        pNode->setDrawPosition(pNode->position());
        return;
    }

    if(pNode->nodeType() == eParticle || pNode->nodeType() == eLight || pNode->nodeType() == eSound || pNode->nodeType() == ePatrolPoint || pNode->nodeType() == eLookPoint)
    {
        pNode->setDrawPosition(pNode->position());
        return;
    }
    QVector3D landPos(pNode->position());
    if(ei::usesTerrainRelativeMapObjectHeight(pNode->nodeType()))
    {
        bool sampled = false;
        if(m_canonicalTerrain)
        {
            landPos.setZ(m_canonicalTerrain->terrainSurfaceHeight(
                landPos.x(), landPos.y(), &sampled));
        }
        if(!sampled)
        {
            // Compatibility fallback for legacy/non-canonical loads only.
            landPos.setZ(0.0f);
            projectPt(landPos);
        }
        landPos = ei::resolveTerrainRelativeMapObjectPosition(
            pNode->nodeType(), pNode->position(), landPos);
    }
    else
    {
        if(pNode->nodeType() == eUnit)
            landPos -= pNode->minPosition();
        projectPt(landPos);
    }
    pNode->setDrawPosition(landPos);
}

bool CLandscape::pickTile(QVector3D& point, CTile*& pTileOut, STileLocation& tileLoc, bool bLand)
{
    int xIndex = int(point.x()/32.0f);
    int yIndex = int(point.y()/32.0f);
    if(m_aSector.isEmpty() || yIndex < 0 || yIndex >= m_aSector.size()
        || m_aSector.at(yIndex).isEmpty() || xIndex < 0 || xIndex >= m_aSector.at(yIndex).size()
        || m_aSector.at(yIndex).at(xIndex) == nullptr)
    {
        return false;
    }

    point.setZ(-1.0f);
    int row, col;
    if(m_aSector[yIndex][xIndex]->pickTile(row, col, point, bLand))
    {
        tileLoc = STileLocation{xIndex, yIndex, row, col, bLand};
        if(bLand)
            pTileOut = &(m_aSector[yIndex][xIndex]->arrTileEdit()[row][col]);
        else
            pTileOut = &(m_aSector[yIndex][xIndex]->arrWaterEdit()[row][col]);

        return true;
    }

    return false;
}

void CLandscape::setTile(const QMap<STileLocation, STileInfo>& arrTileInfo)
{
    int xSec(-1), ySec(-1);
    QMap<STileLocation, STileInfo> arrSecTileInfo;
    for(auto& tile: arrTileInfo.toStdMap())
    {
        if(xSec != tile.first.xSec)
        {
            if(xSec != -1 && ySec != -1 && !arrSecTileInfo.isEmpty())
                m_aSector[ySec][xSec]->setTile(arrSecTileInfo);
            xSec = tile.first.xSec;
            arrSecTileInfo.clear();
        }
        if(ySec != tile.first.ySec)
        {
            if(xSec != -1 && ySec != -1 && !arrSecTileInfo.isEmpty())
                m_aSector[ySec][xSec]->setTile(arrSecTileInfo);
            ySec = tile.first.ySec;
            arrSecTileInfo.clear();
        }

        arrSecTileInfo[tile.first] = tile.second;
    }
    //draw last tile data
    if(!arrSecTileInfo.isEmpty())
        m_aSector[ySec][xSec]->setTile(arrSecTileInfo);
}

void CLandscape::updateSectorDrawData(int xSec, int ySec)
{
    m_aSector[ySec][xSec]->updateDrawData();
    ++m_shadowGeneration;
}

void CLandscape::drawTileGrid(QOpenGLShaderProgram* program)
{
    for(auto& row : m_aSector)
        for(CSector* sector : row)
            sector->drawTileGrid(program);
}

bool CLandscape::previewCanonicalSector(int xSec, int ySec, const QByteArray& payload)
{
    if(ySec < 0 || ySec >= m_aSector.size() || xSec < 0
        || xSec >= m_aSector.at(ySec).size() || !m_aSector.at(ySec).at(xSec))
        return false;
    const bool updated = m_aSector[ySec][xSec]->previewCanonicalPayload(payload);
    if(updated)
        ++m_shadowGeneration;
    return updated;
}

bool CLandscape::previewCanonicalOverlaySector(
    int xSec, int ySec, const QByteArray& payload)
{
    if(ySec < 0 || ySec >= m_aSector.size() || xSec < 0
        || xSec >= m_aSector.at(ySec).size() || !m_aSector.at(ySec).at(xSec))
        return false;
    return m_aSector[ySec][xSec]->previewCanonicalOverlayPayload(payload);
}

bool CLandscape::previewCanonicalOverlaySectorDelta(
    int xSec, int ySec, const QByteArray& beforePayload,
    const QByteArray& afterPayload)
{
    if(ySec < 0 || ySec >= m_aSector.size() || xSec < 0
        || xSec >= m_aSector.at(ySec).size() || !m_aSector.at(ySec).at(xSec))
        return false;
    return m_aSector[ySec][xSec]->previewCanonicalOverlayDelta(
        beforePayload, afterPayload);
}

bool CLandscape::previewCanonicalMaxZ(float maxZ)
{
    if(!std::isfinite(maxZ) || maxZ <= 0.0f || m_aSector.isEmpty())
        return false;
    m_header.maxZ = maxZ;
    for(QVector<CSector*>& row : m_aSector)
        for(CSector* sector : row)
            if(sector)
                sector->setPreviewMaxZ(maxZ);
    ++m_shadowGeneration;
    return true;
}

bool CLandscape::previewCanonicalTile(int globalX, int globalY, bool land,
                                      quint16 packedTile, bool updateSector)
{
    if(globalX < 0 || globalY < 0)
        return false;
    const int xSec = globalX / 16;
    const int ySec = globalY / 16;
    if(ySec >= m_aSector.size() || xSec >= m_aSector.at(ySec).size()
        || !m_aSector.at(ySec).at(xSec))
        return false;
    QVector<QVector<CTile>>& tiles = land
        ? m_aSector[ySec][xSec]->arrTileEdit()
        : m_aSector[ySec][xSec]->arrWaterEdit();
    if(tiles.isEmpty())
        return false;
    const int row = globalY % 16;
    const int col = globalX % 16;
    const SMprPackedTile decoded = SMprPackedTile::decode(packedTile);
    tiles[row][col].setTile(decoded.atlasSlot(), decoded.rotation);
    if(updateSector)
        m_aSector[ySec][xSec]->updateDrawData();
    return true;
}

void CLandscape::projectPositions(QList<CNode*>& aNode)
{
    for(auto& node: aNode)
    {
        projectPosition(node);
    }
}
