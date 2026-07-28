#ifndef MAP_SEC_H
#define MAP_SEC_H

#include <QString>
#include <QVector>
#include <QFile>
#include <QDataStream>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include "types.h"
#include "tile.h"


struct SSpecificQuad //todo: delete
{
    void addVertex(SVertexData* vertex) {m_aVertexPointer.append(vertex);}
    void rotate(int step = 0);
    int m_rotateState;
    QVector<SVertexData*> m_aVertexPointer;
};

struct STile //todo: delete
{
    STile(){}
    STile(ushort packedData);
    ushort packData(ushort m_index, ushort m_texture, ushort m_rotation);
    ushort m_index;
    ushort m_texture;
    ushort m_rotation;
};


///
/// \brief The CSector class realizes a part of the game resources, from which the landscape is assembled
///
class CSector
{
public:
    CSector();
    CSector(QDataStream& stream, float maxZ, int texCount,
            const QVector<SMaterial>* materials);
    ~CSector();
    QByteArray serializeSector();
    void draw(QOpenGLShaderProgram* program);
    void drawTileGrid(QOpenGLShaderProgram* program);
    void drawWater(QOpenGLShaderProgram* program);
    bool intersectsFrustum(const class CViewFrustum& frustum) const;
    void setIndex(UI2& index) {m_index = index; updatePosition(); }
    const UI2& index() {return m_index;}
    bool projectPt(QVector3D& origin, bool land = true);
    bool pickTile(int& row, int& col, QVector3D& point, bool bLand = true);
    void setTile(QVector3D& point, int index, int rotNum, bool bLand = true, int matIndex = 0);
    //void setTile(const STileLocation tileLoc, const STileInfo tileInfo);
    void setTile(const QMap<STileLocation, STileInfo>& arrTileInfo);
    const QVector<QVector<CTile>>& arrTile() const {return m_arrLand;};
    QVector<QVector<CTile>>& arrTileEdit() {return m_arrLand;};
    const QVector<QVector<CTile>>& arrWater() const {return m_arrWater;};
    QVector<QVector<CTile>>& arrWaterEdit() {return m_arrWater;};
    bool existsTileIndices(const QVector<int>& arrInd); // function for find incorrect\coorrupt tile indices
    void updateDrawData();
    void setTerrainAtlasLayout(int textureCount, int columns);
    bool previewCanonicalPayload(const QByteArray& payload);
    bool previewCanonicalOverlayPayload(const QByteArray& payload);
    void setPreviewMaxZ(float maxZ);

private:
    void updatePosition();
    void updateWaterDrawData();
    void generateWaterVertexData();
    void updateBounds();
    void makeVertexData(QVector<QVector<SSecVertex>>& aLandVertex, QVector<STile>& aLandTile, QVector<QVector<SSecVertex>>& aWaterVertex, QVector<STile>& aWaterTile, float maxZ, int texCount);
    void generateVertexDataFromTile();


private:
    struct SWaterVertexData
    {
        QVector3D position;
        QVector3D normal;
        QVector2D texCoord;
        QVector4D animation;
        QVector2D grid;
    };

    UI2 m_index;
    QMatrix4x4 m_modelMatrix;

    QVector<QVector<CTile>> m_arrLand;
    QVector<SVertexData> m_arrLandVrtData;
    QOpenGLBuffer m_vertexBuf;
    QOpenGLBuffer m_indexBuf;
    QOpenGLBuffer m_gridIndexBuf;

    QVector<QVector<CTile>> m_arrWater;
    QVector<SVertexData> m_arrWaterVrtData;
    QVector<SWaterVertexData> m_arrAnimatedWaterVrtData;
    QOpenGLBuffer m_waterVertexBuf;
    QOpenGLBuffer m_waterIndexBuf;
    int m_waterIndexCount{0};
    const QVector<SMaterial>* m_materials{nullptr};
    QVector3D m_boundsCenter;
    float m_boundsRadius{0.0f};
};

#endif // MAP_SEC_H
