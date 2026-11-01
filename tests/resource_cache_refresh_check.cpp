#include <QtWidgets>
#include <QOpenGLFunctions_1_1>
#include "main_window.h"
#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "resourcemanager.h"
#include "settings.h"
#include "mmp_codec.h"
#include "res_file.h"

namespace {
QTextStream out(stdout);int passes=0;
void check(bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<'\n';out.flush();if(!ok)throw what;++passes;}
class Hidden:public QObject {
    bool eventFilter(QObject* object,QEvent* event)override {
        if(event->type()==QEvent::Polish||event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))if(widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QJsonArray rgba(QOpenGLTexture* texture) {
    QByteArray bytes(texture->width()*texture->height()*4,0);
    auto* gl=QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_1_1>();
    check(gl&&gl->initializeOpenGLFunctions(),"desktop texture readback");
    texture->bind();gl->glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());
    return {int(uchar(bytes[0])),int(uchar(bytes[1])),int(uchar(bytes[2])),int(uchar(bytes[3]))};
}
QByteArray figureBytes(float coordinate) {
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream<<quint32(0x31474946);
    for(int count:{1,1,1,3,1,0,0,0,0})stream<<qint32(count);
    for(int i=0;i<10;++i)stream<<float(0);
    for(int i=0;i<12;++i)stream<<coordinate;
    for(int i=0;i<18;++i)stream<<float(1);
    for(int i=0;i<6;++i)stream<<quint16(0);
    return bytes;
}
float coordinate(ei::CFigure* figure) {
    QVector<CPart*> parts;QVector3D shape(1,1,1);QStringList visible;
    figure->getVertexData(parts,shape,visible,false);
    const float result=parts.isEmpty()||parts[0]->vertData().isEmpty()?-100:parts[0]->vertData()[0].position.x();
    qDeleteAll(parts);return result;
}
}
int runAssemblyCatalogCheck() {
    try {
        const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_AUDIT_FIX_ROOT"));check(!root.isEmpty(),"isolated root");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setQuitOnLastWindowClosed(false);
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);
        window.resize(900,650);window.show();qApp->processEvents();auto& view=*window.view();view.makeCurrent();
        check(view.isValid(),"native renderer");
        auto* roots=dynamic_cast<COptStringList*>(view.settings()->opt("resourceRoots"));check(roots,"root option");const auto savedRoots=roots->value();
        const QString model="initqiaudititem",texture="audit_cache_source";
        for(int n=0;n<2;++n) {
            QImage image(4,4,QImage::Format_RGBA8888);image.fill(n?Qt::blue:Qt::red);QString error;
            CResFile archive;archive.addFiledata(texture+".mmp",ei::encodeMmp8888(image,&error));archive.addFiledata(model+".fig",figureBytes(float(n+1)));
            QFile file(root+QString("/source-%1.res").arg(n));const auto bytes=archive.generateResData();
            check(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size(),"write synthetic RES");
        }
        roots->setValue({root+"/source-0.res"});view.refreshAfterResourceSetup();view.makeCurrent();
        auto* textures=CTextureList::getInstance();auto* models=CObjectList::getInstance();
        auto* first=textures->texture(texture,model);auto* firstFigure=models->getFigureResource(model+".fig");
        check(rgba(first)==QJsonArray{255,0,0,255}&&firstFigure&&coordinate(firstFigure)==1,"source A pixels and geometry");
        auto figureLease=models->retainSceneFigure(firstFigure);QWeakPointer<ei::CFigure> weakFigure=figureLease.toWeakRef();
        QJsonObject base{{"Complection",QJsonArray{1,1,1}}};
        auto* node=new CWorldObj(QJsonObject{{"Model name",model},{"Primary texture",texture},{"Base object",base}});
        node->setMapId(17);node->loadFigure();node->loadTexture();const auto authored=node->toJson();
        auto clone=QSharedPointer<CWorldObj>::create(*node);
        auto* mob=new CMob;mob->nodes().append(node);view.m_aMob.append(mob);
        roots->setValue({root+"/source-1.res"});view.refreshAfterResourceSetup();view.makeCurrent();
        auto* second=textures->texture(texture,model);auto* secondFigure=models->getFigureResource(model+".fig");
        check(second!=first&&rgba(second)==QJsonArray{0,0,255,255},"same texture name reloads blue pixels");
        check(secondFigure!=firstFigure&&coordinate(secondFigure)==2,"same model name reloads geometry");
        check(node->renderedFigure()==secondFigure&&node->toJson()==authored&&!mob->isDirty(),"live object rebound without authored-data changes");
        check(rgba(first)==QJsonArray{255,0,0,255}&&coordinate(clone->renderedFigure())==1,"clone retains previous visual resources safely");
        check(textures->textureSources(first)[0].toObject()["provider"].toString().endsWith("source-0.res"),"held pixels retain actual old provenance");
        check(textures->textureSources(second)[0].toObject()["provider"].toString().endsWith("source-1.res"),"new pixels retain actual new provenance");
        figureLease.clear();check(!weakFigure.isNull(),"clone pins old figure");clone.clear();textures->collectRetiredTextures();
        check(weakFigure.isNull()&&textures->textureSources(first).isEmpty(),"unreferenced old model and texture released");
        check(textures->layeredTextureRefreshLifetimeRegressionCheck(),"layered refresh lifetime survives repeated publications");
        view.m_aMob.removeAll(mob);delete mob;
        roots->setValue(savedRoots);view.refreshAfterResourceSetup();view.makeCurrent();textures->trimTransientVisualCache();
        out<<passes<<" PASS / 0 FAIL\n";out.flush();return 0;
    }catch(const QString& error){out<<"FAIL "<<error<<'\n';out.flush();return 1;}
}
