#ifndef DATABASE_VALUE_CODEC_H
#define DATABASE_VALUE_CODEC_H

#include "database_schema.h"
#include <QVariant>

namespace ei {

class DatabaseValueCodec
{
public:
    static QVariant decode(const QByteArray& payload,
                           const SDatabaseStorageField& field,
                           const DatabaseSchema& schema,
                           const QString& family,
                           QString* diagnostic = nullptr);
    static QByteArray encode(const QVariant& value, const QByteArray& sourcePayload,
                             const SDatabaseStorageField& field,
                             const DatabaseSchema& schema,
                             const QString& family,
                             QString* diagnostic = nullptr);
    static bool editable(const SDatabaseStorageField& field);
};

}
#endif
