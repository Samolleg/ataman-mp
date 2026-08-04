#ifndef PROFILE_DATABASE_H
#define PROFILE_DATABASE_H

#include <QByteArray>
#include <QMap>
#include <QSharedPointer>
#include <QStringList>
#include <QVector>
#include <QReadWriteLock>

namespace ei {

enum class EDatabaseDiagnosticSeverity { Info, Warning, Error };

enum class EJmvUnitPerkTableStatus
{
    NotApplicable,
    Missing,
    Malformed,
    Valid
};

struct SDatabaseDiagnostic
{
    SDatabaseDiagnostic() = default;
    SDatabaseDiagnostic(EDatabaseDiagnosticSeverity diagnosticSeverity,
                        const QString& diagnosticCode, const QString& diagnosticMessage,
                        const QString& diagnosticSource)
        : severity(diagnosticSeverity), code(diagnosticCode), message(diagnosticMessage),
          sourcePath(diagnosticSource) {}
    EDatabaseDiagnosticSeverity severity{EDatabaseDiagnosticSeverity::Info};
    QString code;
    QString message;
    QString sourcePath;
};

struct SDatabaseProvenance
{
    QString providerId;
    QString profileId;
    QString runtimeManifestId;
    QString sourcePath;
    QString memberName;
    QString sha256;
    bool overlay{false};
};

struct SMonsterDatabaseRecord
{
    QString name;
    QString baseRace;
    int skinIndex{-1};
    int hairIndex{-1};
    float complexionX{1.0f};
    float complexionY{1.0f};
    float complexionZ{1.0f};
    QStringList items;
    QStringList wears;
    QString weapon;
    QString secondWeapon;
};

struct SRaceModelDatabaseRecord
{
    QString name;
    QString model;
    QStringList primaryTextures;
    QStringList secondaryTextures;
};

struct SItemDatabaseRecord
{
    QString name;
    QString category;
    QString type;
    int typeId{-1};
    QString materialType;
    int textureType1{-1};
    int textureType2{-1};
    int wearOrder{-1};
};

struct SMaterialDatabaseRecord
{
    QString name;
    QString type;
    QString code;
    int id{-1};
};

struct SProfileDatabaseSnapshot
{
    QString providerId;
    QString profileId;
    QString runtimeManifestId;
    QString cacheKey;
    QMap<QString, SMonsterDatabaseRecord> monsters;
    QMap<QString, SRaceModelDatabaseRecord> raceModels;
    QMap<QString, SItemDatabaseRecord> items;
    QMap<QString, SMaterialDatabaseRecord> materialRecords;
    QStringList materials;
    QStringList weapons;
    QStringList armor;
    QStringList quickItems;
    QStringList questItems;
    QStringList lootItems;
    QStringList spells;
    QStringList perks;
    QMap<QString, QByteArray> actionTables;
    QMap<QString, SDatabaseProvenance> actionTableProvenance;
    EJmvUnitPerkTableStatus jmvUnitPerkTableStatus{
        EJmvUnitPerkTableStatus::NotApplicable};
    QMap<QString, int> jmvStrongManByPrototype;
    QString jmvUnitPerkSourcePath;
    QString jmvUnitPerkSha256;
    QString jmvUnitPerkDiagnostic;
    QVector<SDatabaseProvenance> provenance;
    QVector<SDatabaseDiagnostic> diagnostics;

    bool usable() const;
};

struct SProfileDatabaseLoadRequest
{
    QString providerId;
    int providerInterfaceVersion{1};
    QString profileId;
    QString runtimeManifestId;
    QStringList resourceDirectories;
};

struct SParsedDatabaseMembers
{
    QMap<QString, SMonsterDatabaseRecord> monsters;
    QMap<QString, SRaceModelDatabaseRecord> raceModels;
    QMap<QString, SItemDatabaseRecord> items;
    QMap<QString, SMaterialDatabaseRecord> materialRecords;
    QMap<QString, QStringList> namesByCategory;
    QVector<SDatabaseDiagnostic> diagnostics;
};

SParsedDatabaseMembers parseProfileDatabaseMembers(const QMap<QString, QByteArray>& members,
                                                    const QString& sourcePath);
bool validateItemReferenceSyntax(const QString& value, QString* diagnostic = nullptr);

class IProfileDatabaseProvider
{
public:
    virtual ~IProfileDatabaseProvider() = default;
    virtual QString providerId() const = 0;
    virtual int interfaceVersion() const { return 1; }
    virtual QSharedPointer<SProfileDatabaseSnapshot> load(
        const SProfileDatabaseLoadRequest& request) const = 0;
};

class ProfileDatabaseService
{
public:
    ProfileDatabaseService();
    bool registerProvider(const QSharedPointer<IProfileDatabaseProvider>& provider,
                          QString* diagnostic = nullptr);
    QSharedPointer<const SProfileDatabaseSnapshot> snapshot(
        const SProfileDatabaseLoadRequest& request);
    void clear();

private:
    QMap<QString, QSharedPointer<IProfileDatabaseProvider>> m_providers;
    QMap<QString, QSharedPointer<const SProfileDatabaseSnapshot>> m_cache;
};

class ProfileDatabaseContext
{
public:
    static ProfileDatabaseContext& instance();
    void activate(const QSharedPointer<const SProfileDatabaseSnapshot>& snapshot,
                  const QMap<QString, QString>& providerBindings = QMap<QString, QString>());
    QSharedPointer<const SProfileDatabaseSnapshot> current() const;
    quint64 generation() const;
    QString providerForRole(const QString& role) const;

private:
    ProfileDatabaseContext() = default;
    mutable QReadWriteLock m_lock;
    QSharedPointer<const SProfileDatabaseSnapshot> m_current;
    quint64 m_generation{0};
    QMap<QString, QString> m_providerBindings;
};

}

#endif
