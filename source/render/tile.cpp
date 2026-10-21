#include "shader_locations.h"
#include "log.h"
#include "tile.h"
#include "gl_dispatch.h"
#include "math_utils.h"

#include <QOpenGLFunctions>

namespace
{
const ushort kPreviewTileIndices[] = {
    0,1,4, 0,4,3,
    1,2,5, 1,5,4,
    3,4,7, 3,7,6,
    4,5,8, 4,8,7
};

bool contextsShareGpuResources(const QOpenGLContext* first,
                               const QOpenGLContext* second)
{
    if(!first || !second)
        return false;
    if(first == second)
        return true;
    return first->shareGroup()
        && first->shareGroup() == second->shareGroup();
}
}


CTile::CTile():
    m_materialIndex(-1)
{
    reset();
}

CTile::CTile(ushort packedData, ushort x, ushort y, float maxZ, int atlasNumber):
    m_x(x)
  ,m_y(y)
  ,m_maxZ(maxZ)
  ,m_texAtlasNumber(atlasNumber)
  ,m_atlasColumns(qMax(1, atlasNumber))
  ,m_atlasRows(1)
{
    reset();
    m_x = x;
    m_y = y;
    m_index = packedData & 63; //first 6 bits
    m_atlasTexIndex = (packedData >> 6) & 255; // second 8 bits
    m_rotNum = (packedData >> 14) & 3; // 2 bits more
}

CTile::~CTile()
{

}

// nine vertices of tile
/*
^|   6 _7 _8
c|   |\ |\ |
o|   |_\|_\|
l|   3 _4 _5
u|   |\ |\ |
m|   |_\|_\|
n|   0  1  2 ROW---->
*/
void CTile::resetVertices(QVector<SSecVertex>& arrVertex)
{
    if(arrVertex.size() != 9)
    {
        ei::log(eLogWarning, "Cannot reset tile vertices with " + QString::number(arrVertex.size()) + " vertices");
        return;
    }

    for(int i(0); i<arrVertex.size(); ++i)
    {
        m_arrVertex[i/3][i%3] = arrVertex[i];
    }
}


// Функция для вычисления нового индекса после определённого количества поворотов
QPair<int, int> getNewIndex(int i, int j, int rotations) {
    // Привести количество поворотов к диапазону 0-3 (модуль 4)
    rotations = rotations % 4;

    // В зависимости от количества поворотов, возвращаем новый индекс
    switch (rotations) {
        case 0: // 0 поворотов (исходный индекс)
            return {i, j};
        case 1: // 1 поворот
            return {j, 2 - i};
        case 2: // 2 поворота
            return {2 - i, 2 - j};
        case 3: // 3 поворота
            return {2 - j, i};
        default:
            return {i, j}; // Это никогда не произойдет
    }
}

// nine vertices of tile
/*
6 _7 _8
|\ |\ |
|_\|_\|
3 _4 _5
|\ |\ |
|_\|_\|
0  1  2
9 land vertice -> 16 indexes:
ind num:   0 1 2 3    4 5 6 7     8 9 10 11  12 13 14 15
quat ind: {0,1,4,3}, {1,2,5,4}, {3,4,7, 6},  {4, 5, 8, 7}
1 tile -> 16 tex coords with the same indexes
*/
void CTile::generateDrawVertexData(QVector<SVertexData>& outData, int& curIndex)
{
    QVector<QVector2D> tCoord; // store x,y min and x,y max
    tCoord.resize(2);
    tCoord[0] = atlasTileOrigin(tileIndex(), m_texAtlasNumber,
                               m_atlasColumns);
    // QOpenGLTexture uploads QImage scanline zero as the bottom of the GL
    // texture. previewTerrainAtlas writes page row zero into those first
    // scanlines, so packed atlas pages advance upward without another flip.
    // Reversing pageRow here made a 3x3 atlas sample pages 6-8 for stock pages
    // 0-2 and could make the entire map appear as one generated/blank texture.
    tCoord[1].setX(tCoord[0].x() + 1.0f/8.0f/m_atlasColumns);
    tCoord[1].setY(tCoord[0].y() + 1.0f/8.0f/m_atlasRows);
    tCoord[0] += QVector2D(4.0f/512.0f/m_atlasColumns,
                          4.0f/512.0f/m_atlasRows);
    tCoord[1] -= QVector2D(4.0f/512.0f/m_atlasColumns,
                          4.0f/512.0f/m_atlasRows);

    auto calcTexCoord = [&tCoord](QVector2D& texCoord, int row, int col)
    {
        const float xStep = (tCoord[0].x() + tCoord[1].x()) * 0.5f
            - tCoord[0].x();
        const float yStep = (tCoord[0].y() + tCoord[1].y()) * 0.5f
            - tCoord[0].y();
        texCoord.setX(tCoord[0].x() + xStep * col);
        texCoord.setY(tCoord[1].y() - yStep * row);
    };

    for(int row(0); row<3; ++row)
        for(int col(0); col<3; ++col)
        {

            const SSecVertex& vrt = m_arrVertex[row][col];
            outData[curIndex].position.setX(m_x + col + vrt.xOffset/254.0f);
            outData[curIndex].position.setY(m_y + row + vrt.yOffset/254.0f);
            outData[curIndex].position.setZ(vrt.z * m_maxZ/65535.0f);
            outData[curIndex].normal = vrt.normal;
            const QPair<int, int> indN = getNewIndex(row, col, m_rotNum);
            calcTexCoord(outData[curIndex].texCoord, indN.first, indN.second);
            ++curIndex;
        }

}

bool CTile::isProjectPoint(QVector3D& outPoint)
{
    float u,v,t;
    QVector3D dir(0.0f, 0.0f, 1.0f);
    static const int triangleIds[] = {
        0, 1, 3, 3, 1, 4, 1, 2, 4, 4, 2, 5,
        3, 4, 6, 6, 4, 7, 4, 5, 7, 7, 5, 8
    };
    QVector3D points[9];
    for(int point = 0; point < 9; ++point)
        points[point] = pos(point / 3, point % 3);
    for(int i = 0; i < 24; i += 3)
    {
        if(util::ptToTriangle(t, u, v, outPoint, dir,
                              points[triangleIds[i]],
                              points[triangleIds[i + 1]],
                              points[triangleIds[i + 2]]))
        {
            outPoint.setZ(outPoint.z() + t);
            return true;
        }
    }
    return false;
}

bool CTile::isProjectTile(QVector3D& outPoint)
{
    float u,v,t;
    QVector3D dir(0.0f, 0.0f, 1.0f);
    //find suitable vertex data cut for projection, don't need to process all vertices

    QVector3D pt1(pos(0, 0));
    QVector3D pt2(pos(0, 2));
    QVector3D pt3(pos(2, 0));
    QVector3D pt4(pos(2, 2));

    if (util::ptToTriangle(t, u, v, outPoint, dir, pt1, pt2, pt3) ||
        util::ptToTriangle(t, u, v, outPoint, dir, pt3, pt2, pt4))
    {
        outPoint.setZ(outPoint.z() + t);
        return true;
    }


    return false;
}

int CTile::tileIndex() const
{
    int atlasIndex = m_atlasTexIndex >= m_texAtlasNumber ? (m_texAtlasNumber-1) : m_atlasTexIndex;
    return m_index + (atlasIndex*64);
}

void CTile::setTile(int index, int rotNum)
{
    m_index = index%64;
    m_atlasTexIndex = index/64;
    m_rotNum = rotNum;
}

void CTile::setAtlasLayout(int textureCount, int columns)
{
    m_texAtlasNumber = qMax(1, textureCount);
    m_atlasColumns = qBound(1, columns, m_texAtlasNumber);
    m_atlasRows = qMax(1, (m_texAtlasNumber + m_atlasColumns - 1)
                            / m_atlasColumns);
}

ushort CTile::packData()
{
    return (m_index & 63) | ((m_atlasTexIndex & 255) << 6) | ((m_rotNum & 3) << 14);
}

void CTile::reset()
{
    m_index = 0;
    m_atlasTexIndex = 0;
    m_rotNum = 0;
    m_arrVertex.clear();
    m_arrVertex.resize(3);
    for(int i(0); i<3; ++i)
        m_arrVertex[i].resize(3);
}

QVector3D CTile::pos(int row, int col)
{
    const SSecVertex& vrt = m_arrVertex[row][col];
    QVector3D pos;
    pos.setX(m_x + col + vrt.xOffset/254.0f);
    pos.setY(m_y + row + vrt.yOffset/254.0f);
    pos.setZ(vrt.z * m_maxZ/65535.0f);
    return pos;
}

CPreviewTile::CPreviewTile()
{
}

CPreviewTile::~CPreviewTile()
{
    // This is deliberately conditional. At normal CView teardown the view
    // calls releaseGpuResources() with its context current; after an abnormal
    // widget/context teardown this only drops stale names and never enters Qt
    // or the driver with no owning context.
    releaseGpuResources();
}

void CPreviewTile::updateTile(const CTile& tile, int tIndex, int rotation, int mIndex, int xSector, int ySector,
                              const QVector3D& worldOffset)
{
    // Palette buttons and cursor input are not OpenGL ownership boundaries.
    // Build a private CPU generation only; paintGL/draw uploads it later.
    QVector<SVertexData> vertexData(9);
    CTile previewTile = tile;
    previewTile.setTile(tIndex, rotation);
    previewTile.setMaterialIndex(mIndex);

    int curIndex(0);
    previewTile.generateDrawVertexData(vertexData, curIndex);
    for(SVertexData& vertex : vertexData)
        vertex.position.setZ(vertex.position.z()
                             + terrainTilePreviewElevation());
    QMatrix4x4 modelMatrix;
    modelMatrix.setToIdentity();
    modelMatrix.translate(QVector3D(xSector*32.0f, ySector*32.0f, .0f) + worldOffset);

    m_stagedVertexData = vertexData;
    m_stagedIndexData.resize(int(sizeof(kPreviewTileIndices)
                                / sizeof(kPreviewTileIndices[0])));
    for(int index = 0; index < m_stagedIndexData.size(); ++index)
        m_stagedIndexData[index] = kPreviewTileIndices[index];
    m_stagedModelMatrix = modelMatrix;
    ++m_stagedGeneration;
    // Avoid the sentinel equality after the (practically unreachable) wrap.
    if(m_stagedGeneration == 0)
        ++m_stagedGeneration;
    m_geometryDirty = true;
}

bool CPreviewTile::hasStagedGeometry() const
{
    return !m_stagedVertexData.isEmpty() && !m_stagedIndexData.isEmpty();
}

bool CPreviewTile::hasPendingGeometry() const
{
    return hasStagedGeometry()
        && (m_geometryDirty || m_uploadedGeneration != m_stagedGeneration);
}

bool CPreviewTile::hasCurrentCompatibleGpuContext() const
{
    if(!m_vertexBuffer || !m_indexBuffer || m_gpuContext.isNull())
        return false;
    return contextsShareGpuResources(QOpenGLContext::currentContext(),
                                     m_gpuContext.data());
}

void CPreviewTile::abandonGpuResources()
{
    // A buffer name alone is not sufficient authority to call glDeleteBuffers.
    // If its owner is no longer current, the owner context will reclaim it at
    // destruction. Keeping the CPU snapshot lets a later compatible context
    // recreate this generation without touching the stale name.
    m_vertexBuffer = 0;
    m_indexBuffer = 0;
    m_gpuContext.clear();
    m_uploadedGeneration = 0;
    m_geometryDirty = hasStagedGeometry();
}

bool CPreviewTile::uploadPendingGeometry()
{
    if(!hasStagedGeometry())
        return false;

    QOpenGLContext* current = QOpenGLContext::currentContext();
    if(!current)
        return false;

    // The QPointer becomes null when a recreated/destroyed Qt context is
    // gone. Its old GL names cannot be deleted from an unrelated context.
    if((m_vertexBuffer || m_indexBuffer) && m_gpuContext.isNull())
        abandonGpuResources();
    if(!m_gpuContext.isNull()
       && !contextsShareGpuResources(current, m_gpuContext.data()))
        return false;

    if(!hasPendingGeometry() && m_vertexBuffer && m_indexBuffer)
        return true;

    QOpenGLFunctions* functions = current->functions();
    if(!functions)
        return false;

    if(!m_vertexBuffer || !m_indexBuffer)
    {
        // A partially-created pair is still owned by the compatible current
        // context, so it can be disposed before retrying atomically.
        if(m_vertexBuffer)
            functions->glDeleteBuffers(1, &m_vertexBuffer);
        if(m_indexBuffer)
            functions->glDeleteBuffers(1, &m_indexBuffer);
        m_vertexBuffer = 0;
        m_indexBuffer = 0;
        functions->glGenBuffers(1, &m_vertexBuffer);
        functions->glGenBuffers(1, &m_indexBuffer);
        if(!m_vertexBuffer || !m_indexBuffer)
        {
            if(m_vertexBuffer)
                functions->glDeleteBuffers(1, &m_vertexBuffer);
            if(m_indexBuffer)
                functions->glDeleteBuffers(1, &m_indexBuffer);
            m_vertexBuffer = 0;
            m_indexBuffer = 0;
            return false;
        }
        m_gpuContext = current;
    }

    functions->glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    functions->glBufferData(GL_ARRAY_BUFFER,
                            m_stagedVertexData.size()
                                * int(sizeof(SVertexData)),
                            m_stagedVertexData.constData(), GL_STATIC_DRAW);
    functions->glBindBuffer(GL_ARRAY_BUFFER, 0);
    functions->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
    functions->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                            m_stagedIndexData.size() * int(sizeof(ushort)),
                            m_stagedIndexData.constData(), GL_STATIC_DRAW);
    functions->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    m_uploadedGeneration = m_stagedGeneration;
    m_geometryDirty = false;
    return true;
}

bool CPreviewTile::releaseGpuResources()
{
    if(!m_vertexBuffer && !m_indexBuffer)
    {
        m_gpuContext.clear();
        m_uploadedGeneration = 0;
        m_geometryDirty = hasStagedGeometry();
        return true;
    }

    QOpenGLContext* current = QOpenGLContext::currentContext();
    if(!current || m_gpuContext.isNull()
       || !contextsShareGpuResources(current, m_gpuContext.data()))
    {
        abandonGpuResources();
        return false;
    }

    QOpenGLFunctions* functions = current->functions();
    if(!functions)
    {
        abandonGpuResources();
        return false;
    }
    if(m_vertexBuffer)
        functions->glDeleteBuffers(1, &m_vertexBuffer);
    if(m_indexBuffer)
        functions->glDeleteBuffers(1, &m_indexBuffer);
    m_vertexBuffer = 0;
    m_indexBuffer = 0;
    m_gpuContext.clear();
    m_uploadedGeneration = 0;
    m_geometryDirty = hasStagedGeometry();
    return true;
}

void CPreviewTile::draw(QOpenGLShaderProgram* program)
{
    if(!program || !hasStagedGeometry())
        return;

    // draw is only reached from the renderer. It still rechecks the contract
    // so a palette callback can never accidentally rely on a stale VBO.
    if(!uploadPendingGeometry() || !hasCurrentCompatibleGpuContext())
        return;

    program->setUniformValue(ei::uniformLocation(program, "u_modelMmatrix"), m_stagedModelMatrix);

    int offset = 0;
    QOpenGLFunctions* functions = QOpenGLContext::currentContext()->functions();
    if(!functions)
        return;
    functions->glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    int vertLoc = ei::attributeLocation(program, "a_position");
    if(vertLoc >= 0)
    {
        program->enableAttributeArray(vertLoc);
        program->setAttributeBuffer(vertLoc, GL_FLOAT, offset, 3,
                                    int(sizeof(SVertexData)));
    }

    offset += int(sizeof(QVector3D));
    int normLoc = ei::attributeLocation(program, "a_normal");
    if(normLoc >= 0)
    {
        program->enableAttributeArray(normLoc);
        program->setAttributeBuffer(normLoc, GL_FLOAT, offset, 3,
                                    int(sizeof(SVertexData)));
    }

    offset+=int(sizeof(QVector3D)); // size of normal
    int textureLocation = ei::attributeLocation(program, "a_texture");
    if(textureLocation >= 0)
    {
        program->enableAttributeArray(textureLocation);
        program->setAttributeBuffer(textureLocation, GL_FLOAT, offset, 2,
                                    int(sizeof(SVertexData)));
    }
    const int animationLocation = ei::attributeLocation(program, "a_overlayAnimation");
    if(animationLocation >= 0)
    {
        program->disableAttributeArray(animationLocation);
        program->setAttributeValue(animationLocation, QVector4D());
    }
    const int tileAnimationLocation = ei::attributeLocation(program, "a_overlayTileAnimation");
    if(tileAnimationLocation >= 0)
    {
        program->disableAttributeArray(tileAnimationLocation);
        program->setAttributeValue(tileAnimationLocation, QVector3D());
    }
    const int gridLocation = ei::attributeLocation(program, "a_overlayGrid");
    if(gridLocation >= 0)
    {
        program->disableAttributeArray(gridLocation);
        program->setAttributeValue(gridLocation, QVector2D());
    }
    const int emissionLocation = ei::attributeLocation(program, "a_materialEmission");
    if(emissionLocation >= 0)
    {
        program->disableAttributeArray(emissionLocation);
        program->setAttributeValue(emissionLocation, QVector3D());
    }
    functions->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
    ei::gl::DrawElements(GL_TRIANGLES, 24, GL_UNSIGNED_SHORT, nullptr);
}
