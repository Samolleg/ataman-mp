#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QMessageBox>

#include "landscape.h"
#include "res_file.h"
#include "utils.h"
#include "resourcemanager.h"
#include "node.h"
#include "sector.h"
#include "backup_manager.h"
#include "log.h"

CLandscape* CLandscape::m_pLand = nullptr;

CLandscape* CLandscape::getInstance()
{
    if(nullptr == m_pLand)
        m_pLand = new CLandscape();
    return m_pLand;
}

void CLandscape::unloadMpr()
{
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
}

CLandscape::CLandscape():
  m_bDirty(false)
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
    CResFile map(path.filePath());
    QMap<QString, QByteArray> aTmp =  map.bufferOfFiles();  // map contains DIfferentCaseName
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
    if (!readHeader(mpStream))
        return;

    ei::log(eLogInfo, QString("MPR namespace outer:%1 inner:%2 expectedTerrainAtlases:%3")
            .arg(path.baseName(), innerMapName).arg(m_header.nTexture));
    int texCount;
    m_texture = CTextureList::getInstance()->buildLandTex(innerMapName, int(m_header.nTexture), texCount);

    // read sectors
    UI2 secIndex;
    for (uint y(0); y<m_header.nYSector; ++y)
    {
        QVector<CSector*> xSec;
        for (uint x(0); x<m_header.nXSector; ++x)
        {
            secIndex.reset(x, y);
            QDataStream secStream(aComponent.take(innerMapName + genSectorSuffix(int(x), int(y)) + ".sec"));
            util::formatStream(secStream);
            CSector* sector = new CSector(secStream, m_header.maxZ, texCount);
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
}

void CLandscape::save()
{
    saveMapAs(m_filePath);
}

void CLandscape::saveMapAs(const QFileInfo& path)
{
    // TODO: check if source texture has correct textureSize and tileSize with map header

    //QString filePath = path.filePath();
    //QFile file(filePath);
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
            return;
        }
    }

    if(!m_bDirty && !m_originalMprData.isEmpty())
    {
        QSaveFile out(path.filePath());
        if(!out.open(QIODevice::WriteOnly) || out.write(m_originalMprData) != m_originalMprData.size() || !out.commit())
            ei::log(eLogFatal, QString("Atomic MPR copy failed:%1").arg(path.filePath()));
        return;
    }

    CResFile mprFile(m_originalMprData);
    serializeMpr(m_innerMapName, mprFile);
    const QByteArray data = mprFile.generateResData();
    QSaveFile out(path.filePath());
    if(!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
    {
        ei::log(eLogFatal, QString("Atomic MPR save failed:%1").arg(path.filePath()));
        return;
    }
    m_originalMprData = data;
    m_bDirty = false;
}

void CLandscape::draw(QOpenGLShaderProgram* program)
{
    m_texture->bind(0);
    program->setUniformValue("qt_Texture0", 0);
    for (auto& xSec: m_aSector)
        for(auto& ySec: xSec)
            ySec->draw(program);
}

void CLandscape::drawWater(QOpenGLShaderProgram *program)
{
    m_texture->bind(0);
    program->setUniformValue("qt_Texture0", 0);
    for (auto& xSec: m_aSector)
        for(auto& ySec: xSec)
            ySec->drawWater(program);
}

bool CLandscape::projectPt(QVector3D& point)
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
    m_aSector[yIndex][xIndex]->projectPt(point);
    return true;
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
    if(pNode->nodeType() == eUnit)
        landPos -= pNode->minPosition();
    projectPt(landPos);
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
}

void CLandscape::projectPositions(QList<CNode*>& aNode)
{
    for(auto& node: aNode)
    {
        projectPosition(node);
    }
}
