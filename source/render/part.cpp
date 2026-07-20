#include "part.h"
#include "render_performance.h"

#include <QCryptographicHash>
#include <QHash>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QWeakPointer>

#include <cstddef>

namespace {
int& liveMeshCount() { static int value = 0; return value; }
quint64& liveMeshBytes() { static quint64 value = 0; return value; }
}

class CSharedPartMesh
{
public:
    ~CSharedPartMesh()
    {
        buffer.destroy();
        --liveMeshCount();
        liveMeshBytes() -= bytes;
    }
    QOpenGLBuffer buffer;
    int vertexCount{0};
    quint64 bytes{0};
};

namespace {
QHash<QByteArray, QWeakPointer<CSharedPartMesh>> s_meshCache;
QString s_meshCacheScope;

QByteArray meshKey(const QVector<SVertexData>& vertices)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(s_meshCacheScope.toUtf8());
    hash.addData(reinterpret_cast<const char*>(vertices.constData()),
                 vertices.size() * int(sizeof(SVertexData)));
    return hash.result();
}

QSharedPointer<CSharedPartMesh> createMesh(const QVector<SVertexData>& vertices)
{
    QSharedPointer<CSharedPartMesh> mesh(new CSharedPartMesh);
    mesh->vertexCount = vertices.size();
    mesh->bytes = quint64(vertices.size()) * sizeof(SVertexData);
    mesh->buffer.create();
    mesh->buffer.bind();
    mesh->buffer.allocate(vertices.constData(), int(mesh->bytes));
    mesh->buffer.release();
    ++liveMeshCount();
    liveMeshBytes() += mesh->bytes;
    return mesh;
}
}

CPart::CPart():
   m_bShow(true)
{
    m_aVertData.clear();
    m_poseMatrix.setToIdentity();
}

CPart::CPart(const CPart &part)
{
    m_name = part.m_name;
    m_parentName = part.m_parentName;
    m_restOffset = part.m_restOffset;
    m_textureNumber = part.m_textureNumber;
    m_textureUvScale = part.m_textureUvScale;
    m_poseMatrix = part.m_poseMatrix;
    m_bShow = part.m_bShow;
    m_aVertData = part.m_aVertData;
    m_baseVertData = part.m_baseVertData;
    m_sourceVertexIndices = part.m_sourceVertexIndices;
    m_morphApplied = part.m_morphApplied;
    if(m_morphApplied)
        attachPrivateMesh(m_aVertData);
    else
        m_mesh = part.m_mesh;
}

CPart::~CPart()
{
}

void CPart::setCacheScope(const QString& scope)
{
    s_meshCacheScope = scope.toLower();
}

int CPart::sharedMeshCount()
{
    return liveMeshCount();
}

quint64 CPart::sharedMeshBytes()
{
    return liveMeshBytes();
}

void CPart::attachSharedMesh(const QVector<SVertexData>& vertices)
{
    const QByteArray key = meshKey(vertices);
    m_mesh = s_meshCache.value(key).toStrongRef();
    if(!m_mesh)
    {
        m_mesh = createMesh(vertices);
        s_meshCache.insert(key, m_mesh.toWeakRef());
    }
}

void CPart::attachPrivateMesh(const QVector<SVertexData>& vertices)
{
    m_mesh = createMesh(vertices);
}

void CPart::update()
{
    if(m_baseVertData.size() != m_aVertData.size())
        m_baseVertData = m_aVertData;
    attachSharedMesh(m_aVertData);
}

void CPart::setMorphDeltas(const QVector<QVector3D>& deltas)
{
    const bool canApply = !deltas.isEmpty()
        && m_sourceVertexIndices.size() == m_baseVertData.size();
    if(!canApply && !m_morphApplied)
        return;
    if(m_aVertData.size() != m_baseVertData.size())
        m_aVertData = m_baseVertData;
    for(int vertex = 0; vertex < m_aVertData.size(); ++vertex)
    {
        m_aVertData[vertex].position = m_baseVertData[vertex].position;
        if(canApply)
        {
            const int source = m_sourceVertexIndices.at(vertex);
            if(source >= 0 && source < deltas.size())
                m_aVertData[vertex].position += deltas.at(source);
        }
    }
    if(canApply && m_morphApplied && m_mesh)
    {
        m_mesh->buffer.bind();
        m_mesh->buffer.write(0, m_aVertData.constData(), m_aVertData.size() * int(sizeof(SVertexData)));
        m_mesh->buffer.release();
    }
    else if(canApply)
        attachPrivateMesh(m_aVertData);
    else
        attachSharedMesh(m_baseVertData);
    m_morphApplied = canApply;
}

void CPart::draw(QOpenGLShaderProgram* program)
{
    drawImmediate(program);
}

void CPart::drawImmediate(QOpenGLShaderProgram* program)
{
    if (!isDrawable() || !m_mesh)
        return;

    program->setUniformValue("u_partAnimated", true);
    program->setUniformValue("u_partMmatrix", m_poseMatrix);
    program->setUniformValue("u_textureUvScale", m_textureUvScale);

    int offset(0);
    // Tell OpenGL which VBOs to use
    m_mesh->buffer.bind();
    // Tell OpenGL programmable pipeline how to locate vertex position data
    int vertexLocation = program->attributeLocation("a_position");
    program->enableAttributeArray(vertexLocation);
    program->setAttributeBuffer(vertexLocation, GL_FLOAT, offset, 3, int(sizeof(SVertexData)));

    offset+=int(sizeof(QVector3D)); // size of vertex position
    int normLocation = program->attributeLocation("a_normal");
    program->enableAttributeArray(normLocation);
    program->setAttributeBuffer(normLocation, GL_FLOAT, offset, 3, int(sizeof(SVertexData)));

    offset+=int(sizeof(QVector3D)); // size of normal
    int textureLocation = program->attributeLocation("a_texture");
    program->enableAttributeArray(textureLocation);
    program->setAttributeBuffer(textureLocation, GL_FLOAT, offset, 2, int(sizeof(SVertexData)));

    // FIG triangles are already expanded sequentially. The old identity index
    // buffer duplicated every vertex and added a second bind to every part.
    glDrawArrays(GL_TRIANGLES, 0, m_aVertData.count());
    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    if(performance.collecting())
    {
        ++performance.counters().drawCalls;
        performance.counters().triangles += quint64(m_aVertData.count() / 3);
    }
}

bool CPart::drawInstanced(QOpenGLShaderProgram* program, QOpenGLBuffer* instanceBuffer,
                          int instanceBufferOffset, const QVector<QMatrix4x4>& matrices)
{
    if(!isDrawable() || !m_mesh || !program || !instanceBuffer || matrices.size() < 2)
        return false;
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if(!context || !context->extraFunctions())
        return false;
    QOpenGLExtraFunctions* functions = context->extraFunctions();
    const int instanceLocations[] = {
        program->attributeLocation("a_instanceMatrix0"),
        program->attributeLocation("a_instanceMatrix1"),
        program->attributeLocation("a_instanceMatrix2"),
        program->attributeLocation("a_instanceMatrix3")
    };
    for(int location : instanceLocations)
        if(location < 0)
            return false;

    program->setUniformValue("u_instanced", true);
    program->setUniformValue("u_partAnimated", false);
    program->setUniformValue("u_textureUvScale", m_textureUvScale);

    m_mesh->buffer.bind();
    int offset = 0;
    const int vertexLocation = program->attributeLocation("a_position");
    program->enableAttributeArray(vertexLocation);
    program->setAttributeBuffer(vertexLocation, GL_FLOAT, offset, 3, int(sizeof(SVertexData)));
    offset += int(sizeof(QVector3D));
    const int normalLocation = program->attributeLocation("a_normal");
    program->enableAttributeArray(normalLocation);
    program->setAttributeBuffer(normalLocation, GL_FLOAT, offset, 3, int(sizeof(SVertexData)));
    offset += int(sizeof(QVector3D));
    const int textureLocation = program->attributeLocation("a_texture");
    program->enableAttributeArray(textureLocation);
    program->setAttributeBuffer(textureLocation, GL_FLOAT, offset, 2, int(sizeof(SVertexData)));

    instanceBuffer->bind();
    instanceBuffer->write(instanceBufferOffset, matrices.constData(),
                          matrices.size() * int(sizeof(QMatrix4x4)));
    for(int column = 0; column < 4; ++column)
    {
        const int location = instanceLocations[column];
        program->enableAttributeArray(location);
        program->setAttributeBuffer(location, GL_FLOAT,
                                    instanceBufferOffset + column * 4 * int(sizeof(float)),
                                    4, int(sizeof(QMatrix4x4)));
        functions->glVertexAttribDivisor(GLuint(location), 1);
    }
    functions->glDrawArraysInstanced(GL_TRIANGLES, 0, m_aVertData.count(), matrices.size());
    for(int location : instanceLocations)
    {
        functions->glVertexAttribDivisor(GLuint(location), 0);
        program->disableAttributeArray(location);
    }
    instanceBuffer->release();
    m_mesh->buffer.release();
    program->setUniformValue("u_instanced", false);

    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    if(performance.collecting())
    {
        ++performance.counters().drawCalls;
        ++performance.counters().instancedDrawCalls;
        performance.counters().instances += quint64(matrices.size());
        performance.counters().triangles += quint64(m_aVertData.count() / 3)
            * quint64(matrices.size());
    }
    return true;
}


void CPart::drawSelect(QOpenGLShaderProgram *program)
{
    if (!isDrawable() || !m_mesh)
        return;

    program->setUniformValue("u_partAnimated", true);
    program->setUniformValue("u_partMmatrix", m_poseMatrix);

    int offset(0);
    // Tell OpenGL which VBOs to use
    m_mesh->buffer.bind();
    // Tell OpenGL programmable pipeline how to locate vertex position data
    int vertexLocation = program->attributeLocation("a_position");
    program->enableAttributeArray(vertexLocation);
    program->setAttributeBuffer(vertexLocation, GL_FLOAT, offset, 3, int(sizeof(SVertexData)));

    glDrawArrays(GL_TRIANGLES, 0, m_aVertData.count());
}

bool CPart::drawSelectInstanced(QOpenGLShaderProgram* program, QOpenGLBuffer* instanceBuffer,
                                int instanceBufferOffset,
                                const QVector<SPartPickingInstance>& instances)
{
    if(!isDrawable() || !m_mesh || !program || !instanceBuffer || instances.size() < 2)
        return false;
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if(!context || !context->extraFunctions())
        return false;
    QOpenGLExtraFunctions* functions = context->extraFunctions();
    const int matrixLocations[] = {
        program->attributeLocation("a_instanceMatrix0"),
        program->attributeLocation("a_instanceMatrix1"),
        program->attributeLocation("a_instanceMatrix2"),
        program->attributeLocation("a_instanceMatrix3")
    };
    const int colorLocation = program->attributeLocation("a_instanceColor");
    for(int location : matrixLocations)
        if(location < 0)
            return false;
    if(colorLocation < 0)
        return false;

    program->setUniformValue("u_instanced", true);
    program->setUniformValue("u_partAnimated", false);
    m_mesh->buffer.bind();
    const int vertexLocation = program->attributeLocation("a_position");
    program->enableAttributeArray(vertexLocation);
    program->setAttributeBuffer(vertexLocation, GL_FLOAT, 0, 3, int(sizeof(SVertexData)));

    instanceBuffer->bind();
    instanceBuffer->write(instanceBufferOffset, instances.constData(),
                          instances.size() * int(sizeof(SPartPickingInstance)));
    for(int column = 0; column < 4; ++column)
    {
        const int location = matrixLocations[column];
        program->enableAttributeArray(location);
        program->setAttributeBuffer(location, GL_FLOAT,
                                    instanceBufferOffset + column * 4 * int(sizeof(float)),
                                    4, int(sizeof(SPartPickingInstance)));
        functions->glVertexAttribDivisor(GLuint(location), 1);
    }
    program->enableAttributeArray(colorLocation);
    program->setAttributeBuffer(colorLocation, GL_FLOAT,
                                instanceBufferOffset + int(offsetof(SPartPickingInstance, color)),
                                4, int(sizeof(SPartPickingInstance)));
    functions->glVertexAttribDivisor(GLuint(colorLocation), 1);
    functions->glDrawArraysInstanced(GL_TRIANGLES, 0, m_aVertData.count(), instances.size());
    for(int location : matrixLocations)
    {
        functions->glVertexAttribDivisor(GLuint(location), 0);
        program->disableAttributeArray(location);
    }
    functions->glVertexAttribDivisor(GLuint(colorLocation), 0);
    program->disableAttributeArray(colorLocation);
    instanceBuffer->release();
    m_mesh->buffer.release();
    program->setUniformValue("u_instanced", false);
    return true;
}
