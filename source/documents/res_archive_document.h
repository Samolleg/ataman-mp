#ifndef RES_ARCHIVE_DOCUMENT_H
#define RES_ARCHIVE_DOCUMENT_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

struct SResArchiveMember
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
    QByteArray payload;
    QByteArray sha256;
};

struct SResArchiveNewMember
{
    QString name;
    QByteArray payload;
    quint32 lastWriteTime{0};
};

// Lossless reader and controlled member replacement for Evil Islands RES
// archives. Ordinary replacement retains existing bytes wherever possible;
// publishing rebuilds the archive while proving unrelated members unchanged.
class CResArchiveDocument
{
public:
    bool loadBytes(const QByteArray& bytes, const QString& sourceName = QString(),
                   QStringList* errors = nullptr);

    const QByteArray& originalBytes() const { return m_originalBytes; }
    QByteArray serialize() const { return m_originalBytes; }
    const QVector<SResArchiveMember>& members() const { return m_members; }
    QByteArray memberPayload(const QString& name) const;

    bool replaceMember(const QString& name, const QByteArray& expectedPayloadSha256,
                       const QByteArray& replacementPayload, QByteArray* output,
                       QStringList* errors = nullptr) const;

    // Builds a derived archive containing one additional member. This is for
    // run-local resources only: payloads, names, ordering, and timestamps are
    // preserved, but the hash table and payload layout are rebuilt so the game
    // can resolve the new member through its normal archive lookup.
    bool buildWithAppendedMember(const QString& name, const QByteArray& payload,
                                 quint32 lastWriteTime, QByteArray* output,
                                 QStringList* errors = nullptr) const;
    bool buildWithAppendedMembers(const QVector<SResArchiveNewMember>& appended,
                                  QByteArray* output,
                                  QStringList* errors = nullptr) const;

    // Publishes a coherent set of named members into an existing archive.
    // Byte-identical existing members are reused. A differing existing member
    // is rejected unless the caller explicitly authorizes replacement.
    bool buildWithPublishedMembers(const QVector<SResArchiveNewMember>& published,
                                   bool allowReplaceExisting,
                                   QByteArray* output,
                                   QStringList* errors = nullptr) const;

    static bool validateReplacement(const CResArchiveDocument& baseline,
                                    const CResArchiveDocument& candidate,
                                    const QString& changedName,
                                    const QByteArray& expectedReplacement,
                                    QStringList* errors = nullptr);

private:
    int uniqueMemberIndex(const QString& name) const;
    bool buildWithMembers(const QVector<SResArchiveNewMember>& requestedMembers,
                          bool allowIdenticalExisting,
                          bool allowReplaceExisting,
                          QByteArray* output,
                          QStringList* errors) const;

    QByteArray m_originalBytes;
    QByteArray m_tableBytes;
    QByteArray m_namesBytes;
    QVector<SResArchiveMember> m_members;
    QString m_sourceName;
    quint32 m_tableOffset{0};
};

#endif // RES_ARCHIVE_DOCUMENT_H
