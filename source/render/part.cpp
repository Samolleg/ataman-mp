#include "part.h"
#include "render_performance.h"

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
    update(); // create a new vertex buffer owned by this instance
}

CPart::~CPart()
{
    m_vertexBuf.destroy();
}

void CPart::update()
{
    if(m_baseVertData.size() != m_aVertData.size())
        m_baseVertData = m_aVertData;
    // Generate VBOs and transfer data
    m_vertexBuf.create();
    m_vertexBuf.bind();
    m_vertexBuf.allocate(m_aVertData.data(), m_aVertData.count() * int(sizeof(SVertexData)));
    m_vertexBuf.release();
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
    m_vertexBuf.bind();
    m_vertexBuf.write(0, m_aVertData.constData(), m_aVertData.size() * int(sizeof(SVertexData)));
    m_vertexBuf.release();
    m_morphApplied = canApply;
}

void CPart::draw(QOpenGLShaderProgram* program)
{
    if (!m_bShow || m_aVertData.count() == 0)
        return;

    program->setUniformValue("u_partAnimated", true);
    program->setUniformValue("u_partMmatrix", m_poseMatrix);
    program->setUniformValue("u_textureUvScale", m_textureUvScale);

    int offset(0);
    // Tell OpenGL which VBOs to use
    m_vertexBuf.bind();
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


void CPart::drawSelect(QOpenGLShaderProgram *program)
{
    if (!m_bShow || m_aVertData.count() == 0)
        return;

    program->setUniformValue("u_partAnimated", true);
    program->setUniformValue("u_partMmatrix", m_poseMatrix);

    int offset(0);
    // Tell OpenGL which VBOs to use
    m_vertexBuf.bind();
    // Tell OpenGL programmable pipeline how to locate vertex position data
    int vertexLocation = program->attributeLocation("a_position");
    program->enableAttributeArray(vertexLocation);
    program->setAttributeBuffer(vertexLocation, GL_FLOAT, offset, 3, int(sizeof(SVertexData)));

    glDrawArrays(GL_TRIANGLES, 0, m_aVertData.count());
}
