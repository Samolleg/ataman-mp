#ifndef PROCESS_EDIT_LOCK_H
#define PROCESS_EDIT_LOCK_H
#include <QtCore>

// One lifetime lease per physical document pair, shared by this process's
// views and save workers. QLockFile detects a dead owner; live leases never age.
class CProcessEditLock {
    QLockFile m_lock;
    explicit CProcessEditLock(const QString& path):m_lock(path){m_lock.setStaleLockTime(0);}
public:
    static QString identity(const QString& path) {
        QFileInfo f(path);QString p=f.canonicalFilePath();
        if(p.isEmpty())p=QDir(f.absolutePath()).canonicalPath()+"/"+f.fileName();
        return QDir::cleanPath(p).toCaseFolded();
    }
    static QSharedPointer<CProcessEditLock> acquire(QString path,QString* error=nullptr,bool map=true) {
        const auto physical=QFileInfo(path).canonicalFilePath();if(!physical.isEmpty())path=physical;
        if(map){QFileInfo f(path);path=f.dir().filePath(f.completeBaseName()+".mpr");}
        const auto name=identity(path)+(map?".mapper-edit.lock":".mapper-resource.lock");
        static QMutex mutex;static QMap<QString,QWeakPointer<CProcessEditLock>> leases;
        QMutexLocker guard(&mutex);auto lease=leases.value(name).toStrongRef();if(lease)return lease;
        lease=QSharedPointer<CProcessEditLock>(new CProcessEditLock(name));
        if(!lease->m_lock.tryLock(0)) {
            if(error)*error=map?QString::fromUtf8("Карта открыта владельцем в другом маппере (или другим агентом): ")+path
                :QString::fromUtf8("Общий ресурс обновляется другим маппером; повторите операцию: ")+path;
            return {};
        }
        leases[name]=lease;return lease;
    }
};
#endif
