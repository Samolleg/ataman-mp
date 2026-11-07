#include "native_output.h"
#include "rebuild64_viewport_widget.h"
#include "view.h"
#include "landscape.h"
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <QSemaphore>
#include <QApplication>
#include <memory>
#include <QPointer>
CRebuild64Viewport* nativeOutputViewport(CView* view) {
    for(auto* owner=view?view->parentWidget():nullptr;owner;owner=owner->parentWidget())
        if(auto* native=qobject_cast<CRebuild64Viewport*>(owner))return native;
    return nullptr;
}
QImage nativeOutput(CView* view,int mode,const QSize& size,const EirCamera& camera,QString* error,
                   const QString& model,const QString& texture,const QVector3D& shape,const QVector<float>& preview,const QVector<quint64>& objectKeys) {
    if(QThread::currentThread()!=qApp->thread()) {
        // Thumbnail workers wait on their own semaphore; the GUI only submits
        // the request and returns. No nested GUI loop blocks gallery input.
        struct State {QSemaphore done;QImage image;QString error;};
        const auto state=std::make_shared<State>();const QPointer<CView> weak=view;
        QMetaObject::invokeMethod(qApp,[=]{
            auto* viewport=weak?nativeOutputViewport(weak):nullptr;
            if(!viewport){state->error=QObject::tr("Rebuild64: просмотр закрыт.");state->done.release();return;}
            auto* completion=new QObject(qApp);auto* timeout=new QTimer(completion);timeout->setSingleShot(true);
            const auto serial=std::make_shared<quint64>(0);const auto delivered=std::make_shared<bool>(false);
            const auto finish=[=](const QImage& image,const QString& why){if(*delivered)return;*delivered=true;state->image=image;state->error=why;state->done.release();completion->deleteLater();};
            QObject::connect(viewport,&CRebuild64Viewport::outputFinished,completion,[=](quint64 id,const QImage& image,const QString& why,quint64,const QString&,int){if(id==*serial)finish(image,why);});
            QObject::connect(viewport,&QObject::destroyed,completion,[=]{finish({},QObject::tr("Rebuild64: просмотр закрыт."));});
            QObject::connect(qApp,&QCoreApplication::aboutToQuit,completion,[=]{finish({},QObject::tr("Rebuild64: приложение завершает работу."));});
            QObject::connect(timeout,&QTimer::timeout,completion,[=]{finish({},QObject::tr("Rebuild64: время ожидания просмотра истекло."));});
            *serial=viewport->requestOutput(size,mode,camera,model,texture,shape,preview,objectKeys);timeout->start(120000);
        },Qt::QueuedConnection);
        state->done.acquire();if(error)*error=state->error;return state->image;
    }
    auto* viewport=nativeOutputViewport(view);
    if(!viewport){if(error)*error=QObject::tr("Rebuild64: модуль вывода недоступен.");return {};}
    QEventLoop wait;QTimer timeout;timeout.setSingleShot(true);QImage result;QString diagnostic;
    quint64 serial=0;
    const auto connection=QObject::connect(viewport,&CRebuild64Viewport::outputFinished,&wait,
        [&](quint64 id,const QImage& image,const QString& why,quint64,const QString&,int){if(id==serial){result=image;diagnostic=why;wait.quit();}});
    QObject::connect(&timeout,&QTimer::timeout,&wait,[&]{diagnostic=QObject::tr("Rebuild64: время ожидания offscreen-вывода истекло.");wait.quit();});
    QObject::connect(viewport,&QObject::destroyed,&wait,[&]{diagnostic=QObject::tr("Offscreen отменён: вид закрыт.");wait.quit();});
    serial=viewport->requestOutput(size,mode,camera,model,texture,shape,preview,objectKeys);timeout.start(120000);
    wait.exec(QEventLoop::AllEvents);QObject::disconnect(connection);
    if(error)*error=diagnostic;return result;
}
static QImage topdown(CView* view,const QSize& size,bool objects,int pass,const QVector<quint64>& keys,QString* error) {
    auto* viewport=nativeOutputViewport(view);if(!viewport){if(error)*error=QObject::tr("Rebuild64: модуль вывода недоступен.");return {};}
    auto camera=viewport->outputCamera();const auto* land=CLandscape::getInstance();
    const float width=land->widthInWorldUnits(),height=land->heightInWorldUnits();
    if(width<=0||height<=0){if(error)*error=QObject::tr("Сначала откройте карту.");return {};}
    const QVector3D center(width*.5f,height*.5f,0);const float altitude=qMax(256.f,land->maximumHeight()+256.f);
    QMatrix4x4 viewMatrix,projection;viewMatrix.lookAt(center+QVector3D(0,0,altitude),center,QVector3D(0,1,0));
    projection.ortho(-width*.5f,width*.5f,-height*.5f,height*.5f,1,altitude*2);
    const auto clip=projection*viewMatrix;
    for(int row=0;row<4;++row)for(int column=0;column<4;++column)camera.world_to_clip[row*4+column]=row==2?.5f*(clip(2,column)+clip(3,column)):clip(row,column);
    camera.eye[0]=center.x();camera.eye[1]=center.y();camera.eye[2]=altitude;
    camera.target[0]=center.x();camera.target[1]=center.y();camera.target[2]=0;
    camera.visuals=(camera.visuals&1u)|4|8|(objects?16:0); // authored native terrain, water and optional static objects
    camera.world_time=0;camera.wind_force=0;
    return nativeOutput(view,pass?4:2,size,camera,error,{},{},{},{0,0,0,0,1,0,0,float(pass)},keys);
}

QImage nativeTopdown(CView* view,const QSize& size,bool objects,QString* error){return topdown(view,size,objects,0,{},error);}
QImage nativeCartographic(CView* view,int pass,const QSize& size,const QVector<quint64>& keys,QString* error){return topdown(view,size,false,pass,keys,error);}
