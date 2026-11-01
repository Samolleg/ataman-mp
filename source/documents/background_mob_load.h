#ifndef BACKGROUND_MOB_LOAD_H
#define BACKGROUND_MOB_LOAD_H
#include <QFileInfo>
#include <QSharedPointer>
#include <QVector>
#include <QStringList>
#include <atomic>
#include <memory>
class CMobDocument;

// Detached CPU values. Neither worker creates nodes nor touches the GL caches.
struct SPreparedMobLoad {
    QFileInfo source;
    QByteArray bytes, sha256, revision;
    QSharedPointer<const CMobDocument> document;
    QVector<QByteArray> objects;
    QStringList warnings;
    QString error;
    std::shared_ptr<void> backupLease;
    bool valid() const { return error.isEmpty() && !document.isNull(); }
    bool sourceIsCurrent() const;
};
struct SPreparedMobProjection {
    QByteArray cleanBytes;
    QVector<int> rawIndices;
    QString error;
};
SPreparedMobLoad prepareBackgroundMob(const QFileInfo& source,
                                      const std::shared_ptr<std::atomic_bool>& cancel);
SPreparedMobProjection validateBackgroundMobProjection(const SPreparedMobLoad& source,
                                                        const QByteArray& projection);
#endif
