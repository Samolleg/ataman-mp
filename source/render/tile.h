#ifndef CTILE_H
#define CTILE_H

#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QVector3D>
#include <QtMath>
#include "types.h"

struct SSecVertex
{
    qint8  xOffset;
    qint8  yOffset;
    ushort z;
    uint32_t packedNormal; // temp var to unpack normal
    QVector3D normal;

    friend QDataStream& operator>> (QDataStream& st, SSecVertex& vert)
    {
        st >> vert.xOffset >> vert.yOffset >> vert.z >> vert.packedNormal;
        vert.normal.setX((((vert.packedNormal >> 11) & 0x7FF) - 1000.0f) / 1000.0f);
        vert.normal.setY(((vert.packedNormal & 0x7FF) - 1000.0f) / 1000.0f);
        vert.normal.setZ((vert.packedNormal >> 22) / 1000.0f);
        return st;
    }

    friend QDataStream& operator<< (QDataStream& st, SSecVertex& vert)
    {
        uint32_t packedX = qBound(0, int((vert.normal.x() * 1000.0f) + 1000.0f), 2047) & 0x7FF;
        uint32_t packedY = qBound(0, int((vert.normal.y() * 1000.0f) + 1000.0f), 2047) & 0x7FF;
        uint32_t packedZ = qBound(0, int( vert.normal.z() * 1000.0f), 1023) & 0x3FF;

        // Упаковываем компоненты в одно 32-битное число
        vert.packedNormal = (packedZ << 22) | (packedX << 11) | packedY;
        return st << vert.xOffset << vert.yOffset << vert.z << vert.packedNormal;
    }
};

// nine vertices of tile
/*
6 _7 _8
|\ |\ |
|_\|_\|
3 _4 _5
|\ |\ |
|_\|_\|
0  1  2
*/
class CTile
{
public:
    CTile();
    CTile(ushort packedData, ushort x, ushort y, float maxZ, int atlasNumber);
    virtual ~CTile();
    void resetVertices(QVector<SSecVertex>& arrVertex);
    void generateDrawVertexData(QVector<SVertexData>& outData, int& curIndex);
    bool isProjectPoint(QVector3D& outPoint);
    bool isProjectTile(QVector3D& outPoint);
    virtual int tileIndex() const;
    ushort tileRotation() const {return m_rotNum;}
    void setTile(int index, int rotNum);
    void setAtlasLayout(int textureCount, int columns);
    static int preferredAtlasColumns(int textureCount)
    {
        if(textureCount <= 0)
            return 1;
        if(textureCount <= 8)
            return textureCount;
        return qMax(1, qCeil(qSqrt(double(textureCount))));
    }
    static float atlasPageRowOrigin(int atlasIndex, int textureCount,
                                    int columns)
    {
        const int safeTextures = qMax(1, textureCount);
        const int safeColumns = qBound(1, columns, safeTextures);
        const int rows = qMax(1, (safeTextures + safeColumns - 1)
                                  / safeColumns);
        return float(atlasIndex / safeColumns) / float(rows);
    }
    void setMaxZ(float maxZ) { m_maxZ = maxZ; }
    const QVector<QVector<SSecVertex>>& arrVertex(){return m_arrVertex;}
    void setMaterialIndex(short matIndex) {m_materialIndex = matIndex;}
    short materialIndex() const {return m_materialIndex;}
    ushort packData();
protected:
    void reset();
    QVector3D pos(int row, int col);
protected:
    QVector<QVector<SSecVertex>> m_arrVertex; // matrix 3x3 of x,y offsets, z-altitude and normal
    ushort m_x; // start X (left pos to right). max X tile is 2.0f + m_x for third vertex (m_x + 0.0f, m_x + 1.0f, m_x + 2.0f1)
    ushort m_y; // start Y (bottom to top)

    ushort m_index; // index in atlas, 0-63
    ushort m_atlasTexIndex; // index of texture atlas
    ushort m_rotNum; //number of rotation
    float m_maxZ; // update for each tile when changing maximum altitude
    int m_texAtlasNumber{1}; // update for each tile when changing atlas number
    int m_atlasColumns{1};
    int m_atlasRows{1};
    short m_materialIndex; // only for liquid tiles. ind == 1 is equal to disabled liquid
};

class CPreviewTile
{
public:
    CPreviewTile();
    ~CPreviewTile();
    void updateTile(const CTile& tile, int tIndex, int rotation, int mIndex, int xSector, int ySector);
    void draw(QOpenGLShaderProgram* program);

private:
    QVector<SVertexData> m_arrLandVrtData;
    QOpenGLBuffer m_vertexBuf;
    QOpenGLBuffer m_indexBuf;
    QMatrix4x4 m_modelMatrix;
};

inline float terrainTilePreviewElevation()
{
    // Keep the cursor-attached physical tile above the source terrain so the
    // depth test cannot discard an exactly coplanar preview.
    return 0.08f;
}

#endif // CTILE_H
