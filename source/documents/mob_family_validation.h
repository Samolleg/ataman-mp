#ifndef MOB_FAMILY_VALIDATION_H
#define MOB_FAMILY_VALIDATION_H

#include <QByteArray>
#include <QMap>
#include <QSharedPointer>
#include <QSet>
#include <QStringList>
#include <QVector>

#include "endurance_document.h"

namespace ei { struct SProfileDatabaseSnapshot; }

enum class EMobFamilyFindingSeverity
{
    Info,
    Warning,
    Critical,
    Incomplete
};

struct SMobFamilyMember
{
    QString path;
    QString fileName;
    QString logicalName;
    QString sourceRole;
    QString inclusionReason;
    QString requestedBy;
    QStringList shadowedPaths;
    bool missing = false;
    bool inMemory = false;
    QByteArray sourceSha256;
    QString error;
    QVector<SMobObjectSnapshot> objects;
    QVector<SMobIdRangeSnapshot> ranges;
    QString script;
    bool primaryMob = false;
    bool activeRuntimeFamily = true;
    bool hasDiplomacyTable = false;
    int magicTrapCount = 0;
    QStringList magicTrapSectionPaths;
    QVector<SMobObjectSnapshot> magicTraps;
};

struct SMobNidOwner
{
    quint32 nid = 0;
    QString mobPath;
    QString mobName;
    QString sectionPath;
    QString objectName;
    QString prototype;
    quint32 objectType = 0;
    bool hasObjectType = false;
    bool useInScript = false;
    bool activeRuntimeFamily = true;
    bool hasParentId = false;
    quint32 parentId = 0;
};

struct SMobFamilyFinding
{
    EMobFamilyFindingSeverity severity = EMobFamilyFindingSeverity::Info;
    QString code;
    QString category;
    QString message;
    QStringList locations;
    quint32 nid = 0;
    bool hasNid = false;
};

struct SMobFamilyValidationRequest
{
    QString familyLabel;
    QString profileId;
    QStringList seedMobPaths;
    QString mappedPrimaryMobPath;
    QString preferredMobPath;
    QStringList workspaceRoots;
    bool explicitComposition = false;
    bool includeProfileWideMobs = true;
    QStringList mapRoots;
    QMap<QString, QByteArray> inMemoryMobData;
    QSharedPointer<const ei::SProfileDatabaseSnapshot> database;
    QMap<QString, QStringList> assemblyPartsByModel;
    QStringList assemblyResourceSources;
    QString assemblyResourceFingerprint;
};

class CMobFamilyValidationReport
{
public:
    QString familyLabel;
    QString profileId;
    QVector<SMobFamilyMember> members;
    QVector<SMobFamilyFinding> findings;
    QMap<quint32, QVector<SMobNidOwner>> nidOwners;

    int criticalCount() const;
    int warningCount() const;
    int incompleteCount() const;
    bool hasActionableFindings() const;
    QString summary() const;
    QString toText(int findingLimit = -1) const;
    QByteArray findingDigest() const;
    QString describeProposedNid(quint32 nid, const QString& owningMobPath) const;
    bool suggestReplacementNid(const QString& owningMobPath, quint32* nid,
                               bool* sharedRange = nullptr,
                               const QSet<quint32>& additionallyOccupied = QSet<quint32>()) const;
};

class CMobFamilyValidator
{
public:
    static CMobFamilyValidationReport validate(const SMobFamilyValidationRequest& request);
};

#endif // MOB_FAMILY_VALIDATION_H
