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

// Lossless reader and append-only member replacement for Evil Islands RES
// archives. Existing bytes are retained verbatim; a changed payload and a
// minimally patched copy of the original table are appended to the archive.
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

    static bool validateReplacement(const CResArchiveDocument& baseline,
                                    const CResArchiveDocument& candidate,
                                    const QString& changedName,
                                    const QByteArray& expectedReplacement,
                                    QStringList* errors = nullptr);

private:
    int uniqueMemberIndex(const QString& name) const;

    QByteArray m_originalBytes;
    QByteArray m_tableBytes;
    QByteArray m_namesBytes;
    QVector<SResArchiveMember> m_members;
    QString m_sourceName;
    quint32 m_tableOffset{0};
};

#endif // RES_ARCHIVE_DOCUMENT_H
