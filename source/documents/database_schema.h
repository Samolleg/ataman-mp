#ifndef DATABASE_SCHEMA_H
#define DATABASE_SCHEMA_H

#include <QMap>
#include <QString>
#include <QVector>

namespace ei {

enum class EDatabaseStorageType {
    Hex, String, UnsignedLong, SignedLong, Float, Byte,
    UnsignedLongList, FloatList, IndexedFloatList, ByteList,
    StringList, FixedStringList, BitList, TypeList, CapsulatedTypeList,
    AcksUniqueType, FixedString
};

struct SDatabaseStorageField {
    int id{-1};
    EDatabaseStorageType type{EDatabaseStorageType::Hex};
    int modifier{0};
    QByteArray typeData;
};

struct SDatabaseStorageHeader {
    int id{-1};
    int index{0};
    int width{0};
    QString name;
    QString description;
};

struct SDatabaseStorageTable {
    QString family;
    QString memberName;
    int physicalIndex{-1};
    int blockId{-1};
    QString key;
    QString name;
    QString title;
    QVector<SDatabaseStorageField> fields;
    QVector<SDatabaseStorageHeader> headers;
};

class DatabaseSchema
{
public:
    static const DatabaseSchema& instance();
    QVector<SDatabaseStorageTable> tables() const;
    const SDatabaseStorageTable* tableForMember(const QString& memberName,
                                                int physicalIndex) const;
    const SDatabaseStorageField* field(const QString& family, int blockId,
                                       int fieldId) const;
    QVector<SDatabaseStorageHeader> headers(const QString& family, int blockId,
                                            int fieldId) const;
    QString diagnostic() const;

private:
    DatabaseSchema();
    QVector<SDatabaseStorageTable> m_tables;
    QMap<QString, QMap<int, QMap<int, SDatabaseStorageField>>> m_types;
    QMap<QString, QMap<int, QVector<SDatabaseStorageHeader>>> m_headers;
    QString m_diagnostic;
};

}
#endif
