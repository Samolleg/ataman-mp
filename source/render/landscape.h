#ifndef LANDSCAPE_H
#define LANDSCAPE_H

#include <QString>
#include <QFileInfo>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <QList>
#include <QFile>
#include <QDataStream>
#include <QVector>
#include <QVector3D>
#include <QFuture>
#include <functional>

//#include "sector.h"
#include "res_file.h"
#include "tile_form.h"


class CSector;
class CTile;
class CTerrainEditModel;
struct SMprAnimatedTileRecord;

struct STerrainShadowFieldResult
{
    QVector<float> visibility;
    int gridWidth{0};
    int gridHeight{0};
    quint64 nodes{0};
    quint64 samples{0};
    quint64 nanoseconds{0};
};

struct SMapHeader
{
    uint   signature;
    float  maxZ;
    uint   nXSector;
    uint   nYSector;
    uint   nTexture;
    uint   textureSize;
    uint   nTile;
    uint   tileSize;
    ushort nMaterial;
    uint   nAnimTile;

    friend QDataStream& operator>> (QDataStream& is, SMapHeader& head)
    {
        return is >> head.signature >> head.maxZ >> head.nXSector >> head.nYSector  >>
                     head.nTexture >> head.textureSize >> head.nTile >> head.tileSize >>
                     head.nMaterial >> head.nAnimTile;
    }

    friend QDataStream& operator<< (QDataStream& os, const SMapHeader& head)
    {
        return os << head.signature << head.maxZ  << head.nXSector << head.nYSector <<
                     head.nTexture << head.textureSize << head.nTile << head.tileSize <<
                     head.nMaterial << head.nAnimTile;
    }
};

class CNode;

///
/// \brief The CLandscape class provides an implementation of the internal game landscape format (*.mpr)
///
class CLandscape
{
    //TODO: change singletone to simple class to provide several landscape loading (in different tabs)
public:
    using SaveProgress = std::function<void(const QString&, int, int)>;
    static CLandscape* getInstance();
    bool isMprLoad() {return !m_aSector.isEmpty();}
    void unloadMpr();
    void readMap(const QFileInfo& path);
    bool save(const SaveProgress& progress = SaveProgress());
    bool saveMapAs(const QFileInfo& path, const SaveProgress& progress = SaveProgress());
    void draw(QOpenGLShaderProgram* program, const class CViewFrustum* frustum = nullptr);
    void drawTileGrid(QOpenGLShaderProgram* program);
    void drawWater(QOpenGLShaderProgram* program, const class CViewFrustum* frustum = nullptr);
    void drawTilePreview(QOpenGLShaderProgram* program);
    bool projectPt(QVector3D& point, bool land = true);
    bool projectPt(QVector<QVector3D>& aPoint);
    void projectPositions(QList<CNode*>& aNode);
    void projectPosition(CNode* pNode);
    void setCanonicalTerrainModel(const CTerrainEditModel* model)
    { m_canonicalTerrain = model; }
    bool pickTile(QVector3D& point, CTile*& pTileOut, STileLocation& tileLoc, bool bLand = true);
    void setTile(const QMap<STileLocation, STileInfo>& arrTileInfo);
    const QFileInfo& filePath() {return m_filePath;}
    QOpenGLTexture* glTexture() const {return m_texture;}
    uint textureNum() const {return m_header.nTexture;}
    uint widthInWorldUnits() const { return m_header.nXSector * 32u; }
    uint heightInWorldUnits() const { return m_header.nYSector * 32u; }
    const QVector<SMaterial>& materials() const {return m_aMaterial;}
    void setMaterials(const QVector<SMaterial>& arrMat);
    const QVector<ETileType>& tileTypes() const {return m_aTileTypes;}
    void setTileTypes(const QVector<ETileType>& arrType) {m_aTileTypes = arrType;}
    const QVector<SAnimTile>& animTiles() const {return m_aAnimTile;}
    void setAnimTils(const QVector<SAnimTile>& arrAnimTile);
    void updateSectorDrawData(int xSec, int ySec);
    bool previewCanonicalSector(int xSec, int ySec, const QByteArray& payload);
    bool previewCanonicalTerrainSectorDelta(int xSec, int ySec,
                                            const QByteArray& beforePayload,
                                            const QByteArray& afterPayload);
    bool previewCanonicalOverlaySector(int xSec, int ySec, const QByteArray& payload);
    bool previewCanonicalOverlaySectorDelta(int xSec, int ySec,
                                            const QByteArray& beforePayload,
                                            const QByteArray& afterPayload);
    bool previewCanonicalMaxZ(float maxZ);
    bool previewCanonicalTile(int globalX, int globalY, bool land, quint16 packedTile,
                              bool updateSector = true);
    bool previewTerrainAtlas(const QVector<QImage>& tileImages, int textureCount);
    void clearPreviewTerrainAtlas();
    void setDirty(bool bDirty) {m_bDirty = bDirty;}
    bool isDirty() const {return m_bDirty;}
    bool adoptCanonicalSave(const QFileInfo& path, const QByteArray& bytes);
    bool adoptCanonicalTerrainSave(
        const QFileInfo& path, const QByteArray& bytes,
        const QString& innerNamespace, uint textureCount,
        uint textureSize, uint tileCount, uint tileSize, float maxZ,
        const QVector<qint32>& tileTypes,
        const QVector<SMprAnimatedTileRecord>& animatedTiles);
    const QString& mapName() const {return m_innerMapName;}
    quint64 shadowGeneration() const { return m_shadowGeneration; }
    void invalidateTerrainShadowField();
    void updateTerrainShadowVisibility(const QVector3D& sunDirection,
                                       bool allowRebuild = true);
    void applyTerrainShadowUniforms(QOpenGLShaderProgram* program, bool enabled);

private:
    CLandscape();
    ~CLandscape();
    bool readHeader(QDataStream& stream);
    bool serializeMpr(const QString& zoneName, CResFile& mprFile);
    bool uploadTerrainShadowTexture(const STerrainShadowFieldResult& result);
    void refreshMaterialDrawDataIfNeeded();

private:
    static CLandscape* m_pLand;
    SMapHeader m_header;
    QVector<SMaterial> m_aMaterial; //use pointers instead refs?
    QVector<ETileType> m_aTileTypes; //array using tyle type on current map
    QVector<SAnimTile> m_aAnimTile;
    int m_renderTextureCount{1};
    QVector<QVector<CSector*>> m_aSector;
    QOpenGLTexture* m_texture;
    QOpenGLTexture* m_baseAtlasTexture{nullptr};
    int m_baseAtlasColumns{1};
    QOpenGLTexture* m_previewAtlasTexture{nullptr};
    int m_previewAtlasTextureCount{0};
    int m_previewAtlasColumns{0};
    QFileInfo m_filePath;
    QString m_innerMapName;
    QByteArray m_originalMprData;
    QVector<int> m_arrIncorectTiles;
    bool m_bDirty;
    bool m_materialDrawDataDirty;
    const CTerrainEditModel* m_canonicalTerrain;
    quint64 m_shadowGeneration{0};
    quint64 m_terrainShadowFieldGeneration{0};
    QVector3D m_terrainShadowSunDirection;
    QOpenGLTexture* m_terrainShadowTexture{nullptr};
    int m_terrainShadowTextureWidth{0};
    int m_terrainShadowTextureHeight{0};
    QFuture<STerrainShadowFieldResult> m_terrainShadowFuture;
    bool m_terrainShadowFutureActive{false};
    quint64 m_terrainShadowDesiredGeneration{0};
    QVector3D m_terrainShadowDesiredDirection;
    quint64 m_terrainShadowFutureGeneration{0};
    QVector3D m_terrainShadowFutureDirection;
};



#endif // LANDSCAPE_H
