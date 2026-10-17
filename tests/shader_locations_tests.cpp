#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QElapsedTimer>
#include <QThread>
#include <QTextStream>
#include "shader_locations.h"
#include "helper_visibility.h"

namespace {
using Query = decltype(QOpenGLFunctionsPrivate::Functions::GetUniformLocation);
Query originalQuery;
int queries=0, delayMs=0;
GLint QOPENGLF_APIENTRY countedQuery(GLuint program,const char* name)
{ ++queries;if(delayMs)QThread::msleep(delayMs);return originalQuery(program,name); }
class QueryHook : public QOpenGLFunctions {
public:
    QueryHook(){initializeOpenGLFunctions();originalQuery=d_ptr->f.GetUniformLocation;d_ptr->f.GetUniformLocation=countedQuery;}
    ~QueryHook(){d_ptr->f.GetUniformLocation=originalQuery;}
};
}
int main(int argc,char** argv)
{
    QGuiApplication app(argc,argv);QTextStream out(stdout);int passes=0,failed=0;
    auto check=[&](bool ok,const char* name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();ok?++passes:++failed;};
    QOpenGLContext context;check(context.create(),"native context");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();
    if(!context.makeCurrent(&surface))return 2;
    QueryHook hook;
    QOpenGLShaderProgram program;
    const auto compile=[&](QOpenGLShaderProgram& p){return p.addShaderFromSourceCode(QOpenGLShader::Vertex,
        "attribute vec4 position;uniform mat4 matrix;void main(){gl_Position=matrix*position;}")
        &&p.addShaderFromSourceCode(QOpenGLShader::Fragment,
        "uniform vec4 color;void main(){gl_FragColor=color;}")&&ei::linkShaderProgram(p);};
    check(compile(program)&&program.bind(),"linked and bound shader");
    const int q0=queries;program.setUniformValue("color",QVector4D(1,0,0,1));
    check(queries==q0+1,"hook observes real Qt name setter driver query");
    int color=ei::uniformLocation(&program,"color");int matrix=ei::uniformLocation(&program,"matrix");
    check(color>=0&&matrix>=0,"active locations resolved");
    const int warm=queries;delayMs=25;QElapsedTimer timer;timer.start();
    for(int i=0;i<8;++i)program.setUniformValue("color",QVector4D(0,1,0,1));const qint64 oldMs=timer.elapsed();
    check(queries==warm+8&&oldMs>=190,"name setter reproduces driver query stalls");
    const int before=queries;timer.restart();
    for(int i=0;i<8;++i)program.setUniformValue(ei::uniformLocation(&program,"color"),QVector4D(0,0,1,1));const qint64 newMs=timer.elapsed();delayMs=0;
    check(queries==before&&newMs<20,"cached setters never enter delayed driver query");
    GLfloat value[4]{};context.functions()->glGetUniformfv(program.programId(),color,value);
    check(value[0]==0&&value[1]==0&&value[2]==1&&value[3]==1,"cached setter preserves actual GPU uniform value");
    check(ei::uniformLocation(&program,"absent")==-1,"inactive location retained");const int absent=queries;
    check(ei::uniformLocation(&program,"absent")==-1&&queries==absent,"inactive location also avoids repeated query");
    const int attr=ei::attributeLocation(&program,"position");check(attr>=0&&attr==program.attributeLocation("position"),"attribute identity exact");
    const int linked=queries;check(ei::linkShaderProgram(program)&&program.bind(),"relink through cache boundary");
    color=ei::uniformLocation(&program,"color");check(queries==linked+1&&color>=0,"relink invalidates cached locations");
    const int size=ei::shaderLocationStore().size();
    {QOpenGLShaderProgram other;check(compile(other),"independent shader program");ei::uniformLocation(&other,"color");check(ei::shaderLocationStore().size()==size+1,"locations isolated by program object");}
    check(ei::shaderLocationStore().size()==size,"destroyed program releases cache entries");
    QMatrix4x4 identity;CViewFrustum frustum(identity);
    {ei::CHelperVisibilityScope scope(&frustum);ei::CHelperBounds outside;outside.add({10,0,0});check(!outside.visible(),"offscreen helper culled");
     ei::CHelperBounds crossing;crossing.add({-10,0,0});crossing.add({10,0,0});check(crossing.visible(),"route crossing view retained with both ends offscreen");
     ei::CHelperBounds sphere;sphere.add({2,0,0},1.1f);check(sphere.visible(),"marker extent entering view retained");}
    ei::CHelperBounds uncull;uncull.add({10,0,0});check(uncull.visible(),"visibility scope restores ordinary rendering");
    out<<"TIME delayed name setters "<<oldMs<<" ms cached "<<newMs<<" ms\nRESULT "<<passes<<" passed "<<failed<<" failed\n";return failed?1:0;
}
