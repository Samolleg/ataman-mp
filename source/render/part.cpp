#include "part.h"
#include "gl_dispatch.h"
#include "render_performance.h"

#include <QCryptographicHash>
#include <QHash>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLVertexArrayObject>
#include <QSurfaceFormat>
#include <QtMath>
#include <QWeakPointer>

#include <cstddef>

namespace {
int& liveMeshCount() { static int value = 0; return value; }
quint64& liveMeshBytes() { static quint64 value = 0; return value; }
}

class CSharedPartMesh
{
public:
    struct VertexArrays
    {
        QOpenGLVertexArrayObject immediate;
        QOpenGLVertexArrayObject instanced;
        QOpenGLVertexArrayObject selection;
        QOpenGLVertexArrayObject selectionInstanced;
    };

    ~CSharedPartMesh()
    {
        qDeleteAll(vertexArrays);
        vertexArrays.clear();
        buffer.destroy();
        --liveMeshCount();
        liveMeshBytes() -= bytes;
    }
    QOpenGLBuffer buffer;
    int vertexCount{0};
    quint64 bytes{0};
    QHash<GLuint, VertexArrays*> vertexArrays;
};

namespace {
QHash<QByteArray, QWeakPointer<CSharedPartMesh>> s_meshCache;
QString s_meshCacheScope;
bool s_deferGpuCleanupToContextDestruction = false;

// Intentionally process-lifetime storage. During final shutdown the context
// releases the GPU objects in bulk; destroying wrappers individually first is
// slower and has historically exercised fragile driver cleanup paths.
QHash<quintptr, QSharedPointer<CSharedPartMesh>>& shutdownRetainedMeshes()
{
    static auto* meshes = new QHash<quintptr, QSharedPointer<CSharedPartMesh>>;
    return *meshes;
}

struct ProgramLocations
{
    int position{-1};
    int normal{-1};
    int texture{-1};
    int instanceMatrix[4]{-1, -1, -1, -1};
    int instanceColor{-1};
    int partAnimated{-1};
    int partMatrix{-1};
    int textureUvScale{-1};
    int instanced{-1};
};

QHash<GLuint, ProgramLocations> s_programLocations;

const ProgramLocations& locationsFor(QOpenGLShaderProgram* program)
{
    const GLuint programId = program ? program->programId() : 0;
    auto found = s_programLocations.constFind(programId);
    if(found != s_programLocations.constEnd())
        return found.value();

    ProgramLocations locations;
    locations.position = program->attributeLocation("a_position");
    locations.normal = program->attributeLocation("a_normal");
    locations.texture = program->attributeLocation("a_texture");
    locations.instanceMatrix[0] = program->attributeLocation("a_instanceMatrix0");
    locations.instanceMatrix[1] = program->attributeLocation("a_instanceMatrix1");
    locations.instanceMatrix[2] = program->attributeLocation("a_instanceMatrix2");
    locations.instanceMatrix[3] = program->attributeLocation("a_instanceMatrix3");
    locations.instanceColor = program->attributeLocation("a_instanceColor");
    locations.partAnimated = program->uniformLocation("u_partAnimated");
    locations.partMatrix = program->uniformLocation("u_partMmatrix");
    locations.textureUvScale = program->uniformLocation("u_textureUvScale");
    locations.instanced = program->uniformLocation("u_instanced");
    return s_programLocations.insert(programId, locations).value();
}

bool supportsVertexArrays()
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if(!context || QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES)
        return false;
    const QSurfaceFormat format = context->format();
    return format.majorVersion() > 3
        || (format.majorVersion() == 3 && format.minorVersion() >= 3);
}

CSharedPartMesh::VertexArrays* arraysFor(CSharedPartMesh* mesh,
                                         QOpenGLShaderProgram* program)
{
    if(!mesh || !program || !supportsVertexArrays())
        return nullptr;
    const GLuint programId = program->programId();
    CSharedPartMesh::VertexArrays* arrays = mesh->vertexArrays.value(programId, nullptr);
    if(!arrays)
    {
        arrays = new CSharedPartMesh::VertexArrays;
        mesh->vertexArrays.insert(programId, arrays);
    }
    return arrays;
}

bool bindVertexArray(QOpenGLVertexArrayObject& vao, CSharedPartMesh* mesh,
                     QOpenGLShaderProgram* program, bool selection)
{
    if(!mesh || !program)
        return false;
    const ProgramLocations& locations = locationsFor(program);
    if(!vao.isCreated())
    {
        if(!vao.create())
            return false;
        vao.bind();
        mesh->buffer.bind();
        if(locations.position >= 0)
        {
            program->enableAttributeArray(locations.position);
            program->setAttributeBuffer(locations.position, GL_FLOAT, 0, 3,
                                        int(sizeof(SVertexData)));
        }
        if(!selection && locations.normal >= 0)
        {
            program->enableAttributeArray(locations.normal);
            program->setAttributeBuffer(locations.normal, GL_FLOAT,
                                        int(sizeof(QVector3D)), 3,
                                        int(sizeof(SVertexData)));
        }
        if(locations.texture >= 0)
        {
            program->enableAttributeArray(locations.texture);
            program->setAttributeBuffer(locations.texture, GL_FLOAT,
                                        2 * int(sizeof(QVector3D)), 2,
                                        int(sizeof(SVertexData)));
        }
        mesh->buffer.release();
        vao.release();
    }
    vao.bind();
    return true;
}

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
    // MOB preflight constructs helper geometry without a render context (also
    // on the agent worker). Do not publish a GPU holder with bufferId==0.
    if(!QOpenGLContext::currentContext())
        return {};
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
    m_assemblySourceOrder = part.m_assemblySourceOrder;
    m_restOffset = part.m_restOffset;
    m_textureNumber = part.m_textureNumber;
    m_textureUvScale = part.m_textureUvScale;
    m_poseMatrix = part.m_poseMatrix;
    m_bShow = part.m_bShow;
    m_aVertData = part.m_aVertData;
    m_baseVertData = part.m_baseVertData;
    m_sourceVertexIndices = part.m_sourceVertexIndices;
    m_localBoundsCenter = part.m_localBoundsCenter;
    m_localBoundsRadius = part.m_localBoundsRadius;
    m_authoredNavigationMinimum = part.m_authoredNavigationMinimum;
    m_authoredNavigationMaximum = part.m_authoredNavigationMaximum;
    m_hasAuthoredNavigationBounds = part.m_hasAuthoredNavigationBounds;
    m_morphApplied = part.m_morphApplied;
    if(m_morphApplied)
        attachPrivateMesh(m_aVertData);
    else
    {
        m_mesh = part.m_mesh;
        if(!m_mesh)ensureMesh(); // a CPU-only cached assembly can now be uploaded
    }
}

CPart::~CPart()
{
    if(s_deferGpuCleanupToContextDestruction && m_mesh)
        shutdownRetainedMeshes().insert(quintptr(m_mesh.data()), m_mesh);
}

void CPart::setCacheScope(const QString& scope)
{
    s_meshCacheScope = scope.toLower();
}

void CPart::deferGpuCleanupToContextDestruction()
{
    s_deferGpuCleanupToContextDestruction = true;
}

void CPart::releaseRetainedVertexArrays()
{
    for(const auto& mesh: shutdownRetainedMeshes())
    {
        qDeleteAll(mesh->vertexArrays);
        mesh->vertexArrays.clear();
    }
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
    updateLocalBounds(vertices);
    // Keep context-free projection out of the shared GPU cache entirely.
    if(!QOpenGLContext::currentContext())
    {
        m_mesh.clear();
        return;
    }
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
    updateLocalBounds(vertices);
    m_mesh = createMesh(vertices);
}

bool CPart::ensureMesh()
{
    if(!m_mesh && QOpenGLContext::currentContext())
    {
        if(m_morphApplied)attachPrivateMesh(m_aVertData);
        else attachSharedMesh(m_aVertData);
    }
    return bool(m_mesh);
}

void CPart::updateLocalBounds(const QVector<SVertexData>& vertices)
{
    if(vertices.isEmpty())
    {
        m_localBoundsCenter = QVector3D();
        m_localBoundsRadius = 0.0f;
        return;
    }
    QVector3D minimum = vertices.first().position;
    QVector3D maximum = minimum;
    for(const SVertexData& vertex : vertices)
    {
        const QVector3D& point = vertex.position;
        minimum.setX(qMin(minimum.x(), point.x()));
        minimum.setY(qMin(minimum.y(), point.y()));
        minimum.setZ(qMin(minimum.z(), point.z()));
        maximum.setX(qMax(maximum.x(), point.x()));
        maximum.setY(qMax(maximum.y(), point.y()));
        maximum.setZ(qMax(maximum.z(), point.z()));
    }
    m_localBoundsCenter = (minimum + maximum) * 0.5f;
    float radiusSquared = 0.0f;
    for(const SVertexData& vertex : vertices)
        radiusSquared = qMax(radiusSquared,
            (vertex.position - m_localBoundsCenter).lengthSquared());
    m_localBoundsRadius = qSqrt(radiusSquared);
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
    if(canApply && m_morphApplied && m_mesh && QOpenGLContext::currentContext())
    {
        updateLocalBounds(m_aVertData);
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
    if (!isDrawable() || !ensureMesh())
        return;

    const ProgramLocations& locations = locationsFor(program);
    program->setUniformValue(locations.partAnimated, true);
    program->setUniformValue(locations.partMatrix, m_poseMatrix);
    program->setUniformValue(locations.textureUvScale, m_textureUvScale);

    CSharedPartMesh::VertexArrays* arrays = arraysFor(m_mesh.data(), program);
    const bool vertexArrayBound = arrays
        && bindVertexArray(arrays->immediate, m_mesh.data(), program, false);

    if(!vertexArrayBound)
    {
        int offset = 0;
        m_mesh->buffer.bind();
        program->enableAttributeArray(locations.position);
        program->setAttributeBuffer(locations.position, GL_FLOAT, offset, 3,
                                    int(sizeof(SVertexData)));
        offset += int(sizeof(QVector3D));
        program->enableAttributeArray(locations.normal);
        program->setAttributeBuffer(locations.normal, GL_FLOAT, offset, 3,
                                    int(sizeof(SVertexData)));
        offset += int(sizeof(QVector3D));
        program->enableAttributeArray(locations.texture);
        program->setAttributeBuffer(locations.texture, GL_FLOAT, offset, 2,
                                    int(sizeof(SVertexData)));
    }

    // FIG triangles are already expanded sequentially. The old identity index
    // buffer duplicated every vertex and added a second bind to every part.
    ei::gl::DrawArrays(GL_TRIANGLES, 0, m_aVertData.count());
    if(vertexArrayBound)
        arrays->immediate.release();
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
    if(!isDrawable() || !ensureMesh() || !program || !instanceBuffer || matrices.size() < 2)
        return false;
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if(!context || !context->extraFunctions())
        return false;
    QOpenGLExtraFunctions* functions = context->extraFunctions();
    const ProgramLocations& locations = locationsFor(program);
    const int* instanceLocations = locations.instanceMatrix;
    for(int column = 0; column < 4; ++column)
        if(instanceLocations[column] < 0)
            return false;

    program->setUniformValue(locations.instanced, true);
    program->setUniformValue(locations.partAnimated, false);
    program->setUniformValue(locations.textureUvScale, m_textureUvScale);

    CSharedPartMesh::VertexArrays* arrays = arraysFor(m_mesh.data(), program);
    const bool vertexArrayBound = arrays
        && bindVertexArray(arrays->instanced, m_mesh.data(), program, false);
    if(!vertexArrayBound)
    {
        m_mesh->buffer.bind();
        program->enableAttributeArray(locations.position);
        program->setAttributeBuffer(locations.position, GL_FLOAT, 0, 3,
                                    int(sizeof(SVertexData)));
        program->enableAttributeArray(locations.normal);
        program->setAttributeBuffer(locations.normal, GL_FLOAT,
                                    int(sizeof(QVector3D)), 3, int(sizeof(SVertexData)));
        program->enableAttributeArray(locations.texture);
        program->setAttributeBuffer(locations.texture, GL_FLOAT,
                                    2 * int(sizeof(QVector3D)), 2, int(sizeof(SVertexData)));
    }

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
    for(int column = 0; column < 4; ++column)
    {
        const int location = instanceLocations[column];
        if(!vertexArrayBound)
        {
            functions->glVertexAttribDivisor(GLuint(location), 0);
            program->disableAttributeArray(location);
        }
    }
    instanceBuffer->release();
    if(vertexArrayBound)
        arrays->instanced.release();
    else
        m_mesh->buffer.release();
    program->setUniformValue(locations.instanced, false);

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
    if (!isDrawable() || !ensureMesh())
        return;

    const ProgramLocations& locations = locationsFor(program);
    program->setUniformValue(locations.partAnimated, true);
    program->setUniformValue(locations.partMatrix, m_poseMatrix);
    program->setUniformValue(locations.textureUvScale, m_textureUvScale);

    CSharedPartMesh::VertexArrays* arrays = arraysFor(m_mesh.data(), program);
    const bool vertexArrayBound = arrays
        && bindVertexArray(arrays->selection, m_mesh.data(), program, true);

    if(!vertexArrayBound)
    {
        m_mesh->buffer.bind();
        program->enableAttributeArray(locations.position);
        program->setAttributeBuffer(locations.position, GL_FLOAT, 0, 3,
                                    int(sizeof(SVertexData)));
        if(locations.texture >= 0)
        {
            program->enableAttributeArray(locations.texture);
            program->setAttributeBuffer(locations.texture, GL_FLOAT,
                                        2 * int(sizeof(QVector3D)), 2,
                                        int(sizeof(SVertexData)));
        }
    }

    ei::gl::DrawArrays(GL_TRIANGLES, 0, m_aVertData.count());
    if(vertexArrayBound)
        arrays->selection.release();
}

bool CPart::drawSelectInstanced(QOpenGLShaderProgram* program, QOpenGLBuffer* instanceBuffer,
                                int instanceBufferOffset,
                                const QVector<SPartPickingInstance>& instances)
{
    if(!isDrawable() || !ensureMesh() || !program || !instanceBuffer || instances.size() < 2)
        return false;
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if(!context || !context->extraFunctions())
        return false;
    QOpenGLExtraFunctions* functions = context->extraFunctions();
    const ProgramLocations& locations = locationsFor(program);
    const int* matrixLocations = locations.instanceMatrix;
    const int colorLocation = locations.instanceColor;
    for(int column = 0; column < 4; ++column)
        if(matrixLocations[column] < 0)
            return false;
    if(colorLocation < 0)
        return false;

    program->setUniformValue(locations.instanced, true);
    program->setUniformValue(locations.partAnimated, false);
    program->setUniformValue(locations.textureUvScale, m_textureUvScale);
    CSharedPartMesh::VertexArrays* arrays = arraysFor(m_mesh.data(), program);
    const bool vertexArrayBound = arrays
        && bindVertexArray(arrays->selectionInstanced, m_mesh.data(), program, true);
    if(!vertexArrayBound)
    {
        m_mesh->buffer.bind();
        program->enableAttributeArray(locations.position);
        program->setAttributeBuffer(locations.position, GL_FLOAT, 0, 3,
                                    int(sizeof(SVertexData)));
        if(locations.texture >= 0)
        {
            program->enableAttributeArray(locations.texture);
            program->setAttributeBuffer(locations.texture, GL_FLOAT,
                                        2 * int(sizeof(QVector3D)), 2,
                                        int(sizeof(SVertexData)));
        }
    }

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
    for(int column = 0; column < 4; ++column)
    {
        const int location = matrixLocations[column];
        if(!vertexArrayBound)
        {
            functions->glVertexAttribDivisor(GLuint(location), 0);
            program->disableAttributeArray(location);
        }
    }
    if(!vertexArrayBound)
    {
        functions->glVertexAttribDivisor(GLuint(colorLocation), 0);
        program->disableAttributeArray(colorLocation);
    }
    instanceBuffer->release();
    if(vertexArrayBound)
        arrays->selectionInstanced.release();
    else
        m_mesh->buffer.release();
    program->setUniformValue(locations.instanced, false);
    return true;
}
