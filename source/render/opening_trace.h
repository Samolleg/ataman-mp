#pragma once
// Opt-in isolated launch diagnostics. No production settings or files are written.
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <chrono>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace opening {
inline qint64 clock() {
#ifdef Q_OS_WIN
    LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;
#else
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}
inline void span(const char* name,qint64 begin,const QString& detail={}) {
    static QMutex mutex;const auto root=qgetenv("ATAMAN_REBUILD64_LAUNCH_CASE");if(root.isEmpty())return;
    const auto directory=QDir::fromNativeSeparators(QFileInfo(QString::fromUtf8(root)).absolutePath());
    if(!directory.contains("/work/rebuild64-launch-287-",Qt::CaseInsensitive))return;
    const QString path=directory+"/opening-spans.jsonl";
    QJsonObject row{{"stage",QString::fromLatin1(name)},{"begin",double(begin)},{"end",double(clock())},
        {"thread",QString::number(quintptr(QThread::currentThreadId()))},{"detail",detail}};
    QMutexLocker lock(&mutex);QFile f(path);if(f.open(QIODevice::WriteOnly|QIODevice::Append))f.write(QJsonDocument(row).toJson(QJsonDocument::Compact)+'\n');
}
struct Scope {const char* name;qint64 begin;QString detail;Scope(const char* n,const QString& d={}):name(n),begin(clock()),detail(d){}~Scope(){span(name,begin,detail);}};
}
