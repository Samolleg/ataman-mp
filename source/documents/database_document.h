#ifndef DATABASE_DOCUMENT_H
#define DATABASE_DOCUMENT_H

#include <QJsonObject>
#include <QMap>
#include <QSharedPointer>
#include <QSet>
#include <QVariant>
#include <QVector>
#include "profile_database.h"

namespace ei {

enum class EDatabaseFieldType { Text, Integer, Decimal, Reference, TextList };
struct SDatabaseFieldSchema { QString key, label; EDatabaseFieldType type{EDatabaseFieldType::Text}; QString referenceTable; bool writable{true}; bool opaque{false}; int storageId{-1}; int storageOccurrence{0}; };
struct SDatabaseRow { QString stableKey, sourceStableKey; int sourceRecordIndex{-1}; QMap<QString, QVariant> values; bool added{false}; bool deleted{false}; };
struct SDatabaseTable { QString key, label, sourceMember, schemaFamily; int physicalTable{-1}; int blockId{-1}; int identityFieldId{-1}; QVector<SDatabaseFieldSchema> fields; QVector<SDatabaseRow> rows; bool structuralEditing{true}; bool uniqueIdentity{false}; };
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
    QString sourceTablesLuaPath() const;
    QStringList tableKeys() const; const SDatabaseTable* table(const QString&) const;
    bool isDirty() const; bool canUndo() const; bool canRedo() const; QVector<SDatabaseChange> changes() const;
    bool hasUnappliedChanges(QString* = nullptr) const;
    bool identityAvailable(const QString& tableKey, const QString& identity,
                           const QString& exceptRowKey = QString()) const;
    bool fieldEditable(const QString& tableKey, const QString& rowKey,
                       const QString& fieldKey, QString* diagnostic = nullptr) const;
    QString displayIdentity(const QString& tableKey, const QString& rowKey) const;
    bool setValue(const QString&, const QString&, const QString&, const QVariant&, QString* = nullptr);
    bool cloneRow(const QString&, const QString&, const QString&, QString* = nullptr);
    bool deleteRow(const QString&, const QString&, QString* = nullptr);
    bool undo(); bool redo(); QVector<SDatabaseValidationFinding> validate() const;
    QJsonObject pendingChangeManifest() const; bool sourceIsCurrent(QString* = nullptr) const;
    QByteArray buildArchive(QString* = nullptr) const;
    QByteArray buildTablesLua(QString* = nullptr) const;
    bool saveArchive(const QString&, QString* = nullptr) const;
    bool saveBundle(const QString& archivePath, const QString& tablesLuaPath,
                    QString* diagnostic = nullptr) const;
    bool applyToSource(QString* = nullptr);
    QSharedPointer<const SProfileDatabaseSnapshot> currentSnapshot(QString* = nullptr) const;
    bool replaceTables(const QMap<QString, SDatabaseTable>&, const QString&, QString* = nullptr);
    bool replaceTablesWithLua(const QMap<QString, SDatabaseTable>&, const QByteArray&,
                              const QString&, QString* = nullptr);
    bool importNativeBundle(const QByteArray& archiveBytes, const QByteArray& tablesLuaBytes,
                            const QString&, QString* = nullptr);
private:
    struct SState { QMap<QString, SDatabaseTable> tables; SDatabaseChange change; };
    explicit DatabaseDocument(const QSharedPointer<const SProfileDatabaseSnapshot>&);
    SDatabaseRow* row(const QString&, const QString&); void commit(const QMap<QString, SDatabaseTable>&, const SDatabaseChange&);
    QMap<QString, SJmvUnitPerkRecord> desiredJmvUnitPerks() const;
    QMap<QString, SJmvUnitStatRecord> desiredJmvUnitStats() const;
    QSharedPointer<const SProfileDatabaseSnapshot> m_snapshot; QMap<QString, SDatabaseTable> m_tables, m_baselineTables; QSet<QString> m_sourceFieldOccurrences; QVector<SState> m_undo, m_redo; QByteArray m_sourceArchiveBaseline, m_tablesLuaBaseline; QString m_expectedSourceSha256, m_expectedTablesLuaSha256; mutable bool m_buildingReplacementCandidate{false};
};
}
#endif
