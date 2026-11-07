#include "landscape.h"
#include "sector.h"
#include "resourcemanager.h"
#include "opening_trace.h"
#include "legacy_projection_pulse.h"
#include "shader_locations.h"
#include "gl_dispatch.h"
void CLandscape::ensureRenderResources() {
    if(!m_renderDeferred||m_aSector.isEmpty())return;
    opening::Scope timing("gui.gl.deferred_world_materialize",m_filePath.fileName());
    int count=int(m_header.nTexture),columns=1;
    m_texture=CTextureList::getInstance()->buildLandTex(m_innerMapName,count,count,columns,m_deferredAtlasArchive);
    m_baseAtlasTexture=m_texture;m_renderTextureCount=qMax(1,count);m_renderAtlasColumns=qMax(1,columns);
    m_baseAtlasColumns=columns;m_baseAtlasMatchesBaseline=true;
    for(auto& row:m_aSector){for(auto* sector:row){sector->setTerrainAtlasLayout(count,columns);sector->ensureRenderGeometry();}ei::pulseLegacyProjection();}
    m_renderDeferred=false;
}
void CLandscape::drawQueryDepth(QOpenGLShaderProgram* program,bool grid) {
    if(!m_queryOpaque){QImage opaque(1,1,QImage::Format_RGBA8888);opaque.fill(Qt::white);m_queryOpaque.reset(new QOpenGLTexture(opaque,QOpenGLTexture::DontGenerateMipMaps));}
    m_queryOpaque->bind(0);program->setUniformValue(ei::uniformLocation(program,"qt_Texture0"),0);
    for(auto& row:m_aSector)for(auto* sector:row)sector->drawQueryGeometry(program,grid);
}
void CSector::ensureRenderGeometry() {
    if(!m_renderDeferred)return;m_renderDeferred=false;
    m_vertexBuf.create();m_indexBuf.create();m_gridIndexBuf.create();m_waterVertexBuf.create();m_waterIndexBuf.create();
    generateVertexDataFromTile();updateDrawData();updatePosition();
}
void CSector::drawQueryGeometry(QOpenGLShaderProgram* program,bool grid) {
    if(!m_queryVertices.isCreated()) {
        opening::Scope timing("gui.gl.query_depth_mesh");
        QVector<SVertexData> source(16*16*9);int at=0;
        for(auto& row:m_arrLand)for(auto& tile:row)tile.generateDrawVertexData(source,at);
        QVector<QVector3D> vertices;vertices.reserve(source.size());for(const auto& v:source)vertices.append(v.position);
        QVector<ushort> triangles,lines;triangles.reserve(256*24);lines.reserve(256*8);
        for(int t=0;t<256;++t){for(int i:{0,1,4,0,4,3,1,2,5,1,5,4,3,4,7,3,7,6,4,5,8,4,8,7})triangles.append(ushort(t*9+i));for(int i:{0,2,2,8,8,6,6,0})lines.append(ushort(t*9+i));}
        m_queryVertices.create();m_queryVertices.bind();m_queryVertices.allocate(vertices.constData(),vertices.size()*int(sizeof(QVector3D)));m_queryVertices.release();
        m_queryTriangles.create();m_queryTriangles.bind();m_queryTriangles.allocate(triangles.constData(),triangles.size()*int(sizeof(ushort)));m_queryTriangles.release();
        m_queryGrid.create();m_queryGrid.bind();m_queryGrid.allocate(lines.constData(),lines.size()*int(sizeof(ushort)));m_queryGrid.release();
    }
    program->setUniformValue(ei::uniformLocation(program,"u_modelMmatrix"),m_modelMatrix);
    const int position=ei::attributeLocation(program,"a_position");
    m_queryVertices.bind();program->enableAttributeArray(position);program->setAttributeBuffer(position,GL_FLOAT,0,3,sizeof(QVector3D));
    for(const char* name:{"a_normal","a_texture","a_materialEmission","a_materialOpacity"}){int location=ei::attributeLocation(program,name);if(location>=0){program->disableAttributeArray(location);program->setAttributeValue(location,name==QStringLiteral("a_materialOpacity")?1.0f:0.0f);}}
    auto& indices=grid?m_queryGrid:m_queryTriangles;indices.bind();ei::gl::DrawElements(grid?GL_LINES:GL_TRIANGLES,grid?256*8:256*24,GL_UNSIGNED_SHORT,nullptr);
    indices.release();program->disableAttributeArray(position);m_queryVertices.release();
}

void CSector::invalidateQueryGeometry() {
    m_queryVertices.destroy();m_queryTriangles.destroy();m_queryGrid.destroy();
}
