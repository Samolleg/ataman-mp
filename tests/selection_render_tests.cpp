#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <QTextStream>
#include "part.h"
#include "render_queue.h"
#include "render_performance.h"
#include "selection_region.h"
#include "diplomacy_colors.h"

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const QString& label) {
        ++checks; if(!ok) ++failures;
        out << (ok ? "PASS " : "FAIL ") << label << '\n'; out.flush();
    };
    const QSize viewport(128, 128);
    for(const QPoint& a : {QPoint(20,45), QPoint(35,45), QPoint(20,80), QPoint(35,80)})
    {
        const QPoint b(55-a.x(),125-a.y());
        check(selectionReadRegion(QRect(a,b),viewport) == QRect(20,47,16,36),
              "all directions cover identical inclusive rows and columns");
    }
    check(selectionReadRegion(QRect(60,60,1,1),viewport)==QRect(60,67,1,1),"single pixel");
    check(selectionReadRegion(QRect(20,60,16,1),viewport)==QRect(20,67,16,1),"horizontal row");
    check(selectionReadRegion(QRect(60,20,1,16),viewport)==QRect(60,92,1,16),"vertical column");
    check(selectionReadRegion(QRect(-5,-5,10,10),viewport)==QRect(0,123,5,5),"clip top left");
    check(selectionReadRegion(QRect(125,125,10,10),viewport)==QRect(125,0,3,3),"clip bottom right");
    check(selectionReadRegion(QRect(-20,-20,10,10),viewport).isEmpty(),"outside viewport");
    check(selectionReadRegion(QRect(0,0,128,128),viewport)==QRect(0,0,128,128),"full viewport");
    check(selectionReadRegion(QRect(),viewport).isEmpty(),"empty rectangle");

    QSurfaceFormat format; format.setVersion(3,3); format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setDepthBufferSize(24);
    QOpenGLContext context; context.setFormat(format);
    if(!context.create()) { check(false,"create native context"); return 2; }
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    if(!context.makeCurrent(&surface)) { check(false,"hidden context current"); return 2; }
    auto gl = context.functions(); gl->initializeOpenGLFunctions();
    out << "Renderer " << reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER)) << '\n';
    QOpenGLFramebufferObjectFormat fb; fb.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    QOpenGLFramebufferObject fbo(128,128,fb); check(fbo.isValid(),"framebuffer");
    QImage image(8,8,QImage::Format_RGBA8888); image.fill(QColor(0,255,0,0));
    for(int y=3;y<5;++y) for(int x=3;x<5;++x) image.setPixelColor(x,y,QColor(0,255,0,255));
    QOpenGLTexture cutout(image);
    image.fill(QColor(0,255,0,255)); QOpenGLTexture opaque(image);
    image.fill(QColor(0,255,0,25)); QOpenGLTexture belowThreshold(image);
    image.fill(QColor(0,255,0,26)); QOpenGLTexture aboveThreshold(image);
    for(auto* texture : {&cutout,&opaque,&belowThreshold,&aboveThreshold})
    {
        texture->setMinificationFilter(QOpenGLTexture::Nearest);
        texture->setMagnificationFilter(QOpenGLTexture::Nearest);
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
    CPart part;
    const QVector3D positions[] = {{-.8f,-.8f,0},{.8f,-.8f,0},{.8f,.8f,0},
                                  {-.8f,-.8f,0},{.8f,.8f,0},{-.8f,.8f,0}};
    const QVector2D uv[] = {{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
    for(int i=0;i<6;++i) { SVertexData v; v.position=positions[i]; v.normal={0,0,1}; v.texCoord=uv[i]; part.vertData().append(v); }
    part.update();
    CPart alternate(part); alternate.setTextureUvScale(.5f);
    QOpenGLShaderProgram visible, selection;
    const QString root = qEnvironmentVariable("EI_TEST_SOURCE_DIR");
    if(root.isEmpty()) { check(false,"EI_TEST_SOURCE_DIR required"); return 2; }
    for(auto* p : {&visible,&selection})
    {
        const bool linked=p->addShaderFromSourceFile(QOpenGLShader::Vertex,root+"/assets/shaders/vshader.vsh")
            &&p->addShaderFromSourceFile(QOpenGLShader::Fragment,root+"/assets/shaders/"+(p==&visible?QString("fshader.fsh"):QString("select.fsh")))&&p->link();
        check(linked,"actual shaders link"); if(!linked) {out<<p->log();return 2;}
    }
    auto setup = [&](QOpenGLShaderProgram& p) {
        fbo.bind(); gl->glViewport(0,0,128,128); gl->glDisable(GL_BLEND);
        gl->glEnable(GL_DEPTH_TEST); gl->glDepthFunc(GL_LESS); gl->glClearColor(0,0,0,0);
        gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); p.bind();
        QMatrix4x4 identity;
        p.setUniformValue("u_viewMmatrix",identity); p.setUniformValue("u_projMmatrix",identity);
        p.setUniformValue("u_modelMmatrix",identity); p.setUniformValue("qt_Texture0",0);
        p.setUniformValue("u_instanced",false);
        if(&p==&visible) { p.setUniformValue("customColor",QVector4D());p.setUniformValue("u_environmentEnabled",false);p.setUniformValue("u_highlight",false); }
        else p.setUniformValue("u_color",QVector4D(1,0,0,1));
    };
    auto count = [&](QRect screen, int component=0) {
        const QRect r=selectionReadRegion(screen,viewport);
        QByteArray data(r.width()*r.height()*3,0); gl->glPixelStorei(GL_PACK_ALIGNMENT,1);
        gl->glReadPixels(r.x(),r.y(),r.width(),r.height(),GL_RGB,GL_UNSIGNED_BYTE,data.data());
        int n=0; for(int i=component;i<data.size();i+=3) if(uchar(data[i])) ++n; return n;
    };
    const QRect margin(20,45,16,36), center(55,55,16,16), full(0,0,128,128);
    for(float scale : {1.f,.5f})
    {
        part.setTextureUvScale(scale);
        setup(visible); cutout.bind(0); part.drawImmediate(&visible);
        const QImage reference=fbo.toImage();
        if(scale==1.f) check(count(margin,1)==0,"visible transparent margin");
        setup(selection); cutout.bind(0); part.drawSelect(&selection);
        const QImage picked=fbo.toImage();
        bool same=true;
        for(int y=0;y<128;++y) for(int x=0;x<128;++x)
            if((reference.pixelColor(x,y).green()>0)!=(picked.pixelColor(x,y).red()>0)) same=false;
        check(same,QString("immediate visible/selection identical footprint UV %1").arg(scale));
        if(scale==1.f) { check(count(margin)==0,"invisible margin has no ID"); check(count(center)>0,"visible center selected"); picked.save("selection.png"); }
        // A real queue batch with distinct per-instance picking IDs.
        setup(selection); auto& queue=RenderQueueContext::instance(); queue.beginSelection(&selection);
        QMatrix4x4 left,right;left.translate(-.45f,0,0);left.scale(.45f);right.translate(.45f,0,0);right.scale(.45f);
        queue.submitSelection(&part,&cutout,left,{1,0,0,1}); queue.submitSelection(&part,&cutout,right,{0,1,0,1}); queue.end();
        const QImage batch=fbo.toImage();
        check(count(full)>0&&count(full,1)>0,"batch preserves both instance IDs");
        // Compare to actual immediate rendering at the same transforms.
        setup(selection); cutout.bind(0); selection.setUniformValue("u_modelMmatrix",left);part.drawSelect(&selection);
        selection.setUniformValue("u_color",QVector4D(0,1,0,1));selection.setUniformValue("u_modelMmatrix",right);part.drawSelect(&selection);
        check(batch==fbo.toImage(),QString("real instanced queue equals immediate UV %1").arg(scale));
        setup(selection);cutout.bind(0);
        QOpenGLBuffer instanceBuffer;check(instanceBuffer.create(),"instance buffer creation");
        instanceBuffer.bind();instanceBuffer.allocate(2*int(sizeof(SPartPickingInstance)));instanceBuffer.release();
        QVector<SPartPickingInstance> instances(2);instances[0].matrix=left;instances[0].color={1,0,0,1};instances[1].matrix=right;instances[1].color={0,1,0,1};
        check(part.drawSelectInstanced(&selection,&instanceBuffer,0,instances),"actual instanced draw executed");
        check(batch==fbo.toImage(),"explicit GPU instance buffer agrees with queue");
        setup(visible);cutout.bind(0);
        check(part.drawInstanced(&visible,&instanceBuffer,0,{left,right}),"visible instanced draw executed");
        const QImage visibleBatch=fbo.toImage();
        setup(visible);cutout.bind(0);visible.setUniformValue("u_modelMmatrix",left);part.drawImmediate(&visible);
        visible.setUniformValue("u_modelMmatrix",right);part.drawImmediate(&visible);
        check(visibleBatch==fbo.toImage(),QString("visible instanced UV matches immediate %1").arg(scale));
    }
    part.setTextureUvScale(1.f);
    setup(selection); auto& queue=RenderQueueContext::instance();queue.beginSelection(&selection);
    QMatrix4x4 left,right;left.translate(-.45f,0,0);left.scale(.45f);right.translate(.45f,0,0);right.scale(.45f);
    queue.submitSelection(&part,&cutout,left,{1,0,0,1});queue.submitSelection(&part,&opaque,right,{0,1,0,1});queue.end();
    check(count(full,1)>count(full)*8,"same mesh different textures retain different silhouettes");
    setup(selection);queue.beginSelection(&selection);
    queue.submitSelection(&part,&cutout,left,{1,0,0,1});queue.submitSelection(&alternate,&cutout,right,{0,1,0,1});queue.end();
    const QImage mixed=fbo.toImage();
    setup(selection);cutout.bind(0);selection.setUniformValue("u_modelMmatrix",left);part.drawSelect(&selection);
    selection.setUniformValue("u_color",QVector4D(0,1,0,1));selection.setUniformValue("u_modelMmatrix",right);alternate.drawSelect(&selection);
    check(mixed==fbo.toImage(),"different UV scales split queue correctly");
    CPart morph(part);morph.setSourceVertexIndices({0,1,2,3,4,5});
    morph.setMorphDeltas(QVector<QVector3D>(6,QVector3D(0,0,.1f)));
    check(!morph.canInstance(),"morphed part takes immediate queue fallback");
    setup(selection);queue.beginSelection(&selection);queue.submitSelection(&morph,&cutout,left,{1,0,0,1});queue.submitSelection(&morph,&cutout,right,{0,1,0,1});queue.end();
    const QImage fallback=fbo.toImage();
    setup(selection);cutout.bind(0);selection.setUniformValue("u_modelMmatrix",left);morph.drawSelect(&selection);
    selection.setUniformValue("u_color",QVector4D(0,1,0,1));selection.setUniformValue("u_modelMmatrix",right);morph.drawSelect(&selection);
    check(fallback==fbo.toImage(),"real immediate queue fallback footprint");
    setup(selection);belowThreshold.bind(0);part.drawSelect(&selection);check(count(full)==0,"alpha25 discarded");
    setup(selection);aboveThreshold.bind(0);part.drawSelect(&selection);check(count(full)>0,"alpha26 retained");
    setup(selection);QMatrix4x4 front;front.translate(0,0,-.5f);selection.setUniformValue("u_modelMmatrix",front);
    cutout.bind(0);part.drawSelect(&selection);selection.setUniformValue("u_modelMmatrix",QMatrix4x4());selection.setUniformValue("u_color",QVector4D(0,1,0,1));
    opaque.bind(0);part.drawSelect(&selection);
    check(count(margin,1)==margin.width()*margin.height()&&count(center)==center.width()*center.height(),"transparent front permits picking opaque geometry behind");
    // Diplomacy colors travel with each queued model, including identical meshes.
    auto tintImage=[&](const QVector4D& first,const QVector4D& second) {
        setup(visible); auto& colorQueue=RenderQueueContext::instance(); colorQueue.begin(&visible,{});
        colorQueue.setDiplomacyTint(first);
        colorQueue.submit(&part,&opaque,left,ERenderMaterialClass::Opaque,false);
        colorQueue.setDiplomacyTint(second);
        colorQueue.submit(&part,&opaque,right,ERenderMaterialClass::Opaque,false);
        colorQueue.end(); return fbo.toImage();
    };
    const QImage untinted=tintImage({},{});
    const auto enemy=diplomacy::mapTint(diplomacy::relationColor(2));
    const auto ally=diplomacy::mapTint(diplomacy::relationColor(0));
    const auto neutral=diplomacy::mapTint(diplomacy::relationColor(1));
    const auto unknown=diplomacy::mapTint(diplomacy::groupColor(-1));
    const QImage redGreen=tintImage(enemy,ally);
    const QColor red=redGreen.pixelColor(35,64),green=redGreen.pixelColor(93,64);
    out<<"Diplomacy GPU pixels "<<red.name()<<" "<<green.name()<<'\n';
    check(red.red()>red.green()*1.5 && green.green()>green.red()*2,"actual GPU same-mesh NPCs retain independent red and green tints");
    const QImage yellowGray=tintImage(neutral,unknown);
    const QColor yellow=yellowGray.pixelColor(35,64),gray=yellowGray.pixelColor(93,64);
    check(yellow.red()>yellow.blue()*2 && yellow.green()>yellow.blue()*2
        && qAbs(gray.red()-gray.blue())<55,"actual GPU neutral yellow and unknown gray");
    check(tintImage({},{} )==untinted,"disabling mode restores exact ordinary rendered bytes");
    const QImage isolated=tintImage(enemy,{});
    check(isolated.pixelColor(93,64)==untinted.pixelColor(93,64),"tint cannot leak from NPC to following uncolored scenery");
    SRenderItem batchA,batchB; batchA.instanceEligible=batchB.instanceEligible=true;
    batchA.materialClass=batchB.materialClass=ERenderMaterialClass::Opaque;
    batchA.diplomacyTint=enemy; batchB.diplomacyTint=ally;
    check(!shareInstanceBatch(batchA,batchB),"different diplomatic colors split instance batch");
    batchB.diplomacyTint=enemy; check(shareInstanceBatch(batchA,batchB),"equal diplomatic colors still permit instancing");
    setup(selection); queue.beginSelection(&selection); queue.setDiplomacyTint(enemy);
    queue.submitSelection(&part,&cutout,left,{1,0,0,1});queue.submitSelection(&part,&opaque,right,{0,1,0,1});queue.end();
    check(count(full,1)>count(full)*8,"diplomacy never replaces picking IDs or alpha silhouettes");
    check(gl->glGetError()==GL_NO_ERROR,"OpenGL status");
    RenderQueueContext::instance().releaseGpuResources();
    out<<"TOTAL "<<checks<<" assertions, failures="<<failures<<'\n';out.flush();
    return failures?1:0;
}
