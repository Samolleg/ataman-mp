#ifndef RES_ARCHIVE_INDEX_H
#define RES_ARCHIVE_INDEX_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

struct SResIndexedMember
{
    int tableIndex{-1};
    quint32 nextIndex{quint32(-1)};
    quint32 dataSize{0};
    quint32 dataOffset{0};
    quint32 lastWriteTime{0};
    quint16 nameLength{0};
    quint32 nameOffset{0};
    QByteArray encodedName;
    QString name;
};

// File-backed RES reader. Opening reads only the header, hash table, and name
// buffer. Payload reads and hashes are explicit and bounded by the indexed
// offsets captured at open time.
class CResArchiveIndex
{
public:
    bool open(const QString& path, QString* error = nullptr);
    bool isOpen() const { return !m_path.isEmpty(); }
    QString path() const { return m_path; }
    qint64 sourceSize() const { return m_size; }
    QDateTime sourceModified() const { return m_modified; }
    QByteArray sourceHeaderSha256() const { return m_headerSha256; }
    int memberCount() const { return m_members.size(); }
    const SResIndexedMember& member(int index) const { return m_members.at(index); }
    const QVector<SResIndexedMember>& members() const { return m_members; }
    int memberIndex(const QString& name) const;
    qint64 retainedPayloadBytes() const { return 0; }

    bool sourceIsCurrent(QString* error = nullptr) const;
    QByteArray readMember(int index, QString* error = nullptr) const;
    QByteArray readMember(const QString& name, QString* error = nullptr) const;
    QByteArray readMemberPrefix(int index, qint64 maximumBytes,
                                QString* error = nullptr) const;
    QByteArray memberSha256(int index, QString* error = nullptr) const;
    QByteArray memberSha256(const QString& name, QString* error = nullptr) const;

private:
    QByteArray readRange(quint32 offset, quint32 size, qint64 maximumBytes,
                         QString* error) const;

    QString m_path;
    quint64 m_generation{0};
    qint64 m_size{-1};
    QDateTime m_modified;
    QByteArray m_headerSha256;
    QVector<SResIndexedMember> m_members;
    QHash<QString, QVector<int>> m_byName;
};

class CResArchiveTransaction
{
public:
    explicit CResArchiveTransaction(const CResArchiveIndex& source);

    bool replace(const QString& name, const QByteArray& expectedSha256,
                 const QByteArray& payload, QString* error = nullptr);
    bool add(const QString& name, const QByteArray& payload, quint32 lastWriteTime,
             QString* error = nullptr);
    bool rename(const QString& name, const QString& newName, QString* error = nullptr);
    bool remove(const QString& name, QString* error = nullptr);
    bool writeCandidate(const QString& destinationPath, QString* error = nullptr) const;

private:
    struct Change
    {
        enum Kind { Replace, Add, Rename, Delete } kind{Replace};
        QString originalName;
        QString finalName;
        QByteArray payload;
        quint32 lastWriteTime{0};
    };

    bool validateName(const QString& name, const QString& ignoredName,
                      QString* error) const;
    QString finalNameFor(const QString& original) const;
    bool isDeleted(const QString& original) const;

    CResArchiveIndex m_source;
    QVector<Change> m_changes;
};

#endif // RES_ARCHIVE_INDEX_H
