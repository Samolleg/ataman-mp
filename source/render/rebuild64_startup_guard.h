#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// A separate marker per process/session keeps concurrently running mappers
// independent. It is a startup guard, not an exception handler or a save journal.
class CRebuild64StartupGuard {
public:
    explicit CRebuild64StartupGuard(const QString& directory):m_directory(directory) {
        m_path=QDir(directory).filePath(QStringLiteral("startup-%1-%2.json")
            .arg(QCoreApplication::applicationPid()).arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    }
    ~CRebuild64StartupGuard(){clear();}
    bool arm() {
        if(!QDir().mkpath(m_directory))return false;
        QSaveFile file(m_path);
        if(!file.open(QIODevice::WriteOnly))return false;
        const auto data=QJsonDocument(QJsonObject{{"schema",1},
            {"pid",QString::number(QCoreApplication::applicationPid())},
            {"birth",birth(QCoreApplication::applicationPid())},
            {"executable",QCoreApplication::applicationFilePath()}}).toJson();
        if(file.write(data)!=data.size())return false;
        m_armed=file.commit();return m_armed;
    }
    void clear(){if(m_armed){QFile::remove(m_path);m_armed=false;}}
    static QStringList interrupted(const QString& directory) {
        QStringList paths;
        for(const auto& name:QDir(directory).entryList({QStringLiteral("startup-*.json")},QDir::Files)) {
            const auto path=QDir(directory).filePath(name);QFile file(path);
            if(!file.open(QIODevice::ReadOnly))continue;
            const auto object=QJsonDocument::fromJson(file.readAll()).object();
            if(object.value("schema").toInt()!=1)continue;
            const auto pid=object.value("pid").toString().toLongLong();
            const auto current=birth(pid);
            // An inaccessible live process is deliberately treated as alive.
            if(current==QStringLiteral("unknown"))continue;
            if(current.isEmpty()||current!=object.value("birth").toString())paths.append(path);
        }
        return paths;
    }
private:
    static QString birth(qint64 pid) {
#ifdef Q_OS_WIN
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));
        if(!process)return GetLastError()==ERROR_INVALID_PARAMETER?QString():QStringLiteral("unknown");
        FILETIME created{},exited{},kernel{},user{};
        const bool valid=GetProcessTimes(process,&created,&exited,&kernel,&user)!=0;
        DWORD code=0;const bool alive=GetExitCodeProcess(process,&code)&&code==STILL_ACTIVE;CloseHandle(process);
        if(!alive)return {};
        if(!valid)return QStringLiteral("unknown");
        return QString::number((quint64(created.dwHighDateTime)<<32)|created.dwLowDateTime);
#else
        Q_UNUSED(pid)
        return QStringLiteral("unknown");
#endif
    }
    QString m_directory,m_path;
    bool m_armed{false};
};
