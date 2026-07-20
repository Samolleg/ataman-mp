#ifndef RESOURCE_PROFILE_H
#define RESOURCE_PROFILE_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>

enum class EProfileCapabilityStatus
{
    Unsupported,
    Experimental,
    Available,
    Validated
};

struct SProfileCapability
{
    QString id;
    EProfileCapabilityStatus status{EProfileCapabilityStatus::Unsupported};
    QString reason;
    QString runtimeManifestId;
};

struct SRuntimeComponent
{
    QString id;
    QString path;
    bool required{true};
    QStringList acceptedSha256;
};

struct SRuntimeComponentEvaluation
{
    QString id;
    QString path;
    bool exists{false};
    bool hashAccepted{false};
    qint64 size{0};
    QString sha256;
};

struct SProfileProviderBinding
{
    SProfileProviderBinding() = default;
    SProfileProviderBinding(const QString& providerId, int version)
        : id(providerId), interfaceVersion(version) {}
    QString id;
    int interfaceVersion{1};
};

struct SResourceProfile
{
    int schemaVersion{1};
    QString id;
    QString displayName;
    QString source;
    QStringList fixedModRoots;
    QString searchParent;
    QStringList directoryNameContains;
    QStringList requiredFiles;
    QStringList requiredDirectories;
    QStringList signatureFiles;
    QStringList resourceDirectories;
    QStringList mapDirectories;
    QString runtimeManifestId;
    QVector<SRuntimeComponent> runtimeComponents;
    QMap<QString, SProfileCapability> capabilities;
    QMap<QString, SProfileProviderBinding> providers;
};

struct SResourceProfileEvaluation
{
    bool valid{false};
    QString gameRoot;
    QString modRoot;
    QString profileId;
    QString profileName;
    QStringList resourceDirectories;
    QStringList mapDirectories;
    QStringList messages;
    int archiveCount{0};
    QString runtimeManifestId;
    bool runtimeStackComplete{false};
    bool runtimeStackValidated{false};
    QVector<SRuntimeComponentEvaluation> runtimeComponents;
    QMap<QString, SProfileCapability> capabilities;
    QMap<QString, SProfileProviderBinding> providers;
};

QVector<SResourceProfile> builtInResourceProfiles();
QVector<SResourceProfile> loadResourceProfiles(const QString& externalDirectory = QString());
SResourceProfileEvaluation evaluateResourceProfile(const QString& gameRoot,
                                                   const SResourceProfile& profile);
QStringList discoverGameInstallations();
QString profileCapabilityStatusName(EProfileCapabilityStatus status);

#endif // RESOURCE_PROFILE_H
