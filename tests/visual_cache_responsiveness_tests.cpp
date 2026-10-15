
#include "persistent_visual_cache.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir scratch(QDir::currentPath()+"/cache-benchmark-XXXXXX");
    if(!scratch.isValid())return 1;
    const QString category=scratch.path()+"/cache/character-plans";QDir().mkpath(category);
    for(int i=0;i<20000;++i){QFile f(category+"/"+QString::number(i)+".json");if(!f.open(QIODevice::WriteOnly)||f.write(QByteArray(512,'x'))!=512)return 2;}
    QFile keep(category+"/keep.bin");if(!keep.open(QIODevice::WriteOnly)||keep.write("preserve")!=8)return 3;keep.close();
    ei::setVisualCacheLimitMiB(1);
    QElapsedTimer gap;gap.start();qint64 maxGap=0;int beats=0;
    QTimer heartbeat;heartbeat.setInterval(5);heartbeat.setTimerType(Qt::PreciseTimer);
    QObject::connect(&heartbeat,&QTimer::timeout,[&](){maxGap=qMax(maxGap,gap.restart());++beats;});heartbeat.start();
    QElapsedTimer call;call.start();ei::recordVisualCacheFileWrite(category,0,512);
    const auto callMs=call.elapsed();
    if(app.arguments().contains("--quit-during-maintenance")) {
        QElapsedTimer shutdown;shutdown.start();QTimer::singleShot(0,&app,&QCoreApplication::quit);app.exec();
        const bool pass=!ei::visualCacheMaintenancePending() && shutdown.elapsed()<200;
        QTextStream(stdout)<<QJsonDocument(QJsonObject{{"pass",pass},{"shutdownMs",shutdown.elapsed()}}).toJson();
        return pass?0:6;
    }
    QCoreApplication::processEvents();
#ifndef CACHE_BASELINE
    QElapsedTimer deadline;deadline.start();
    while(ei::visualCacheMaintenancePending()&&deadline.elapsed()<30000){QCoreApplication::processEvents();QThread::msleep(2);}
    if(ei::visualCacheMaintenancePending())return 4;
#endif
    QCoreApplication::processEvents();
    qint64 bytes=0;int files=0;QDirIterator it(category,{"*.json"},QDir::Files);
    while(it.hasNext()){it.next();bytes+=it.fileInfo().size();++files;}
    const bool pass=bytes<=1024*1024*9/10 && QFileInfo::exists(category+"/keep.bin")
        && callMs<200 && maxGap<200;
    QJsonObject result{{"pass",pass},{"callMs",callMs},{"heartbeatMaxMs",maxGap},{"heartbeats",beats},{"retainedBytes",bytes},{"retainedFiles",files},{"fixtureFiles",20000}};
    QTextStream(stdout)<<QJsonDocument(result).toJson();
    return pass?0:5;
}
