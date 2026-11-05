#pragma once
#include "res_archive_index.h"
#include "log.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent>
#include <QEventLoop>
#include <QThread>
#include "native_sha256.h"

// Supplemental audit hashes for legacy byte-backed writers are calculated off
// the GUI thread. Normal map batches reuse their existing journal digests.
inline QByteArray resAuditDigest(const QByteArray& bytes){
    const auto calculate=[bytes](){CNativeSha256 hash;if(hash.isAvailable()){hash.addData(bytes);const auto value=hash.result();if(!value.isEmpty())return value.toHex();}return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();};
    if(bytes.size()<=1024*1024)return calculate();
    QFutureWatcher<QByteArray> watcher;QEventLoop loop;
    QObject::connect(&watcher,&QFutureWatcher<QByteArray>::finished,&loop,&QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(calculate));
    if(QCoreApplication::instance()&&QThread::currentThread()==QCoreApplication::instance()->thread())loop.exec(QEventLoop::ExcludeUserInputEvents);
    else watcher.waitForFinished();
    return watcher.result();
}
// Thread-safe observations reuse digests already calculated by the save path.
// Reading the RES directory for namespace names never reads texture payloads.
struct SResObservedState { QByteArray sha; qint64 size{-1}; QDateTime modified; };
inline QMutex& resObservationMutex(){static QMutex mutex;return mutex;}
inline QMap<QString,SResObservedState>& resObservations(){static QMap<QString,SResObservedState> states;return states;}
inline SResObservedState knownResState(const QString& path){QMutexLocker guard(&resObservationMutex());return resObservations().value(QFileInfo(path).absoluteFilePath().toCaseFolded());}
inline void rememberResState(const QString& path,const QByteArray& sha,const QFileInfo& info){
    if(info.suffix().compare("res",Qt::CaseInsensitive)||sha.isEmpty())return;
    QMutexLocker guard(&resObservationMutex());SResObservedState state;state.sha=sha;state.size=info.size();state.modified=info.lastModified();
    resObservations()[info.absoluteFilePath().toCaseFolded()]=state;
}
inline QSet<QString> resNamespaces(const QString& path){
    CResArchiveIndex index;QSet<QString> names;if(!index.open(path))return names;
    const QRegularExpression pages("^(.+)[0-9]{3}\\.mmp$",QRegularExpression::CaseInsensitiveOption);
    for(const auto& member:index.members()){const auto match=pages.match(member.name);if(match.hasMatch())names.insert(match.captured(1).toCaseFolded());}
    return names;
}
struct SResWriteAudit { QString path; QByteArray before; qint64 size{-1}; QDateTime modified; QSet<QString> namespaces; };
inline SResWriteAudit beginResWriteAudit(const QString& path,const QByteArray& before){
    SResWriteAudit audit;if(QFileInfo(path).suffix().compare("res",Qt::CaseInsensitive))return audit;
    audit.path=QFileInfo(path).absoluteFilePath();audit.before=before;const QFileInfo info(path);audit.size=info.exists()?info.size():-1;audit.modified=info.lastModified();audit.namespaces=resNamespaces(path);
    return audit;
}
inline QJsonArray resSortedNames(const QSet<QString>& names){auto list=names.values();list.sort();return QJsonArray::fromStringList(list);}
inline void finishResWriteAudit(const SResWriteAudit& audit,const QByteArray& after,const char* operation){
    if(audit.path.isEmpty())return;
    const QFileInfo info(audit.path);const auto names=resNamespaces(audit.path);
    QJsonObject row{{"pid",double(QCoreApplication::applicationPid())},{"exe",QCoreApplication::applicationFilePath()},
        {"version",QCoreApplication::applicationVersion()},{"operation",QString::fromLatin1(operation)},{"path",audit.path},
        {"beforeSha256",QString::fromLatin1(audit.before)},{"afterSha256",QString::fromLatin1(after)},
        {"beforeBytes",double(audit.size)},{"afterBytes",double(info.size())},
        {"beforeMtime",audit.modified.toUTC().toString(Qt::ISODateWithMs)},{"afterMtime",info.lastModified().toUTC().toString(Qt::ISODateWithMs)},
        {"namespacesAdded",resSortedNames(names-audit.namespaces)},{"namespacesRemoved",resSortedNames(audit.namespaces-names)}};
    ei::log(eLogInfo,"RES write "+QString::fromUtf8(QJsonDocument(row).toJson(QJsonDocument::Compact)));
    rememberResState(audit.path,after,info);
}
inline QJsonObject resStateJson(const SResObservedState& state){return {{"sha256",state.sha.isEmpty()?QString("unknown"):QString::fromLatin1(state.sha)},{"bytes",double(state.size)},{"mtime",state.modified.toUTC().toString(Qt::ISODateWithMs)}};}
