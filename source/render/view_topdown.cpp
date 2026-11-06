#include "view.h"
#include "landscape.h"
#include "gl_dispatch.h"
#include "shader_locations.h"
#include "camera.h"
#include "resourcemanager.h"
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QThread>
#include <QApplication>
#include <functional>
namespace { struct RestoreCapture { std::function<void()> restore; ~RestoreCapture(){restore();} }; }

QImage CView::captureTopdownPass(ETopdownPass pass, const QSize& size,
                               const QVector<SRenderItem>& items, QString* error)
{
    if(QThread::currentThread()!=thread() || !isValid() || m_minimapCaptureActive || m_snapshotCaptureActive) {
        *error="Renderer/context unavailable or already capturing";return {};
    }
    // The ordinary widget draw path closes its widget on shader failure.
    // An export must refuse that renderer before entering the presentation
    // path, rather than closing a document-bearing viewport as a side effect.
    if(!m_landProgram.isLinked() || !m_program.isLinked()) {
        *error="Terrain/object shader pipeline unavailable";return {};
    }
    const auto* previousContext=QGLContext::currentContext();
    makeCurrent();
    auto* context=QOpenGLContext::currentContext();
    if(!context) {*error="No owning GL context";return {};}
    auto* gl=context->functions();
    GLint framebuffer=0,viewport[4],program=0,depthFunc=0,blend[4],activeTexture=0,arrayBuffer=0,elementBuffer=0,vertexArray=0,texture0=0;
    GLfloat clearColor[4];GLboolean colorMask[4],depthMask=GL_TRUE;
    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);gl->glGetIntegerv(GL_VIEWPORT,viewport);
    gl->glGetIntegerv(GL_CURRENT_PROGRAM,&program);gl->glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);
    gl->glGetIntegerv(GL_BLEND_SRC_RGB,&blend[0]);gl->glGetIntegerv(GL_BLEND_DST_RGB,&blend[1]);
    gl->glGetIntegerv(GL_BLEND_SRC_ALPHA,&blend[2]);gl->glGetIntegerv(GL_BLEND_DST_ALPHA,&blend[3]);
    gl->glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
    gl->glActiveTexture(GL_TEXTURE0);gl->glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture0);gl->glActiveTexture(activeTexture);
    const bool hasVertexArrays=context->format().majorVersion()>=3;
    if(hasVertexArrays)gl->glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vertexArray);
    gl->glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&arrayBuffer);gl->glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&elementBuffer);
    gl->glGetFloatv(GL_COLOR_CLEAR_VALUE,clearColor);gl->glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);
    gl->glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
    const bool depth=gl->glIsEnabled(GL_DEPTH_TEST),blendOn=gl->glIsEnabled(GL_BLEND),cull=gl->glIsEnabled(GL_CULL_FACE),scissor=gl->glIsEnabled(GL_SCISSOR_TEST);
    const bool alphaTest=!context->isOpenGLES()&&ei::gl::IsEnabled(GL_ALPHA_TEST);
    GLint maximum=0;gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
    const auto projection=m_projection,view=m_minimapCaptureView;
    const auto camera=m_minimapCaptureCameraPosition;
    const bool land=m_landscapeVisible,drawLand=m_bDrawLand,water=m_bDrawWater,grid=m_tileGridVisible,preview=m_bPreviewTile;
    const auto anchor=m_visualWarmupCameraAnchor,pivot=m_visualWarmupCameraPivot,rotation=m_visualWarmupCameraRotation;
    const bool warmup=m_visualWarmupCameraValid;
    const bool mipmapping=CTextureList::getInstance()->terrainMipmappingEnabled();
    // Destruction order is intentional: delete the private FBO before restoring
    // its caller's framebuffer/context, including on allocation/readback failure.
    RestoreCapture restore{[&](){
        m_projection=projection;m_minimapCaptureView=view;m_minimapCaptureCameraPosition=camera;
        m_landscapeVisible=land;m_bDrawLand=drawLand;m_bDrawWater=water;m_tileGridVisible=grid;m_bPreviewTile=preview;
        m_topdownPass=ETopdownPass::None;m_topdownItems=nullptr;m_minimapCaptureActive=false;m_snapshotCaptureActive=false;
        m_visualWarmupCameraAnchor=anchor;m_visualWarmupCameraPivot=pivot;m_visualWarmupCameraRotation=rotation;m_visualWarmupCameraValid=warmup;
        CTextureList::getInstance()->setTerrainMipmappingEnabled(mipmapping);
        m_landProgram.bind();m_landProgram.setUniformValue(ei::uniformLocation(&m_landProgram,"u_cartographicPass"),0);
        m_landProgram.setUniformValue(ei::uniformLocation(&m_landProgram,"u_cartographicOverlayFilter"),0);
        m_program.bind();m_program.setUniformValue(ei::uniformLocation(&m_program,"u_cartographicPass"),0);
        gl->glBindFramebuffer(GL_FRAMEBUFFER,GLuint(framebuffer));gl->glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        gl->glUseProgram(GLuint(program));gl->glDepthFunc(GLenum(depthFunc));gl->glDepthMask(depthMask);
        gl->glBlendFuncSeparate(blend[0],blend[1],blend[2],blend[3]);
        gl->glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        gl->glClearColor(clearColor[0],clearColor[1],clearColor[2],clearColor[3]);
        for(const auto& state:QVector<QPair<GLenum,bool>>{{GL_DEPTH_TEST,depth},{GL_BLEND,blendOn},{GL_CULL_FACE,cull},{GL_SCISSOR_TEST,scissor}})
            if(state.second)gl->glEnable(state.first);else gl->glDisable(state.first);
        if(hasVertexArrays)context->extraFunctions()->glBindVertexArray(vertexArray);
        if(!context->isOpenGLES()){if(alphaTest)ei::gl::Enable(GL_ALPHA_TEST);else ei::gl::Disable(GL_ALPHA_TEST);}
        gl->glBindBuffer(GL_ARRAY_BUFFER,arrayBuffer);gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,elementBuffer);
        gl->glActiveTexture(GL_TEXTURE0);gl->glBindTexture(GL_TEXTURE_2D,texture0);gl->glActiveTexture(activeTexture);
        if(previousContext && previousContext!=QGLContext::currentContext())const_cast<QGLContext*>(previousContext)->makeCurrent();
    }};
    if(size.width()>maximum || size.height()>maximum) {*error="EXPORT_SIZE_LIMIT: GL texture limit "+QString::number(maximum);return {};}
    if(!m_landProgram.bind() || !m_program.bind()) {*error="Could not bind terrain/object shader pipeline";return {};}
    QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    QOpenGLFramebufferObject target(size,format);
    if(!target.isValid() || !target.bind()) {*error="Offscreen framebuffer unavailable";return {};}
    if(pass==ETopdownPass::None) {
        // Fresh current-camera frame even when paintGL deliberately skips a
        // hidden/minimized widget. No QWidget::grab, swap or camera update.
        m_snapshotCaptureActive=true;
        m_snapshotFramebuffer=target.handle();m_snapshotSize=size;
        gl->glViewport(0,0,size.width(),size.height());
        gl->glDisable(GL_SCISSOR_TEST);gl->glEnable(GL_DEPTH_TEST);
        gl->glDepthFunc(GL_LESS);gl->glDepthMask(GL_TRUE);
        gl->glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);gl->glEnable(GL_BLEND);
        gl->glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        if(m_editorServicePass){drawEditorServiceScene();drawTestOverlaysOpenGl();}
        else draw();
        gl->glFinish();
        auto image=target.toImage();
        if(image.isNull())*error="Current viewport framebuffer readback failed";
        return image;
    }
    const float w=m_pLand->widthInWorldUnits(),h=m_pLand->heightInWorldUnits();
    const float height=qMax(4096.0f,m_pLand->maximumHeight()+4096.0f);
    const QVector3D center(w/2,h/2,0);
    m_minimapCaptureCameraPosition=center+QVector3D(0,0,height);
    m_minimapCaptureView.setToIdentity();m_minimapCaptureView.lookAt(m_minimapCaptureCameraPosition,center,QVector3D(0,1,0));
    m_projection.setToIdentity();m_projection.ortho(-w/2,w/2,-h/2,h/2,1,height*2);
    m_minimapCaptureActive=true;m_topdownPass=pass;m_topdownItems=&items;
    m_landscapeVisible=true;m_bDrawLand=true;m_bPreviewTile=false;m_tileGridVisible=false;
    m_bDrawWater=pass==ETopdownPass::Composite||pass==ETopdownPass::Terrain||pass==ETopdownPass::Vegetation||pass==ETopdownPass::Water||pass==ETopdownPass::WaterMask;
    gl->glViewport(0,0,size.width(),size.height());gl->glDisable(GL_SCISSOR_TEST);gl->glDisable(GL_CULL_FACE);
    gl->glEnable(GL_DEPTH_TEST);gl->glDepthFunc(GL_LESS);gl->glDepthMask(GL_TRUE);
    gl->glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);gl->glEnable(GL_BLEND);
    // Store correct transparent alpha, rather than the historical alpha^2.
    gl->glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    draw();gl->glFinish();
    auto image=target.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if(image.isNull()) {*error="Framebuffer readback failed";return {};}
    // QOpenGLFramebufferObject::toImage() flips GL bottom-left rows once.
    // lookAt(up=+Y) therefore gives +X right / +Y up in top-left PNG rows.
    if(topdownMask(pass)) {
        QImage mask(size,QImage::Format_Grayscale8);
        for(int y=0;y<size.height();++y) {
            const auto* row=reinterpret_cast<const QRgb*>(image.constScanLine(y));auto* dst=mask.scanLine(y);
            for(int x=0;x<size.width();++x)dst[x]=uchar(qAlpha(row[x]));
        }
        return mask;
    }
    return image;
}

QImage CView::captureEditorServiceLayer(const QSize& size, QString* error)
{
    if(m_editorServicePass)return {};
    m_editorServicePass=true;
    RestoreCapture restore{[this]{m_editorServicePass=false;}};
    return captureTopdownPass(ETopdownPass::None,size,{},error);
}
