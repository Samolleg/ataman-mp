#ifndef LOSSLESS_DATABASE_MEMBER_H
#define LOSSLESS_DATABASE_MEMBER_H

#include <QByteArray>
#include <QString>
#include <QVector>

namespace ei {

struct SLosslessDatabaseField
{
    SLosslessDatabaseField() = default;
    SLosslessDatabaseField(int fieldId, int fieldOccurrence,
                           const QByteArray& fieldPayload, const QByteArray& fieldEncoded)
        : id(fieldId), occurrence(fieldOccurrence), payload(fieldPayload), encoded(fieldEncoded) {}
    int id{-1};
    int occurrence{-1};
    QByteArray payload;
    QByteArray encoded;
};

class LosslessDatabaseMember
{
public:
    static LosslessDatabaseMember parse(const QByteArray& bytes,
                                        QString* diagnostic = nullptr);
    bool valid() const;
    QByteArray serialize() const;
    int tableCount() const;
    int recordCount(int tableIndex) const;
    QVector<SLosslessDatabaseField> fields(int tableIndex, int recordIndex) const;
    int findRecordByFieldPayload(int tableIndex, int fieldId,
                                 const QByteArray& payload) const;
    bool replaceFieldPayload(int tableIndex, int recordIndex, int fieldId,
                             int occurrence, const QByteArray& payload,
                             QString* diagnostic = nullptr);
    int findRecordByEncodedFields(int tableIndex,
                                  const QVector<SLosslessDatabaseField>& fields) const;
    bool cloneRecord(int tableIndex, int recordIndex, QString* diagnostic = nullptr);
    bool removeRecord(int tableIndex, int recordIndex, QString* diagnostic = nullptr);
    bool cloneRecordByFieldPayload(int tableIndex, int fieldId, const QByteArray& sourcePayload,
                                   const QByteArray& newPayload, QString* diagnostic = nullptr);
    bool removeRecordByFieldPayload(int tableIndex, int fieldId, const QByteArray& payload,
                                    QString* diagnostic = nullptr);

private:
    struct Node
    {
        int id{-1};
        QByteArray header;
        QByteArray payload;
        QVector<Node> children;
        bool container{false};
        bool dirty{false};
    };
    static bool parseNode(const QByteArray& bytes, int offset, int limit,
                          Node* node, int* next, int depth, bool requireChildren,
                          QString* diagnostic);
    static QByteArray encodeNode(const Node& node);
    static QByteArray encodeHeader(int id, int payloadSize);
    Node* fieldNode(int tableIndex, int recordIndex, int fieldId, int occurrence);
    const Node* recordNode(int tableIndex, int recordIndex) const;

    QByteArray m_original;
    QByteArray m_suffix;
    Node m_root;
    bool m_valid{false};
};

}
#endif
