#ifndef LOG_ROTATION_H
#define LOG_ROTATION_H
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>

namespace ei {
inline bool boundLogTail(const QString& path, qint64 limit = 10LL*1024*1024)
{
    if(QFileInfo(path).size()<=limit)return true;
    QFile input(path);
    if(!input.open(QIODevice::ReadOnly) || !input.seek(input.size()-limit))return false;
    input.readLine(); // retain complete lines and UTF-8 characters
    const auto tail=input.readAll();input.close();
    QSaveFile output(path);output.setDirectWriteFallback(false);
    return output.open(QIODevice::WriteOnly) && output.write(tail)==tail.size() && output.commit();
}
// Caller owns its writer lock; files are closed between appends.
inline bool rotateLog(const QString& path, qint64 incomingBytes,
                      qint64 limit = 10LL*1024*1024, int files = 5)
{
    if(!boundLogTail(path,limit))return false;
    for(int i=1;i<files;++i)if(!boundLogTail(path+"."+QString::number(i),limit))return false;
    if(QFileInfo(path).size() + incomingBytes <= limit) return true;
    const QString oldest = path + "." + QString::number(files-1);
    if(QFile::exists(oldest) && !QFile::remove(oldest)) return false;
    for(int i=files-2;i>=1;--i) {
        const QString from=path+"."+QString::number(i);
        if(QFile::exists(from) && !QFile::rename(from,path+"."+QString::number(i+1))) return false;
    }
    return !QFile::exists(path) || QFile::rename(path,path+".1");
}
inline void pruneWorkflowLogs(const QDir& directory, int keep = 30)
{
    const auto logs=directory.entryInfoList({"workflow-*.log"},QDir::Files|QDir::NoSymLinks,QDir::Time);
    for(int i=0;i<qMin(keep,logs.size());++i) {
        boundLogTail(logs[i].absoluteFilePath());
        for(int j=1;j<5;++j)boundLogTail(logs[i].absoluteFilePath()+"."+QString::number(j));
    }
    for(int i=keep;i<logs.size();++i) {
        const auto path=logs[i].absoluteFilePath();
        if(QFile::remove(path))for(int j=1;j<5;++j)QFile::remove(path+"."+QString::number(j));
    }
}
}
#endif
