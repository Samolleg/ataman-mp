#ifndef ENDURANCE_DOCUMENT_H
#define ENDURANCE_DOCUMENT_H

#include <QByteArray>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

enum class EValidationSeverity
{
    Info,
    Warning,
    Critical,
    Error
};

struct SValidationFinding
{
    QString code;
    EValidationSeverity severity;
    QString message;
    QString location;

    QJsonObject toJson() const;
};

class CValidationReport
{
public:
    void add(const QString& code, EValidationSeverity severity, const QString& message, const QString& location = QString());
    const QVector<SValidationFinding>& findings() const { return m_findings; }
    bool hasErrors() const;
    QJsonArray toJson() const;
    QString toText() const;

private:
    QVector<SValidationFinding> m_findings;
};

struct SMobLeafEdit
{
    SMobLeafEdit() = default;
    SMobLeafEdit(const QString& path, const QByteArray& expectedSha256,
                 const QByteArray& payload, quint32 type = 0,
                 const QByteArray& originalPayload = QByteArray())
        : sectionPath(path), expectedSectionSha256(expectedSha256),
          replacementPayload(payload), sectionType(type),
          sourcePayload(originalPayload) {}

    QString sectionPath;
    QByteArray expectedSectionSha256;
    QByteArray replacementPayload;
    quint32 sectionType = 0;
    QByteArray sourcePayload;
};

class CMobEditTransaction
{
public:
    explicit CMobEditTransaction(const QString& description = QString())
        : m_description(description) {}

    void replaceLeafPayload(const QString& sectionPath,
                            const QByteArray& expectedSectionSha256,
                            const QByteArray& replacementPayload,
                            quint32 sectionType = 0,
                            const QByteArray& sourcePayload = QByteArray());
    const QString& description() const { return m_description; }
    const QVector<SMobLeafEdit>& edits() const { return m_edits; }
    bool isEmpty() const { return m_edits.isEmpty(); }
    bool validatedChangedSaveClass(QString* className = nullptr) const;

private:
    QString m_description;
    QVector<SMobLeafEdit> m_edits;
};

struct SMobSection
{
    quint32 type = 0;
    quint32 declaredLength = 0;
    quint64 offset = 0;
    int occurrence = 0;
    QString path;
    QByteArray rawBytes;
    QByteArray sha256;
    bool container = false;
    bool opaque = true;
    QVector<SMobSection> children;
};

struct SMobObjectSnapshot
{
    QString sectionPath;
    quint32 sectionType = 0;
    int occurrence = 0;
    bool hasNid = false;
    quint32 nid = 0;
    bool hasObjectType = false;
    quint32 objectType = 0;
    bool hasParentId = false;
    quint32 parentId = 0;
    bool useInScript = false;
    QString name;
    QString prototype;
    bool hasPosition = false;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    QByteArray subtreeSha256;

    QJsonObject toJson() const;
};

struct SMobIdRangeSnapshot
{
    QString sectionPath;
    bool mainRange = false;
    bool effective = false;
    quint32 minimum = 0;
    quint32 maximum = 0;
    int occurrence = 0;
};

enum class EMobDiffKind
{
    Unchanged,
    Added,
    Removed,
    Modified,
    Moved,
    Ambiguous
};

struct SMobDiffEntry
{
    SMobDiffEntry() = default;
    SMobDiffEntry(EMobDiffKind entryKind, int before, int after,
                  const QString& method, double matchConfidence)
        : kind(entryKind), baselineIndex(before), candidateIndex(after),
          matchMethod(method), confidence(matchConfidence) {}

    EMobDiffKind kind = EMobDiffKind::Unchanged;
    int baselineIndex = -1;
    int candidateIndex = -1;
    QString matchMethod;
    double confidence = 0.0;

    QJsonObject toJson(const QVector<SMobObjectSnapshot>& baseline,
                       const QVector<SMobObjectSnapshot>& candidate) const;
};

struct SMobDocumentDiffEntry
{
    QString kind;
    QString category;
    QString sectionPath;
    quint32 sectionType = 0;
    QByteArray baselineSha256;
    QByteArray candidateSha256;
    QString details;

    QJsonObject toJson() const;
};

class CMobSemanticDiff
{
public:
    static CMobSemanticDiff compare(const class CMobDocument& baseline,
                                    const class CMobDocument& candidate);

    const QVector<SMobDiffEntry>& entries() const { return m_entries; }
    const QVector<SMobDocumentDiffEntry>& documentEntries() const { return m_documentEntries; }
    QJsonObject toJson() const;
    QString toText() const;

private:
    QVector<SMobObjectSnapshot> m_baseline;
    QVector<SMobObjectSnapshot> m_candidate;
    QVector<SMobDiffEntry> m_entries;
    QVector<SMobDocumentDiffEntry> m_documentEntries;
};

class CMobDocument
{
public:
    struct SLeverConversionValues
    {
        quint32 openType = 1;
        quint32 keyId = 0;
        quint32 handsSkill = 0;
        quint8 currentState = 0;
        quint8 totalStates = 2;
        bool cycled = false;
        bool door = false;
        bool recalculateGraph = false;
    };

    bool load(const QFileInfo& file, CValidationReport* report = nullptr);
    bool loadBytes(const QByteArray& bytes, const QString& sourceName,
                   CValidationReport* report = nullptr);
    QByteArray serialize() const { return m_originalBytes; }
    const QByteArray& originalBytes() const { return m_originalBytes; }
    const QVector<SMobSection>& sections() const { return m_sections; }
    bool hasEmbeddedAiGraph() const;
    bool removeEmbeddedAiGraph(QByteArray* output,
                               CValidationReport* report = nullptr) const;
    const QVector<SMobObjectSnapshot>& objects() const { return m_objects; }
    const QVector<SMobIdRangeSnapshot>& idRanges() const { return m_idRanges; }
    bool isQuestMob() const { return m_questMob; }
    const QVector<quint32>& duplicateIds() const { return m_duplicateIds; }
    bool applyTransaction(const CMobEditTransaction& transaction,
                          QByteArray* output, CValidationReport* report = nullptr) const;
    bool replaceEmbeddedScript(const QString& script, QByteArray* output,
                               CValidationReport* report = nullptr) const;
    bool embeddedScript(QString* script,
                        CValidationReport* report = nullptr) const;
    bool duplicateObjectSubtree(const QString& objectPath,
                                const QByteArray& expectedSubtreeSha256,
                                QByteArray* output,
                                CValidationReport* report = nullptr) const;
    bool removeObjectSubtree(const QString& objectPath,
                             const QByteArray& expectedSubtreeSha256,
                             QByteArray* output,
                             CValidationReport* report = nullptr) const;
    bool rebuildObjectSequence(const QStringList& objectPaths,
                               const QVector<QByteArray>& expectedSubtreeSha256,
                               QByteArray* output,
                               CValidationReport* report = nullptr) const;
    bool rebuildObjectSequenceFromRaw(const QVector<QByteArray>& objectSubtrees,
                                      const QVector<QByteArray>& expectedSubtreeSha256,
                                      QByteArray* output,
                                      CValidationReport* report = nullptr) const;
    bool objectSubtreeBytes(const QString& objectPath,
                            const QByteArray& expectedSubtreeSha256,
                            QByteArray* output) const;
    static bool convertWorldObjectSubtreeToLever(
        const QByteArray& worldObjectSubtree,
        const SLeverConversionValues& values,
        QByteArray* leverSubtree,
        CValidationReport* report = nullptr);
    bool reconcileLogicPointStructure(const CMobDocument& cleanProjection,
                                      const CMobDocument& editedProjection,
                                      QByteArray* output,
                                      CValidationReport* report = nullptr) const;
    bool deriveLeafTransaction(const CMobDocument& cleanProjection,
                               const CMobDocument& editedProjection,
                               CMobEditTransaction* transaction,
                               CValidationReport* report = nullptr) const;
    static bool buildProjectionTransactionCandidate(
        const QByteArray& sourceBytes, const QString& sourceName,
        const QByteArray& cleanProjectionBytes,
        const QByteArray& editedProjectionBytes,
        CMobEditTransaction* transaction, QByteArray* candidate,
        CValidationReport* report = nullptr);
    static bool encodeCp1251Strict(const QString& value, QByteArray* output,
                                   QString* error = nullptr);
    bool isDirty() const { return false; }

private:
    QByteArray m_originalBytes;
    QVector<SMobSection> m_sections;
    QVector<SMobObjectSnapshot> m_objects;
    QVector<SMobIdRangeSnapshot> m_idRanges;
    QVector<quint32> m_duplicateIds;
    bool m_questMob = false;
};

#include "mpr_document.h"

#endif
