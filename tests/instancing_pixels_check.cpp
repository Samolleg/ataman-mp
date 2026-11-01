#include <QtWidgets>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include "main_window.h"
#include "view.h"
#include "part.h"
#include "render_queue.h"
#include "render_performance.h"

int runAssemblyCatalogCheck() {
    QTextStream out(stdout);int passes=0;
    const auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<'\n';out.flush();if(!ok)throw what;++passes;};
    try {
        const auto root=qEnvironmentVariable("EI_AUDIT_FIX_ROOT");check(!root.isEmpty(),"isolated root");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);
        window.resize(640,480);window.show();qApp->processEvents();auto* view=window.view();view->makeCurrent();
        check(view->isValid(),"hidden native renderer");
        QOpenGLShaderProgram program;
        check(program.addShaderFromSourceCode(QOpenGLShader::Vertex,R"(
            #version 120
            attribute vec3 a_position;attribute vec2 a_texture;attribute vec3 a_normal;
            attribute vec4 a_instanceMatrix0,a_instanceMatrix1,a_instanceMatrix2,a_instanceMatrix3,a_instanceColor;
            uniform bool u_instanced,u_partAnimated;uniform mat4 u_modelMmatrix,u_partMmatrix;uniform vec4 u_color;
            varying vec2 uv;varying vec4 color;varying float normalZ;
            void main(){mat4 m=u_instanced?mat4(a_instanceMatrix0,a_instanceMatrix1,a_instanceMatrix2,a_instanceMatrix3):u_modelMmatrix;
                vec4 p=vec4(a_position,1.0);if(!u_instanced&&u_partAnimated)p=u_partMmatrix*p;
                gl_Position=m*p;uv=a_texture;color=u_instanced?a_instanceColor:u_color;normalZ=a_normal.z;}
        )")&&program.addShaderFromSourceCode(QOpenGLShader::Fragment,R"(
            #version 120
            uniform sampler2D qt_Texture0;uniform int u_localShadowSkipIndex;uniform bool u_testPicking;
            uniform vec4 u_diplomacyTint;varying vec2 uv;varying vec4 color;varying float normalZ;
            void main(){vec4 base=texture2D(qt_Texture0,uv);
                if(u_testPicking)gl_FragColor=color;
                else gl_FragColor=vec4(base.rgb*normalZ*(u_localShadowSkipIndex<0?vec3(0.2,0.8,0.3):vec3(0.9,0.1,0.2))+u_diplomacyTint.rgb*0.1,1.0);}
        )")&&program.link(),"comparison shader linked: "+program.log());
        CPart part;QString name="synthetic";part.setName(name);
        for(const auto p:{QVector3D(-.12f,-.12f,0),QVector3D(.12f,-.12f,0),QVector3D(0,.12f,0)}){
            SVertexData vertex;vertex.position=p;vertex.normal={0,0,1};vertex.texCoord={0,0};part.vertData().append(vertex);
        }
        part.update();QImage pixels(2,2,QImage::Format_RGBA8888);pixels.fill(Qt::white);QOpenGLTexture texture(pixels);
        QOpenGLFramebufferObject fbo(256,256,QOpenGLFramebufferObject::CombinedDepthStencil);
        check(fbo.isValid(),"comparison framebuffer");
        QVector<SRenderItem> items;
        for(int i=0;i<24;++i){QMatrix4x4 matrix;matrix.translate(-.8f+(i%6)*.3f,-.75f+(i/6)*.45f,0);
            auto item=RenderQueueContext::prepareStaticItem(&part,&texture,matrix,ERenderMaterialClass::Opaque,true,100+i);
            item.pickingColor={float(i+1)/255,0,0,1};items.append(item);}
        items.last().diplomacyTint={1,0,0,1}; // a singleton after a successful instance run
        auto& queue=RenderQueueContext::instance();auto& performance=RenderPerformanceContext::instance();
        const auto render=[&](bool instanced,bool picking,QVector<quintptr> owners){
            fbo.bind();auto* gl=QOpenGLContext::currentContext()->functions();gl->glViewport(0,0,256,256);
            gl->glDisable(GL_SCISSOR_TEST);gl->glDisable(GL_CULL_FACE);gl->glDisable(GL_DEPTH_TEST);gl->glClearColor(0,0,0,0);gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            program.bind();program.setUniformValue("u_testPicking",picking);program.setUniformValue("qt_Texture0",0);
            performance.beginFrame(true,false);if(picking)queue.beginSelection(&program);else queue.begin(&program,{});
            queue.setLocalShadowOwners(owners);
            for(auto item:items){item.instanceEligible=instanced;queue.submitPrepared(item,true,true);}
            queue.flush();queue.end();gl->glFinish();auto image=fbo.toImage();fbo.release();return image;
        };
        for(const auto owners:{QVector<quintptr>{},QVector<quintptr>{101,107}}){
            auto immediate=render(false,false,owners);auto batched=render(true,false,owners);
            const auto counts=performance.currentFrameCounters();
            check(immediate==batched,"color pixels identical, local owners "+QString::number(owners.size()));
            check(counts.instancedDrawCalls>0&&counts.drawCalls<24,"real GPU instancing reduces draw calls to "+QString::number(counts.drawCalls));
            check(batched.pixelColor(218,52).alpha()>0,"singleton remains correctly positioned after instance batch");
        }
        auto immediate=render(false,true,{});auto batched=render(true,true,{});
        check(immediate==batched,"all picking pixels identical");
        check(performance.currentFrameCounters().instancedDrawCalls>0,"picking uses actual instance path");
        QSet<int> ids;for(int y=0;y<batched.height();++y)for(int x=0;x<batched.width();++x)if(batched.pixelColor(x,y).alpha())ids.insert(batched.pixelColor(x,y).red());
        check(ids.size()==24,"24 distinct picking identities preserved");
        QImage red(2,2,QImage::Format_RGBA8888),blue=red;
        red.fill(Qt::red);blue.fill(Qt::blue);
        QOpenGLTexture storageA(red),storageB(blue);
        const auto coincident=[&](bool stable,bool swapped) {
            fbo.bind();auto* gl=QOpenGLContext::currentContext()->functions();
            gl->glViewport(0,0,256,256);gl->glEnable(GL_DEPTH_TEST);gl->glDepthFunc(GL_LESS);gl->glDepthMask(GL_TRUE);
            gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            program.bind();program.setUniformValue("u_testPicking",false);
            queue.begin(&program,{});queue.setStableResourceOrder(stable);
            for(int i=0;i<2;++i) {
                auto* texture=(bool(i)==swapped)?&storageA:&storageB;
                auto item=RenderQueueContext::prepareStaticItem(&part,texture,QMatrix4x4(),ERenderMaterialClass::Opaque,false,0);
                item.stableTextureKey=i?"blue":"red";item.stableMeshKey="triangle";
                queue.submitPrepared(item,true,false);
            }
            queue.end();gl->glFinish();const auto result=fbo.toImage();fbo.release();return result;
        };
        const auto unstableBefore=coincident(false,false),stableBefore=coincident(true,false);
        storageA.destroy();storageA.setData(blue);storageB.destroy();storageB.setData(red);
        const auto unstableAfter=coincident(false,true),stableAfter=coincident(true,true);
        check(unstableBefore!=unstableAfter,"coplanar depth winner changes with pointer/resource assignment (original cause)");
        check(stableBefore==stableAfter,"stable resource order preserves every coplanar color pixel after relocation");
        performance.endFrame();out<<passes<<" PASS / 0 FAIL\n";return 0;
    }catch(const QString& error){out<<"FAIL "<<error<<'\n';return 1;}
}
