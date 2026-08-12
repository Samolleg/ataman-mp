#ifndef DATABASE_DOCUMENT_H
#define DATABASE_DOCUMENT_H

#include <QJsonObject>
#include <QMap>
#include <QSharedPointer>
#include <QVariant>
#include <QVector>
#include "profile_database.h"

namespace ei {

enum class EDatabaseFieldType { Text, Integer, Decimal, Reference, TextList };
struct SDatabaseFieldSchema { QString key, label; EDatabaseFieldType type{EDatabaseFieldType::Text}; QString referenceTable; bool writable{true}; bool opaque{false}; };
struct SDatabaseRow { QString stableKey; QMap<QString, QVariant> values; bool added{false}; bool deleted{false}; };
struct SDatabaseTable { QString key, label, sourceMember; QVector<SDatabaseFieldSchema> fields; QVector<SDatabaseRow> rows; };
struct SDatabaseChange {
    SDatabaseChange() = default;
    SDatabaseChange(const QString&t,const QString&r,const QString&f,const QVariant&b,const QVariant&a,const QString&d):tableKey(t),rowKey(r),fieldKey(f),before(b),after(a),description(d){}
    QString tableKey, rowKey, fieldKey; QVariant before, after; QString description;
};
struct SDatabaseValidationFinding {
    enum class Severity { Info, Warning, Error };
    SDatabaseValidationFinding() = default;
    SDatabaseValidationFinding(Severity s,const QString&c,const QString&t,const QString&r,const QString&m,bool v):severity(s),code(c),tableKey(t),rowKey(r),message(m),verified(v){}
    Severity severity{Severity::Info}; QString code, tableKey, rowKey, message; bool verified{true};
};

// Narrow authoring API for the integrated editor and future Resource Browser.
class DatabaseDocument
{
public:
    static QSharedPointer<DatabaseDocument> fromSnapshot(const QSharedPointer<const SProfileDatabaseSnapshot>&, QString* = nullptr);
    QString profileId() const; QString providerId() const; QString sourceArchivePath() const; QString sourceArchiveSha256() const;
    QStringList tableKeys() const; const SDatabaseTable* table(const QString&) const;
    bool isDirty() const; bool canUndo() const; bool canRedo() const; QVector<SDatabaseChange> changes() const;
    bool setValue(const QString&, const QString&, const QString&, const QVariant&, QString* = nullptr);
    bool cloneRow(const QString&, const QString&, const QString&, QString* = nullptr);
    bool deleteRow(const QString&, const QString&, QString* = nullptr);
    bool undo(); bool redo(); QVector<SDatabaseValidationFinding> validate() const;
    QJsonObject pendingChangeManifest() const; bool sourceIsCurrent(QString* = nullptr) const;
private:
    struct SState { QMap<QString, SDatabaseTable> tables; SDatabaseChange change; };
    explicit DatabaseDocument(const QSharedPointer<const SProfileDatabaseSnapshot>&);
    SDatabaseRow* row(const QString&, const QString&); void commit(const QMap<QString, SDatabaseTable>&, const SDatabaseChange&);
    QSharedPointer<const SProfileDatabaseSnapshot> m_snapshot; QMap<QString, SDatabaseTable> m_tables; QVector<SState> m_undo, m_redo;
};
}
#endif
