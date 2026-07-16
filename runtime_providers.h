#ifndef RUNTIME_PROVIDERS_H
#define RUNTIME_PROVIDERS_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct SRuntimeProviderResult
{
    bool success{false};
    QString code;
    QString message;
    QJsonObject evidence;
};

struct SCharacterAppearanceRequest
{
    QString profileId;
    QString runtimeManifestId;
    QString prototype;
    QString model;
    QString primaryTexture;
    QString secondaryTexture;
};

class ICharacterAppearanceResolver
{
public:
    virtual ~ICharacterAppearanceResolver() = default;
    virtual QString providerId() const = 0;
    virtual SRuntimeProviderResult resolve(const SCharacterAppearanceRequest& request,
                                           QJsonObject* appearance) const = 0;
};

struct SEquipmentLayoutRequest
{
    QString profileId;
    QString runtimeManifestId;
    QString prototype;
    QStringList weapons;
    QStringList armor;
    QStringList perks;
};

class IEquipmentLayoutResolver
{
public:
    virtual ~IEquipmentLayoutResolver() = default;
    virtual QString providerId() const = 0;
    virtual SRuntimeProviderResult resolve(const SEquipmentLayoutRequest& request,
                                           QJsonObject* equipmentLayout) const = 0;
};

class IPrototypeResolver
{
public:
    virtual ~IPrototypeResolver() = default;
    virtual QString providerId() const = 0;
    virtual SRuntimeProviderResult resolve(const QString& profileId,
                                           const QString& runtimeManifestId,
                                           const QString& prototype,
                                           QJsonObject* resolvedPrototype) const = 0;
};

class IRuntimeSemanticValidator
{
public:
    virtual ~IRuntimeSemanticValidator() = default;
    virtual QString providerId() const = 0;
    virtual SRuntimeProviderResult validate(const QString& profileId,
                                            const QString& runtimeManifestId,
                                            const QJsonObject& documentManifest,
                                            QJsonObject* validationReport) const = 0;
};

struct SMapTestLaunchRequest
{
    int protocolVersion{1};
    QString runId;
    QString profileId;
    QString runtimeManifestId;
    QJsonObject runtimeComponentHashes;
    QString stagedMprPath;
    QString internalMapName;
    QStringList orderedStagedMobPaths;
    QString activeMobPath;
    QJsonObject sourceSha256;
    QJsonObject stagedSha256;
    QJsonObject sessionSettings;
    QString logPath;
    QString resultPath;
};

class IMapTestLauncher
{
public:
    virtual ~IMapTestLauncher() = default;
    virtual QString providerId() const = 0;
    virtual SRuntimeProviderResult preflight(const SMapTestLaunchRequest& request) const = 0;
    virtual SRuntimeProviderResult launch(const SMapTestLaunchRequest& request) = 0;
    virtual SRuntimeProviderResult stop(const QString& runId) = 0;
};

QJsonObject mapTestLaunchRequestToJson(const SMapTestLaunchRequest& request);
bool validateMapTestLaunchRequest(const SMapTestLaunchRequest& request,
                                  const QString& runRoot, QStringList* errors);

#endif // RUNTIME_PROVIDERS_H
