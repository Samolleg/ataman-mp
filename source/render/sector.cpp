#include <QtGlobal>
#include <QtEndian>
#include <algorithm>
#include <cstring>

#include "sector.h"
#include "gl_dispatch.h"
#include "render_performance.h"
#include "landscape.h"
#include "math_utils.h"
#include "log.h"
#include "overlay_material_animation.h"
#include "utils.h"
#include "view_frustum.h"

static const uint secSignature = 0xcf4bf774;
static const int nVertex = 33; // vertices count by 1 side of sector
//const int nSecVertex = nVertex * nVertex;
static const int nTile = 16;   //tiles count by 1 side of sector
//static constexpr int nSecTile = nTile * nTile;

CSector::~CSector()
{
    m_vertexBuf.destroy();
    m_indexBuf.destroy();
    m_gridIndexBuf.destroy();
    m_waterVertexBuf.destroy();
    m_waterIndexBuf.destroy();
}

CSector::CSector(QDataStream& stream, float maxZ, int texCount,
                 const QVector<SMaterial>* materials)
    :m_indexBuf(QOpenGLBuffer::IndexBuffer)
    ,m_gridIndexBuf(QOpenGLBuffer::IndexBuffer)
    ,m_waterIndexBuf(QOpenGLBuffer::IndexBuffer)
    ,m_materials(materials)
{
    uint signature;
    stream >> signature;
    if(signature != secSignature)
    {
        ei::log(eLogFatal, "Incorrect sector signature");
        return;
    }
    //init opengl buffers
    m_vertexBuf.create();
    m_indexBuf.create();
    m_gridIndexBuf.create();
    m_waterVertexBuf.create();
    m_waterIndexBuf.create();
    m_modelMatrix.setToIdentity();

    //continue read *.sec data
    quint8 secType;
    stream >> secType;
    const bool bWater = secType == 3;
    m_arrLand.clear();
    SSecVertex sv;
    QVector<QVector<SSecVertex>> landVertex;
    QVector<SSecVertex> aLine;
    for (int column(0); column<nVertex; ++column)
    {
        aLine.clear();
        for(int row(0); row<nVertex; ++row)
        {
            stream >> sv;
            aLine.append(sv);
        }
        landVertex.append(aLine);
    }

    QVector<QVector<SSecVertex>> waterVertex;
    if(bWater)
    {
        for (int column(0); column<nVertex; ++column)
        {
            aLine.clear();
            for(int row(0); row<nVertex; ++row)
            {
                stream >> sv;
                aLine.append(sv);
            }
            waterVertex.append(aLine);
        }
    }

    // convert filedata to tile-specific quads. x,y(0,0) is left down corner. Move to right by column then up by row
    m_arrLand.resize(nTile);
    QVector<SSecVertex> arrVertex;
    for(int i(0); i<nTile; ++i)
    {
        m_arrLand[i].resize(nTile);
    }

    QVector<STile> aLandTiles; //todo: remove
    ushort tilePacked;
    for (int row(0); row<nTile; ++row)
    {
        for(int col(0); col<nTile; ++col)
        {
            stream >> tilePacked;
            STile tile(tilePacked); //todo: remove
            aLandTiles.append(tile); //todo: remove
            m_arrLand[row][col] = CTile(tilePacked, col*2, row*2, maxZ, texCount);

            arrVertex.clear();
            arrVertex.resize(9);
            // step for vertices = 2*step for tiles
            arrVertex[0] = landVertex[row*2][col*2];
            arrVertex[1] = landVertex[row*2][col*2+1];
            arrVertex[2] = landVertex[row*2][col*2+2];
            arrVertex[3] = landVertex[row*2+1][col*2];
            arrVertex[4] = landVertex[row*2+1][col*2+1];
            arrVertex[5] = landVertex[row*2+1][col*2+2];
            arrVertex[6] = landVertex[row*2+2][col*2];
            arrVertex[7] = landVertex[row*2+2][col*2+1];
            arrVertex[8] = landVertex[row*2+2][col*2+2];
            m_arrLand[row][col].resetVertices(arrVertex);
        }
    }

    //QVector<STile> aWaterTiles;
    if(bWater)
    {
        m_arrWater.resize(nTile);
        QVector<SSecVertex> arrVertex;
        for(int i(0); i<nTile; ++i)
        {
            m_arrWater[i].resize(nTile);
        }

        for (int row(0); row<nTile; ++row)
        {
            for(int col(0); col<nTile; ++col)
            {
                stream >> tilePacked;
                m_arrWater[row][col] = CTile(tilePacked, col*2, row*2, maxZ, texCount);

                arrVertex.clear();
                arrVertex.resize(9);
                // step for vertices = 2*step for tiles
                arrVertex[0] = waterVertex[row*2][col*2];
                arrVertex[1] = waterVertex[row*2][col*2+1];
                arrVertex[2] = waterVertex[row*2][col*2+2];
                arrVertex[3] = waterVertex[row*2+1][col*2];
                arrVertex[4] = waterVertex[row*2+1][col*2+1];
                arrVertex[5] = waterVertex[row*2+1][col*2+2];
                arrVertex[6] = waterVertex[row*2+2][col*2];
                arrVertex[7] = waterVertex[row*2+2][col*2+1];
                arrVertex[8] = waterVertex[row*2+2][col*2+2];
                m_arrWater[row][col].resetVertices(arrVertex);
            }
        }

        short matIndex;
        for (int row(0); row<nTile; ++row)
        {
            for(int col(0); col<nTile; ++col)
            {
                stream >> matIndex;
                m_arrWater[row][col].setMaterialIndex(matIndex);
            }
        }

    }

    //makeVertexData(landVertex, aLandTiles, waterVertex, aWaterTiles, maxZ, texCount);
    generateVertexDataFromTile();
}

bool CSector::previewCanonicalPayload(const QByteArray& payload)
{
    QByteArray copy = payload;
    QDataStream stream(&copy, QIODevice::ReadOnly);
    util::formatStream(stream);

    quint32 signature = 0;
    quint8 type = 0;
    stream >> signature >> type;
    if(signature != secSignature || (type != 0 && type != 3))
        return false;

    const auto readVertexGrid = [&stream]() {
        QVector<QVector<SSecVertex>> vertices(nVertex);
        for(int row = 0; row < nVertex; ++row)
        {
            vertices[row].resize(nVertex);
            for(int col = 0; col < nVertex; ++col)
                stream >> vertices[row][col];
        }
        return vertices;
    };
    const QVector<QVector<SSecVertex>> landVertex = readVertexGrid();
    const QVector<QVector<SSecVertex>> waterVertex = type == 3
        ? readVertexGrid() : QVector<QVector<SSecVertex>>();
    if(stream.status() != QDataStream::Ok)
        return false;

    if(m_arrLand.size() != nTile)
    {
        m_arrLand.resize(nTile);
        for(QVector<CTile>& row : m_arrLand)
            row.resize(nTile);
    }
    if(type == 3 && m_arrWater.size() != nTile)
        m_arrWater = m_arrLand;
    else if(type == 0)
        m_arrWater.clear();

    QVector<SSecVertex> tileVertices(9);
    const auto resetTileVertices = [&tileVertices](
        CTile& tile, const QVector<QVector<SSecVertex>>& vertices,
        int row, int col) {
        tileVertices[0] = vertices[row * 2][col * 2];
        tileVertices[1] = vertices[row * 2][col * 2 + 1];
        tileVertices[2] = vertices[row * 2][col * 2 + 2];
        tileVertices[3] = vertices[row * 2 + 1][col * 2];
        tileVertices[4] = vertices[row * 2 + 1][col * 2 + 1];
        tileVertices[5] = vertices[row * 2 + 1][col * 2 + 2];
        tileVertices[6] = vertices[row * 2 + 2][col * 2];
        tileVertices[7] = vertices[row * 2 + 2][col * 2 + 1];
        tileVertices[8] = vertices[row * 2 + 2][col * 2 + 2];
        tile.resetVertices(tileVertices);
    };
    for(int row = 0; row < nTile; ++row)
        for(int col = 0; col < nTile; ++col)
        {
            resetTileVertices(m_arrLand[row][col], landVertex, row, col);
            if(type == 3)
                resetTileVertices(m_arrWater[row][col], waterVertex, row, col);
        }

    quint16 packed = 0;
    for(int row = 0; row < nTile; ++row)
        for(int col = 0; col < nTile; ++col)
        {
            stream >> packed;
            const STile decoded(packed);
            m_arrLand[row][col].setTile(
                decoded.m_index + decoded.m_texture * 64, decoded.m_rotation);
        }
    if(type == 3)
    {
        for(int row = 0; row < nTile; ++row)
            for(int col = 0; col < nTile; ++col)
            {
                stream >> packed;
                const STile decoded(packed);
                m_arrWater[row][col].setTile(
                    decoded.m_index + decoded.m_texture * 64, decoded.m_rotation);
            }
        qint16 material = -1;
        for(int row = 0; row < nTile; ++row)
            for(int col = 0; col < nTile; ++col)
            {
                stream >> material;
                m_arrWater[row][col].setMaterialIndex(material);
            }
    }
    if(stream.status() != QDataStream::Ok)
        return false;
    updateDrawData();
    return true;
}

bool CSector::previewCanonicalOverlayPayload(const QByteArray& payload)
{
    QByteArray copy = payload;
    QDataStream stream(&copy, QIODevice::ReadOnly);
    util::formatStream(stream);
    quint32 signature = 0;
    quint8 type = 0;
    stream >> signature >> type;
    if(signature != secSignature || (type != 0 && type != 3))
        return false;

    QVector<QVector<SSecVertex>> waterVertex;
    // Overlay-only previews never consume land vertices. Skipping their fixed
    // native byte block avoids decoding 1,089 packed normals on every live
    // overlay stamp.
    if(stream.skipRawData(nVertex * nVertex * 8) != nVertex * nVertex * 8)
        return false;
    if(type == 3)
    {
        waterVertex.resize(nVertex);
        for(int row = 0; row < nVertex; ++row)
        {
            waterVertex[row].resize(nVertex);
            for(int col = 0; col < nVertex; ++col)
                stream >> waterVertex[row][col];
        }
    }
    quint16 packed = 0;
    for(int tile = 0; tile < nTile * nTile; ++tile)
        stream >> packed; // land tiles are unchanged by overlay commands
    if(stream.status() != QDataStream::Ok)
        return false;

    if(type == 0)
    {
        m_arrWater.clear();
        updateWaterDrawData();
        return true;
    }
    if(m_arrWater.size() != nTile)
    {
        m_arrWater = m_arrLand;
        if(m_arrWater.size() != nTile)
            return false;
    }
    QVector<SSecVertex> tileVertices(9);
    for(int row = 0; row < nTile; ++row)
        for(int col = 0; col < nTile; ++col)
        {
            tileVertices[0] = waterVertex[row * 2][col * 2];
            tileVertices[1] = waterVertex[row * 2][col * 2 + 1];
            tileVertices[2] = waterVertex[row * 2][col * 2 + 2];
            tileVertices[3] = waterVertex[row * 2 + 1][col * 2];
            tileVertices[4] = waterVertex[row * 2 + 1][col * 2 + 1];
            tileVertices[5] = waterVertex[row * 2 + 1][col * 2 + 2];
            tileVertices[6] = waterVertex[row * 2 + 2][col * 2];
            tileVertices[7] = waterVertex[row * 2 + 2][col * 2 + 1];
            tileVertices[8] = waterVertex[row * 2 + 2][col * 2 + 2];
            m_arrWater[row][col].resetVertices(tileVertices);
            stream >> packed;
            const STile decoded(packed);
            m_arrWater[row][col].setTile(
                decoded.m_index + decoded.m_texture * 64, decoded.m_rotation);
        }
    qint16 material = -1;
    for(int row = 0; row < nTile; ++row)
        for(int col = 0; col < nTile; ++col)
        {
            stream >> material;
            m_arrWater[row][col].setMaterialIndex(material);
        }
    if(stream.status() != QDataStream::Ok)
        return false;
    updateWaterDrawData();
    return true;
}

bool CSector::previewCanonicalOverlayDelta(const QByteArray& beforePayload,
                                           const QByteArray& afterPayload)
{
    static const int vertexSize = 8;
    static const int vertexBlockSize = nVertex * nVertex * vertexSize;
    static const int tileBlockSize = nTile * nTile * int(sizeof(quint16));
    static const int landVertexOffset = 5;
    static const int overlayVertexOffset = landVertexOffset + vertexBlockSize;
    static const int overlayTileOffset = landVertexOffset
        + 2 * vertexBlockSize + tileBlockSize;
    static const int overlayMaterialOffset = overlayTileOffset + tileBlockSize;
    static const int overlaySectorSize = overlayMaterialOffset + tileBlockSize;

    if(beforePayload.size() < 5 || afterPayload.size() < 5
       || qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(
              beforePayload.constData())) != secSignature
       || qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(
              afterPayload.constData())) != secSignature
       || quint8(beforePayload.at(4)) != 3 || quint8(afterPayload.at(4)) != 3
       || beforePayload.size() < overlaySectorSize
       || afterPayload.size() < overlaySectorSize
       || m_arrWater.size() != nTile)
        return previewCanonicalOverlayPayload(afterPayload);

    QSet<int> changedTiles;
    bool rebuildIndices = false;
    const auto addVertexTiles = [&changedTiles](int vertexX, int vertexY)
    {
        const int maximumTileX = qMin(nTile - 1, vertexX / 2);
        const int maximumTileY = qMin(nTile - 1, vertexY / 2);
        const int minimumTileX = vertexX > 0 && vertexX % 2 == 0
            ? maximumTileX - 1 : maximumTileX;
        const int minimumTileY = vertexY > 0 && vertexY % 2 == 0
            ? maximumTileY - 1 : maximumTileY;
        for(int tileY = qMax(0, minimumTileY); tileY <= maximumTileY; ++tileY)
            for(int tileX = qMax(0, minimumTileX); tileX <= maximumTileX; ++tileX)
                changedTiles.insert(tileY * nTile + tileX);
    };
    for(int vertex = 0; vertex < nVertex * nVertex; ++vertex)
    {
        const int offset = overlayVertexOffset + vertex * vertexSize;
        if(std::memcmp(beforePayload.constData() + offset,
                       afterPayload.constData() + offset, vertexSize) != 0)
            addVertexTiles(vertex % nVertex, vertex / nVertex);
    }
    for(int tile = 0; tile < nTile * nTile; ++tile)
    {
        const int packedOffset = overlayTileOffset + tile * 2;
        const int materialOffset = overlayMaterialOffset + tile * 2;
        if(std::memcmp(beforePayload.constData() + packedOffset,
                       afterPayload.constData() + packedOffset, 2) != 0)
            changedTiles.insert(tile);
        if(std::memcmp(beforePayload.constData() + materialOffset,
                       afterPayload.constData() + materialOffset, 2) != 0)
        {
            changedTiles.insert(tile);
            rebuildIndices = true;
        }
    }
    if(changedTiles.isEmpty())
        return true;

    const auto readVertex = [&afterPayload](int x, int y)
    {
        const int offset = overlayVertexOffset + (y * nVertex + x) * vertexSize;
        const uchar* bytes = reinterpret_cast<const uchar*>(
            afterPayload.constData() + offset);
        SSecVertex vertex;
        vertex.xOffset = qint8(bytes[0]);
        vertex.yOffset = qint8(bytes[1]);
        vertex.z = qFromLittleEndian<quint16>(bytes + 2);
        vertex.packedNormal = qFromLittleEndian<quint32>(bytes + 4);
        vertex.normal.setX((int((vertex.packedNormal >> 11) & 0x7ff) - 1000) / 1000.0f);
        vertex.normal.setY((int(vertex.packedNormal & 0x7ff) - 1000) / 1000.0f);
        vertex.normal.setZ(int(vertex.packedNormal >> 22) / 1000.0f);
        return vertex;
    };
    for(int tile : changedTiles)
    {
        const int row = tile / nTile;
        const int col = tile % nTile;
        QVector<SSecVertex> vertices;
        vertices.reserve(9);
        for(int y = row * 2; y <= row * 2 + 2; ++y)
            for(int x = col * 2; x <= col * 2 + 2; ++x)
                vertices.append(readVertex(x, y));
        m_arrWater[row][col].resetVertices(vertices);
        const uchar* packedBytes = reinterpret_cast<const uchar*>(
            afterPayload.constData() + overlayTileOffset + tile * 2);
        const STile packed(qFromLittleEndian<quint16>(packedBytes));
        m_arrWater[row][col].setTile(
            packed.m_index + packed.m_texture * 64, packed.m_rotation);
        const uchar* materialBytes = reinterpret_cast<const uchar*>(
            afterPayload.constData() + overlayMaterialOffset + tile * 2);
        m_arrWater[row][col].setMaterialIndex(
            qFromLittleEndian<qint16>(materialBytes));
    }
    updateWaterTiles(changedTiles, rebuildIndices);
    return true;
}

void CSector::setPreviewMaxZ(float maxZ)
{
    for(QVector<CTile>& row : m_arrLand)
        for(CTile& tile : row)
            tile.setMaxZ(maxZ);
    for(QVector<CTile>& row : m_arrWater)
        for(CTile& tile : row)
            tile.setMaxZ(maxZ);
    updateDrawData();
}

QByteArray CSector::serializeSector()
{
    QByteArray secData;
    QDataStream secStream(&secData, QIODevice::WriteOnly);
    util::formatStream(secStream);
    bool bWater = !m_arrWater.isEmpty();
    secStream << secSignature;
    secStream << (bWater ? quint8(3) : quint8(0));

    //generate SSecVertex line by line
    QVector<SSecVertex> arrVrt;
    arrVrt.resize(nVertex*nVertex);
    QVector<ushort> arrLandTilePacked;
    arrLandTilePacked.resize(nTile*nTile);
    //water tiles
    QVector<SSecVertex> arrWaterVrt;
    QVector<ushort> arrWaterTilePacked;
    QVector<short> arrWaterMaterial;
    if(bWater)
    {
        arrWaterVrt.resize(nVertex*nVertex);
        arrWaterTilePacked.resize(nTile*nTile);
        arrWaterMaterial.resize(nTile*nTile);
    }

    auto tileToLine = [](QVector<SSecVertex>& outVrt, const QVector<QVector<SSecVertex>>& arr, int row, int col)
    { // converts 3x3 tile vertex data to 1 line of data
        outVrt[(row*2+0)*nVertex + col*2 + 0] = arr[0][0];
        outVrt[(row*2+0)*nVertex + col*2 + 1] = arr[0][1];
        outVrt[(row*2+1)*nVertex + col*2 + 0] = arr[1][0];
        outVrt[(row*2+1)*nVertex + col*2 + 1] = arr[1][1];
        if(col == nTile-1)
        { // last tile in row, write right side of tile
            outVrt[(row*2+0)*nVertex + col*2 + 2] = arr[0][2];
            outVrt[(row*2+1)*nVertex + col*2 + 2] = arr[1][2];
        }
        if(row == nTile-1)
        { // last tile row, write top side
            outVrt[(row*2+2)*nVertex + col*2 + 0] = arr[2][0];
            outVrt[(row*2+2)*nVertex + col*2 + 1] = arr[2][1];
            if(col == nTile-1)
            { // last tile of sector, write right side of tile
                outVrt[(row*2+0)*nVertex + col*2 + 2] = arr[0][2];
                outVrt[(row*2+1)*nVertex + col*2 + 2] = arr[1][2];
                outVrt[(row*2+2)*nVertex + col*2 + 2] = arr[2][2];
            }
        }
    };

    for(int row(0); row<nTile; ++row)
        for(int col(0); col<nTile; ++col)
        {
            tileToLine(arrVrt, m_arrLand[row][col].arrVertex(), row, col);
            arrLandTilePacked[row*nTile + col] = m_arrLand[row][col].packData();
            if(bWater)
            {
                tileToLine(arrWaterVrt, m_arrWater[row][col].arrVertex(), row, col);
                arrWaterTilePacked[row*nTile + col] = m_arrWater[row][col].packData();
                arrWaterMaterial[row*nTile + col] = m_arrWater[row][col].materialIndex();
            }
        }

    for(auto& vrt: arrVrt)
        secStream << vrt;

    if(bWater)
        for(auto& vrt: arrWaterVrt)
            secStream << vrt;

    for(auto& tile: arrLandTilePacked)
        secStream << tile;

    if(bWater)
    {
        for(auto& tile: arrWaterTilePacked)
            secStream << tile;

        for(auto& mat: arrWaterMaterial)
            secStream << mat;
    }

    return secData;
}

void SSpecificQuad::rotate(int step)
{
    Q_ASSERT(m_aVertexPointer.size() == 16);
    QVector<QVector2D> aTmpRow;
    aTmpRow.resize(4);
    for(int i(0); i<step; ++i)
    {
        aTmpRow[0] = m_aVertexPointer[0]->texCoord;
        aTmpRow[1] = m_aVertexPointer[1]->texCoord;
        aTmpRow[2] = m_aVertexPointer[2]->texCoord;
        aTmpRow[3] = m_aVertexPointer[3]->texCoord;

        m_aVertexPointer[0]->texCoord = m_aVertexPointer[5]->texCoord;
        m_aVertexPointer[1]->texCoord = m_aVertexPointer[6]->texCoord;
        m_aVertexPointer[2]->texCoord = m_aVertexPointer[7]->texCoord;
        m_aVertexPointer[3]->texCoord = m_aVertexPointer[4]->texCoord;

        m_aVertexPointer[4]->texCoord  = m_aVertexPointer[13]->texCoord;
        m_aVertexPointer[5]->texCoord  = m_aVertexPointer[14]->texCoord;
        m_aVertexPointer[6]->texCoord = m_aVertexPointer[15]->texCoord;
        m_aVertexPointer[7]->texCoord = m_aVertexPointer[12]->texCoord;

        m_aVertexPointer[12]->texCoord = m_aVertexPointer[9]->texCoord;
        m_aVertexPointer[13]->texCoord = m_aVertexPointer[10]->texCoord;
        m_aVertexPointer[14]->texCoord = m_aVertexPointer[11]->texCoord;
        m_aVertexPointer[15]->texCoord = m_aVertexPointer[8]->texCoord;

        m_aVertexPointer[8]->texCoord  = aTmpRow[1];
        m_aVertexPointer[9]->texCoord  = aTmpRow[2];
        m_aVertexPointer[10]->texCoord = aTmpRow[3];
        m_aVertexPointer[11]->texCoord = aTmpRow[0];
    }
    m_rotateState = step;
}

void CSector::makeVertexData(QVector<QVector<SSecVertex>>& aLandVertex, QVector<STile>& aLandTile,
                             QVector<QVector<SSecVertex>>& aWaterVertex, QVector<STile>& aWaterTile,
                             float maxZ, int texCount)
{
/*
^|   6 _7 _8
c|   |\ |\ |
o|   |_\|_\|
l|   3 _4 _5
u|   |\ |\ |
m|   |_\|_\|
n|   0  1  2 ROW---->
specific quad is
4 land quads -> 16 points -> 16 indexes,  {0,1,4,3}, {1,2,5,4}, {3,4,7,6}, {4,5,8,7}
1 tile -> 16 tex coords with the same indexes
TODO: use 9 vertex, make quad via index array
*/

    Q_ASSERT(aLandVertex.size() == 33);
    Q_ASSERT(aLandTile.size() == 256);
    QVector<QVector2D> tCoord; // min + max
    tCoord.resize(2);
    //SVertexData vData;
    m_arrLandVrtData.resize(aLandTile.size()*16);
    m_arrWaterVrtData.resize(aWaterTile.size()*16);
    int idVert(0);

    auto calcTexCoordBox = [&tCoord, texCount](STile& tile)
    {
      tCoord[0].setX((tile.m_index%8/8.0f+tile.m_texture)/texCount);
      tCoord[0].setY((7-int(tile.m_index/8))/8.0f);

      tCoord[1].setX(tCoord[0].x() + 1.0f/8.0f/texCount);
      tCoord[1].setY(tCoord[0].y() + 1.0f/8.0f);

      tCoord[0] += QVector2D(4.0f/512.0f/texCount, 4.0f/512.0f/texCount);
      tCoord[1] -= QVector2D(4.0f/512.0f/texCount, 4.0f/512.0f/texCount);
    };

    int vertIndexQuad(0);

    auto calcTexCoord = [&vertIndexQuad, &tCoord](SVertexData& vrt)
    {
        QVector2D texC = QVector2D(tCoord[0].x(), tCoord[1].y());
        switch(vertIndexQuad)
        {
        case 1:
        case 2:
        case 9:
        case 10:
        case 4:
        case 7:
        case 12:
        case 15:
        {
            texC.setX((tCoord[0].x() + tCoord[1].x())/2.0f);
            break;
        }
        case 5:
        case 6:
        case 13:
        case 14:
        {
            texC.setX(tCoord[1].x());
            break;
        }
        default:
            break;
        }

        switch (vertIndexQuad)
        {
        case 3:
        case 2:
        case 7:
        case 6:
        case 8:
        case 9:
        case 12:
        case 13:
        {
            texC.setY((tCoord[0].y() + tCoord[1].y())/2.0f);
            break;
        }
        case 11:
        case 10:
        case 15:
        case 14:
        {
            texC.setY(tCoord[0].y());
            break;
        }
        }

        vrt.texCoord = texC;
    };

    auto buildGeom = [maxZ, &vertIndexQuad, &calcTexCoord](QVector<QVector<SSecVertex>>& aVertex, SVertexData& vrt, int x, int y)
    {
        if(vertIndexQuad == 1 || vertIndexQuad == 2 || vertIndexQuad == 9 || vertIndexQuad == 10
            || vertIndexQuad == 4 || vertIndexQuad == 7 || vertIndexQuad == 12 || vertIndexQuad == 15)
            x+=1;
        else if(vertIndexQuad == 5 || vertIndexQuad == 6 || vertIndexQuad == 13 || vertIndexQuad == 14)
            x+=2;
        if(vertIndexQuad == 3 || vertIndexQuad == 2 || vertIndexQuad == 7 || vertIndexQuad == 6
            || vertIndexQuad == 8 || vertIndexQuad == 9 || vertIndexQuad == 12 || vertIndexQuad == 13)
            y+=1;
        else if(vertIndexQuad == 11 || vertIndexQuad == 10 || vertIndexQuad == 15 || vertIndexQuad == 14)
            y+=2;
        vrt.position.setX(x+aVertex[y][x].xOffset/254.0f);
        vrt.position.setY(y+aVertex[y][x].yOffset/254.0f);
        vrt.position.setZ(aVertex[y][x].z*maxZ/65535.0f);
        vrt.normal = aVertex[y][x].normal;
        calcTexCoord(vrt);
    };

    for(int i(0); i<aLandTile.size(); ++i)
    {
        calcTexCoordBox(aLandTile[i]);
        SSpecificQuad quad;
        int vrtX = i%16*2;
        int vrtY = int(i/16*2);
        for(vertIndexQuad = 0; vertIndexQuad < 16 ; ++vertIndexQuad)
        {
            buildGeom(aLandVertex, m_arrLandVrtData[idVert], vrtX, vrtY);
            quad.addVertex(&m_arrLandVrtData[idVert]);
            ++idVert;
        }
        quad.rotate(aLandTile[i].m_rotation);
    }

    idVert=0;
    for(int i(0); i<aWaterTile.size(); ++i)
    {
        calcTexCoordBox(aWaterTile[i]);
        SSpecificQuad quad;
        int vrtX = i%16*2;
        int vrtY = int(i/16*2);
        for(vertIndexQuad = 0; vertIndexQuad < 16 ; ++vertIndexQuad)
        {
            buildGeom(aWaterVertex, m_arrWaterVrtData[idVert], vrtX, vrtY);
            quad.addVertex(&m_arrWaterVrtData[idVert]);
            ++idVert;
        }
        quad.rotate(aWaterTile[i].m_rotation);
    }
}

void CSector::generateVertexDataFromTile()
{
    // CTile::generateDrawVertexData emits the 3x3 vertex grid once per tile.
    // The previous allocation retained the old GL_QUADS-era value of 16 and
    // drawWater() consequently derived an index count beyond its index buffer.
    // Desktop drivers happened to tolerate that invalid draw, while ANGLE
    // correctly rejects it with GL_INVALID_OPERATION.
    const int vertSize = m_arrLand.size() * m_arrLand.front().size() * 9;
    //m_aVertexData.clear(); // to recalc textures ?
    m_arrLandVrtData.resize(vertSize); // todo: check size. vrt size == tile count * 9 (verts per tile)
    int curIndex(0);
    for(int row(0); row<m_arrLand.size(); ++row)
        for(int col(0); col<m_arrLand[row].size(); ++col)
            m_arrLand[row][col].generateDrawVertexData(m_arrLandVrtData, curIndex);

    Q_ASSERT(curIndex == m_arrLandVrtData.size());
    generateWaterVertexData();
}

void CSector::generateWaterVertexData()
{
    const bool hasWater = !m_arrWater.isEmpty();
    if(!hasWater)
    {
        m_arrWaterVrtData.clear();
        m_arrAnimatedWaterVrtData.clear();
    }
    else
    {
        const int vertSize = m_arrWater.size() * m_arrWater.front().size() * 9;
        m_arrWaterVrtData.fill(SVertexData(), vertSize);
        int waterIndex = 0;
        for(int row = 0; row < m_arrWater.size(); ++row)
            for(int col = 0; col < m_arrWater.at(row).size(); ++col)
            {
                if(m_arrWater.at(row).at(col).materialIndex() >= 0)
                    m_arrWater[row][col].generateDrawVertexData(
                        m_arrWaterVrtData, waterIndex);
                else
                    waterIndex += 9;
            }
        Q_ASSERT(waterIndex == m_arrWaterVrtData.size());
        m_arrAnimatedWaterVrtData.fill(SWaterVertexData(),
                                       m_arrWaterVrtData.size());
    }

    QVector<qint16> overlayMaterials;
    overlayMaterials.reserve(nTile * nTile);
    for(int row = 0; hasWater && row < m_arrWater.size(); ++row)
        for(int col = 0; col < m_arrWater.at(row).size(); ++col)
            overlayMaterials.append(qint16(m_arrWater.at(row).at(col).materialIndex()));
    for(int row = 0; hasWater && row < m_arrWater.size(); ++row)
        for(int col = 0; col < m_arrWater.at(row).size(); ++col)
        {
            const int materialIndex = m_arrWater.at(row).at(col).materialIndex();
            if(materialIndex < 0)
                continue;
            const SMaterial* material = m_materials && materialIndex >= 0
                    && materialIndex < m_materials->size()
                ? &m_materials->at(materialIndex) : nullptr;
            const ei::SOverlayMaterialAnimation animation =
                ei::overlayMaterialAnimation(material);
            for(int vertex = 0; vertex < 9; ++vertex)
            {
                const int index = (row * nTile + col) * 9 + vertex;
                const SVertexData& source = m_arrWaterVrtData.at(index);
                SWaterVertexData& target = m_arrAnimatedWaterVrtData[index];
                target.position = source.position;
                target.normal = source.normal;
                target.texCoord = source.texCoord;
                const int gridX = col * 2 + vertex % 3;
                const int gridY = row * 2 + vertex / 3;
                const int sharedMaterialIndex = ei::sharedOverlayMaterialIndex(
                    overlayMaterials, nTile, nTile, gridX, gridY);
                const SMaterial* sharedMaterial = m_materials
                        && sharedMaterialIndex >= 0
                        && sharedMaterialIndex < m_materials->size()
                    ? &m_materials->at(sharedMaterialIndex) : nullptr;
                target.animation = QVector4D(animation.waveMultiplier,
                    animation.authoredWarpSpeed, ei::overlayWindNoise(gridX, gridY),
                    animation.animated ? 1.0f : 0.0f);
                target.grid = QVector2D(float(gridX), float(gridY));
                target.emission = ei::terrainMaterialEmission(sharedMaterial);
            }
        }

    m_arrLitLandVrtData.resize(m_arrLandVrtData.size());
    for(int row = 0; row < m_arrLand.size(); ++row)
        for(int col = 0; col < m_arrLand.at(row).size(); ++col)
            for(int vertex = 0; vertex < 9; ++vertex)
            {
                const int index = (row * nTile + col) * 9 + vertex;
                const SVertexData& source = m_arrLandVrtData.at(index);
                STerrainVertexData& target = m_arrLitLandVrtData[index];
                target.position = source.position;
                target.normal = source.normal;
                target.texCoord = source.texCoord;
                const int gridX = col * 2 + vertex % 3;
                const int gridY = row * 2 + vertex / 3;
                const int sharedMaterialIndex = ei::sharedOverlayMaterialIndex(
                    overlayMaterials, nTile, nTile, gridX, gridY);
                const SMaterial* sharedMaterial = m_materials
                        && sharedMaterialIndex >= 0
                        && sharedMaterialIndex < m_materials->size()
                    ? &m_materials->at(sharedMaterialIndex) : nullptr;
                target.emission = QVector3D();
                target.shadowVisibility = 1.0f;
                if(sharedMaterial && hasWater)
                {
                    const QVector3D overlayEmission =
                        ei::terrainMaterialEmission(sharedMaterial);
                    const SVertexData& overlay = m_arrWaterVrtData.at(index);
                    target.emission = ei::terrainUnderlayEmission(overlayEmission,
                        source.position.z(), overlay.position.z());
                }
            }
}

void CSector::updateBounds()
{
    QVector3D minimum;
    QVector3D maximum;
    bool initialized = false;
    const auto includeVertices = [&minimum, &maximum, &initialized](
        const QVector<SVertexData>& vertices) {
        for(const SVertexData& vertex : vertices)
        {
            if(!initialized)
            {
                minimum = maximum = vertex.position;
                initialized = true;
            }
            else
            {
                minimum.setX(qMin(minimum.x(), vertex.position.x()));
                minimum.setY(qMin(minimum.y(), vertex.position.y()));
                minimum.setZ(qMin(minimum.z(), vertex.position.z()));
                maximum.setX(qMax(maximum.x(), vertex.position.x()));
                maximum.setY(qMax(maximum.y(), vertex.position.y()));
                maximum.setZ(qMax(maximum.z(), vertex.position.z()));
            }
        }
    };
    includeVertices(m_arrLandVrtData);
    includeVertices(m_arrWaterVrtData);
    const QVector3D sectorOffset(float(m_index.x) * 32.0f,
                                 float(m_index.y) * 32.0f, 0.0f);
    if(initialized)
    {
        m_boundsCenter = (minimum + maximum) * 0.5f + sectorOffset;
        m_boundsRadius = (maximum - minimum).length() * 0.5f;
        if(!m_arrWaterVrtData.isEmpty())
            m_boundsRadius += 1.0f;
    }
    else
    {
        m_boundsCenter = sectorOffset + QVector3D(16.0f, 16.0f, 0.0f);
        m_boundsRadius = 23.0f;
    }
}

void CSector::updateWaterTiles(const QSet<int>& localTiles, bool rebuildIndices)
{
    const int expectedVertices = nTile * nTile * 9;
    if(m_arrWaterVrtData.size() != expectedVertices
       || m_arrAnimatedWaterVrtData.size() != expectedVertices)
    {
        updateWaterDrawData(true);
        return;
    }
    for(int tile : localTiles)
    {
        if(tile < 0 || tile >= nTile * nTile)
            continue;
        const int row = tile / nTile;
        const int col = tile % nTile;
        int vertexIndex = tile * 9;
        if(m_arrWater.at(row).at(col).materialIndex() >= 0)
            m_arrWater[row][col].generateDrawVertexData(
                m_arrWaterVrtData, vertexIndex);
        else
            for(int vertex = 0; vertex < 9; ++vertex)
                m_arrWaterVrtData[tile * 9 + vertex] = SVertexData();

        const int materialIndex = m_arrWater.at(row).at(col).materialIndex();
        const SMaterial* material = m_materials && materialIndex >= 0
                && materialIndex < m_materials->size()
            ? &m_materials->at(materialIndex) : nullptr;
        const ei::SOverlayMaterialAnimation animation =
            ei::overlayMaterialAnimation(material);
        for(int vertex = 0; vertex < 9; ++vertex)
        {
            const int index = tile * 9 + vertex;
            const SVertexData& source = m_arrWaterVrtData.at(index);
            SWaterVertexData& target = m_arrAnimatedWaterVrtData[index];
            target.position = source.position;
            target.normal = source.normal;
            target.texCoord = source.texCoord;
            const int gridX = col * 2 + vertex % 3;
            const int gridY = row * 2 + vertex / 3;
            target.animation = QVector4D(animation.waveMultiplier,
                animation.authoredWarpSpeed, ei::overlayWindNoise(gridX, gridY),
                animation.animated ? 1.0f : 0.0f);
            target.grid = QVector2D(float(gridX), float(gridY));
        }
    }

    // Material edits can change the shared vertex chosen at a tile boundary.
    // Refresh the inexpensive emissive payload for the sector while retaining
    // the branch's incremental geometry rebuild and single orphaning upload.
    QVector<qint16> overlayMaterials;
    overlayMaterials.reserve(nTile * nTile);
    for(int row = 0; row < m_arrWater.size(); ++row)
        for(int col = 0; col < m_arrWater.at(row).size(); ++col)
            overlayMaterials.append(qint16(
                m_arrWater.at(row).at(col).materialIndex()));
    for(int row = 0; row < m_arrWater.size(); ++row)
        for(int col = 0; col < m_arrWater.at(row).size(); ++col)
            for(int vertex = 0; vertex < 9; ++vertex)
            {
                const int index = (row * nTile + col) * 9 + vertex;
                const int gridX = col * 2 + vertex % 3;
                const int gridY = row * 2 + vertex / 3;
                const int sharedMaterialIndex = ei::sharedOverlayMaterialIndex(
                    overlayMaterials, nTile, nTile, gridX, gridY);
                const SMaterial* sharedMaterial = m_materials
                        && sharedMaterialIndex >= 0
                        && sharedMaterialIndex < m_materials->size()
                    ? &m_materials->at(sharedMaterialIndex) : nullptr;
                m_arrAnimatedWaterVrtData[index].emission =
                    ei::terrainMaterialEmission(sharedMaterial);
                if(index < m_arrLitLandVrtData.size())
                {
                    const SVertexData& land = m_arrLandVrtData.at(index);
                    const SVertexData& overlay = m_arrWaterVrtData.at(index);
                    m_arrLitLandVrtData[index].emission =
                        ei::terrainUnderlayEmission(
                            ei::terrainMaterialEmission(sharedMaterial),
                            land.position.z(), overlay.position.z());
                }
            }
    updateBounds();
    m_waterVertexBuf.bind();
    // Match the fast terrain-sculpt ownership contract: glBufferData replaces
    // (orphans) the live allocation so the GPU can finish drawing the previous
    // frame without serializing against CPU writes. QOpenGLBuffer::write uses
    // glBufferSubData on the buffer currently consumed by drawWater(), which
    // caused a driver fence on every overlay stamp despite the small upload.
    m_waterVertexBuf.allocate(m_arrAnimatedWaterVrtData.constData(),
        m_arrAnimatedWaterVrtData.size() * int(sizeof(SWaterVertexData)));
    m_waterVertexBuf.release();
    m_vertexBuf.bind();
    m_vertexBuf.allocate(m_arrLitLandVrtData.constData(),
        m_arrLitLandVrtData.size() * int(sizeof(STerrainVertexData)));
    m_vertexBuf.release();
    if(!rebuildIndices)
        return;

    const QVector<ushort> tileIndices{
        0,1,4, 0,4,3, 1,2,5, 1,5,4,
        3,4,7, 3,7,6, 4,5,8, 4,8,7
    };
    QVector<ushort> waterIndices;
    waterIndices.reserve(nTile * nTile * tileIndices.size());
    for(int row = 0; row < nTile; ++row)
        for(int col = 0; col < nTile; ++col)
        {
            if(m_arrWater.at(row).at(col).materialIndex() < 0)
                continue;
            const ushort tileOffset = ushort((row * nTile + col) * 9);
            for(ushort index : tileIndices)
                waterIndices.append(index + tileOffset);
        }
    m_waterIndexCount = waterIndices.size();
    m_waterIndexBuf.bind();
    m_waterIndexBuf.allocate(waterIndices.constData(),
        waterIndices.size() * int(sizeof(ushort)));
    m_waterIndexBuf.release();
}

void CSector::updateWaterDrawData(bool rebuildIndices)
{
    generateWaterVertexData();
    updateBounds();
    if(m_arrWaterVrtData.isEmpty())
    {
        m_waterIndexCount = 0;
        return;
    }
    m_waterVertexBuf.bind();
    m_waterVertexBuf.allocate(m_arrAnimatedWaterVrtData.data(),
        m_arrAnimatedWaterVrtData.count() * int(sizeof(SWaterVertexData)));
    m_waterVertexBuf.release();
    if(!rebuildIndices)
        return;
    const QVector<ushort> tileIndices{
        0,1,4, 0,4,3,
        1,2,5, 1,5,4,
        3,4,7, 3,7,6,
        4,5,8, 4,8,7
    };
    QVector<ushort> waterIndices;
    waterIndices.reserve(nTile * nTile * tileIndices.size());
    for(int row = 0; row < nTile; ++row)
        for(int col = 0; col < nTile; ++col)
        {
            if(m_arrWater.at(row).at(col).materialIndex() < 0)
                continue;
            const ushort tileOffset = ushort((row * nTile + col) * 9);
            for(ushort index : tileIndices)
                waterIndices.append(index + tileOffset);
        }
    m_waterIndexCount = waterIndices.size();
    m_waterIndexBuf.bind();
    m_waterIndexBuf.allocate(waterIndices.data(),
                             waterIndices.count() * int(sizeof(ushort)));
    m_waterIndexBuf.release();
}

void CSector::updatePosition()
{
    m_modelMatrix.translate(QVector3D(m_index.x*32.0f, m_index.y*32.0f, .0f));
    updateBounds();
    m_vertexBuf.bind();
    m_vertexBuf.allocate(m_arrLitLandVrtData.data(),
                         m_arrLitLandVrtData.count()*int(sizeof(STerrainVertexData)));
    m_vertexBuf.release();

    QVector<ushort> aInd;
    ushort indOffset = 0;
    // OpenGL ES 2.0 (the Qt 5 ANGLE path) does not support GL_QUADS. Store
    // the same four terrain cells as ordinary triangles so desktop and GLES
    // consume one portable index buffer.
    const QVector<ushort> arrTileInd{
        0,1,4, 0,4,3,
        1,2,5, 1,5,4,
        3,4,7, 3,7,6,
        4,5,8, 4,8,7
    };
    for(int i(0); i<256; ++i) // 256 - tile number per sector
    {
        for(auto ind: arrTileInd)
        {
            aInd.append(ind += indOffset);
        }
        indOffset += 9; // vertices per tile.
    }

    m_indexBuf.bind();
    m_indexBuf.allocate(aInd.data(), aInd.count() * int(sizeof(ushort)));
    m_indexBuf.release();

    QVector<ushort> gridIndices;
    gridIndices.reserve(256 * 8);
    indOffset = 0;
    const QVector<ushort> tileBorder{0, 2, 2, 8, 8, 6, 6, 0};
    for(int i = 0; i < 256; ++i)
    {
        for(ushort index : tileBorder)
            gridIndices.append(index + indOffset);
        indOffset += 9;
    }
    m_gridIndexBuf.bind();
    m_gridIndexBuf.allocate(gridIndices.data(), gridIndices.count() * int(sizeof(ushort)));
    m_gridIndexBuf.release();

    //water section
    if(!m_arrWaterVrtData.isEmpty())
    {
        //m_modelMatrix.translate(QVector3D(m_index.x*32.0f, m_index.y*32.0f, .0f));
        m_waterVertexBuf.bind();
        m_waterVertexBuf.allocate(m_arrAnimatedWaterVrtData.data(),
            m_arrAnimatedWaterVrtData.count() * int(sizeof(SWaterVertexData)));
        m_waterVertexBuf.release();

        QVector<ushort> waterIndices;
        waterIndices.reserve(aInd.size());
        for(int row = 0; row < nTile; ++row)
            for(int col = 0; col < nTile; ++col)
            {
                if(m_arrWater.at(row).at(col).materialIndex() < 0)
                    continue;
                const ushort tileOffset = ushort((row * nTile + col) * 9);
                for(ushort index : arrTileInd)
                    waterIndices.append(index + tileOffset);
            }
        m_waterIndexCount = waterIndices.size();
        m_waterIndexBuf.bind();
        m_waterIndexBuf.allocate(waterIndices.data(),
                                 waterIndices.count() * int(sizeof(ushort)));
        m_waterIndexBuf.release();
    }
    else
        m_waterIndexCount = 0;
}

bool CSector::pickTile(int& outRow, int& outCol, QVector3D& point, bool bLand)
{
    if(!bLand && m_arrWater.isEmpty())
        return false;

    // convert pos to local coords
    point.setX(point.x()-m_index.x*32.0f);
    point.setY(point.y()-m_index.y*32.0f);

    if(bLand)
    {
        for(int row(0); row<m_arrLand.size(); ++row)
            for(int col(0); col<m_arrLand[row].size(); ++col)
            {
                if(m_arrLand[row][col].isProjectTile(point))
                {
                    outRow = row;
                    outCol = col;
                    return true;
                }
            }
    }
    else
    {
        for(int row(0); row<m_arrWater.size(); ++row)
            for(int col(0); col<m_arrWater[row].size(); ++col)
            {
                if(m_arrWater[row][col].materialIndex() >= 0
                   && m_arrWater[row][col].isProjectPoint(point))
                {
                    outRow = row;
                    outCol = col;
                    return true;
                }
            }
    }
    return false;
}


void CSector::draw(QOpenGLShaderProgram* program)
{

    if (m_arrLandVrtData.count() == 0)
        return;

    program->setUniformValue("u_modelMmatrix", m_modelMatrix);

    int offset = 0;
    m_vertexBuf.bind();
    int vertLoc = program->attributeLocation("a_position");
    program->enableAttributeArray(vertLoc);
    program->setAttributeBuffer(vertLoc, GL_FLOAT, offset, 3, int(sizeof(STerrainVertexData)));

    offset += int(sizeof(QVector3D));
    int normLoc = program->attributeLocation("a_normal");
    program->enableAttributeArray(normLoc);
    program->setAttributeBuffer(normLoc, GL_FLOAT, offset, 3, int(sizeof(STerrainVertexData)));

    offset+=int(sizeof(QVector3D)); // size of normal
    int textureLocation = program->attributeLocation("a_texture");
    program->enableAttributeArray(textureLocation);
    program->setAttributeBuffer(textureLocation, GL_FLOAT, offset, 2, int(sizeof(STerrainVertexData)));
    offset += int(sizeof(QVector2D));
    const int emissionLocation = program->attributeLocation("a_materialEmission");
    if(emissionLocation >= 0)
    {
        program->enableAttributeArray(emissionLocation);
        program->setAttributeBuffer(emissionLocation, GL_FLOAT, offset, 3,
                                    int(sizeof(STerrainVertexData)));
    }
    offset += int(sizeof(QVector3D));
    const int terrainShadowLocation = program->attributeLocation("a_terrainSunVisibility");
    if(terrainShadowLocation >= 0)
    {
        program->enableAttributeArray(terrainShadowLocation);
        program->setAttributeBuffer(terrainShadowLocation, GL_FLOAT, offset, 1,
                                    int(sizeof(STerrainVertexData)));
    }
    const int animationLocation = program->attributeLocation("a_overlayAnimation");
    if(animationLocation >= 0)
    {
        program->disableAttributeArray(animationLocation);
        program->setAttributeValue(animationLocation, QVector4D());
    }
    const int gridLocation = program->attributeLocation("a_overlayGrid");
    if(gridLocation >= 0)
    {
        program->disableAttributeArray(gridLocation);
        program->setAttributeValue(gridLocation, QVector2D());
    }

    m_indexBuf.bind();
    constexpr int indexNum = 256*24; // 256 tiles * 4 cells * 2 triangles * 3 indices
    ei::gl::DrawElements(GL_TRIANGLES, indexNum, GL_UNSIGNED_SHORT, nullptr);
    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    if(performance.collecting())
    {
        ++performance.counters().drawCalls;
        ++performance.counters().terrainDrawCalls;
        performance.counters().triangles += quint64(indexNum / 3);
    }

}

void CSector::collectTerrainHeights(QVector<float>& heights,
                                    QVector<quint8>& present,
                                    int gridWidth, int gridHeight) const
{
    const float offsetX = float(m_index.x) * 32.0f;
    const float offsetY = float(m_index.y) * 32.0f;
    for(const SVertexData& vertex : m_arrLandVrtData)
    {
        const int x = qRound(vertex.position.x() + offsetX);
        const int y = qRound(vertex.position.y() + offsetY);
        if(x < 0 || y < 0 || x >= gridWidth || y >= gridHeight)
            continue;
        const int index = y * gridWidth + x;
        heights[index] = vertex.position.z();
        present[index] = 1;
    }
}

void CSector::applyTerrainShadowVisibility(const QVector<float>& visibility,
                                           int gridWidth, int gridHeight)
{
    const float offsetX = float(m_index.x) * 32.0f;
    const float offsetY = float(m_index.y) * 32.0f;
    for(int i = 0; i < m_arrLitLandVrtData.size(); ++i)
    {
        STerrainVertexData& vertex = m_arrLitLandVrtData[i];
        const int x = qRound(vertex.position.x() + offsetX);
        const int y = qRound(vertex.position.y() + offsetY);
        vertex.shadowVisibility = x >= 0 && y >= 0
                && x < gridWidth && y < gridHeight
            ? visibility.value(y * gridWidth + x, 1.0f) : 1.0f;
    }
    m_vertexBuf.bind();
    m_vertexBuf.allocate(m_arrLitLandVrtData.constData(),
        m_arrLitLandVrtData.size() * int(sizeof(STerrainVertexData)));
    m_vertexBuf.release();
}

void CSector::drawWater(QOpenGLShaderProgram *program)
{
    if (m_arrWaterVrtData.count() == 0 || m_waterIndexCount == 0)
        return;

    program->setUniformValue("u_modelMmatrix", m_modelMatrix);

    int offset = 0;
    m_waterVertexBuf.bind();
    int vertLoc = program->attributeLocation("a_position");
    program->enableAttributeArray(vertLoc);
    program->setAttributeBuffer(vertLoc, GL_FLOAT, offset, 3, int(sizeof(SWaterVertexData)));

    offset += int(sizeof(QVector3D));
    int normLoc = program->attributeLocation("a_normal");
    program->enableAttributeArray(normLoc);
    program->setAttributeBuffer(normLoc, GL_FLOAT, offset, 3, int(sizeof(SWaterVertexData)));

    offset+=int(sizeof(QVector3D)); // size of normal
    int textureLocation = program->attributeLocation("a_texture");
    program->enableAttributeArray(textureLocation);
    program->setAttributeBuffer(textureLocation, GL_FLOAT, offset, 2, int(sizeof(SWaterVertexData)));

    offset += int(sizeof(QVector2D));
    const int animationLocation = program->attributeLocation("a_overlayAnimation");
    if(animationLocation >= 0)
    {
        program->enableAttributeArray(animationLocation);
        program->setAttributeBuffer(animationLocation, GL_FLOAT, offset, 4,
                                    int(sizeof(SWaterVertexData)));
    }
    offset += int(sizeof(QVector4D));
    const int gridLocation = program->attributeLocation("a_overlayGrid");
    if(gridLocation >= 0)
    {
        program->enableAttributeArray(gridLocation);
        program->setAttributeBuffer(gridLocation, GL_FLOAT, offset, 2,
                                    int(sizeof(SWaterVertexData)));
    }
    offset += int(sizeof(QVector2D));
    const int emissionLocation = program->attributeLocation("a_materialEmission");
    if(emissionLocation >= 0)
    {
        program->enableAttributeArray(emissionLocation);
        program->setAttributeBuffer(emissionLocation, GL_FLOAT, offset, 3,
                                    int(sizeof(SWaterVertexData)));
    }
    const int terrainShadowLocation = program->attributeLocation("a_terrainSunVisibility");
    if(terrainShadowLocation >= 0)
    {
        program->disableAttributeArray(terrainShadowLocation);
        program->setAttributeValue(terrainShadowLocation, 1.0f);
    }

    m_waterIndexBuf.bind();
    ei::gl::DrawElements(GL_TRIANGLES, m_waterIndexCount, GL_UNSIGNED_SHORT, nullptr);
    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    if(performance.collecting())
    {
        ++performance.counters().drawCalls;
        ++performance.counters().terrainDrawCalls;
        performance.counters().triangles += quint64(m_waterIndexCount / 3);
    }
}

bool CSector::intersectsFrustum(const CViewFrustum& frustum) const
{
    return frustum.intersectsSphere(m_boundsCenter, m_boundsRadius);
}

void CSector::drawTileGrid(QOpenGLShaderProgram* program)
{
    if(m_arrLandVrtData.isEmpty())
        return;
    program->setUniformValue("u_modelMmatrix", m_modelMatrix);
    m_vertexBuf.bind();
    int offset = 0;
    const int position = program->attributeLocation("a_position");
    program->enableAttributeArray(position);
    program->setAttributeBuffer(position, GL_FLOAT, offset, 3, int(sizeof(SVertexData)));
    offset += int(sizeof(QVector3D));
    const int normal = program->attributeLocation("a_normal");
    program->enableAttributeArray(normal);
    program->setAttributeBuffer(normal, GL_FLOAT, offset, 3, int(sizeof(SVertexData)));
    offset += int(sizeof(QVector3D));
    const int texture = program->attributeLocation("a_texture");
    program->enableAttributeArray(texture);
    program->setAttributeBuffer(texture, GL_FLOAT, offset, 2, int(sizeof(SVertexData)));
    m_gridIndexBuf.bind();
    glDrawElements(GL_LINES, 256 * 8, GL_UNSIGNED_SHORT, nullptr);
}

bool CSector::projectPt(QVector3D& point, bool land)
{
    QVector<QVector<CTile>>& tiles = land ? m_arrLand : m_arrWater;
    if(tiles.isEmpty())
        return false;
    QVector3D origin(point.x()-m_index.x*32.0f, point.y()-m_index.y*32.0f, 0.0f); // point in sector local coords

    int approxDeviation = 2;
    int xApprox = origin.x()/2;
    int xMin = qMax(0, xApprox-approxDeviation);
    int xMax = qMin(16, xApprox+approxDeviation);

    int yApprox = origin.y()/2;
    int yMin = qMax(0, yApprox-approxDeviation);
    int yMax = qMin(16, yApprox+approxDeviation);

    for(int row(yMin); row<yMax; ++row)
        for(int col(xMin); col<xMax; ++col)
        {
            if((land || tiles[row][col].materialIndex() >= 0)
               && tiles[row][col].isProjectPoint(origin))
            {
                point.setZ(point.z() + origin.z());
                return true;
            }
        }

    return false;
}

void CSector::setTile(QVector3D& point, int index, int rotNum, bool bLand, int matIndex)
{
    int row, col;

    if(!pickTile(row, col, point, bLand))
        return;

    if(bLand)
        m_arrLand[row][col].setTile(index, rotNum);
    else
    {
        m_arrWater[row][col].setTile(index, rotNum);
        m_arrWater[row][col].setMaterialIndex(short(matIndex));
    }
    generateVertexDataFromTile(); //todo: apply changes locally, stop re-generating all data
    m_modelMatrix.setToIdentity(); // todo
    updatePosition(); //todo
}

//void CSector::setTile(const STileLocation tileLoc, const STileInfo tileInfo)
//{
//    CTile* pTile = nullptr;
//    if(tileLoc.bLand)
//        pTile = &(m_arrLand[tileLoc.row][tileLoc.col]);
//    else
//    {
//        pTile = &(m_arrWater[tileLoc.row][tileLoc.col]);
//        pTile->setMaterialIndex(tileInfo.matIndex);
//    }
//    pTile->setTile(tileInfo.index, tileInfo.rotNum);
//    updateDrawData();
//}

void CSector::setTile(const QMap<STileLocation, STileInfo>& arrTileInfo)
{
    CTile* pTile = nullptr;
    for(auto& tile: arrTileInfo.toStdMap())
    {
        if(tile.first.bLand)
            pTile = &(m_arrLand[tile.first.row][tile.first.col]);
        else
        {
            pTile = &(m_arrWater[tile.first.row][tile.first.col]);
            pTile->setMaterialIndex(tile.second.matIndex);
        }
        pTile->setTile(tile.second.index, tile.second.rotNum);
    }
    updateDrawData();
}

bool CSector::existsTileIndices(const QVector<int>& arrInd)
{
    bool bRes = false;
    for(int row(0); row<m_arrLand.size(); ++row)
        for(int col(0); col<m_arrLand[row].size(); ++col)
        {
            if(arrInd.contains(m_arrLand[row][col].tileIndex()))
            {
                qDebug() << "tile row: " << row << " col: " << col << " has invalid index of tile:" << m_arrLand[row][col].tileIndex();
                bRes = true;
            }
        }
    return bRes;
}

void CSector::updateDrawData()
{
    generateVertexDataFromTile(); //todo: apply changes locally, stop re-generating all data
    m_modelMatrix.setToIdentity(); // todo
    updatePosition(); //todo
}

STile::STile(ushort packedData)
{
    m_index = packedData & 63; //first 6 bits
    m_texture = (packedData >> 6) & 255; // second 8 bits
    m_rotation = (packedData >> 14) & 3; // 2 bits more
}

ushort STile::packData(ushort m_index, ushort m_texture, ushort m_rotation)
{

    return (m_index & 63) | ((m_texture & 255) << 6) | ((m_rotation & 3) << 14);
}
