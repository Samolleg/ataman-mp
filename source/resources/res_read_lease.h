#pragma once

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QThread>
#include <QCoreApplication>
#include <QException>
#include <QMap>
#include <QVector>
#include <vector>
#include <QCryptographicHash>
#include <functional>
#include <memory>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Header-only so standalone document/reader test targets share the same gate
// without changing their source lists. The mutex protects admission only, never I/O.
namespace resIo {
struct State {
    int readers{0};
    int waitingWriters{0};
    Qt::HANDLE writer{nullptr};
    quint64 generation{0};
    QHash<Qt::HANDLE, int> readerThreads;
    QWaitCondition changed;
};
struct Registry {
    QMutex mutex;
    QHash<QString, std::shared_ptr<State>> paths;
};
inline Registry& registry() { static Registry value; return value; }
inline QString pathKey(const QString& path)
{
    const QFileInfo info(path);
    // Canonicalize the parent too: a new destination must retain its identity
    // after creation, including when its parent is reached through a junction.
    const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
    const QString canonical = info.canonicalFilePath();
    QString key = QDir::cleanPath(!canonical.isEmpty() ? canonical
        : parent.isEmpty() ? info.absoluteFilePath() : QDir(parent).filePath(info.fileName()));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
inline std::shared_ptr<State> state(const QString& key)
{
    auto& value = registry().paths[key];
    if(!value) value = std::make_shared<State>();
    return value;
}
// Resolve filesystem identity at snapshot creation, not at every thumbnail/cache
// check. Handles refer to the same registry State used by path-based publishers.
struct ReadPath {
    QString key;
    std::shared_ptr<State> handle;
};
inline ReadPath readPath(const QString& path)
{
    const QString key = pathKey(path);
    auto& r = registry(); QMutexLocker lock(&r.mutex);
    return {key, state(key)};
}
inline QVector<ReadPath> readPaths(const QVector<QString>& paths)
{
    QMap<QString, ReadPath> ordered;
    for(const auto& path : paths) {
        const auto resolved = readPath(path);
        ordered.insert(resolved.key, resolved);
    }
    return ordered.values().toVector();
}
inline quint64 generation(const std::shared_ptr<State>& handle)
{
    auto& r = registry(); QMutexLocker lock(&r.mutex);
    return handle->generation;
}
inline quint64 generation(const QString& path)
{
    return generation(readPath(path).handle);
}
inline QVector<quint64> generations(const QVector<ReadPath>& paths)
{
    QVector<quint64> result; result.reserve(paths.size());
    auto& r = registry(); QMutexLocker lock(&r.mutex);
    for(const auto& path : paths) result.append(path.handle->generation);
    return result;
}
inline bool generationsMatch(const QVector<ReadPath>& paths, const QVector<quint64>& snapshot)
{
    if(paths.size() != snapshot.size()) return false;
    auto& r = registry(); QMutexLocker lock(&r.mutex);
    for(int i = 0; i < paths.size(); ++i)
        // After file reads are released, rasterization may overlap a publisher
        // whose generation has not advanced yet. Reject that in-progress result.
        if(paths[i].handle->generation != snapshot[i] || paths[i].handle->writer) return false;
    return true;
}
class ReadLease {
public:
    explicit ReadLease(const QString& path, QString* error = nullptr, int timeoutMs = 10000)
        : ReadLease(readPath(path), error, timeoutMs) {}
    explicit ReadLease(const ReadPath& path, QString* error = nullptr, int timeoutMs = 10000)
    {
        auto& r = registry(); QMutexLocker lock(&r.mutex);
        m_state = path.handle;
        if(!m_state) {
            if(error) *error = QStringLiteral("reader-admission: invalid source handle");
            return;
        }
        m_thread = QThread::currentThreadId();
        // Writer-owned validation and admitted nested reads never wait. A fresh
        // worker sleeps with the registry mutex released, giving writers priority.
        // The GUI must not wait: publication may itself need this thread.
        QElapsedTimer timer; timer.start();
        while(m_state->writer != m_thread
           && (m_state->writer || m_state->waitingWriters)
           && !m_state->readerThreads.value(m_thread)) {
            const bool gui = QCoreApplication::instance()
                && QThread::currentThread() == QCoreApplication::instance()->thread();
            const qint64 remaining = timeoutMs - timer.elapsed();
            if(gui || remaining <= 0) {
                if(error) *error = gui
                    ? QStringLiteral("reader-admission: GUI read cannot wait for publication: %1").arg(path.key)
                    : QStringLiteral("reader-admission timeout after %1 ms: %2; publication still in progress")
                        .arg(timer.elapsed()).arg(path.key);
                m_state.reset(); return;
            }
            m_state->changed.wait(&r.mutex, static_cast<unsigned long>(remaining));
        }
        ++m_state->readers; ++m_state->readerThreads[m_thread];
    }
    ~ReadLease() { release(); }
    bool acquired() const { return bool(m_state); }
    void release() {
        if(!m_state) return;
        auto& r = registry(); QMutexLocker lock(&r.mutex);
        --m_state->readers;
        if(--m_state->readerThreads[m_thread] == 0) m_state->readerThreads.remove(m_thread);
        m_state->changed.wakeAll(); m_state.reset();
    }
    ReadLease(const ReadLease&) = delete;
    ReadLease& operator=(const ReadLease&) = delete;
private:
    std::shared_ptr<State> m_state;
    Qt::HANDLE m_thread{nullptr};
};
// Stable, ordered snapshot admission, before taking any provider/cache mutex.
// The total background admission budget is bounded, even with several archives.
class ReadLeases {
public:
    explicit ReadLeases(const QVector<QString>& paths, QString* error = nullptr, int timeoutMs = 10000) {
        acquire(readPaths(paths), error, timeoutMs);
    }
    // readPaths returns a unique canonical-key order. Reuse that immutable order
    // for the lifetime of a provider snapshot, preserving multi-path admission.
    explicit ReadLeases(const QVector<ReadPath>& paths, QString* error = nullptr, int timeoutMs = 10000) {
        acquire(paths, error, timeoutMs);
    }
    bool acquired() const { return m_acquired; }
    void release() { m_leases.clear(); m_acquired = false; }
private:
    void acquire(const QVector<ReadPath>& paths, QString* error, int timeoutMs) {
        QElapsedTimer timer; timer.start();
        for(const auto& path : paths) {
            std::unique_ptr<ReadLease> lease(new ReadLease(path, error,
                int(qMax<qint64>(0, timeoutMs - timer.elapsed()))));
            if(!lease->acquired()) { m_leases.clear(); return; }
            m_leases.push_back(std::move(lease));
        }
        m_acquired = true;
    }
    bool m_acquired{false};
    std::vector<std::unique_ptr<ReadLease>> m_leases;
};
// QtConcurrent transports this explicit error to the gallery result handler;
// returning a null image would incorrectly make the failure a cached preview.
class ReadFailure final : public QException {
public:
    explicit ReadFailure(const QString& reason) : error(reason) {}
    void raise() const override { throw *this; }
    ReadFailure* clone() const override { return new ReadFailure(*this); }
    QString error;
};
class PublicationLease {
public:
    explicit PublicationLease(const QString& path, QString* error = nullptr, int timeoutMs = 5000)
    {
        const QString key = pathKey(path);
        auto& r = registry(); QMutexLocker lock(&r.mutex);
        m_state = state(key);
        const auto thread = QThread::currentThreadId();
        if(m_state->writer == thread || m_state->readerThreads.value(thread)) {
            if(error) *error = QStringLiteral("reader-drain: recursive publication/read upgrade refused: %1").arg(path);
            m_state.reset(); return;
        }
        ++m_state->waitingWriters;
        QElapsedTimer timer; timer.start();
        while(m_state->writer || m_state->readers) {
            const qint64 remaining = timeoutMs - timer.elapsed();
            if(remaining <= 0) {
                if(error) *error = QStringLiteral("reader-drain timeout after %1 ms: %2 (active readers %3)")
                    .arg(timer.elapsed()).arg(path).arg(m_state->readers);
                --m_state->waitingWriters; m_state->changed.wakeAll(); m_state.reset(); return;
            }
            m_state->changed.wait(&r.mutex, static_cast<unsigned long>(remaining));
        }
        --m_state->waitingWriters; m_state->writer = thread;
    }
    ~PublicationLease() {
        if(!m_state) return;
        auto& r = registry(); QMutexLocker lock(&r.mutex);
        ++m_state->generation; m_state->writer = nullptr; m_state->changed.wakeAll();
    }
    bool acquired() const { return bool(m_state); }
    PublicationLease(const PublicationLease&) = delete;
    PublicationLease& operator=(const PublicationLease&) = delete;
private:
    std::shared_ptr<State> m_state;
};
inline QString fingerprint(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
inline bool matches(const QString& path, const QByteArray& expected, bool existed, QString* error)
{
    const QFileInfo info(path);
    if(!existed && !info.exists()) return true;
    if(!info.isFile()) {
        if(error) *error = QStringLiteral("missing: %1").arg(path);
        return false;
    }
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) {
        if(error) *error = QStringLiteral("open: %1; Qt file error %2: %3")
            .arg(path).arg(int(file.error())).arg(file.errorString());
        return false;
    }
    const QByteArray current = file.readAll();
    if(file.error() != QFileDevice::NoError || current.size() != file.size()) {
        if(error) *error = QStringLiteral("read: %1; Qt file error %2: %3")
            .arg(path).arg(int(file.error())).arg(file.errorString());
        return false;
    }
    file.close();
    if(existed && current == expected) return true;
    if(error) *error = QStringLiteral("mismatch: %1; expected %2 bytes SHA256 %3; actual %4 bytes SHA256 %5")
        .arg(path).arg(expected.size()).arg(fingerprint(expected)).arg(current.size()).arg(fingerprint(current));
    return false;
}
// This is a separate native probe, not a late GetLastError attributed to Qt.
// Retry only when DELETE access is currently denied by sharing/locking (32/33).
inline unsigned long sharingProbe(const QString& path)
{
#ifdef Q_OS_WIN
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
        DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); return 0; }
    return GetLastError();
#else
    Q_UNUSED(path); return 0;
#endif
}
// Caller owns an exclusive lease and the backup/recovery transaction. Each retry
// uses fresh QSaveFile staging and checks original bytes again; never delete+rename.
inline bool commitWithRetry(QSaveFile& first, const QString& path, const QByteArray& after,
                            const std::function<bool(QString*)>& beforeCheck,
                            QString* error, int* attempts = nullptr)
{
    QSaveFile* output = &first;
    std::unique_ptr<QSaveFile> retry;
    for(int attempt = 1; attempt <= 3; ++attempt) {
        if(attempts) *attempts = attempt;
        if(!beforeCheck(error)) { output->cancelWriting(); return false; }
        if(output->commit()) { if(error) error->clear(); return true; }
        const auto code = output->error();
        const QString reason = output->errorString();
        const unsigned long native = sharingProbe(path);
        if(error) *error = QStringLiteral("Atomic replacement failed: %1\ncommit attempt %2; Qt file error %3: %4; DELETE-sharing probe %5")
            .arg(path).arg(attempt).arg(int(code)).arg(reason).arg(native);
        if(attempt == 3 || (native != 32 && native != 33)) return false;
        QThread::msleep(50);
        if(!beforeCheck(error)) return false;
        retry.reset(new QSaveFile(path)); output = retry.get();
        output->setDirectWriteFallback(false);
        if(!output->open(QIODevice::WriteOnly) || output->write(after) != after.size() || !output->flush()) {
            if(error) *error = QStringLiteral("retry staging: %1; Qt file error %2: %3")
                .arg(path).arg(int(output->error())).arg(output->errorString());
            output->cancelWriting(); return false;
        }
    }
    return false;
}
} // namespace resIo
