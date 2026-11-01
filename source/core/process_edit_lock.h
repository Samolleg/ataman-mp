#ifndef PROCESS_EDIT_LOCK_H
#define PROCESS_EDIT_LOCK_H
#include <QtCore>
#include "process_edit_lock_store.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// One lifetime lease per physical document pair, shared by this process's
// views and save workers. QLockFile detects a dead owner; live leases never age.
class CProcessEditLock {
    QLockFile m_lock;
    explicit CProcessEditLock(const QString& path):m_lock(path){m_lock.setStaleLockTime(0);}
    static QString physicalPath(const QString& path) {
        const QFileInfo file(path);
        const auto absolute=QDir::cleanPath(file.absoluteFilePath());
#ifdef Q_OS_WIN
        // Qt 5.12 canonicalFilePath leaves directory junctions unresolved. A
        // hash of that spelling would split one document into independent locks.
        auto native=QDir::toNativeSeparators(absolute);
        if(!native.startsWith("\\\\?\\")) {
            if(native.startsWith("\\\\"))native="\\\\?\\UNC\\"+native.mid(2);
            else native="\\\\?\\"+native;
        }
        const HANDLE handle=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),0,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(handle!=INVALID_HANDLE_VALUE) {
            // The retained MinGW headers default to pre-Vista declarations.
            typedef DWORD (WINAPI *FinalPath)(HANDLE,LPWSTR,DWORD,DWORD);
            static const auto finalPath=reinterpret_cast<FinalPath>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetFinalPathNameByHandleW"));
            const DWORD size=finalPath?finalPath(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS):0;
            QVector<wchar_t> buffer(int(size)+1);
            const DWORD written=size?finalPath(handle,buffer.data(),DWORD(buffer.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS):0;
            CloseHandle(handle);
            if(written&&written<DWORD(buffer.size())) {
                auto resolved=QString::fromWCharArray(buffer.constData(),int(written));
                if(resolved.startsWith("\\\\?\\UNC\\"))resolved="\\\\"+resolved.mid(8);
                else if(resolved.startsWith("\\\\?\\"))resolved=resolved.mid(4);
                return QDir::fromNativeSeparators(resolved);
            }
        }
#endif
        // Save As may name a file (or directory) that does not exist yet. Resolve
        // its nearest existing ancestor so creating it cannot change the key.
        if(!file.exists()) {
            const auto parent=file.absolutePath();
            if(parent!=absolute)return QDir(physicalPath(parent)).filePath(file.fileName());
        }
        const auto canonical=file.canonicalFilePath();
        return canonical.isEmpty()?absolute:canonical;
    }
    static QString documentPath(QString path,bool map) {
        path=physicalPath(path);
        if(map){QFileInfo f(path);path=f.dir().filePath(f.completeBaseName()+".mpr");}
        return path;
    }
public:
    static QString identity(const QString& path) {
        return QDir::cleanPath(physicalPath(path)).toCaseFolded();
    }
    static QString lockFilePath(const QString& path,bool map=true) {
        const auto key=identity(documentPath(path,map));
        const auto hash=QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(),QCryptographicHash::Sha256).toHex());
        // Machine-wide store; document identity and suffixes remain unchanged.
        const auto root=mapper_edit_locks::directory();
        if(root.isEmpty())return {};
        return QDir(root).filePath(hash
            +(map?".mapper-edit.lock":".mapper-resource.lock"));
    }
    static QSharedPointer<CProcessEditLock> acquire(QString path,QString* error=nullptr,bool map=true) {
        path=documentPath(path,map);
        const auto name=lockFilePath(path,map);
        static QMutex mutex;static QMap<QString,QWeakPointer<CProcessEditLock>> leases;
        QMutexLocker guard(&mutex);auto lease=leases.value(name).toStrongRef();if(lease)return lease;
        const auto directory=mapper_edit_locks::directory();
        const auto busy=[&](){
            if(error)*error=map?QString::fromUtf8("Карта открыта владельцем в другом маппере (или другим агентом): ")+path
                :QString::fromUtf8("Общий ресурс обновляется другим маппером; повторите операцию: ")+path;
            if(error)*error+=QString::fromUtf8("; папка блокировок: ")+QDir::toNativeSeparators(directory);
        };
        // Probe both old locations without tryLock: even a successful probe of
        // an existing dead lock would create/remove a file inside the mod.
        const auto key=identity(path);
        const auto hash=QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(),QCryptographicHash::Sha256).toHex());
        const auto suffix=map?QStringLiteral(".mapper-edit.lock"):QStringLiteral(".mapper-resource.lock");
        const auto oldStore=QDir(QFileInfo(key).absolutePath()).filePath(".mapper-service/locks/"+hash+suffix);
        for(const auto& old:{oldStore,key+suffix})
            if(mapper_edit_locks::existingOwner(old)){busy();return {};}
        if(!mapper_edit_locks::prepareDirectory(directory,error))return {};
        lease=QSharedPointer<CProcessEditLock>(new CProcessEditLock(name));
        if(!lease->m_lock.tryLock(0)) {
            if(lease->m_lock.error()==QLockFile::LockFailedError)busy();
            else if(error)*error=QString::fromUtf8("Не удалось создать служебный файл блокировки: ")+name;
            return {};
        }
        // Old editors cannot see ProgramData leases: update all simultaneous copies.
        leases[name]=lease;return lease;
    }
};
#endif
