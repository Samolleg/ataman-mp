#include "resource_profile.h"
#include "../runtime/runtime_variant_registry.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStorageInfo>

namespace {
QStringList jsonStrings(const QJsonObject& object, const char* key)
{
    QStringList result;
    for(const QJsonValue& value : object.value(key).toArray())
        if(value.isString())
            result.append(value.toString());
    return result;
}

EProfileCapabilityStatus capabilityStatusFromString(const QString& value)
{
    if(value.compare("experimental", Qt::CaseInsensitive) == 0)
        return EProfileCapabilityStatus::Experimental;
    if(value.compare("available", Qt::CaseInsensitive) == 0)
        return EProfileCapabilityStatus::Available;
    if(value.compare("validated", Qt::CaseInsensitive) == 0)
        return EProfileCapabilityStatus::Validated;
    return EProfileCapabilityStatus::Unsupported;
}

SProfileCapability capability(const QString& id, EProfileCapabilityStatus status,
                              const QString& reason, const QString& manifest = QString())
{
    SProfileCapability value;
    value.id = id;
    value.status = status;
    value.reason = reason;
    value.runtimeManifestId = manifest;
    return value;
}

SRuntimeComponent runtimeComponent(const QString& id, const QString& path,
                                   const QStringList& acceptedSha256 = QStringList(),
                                   bool enforceHash = true)
{
    SRuntimeComponent value;
    value.id = id;
    value.path = path;
    value.acceptedSha256 = acceptedSha256;
    value.enforceHash = enforceHash;
    return value;
}

QStringList registeredRuntimeHashes(ei::runtime::ERuntimeProfile profile)
{
    QStringList hashes;
    for(std::size_t index = 0; index < ei::runtime::runtimeVariantCount(); ++index)
    {
        const ei::runtime::SRuntimeVariant& variant = ei::runtime::kRuntimeVariants[index];
        if(variant.profile == profile)
            hashes.append(QString::fromLatin1(variant.sha256));
    }
    return hashes;
}

QString sha256File(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex()).toUpper();
}

bool profileFromJson(const QByteArray& bytes, const QString& source,
                     SResourceProfile* profile, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if(parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if(error)
            *error = parseError.errorString();
        return false;
    }
    const QJsonObject object = document.object();
    const int schemaVersion = object.value("schemaVersion").toInt();
    if(schemaVersion != 1 && schemaVersion != 2)
    {
        if(error)
            *error = "Unsupported or missing schemaVersion";
        return false;
    }

    SResourceProfile value;
    value.schemaVersion = schemaVersion;
    value.id = object.value("id").toString().trimmed();
    value.displayName = object.value("displayName").toString().trimmed();
    value.source = source;
    value.fixedModRoots = jsonStrings(object, "fixedModRoots");
    value.searchParent = object.value("searchParent").toString();
    value.directoryNameContains = jsonStrings(object, "directoryNameContains");
    value.requiredFiles = jsonStrings(object, "requiredFiles");
    value.requiredDirectories = jsonStrings(object, "requiredDirectories");
    value.signatureFiles = jsonStrings(object, "signatureFiles");
    value.resourceDirectories = jsonStrings(object, "resourceDirectories");
    value.mapDirectories = jsonStrings(object, "mapDirectories");
    if(schemaVersion >= 2)
    {
        const QJsonObject runtime = object.value("runtime").toObject();
        value.runtimeManifestId = runtime.value("manifestId").toString().trimmed();
        for(const QJsonValue& componentValue : runtime.value("components").toArray())
        {
            const QJsonObject componentObject = componentValue.toObject();
            SRuntimeComponent component;
            component.id = componentObject.value("id").toString().trimmed();
            component.path = componentObject.value("path").toString().trimmed();
            component.required = !componentObject.contains("required") || componentObject.value("required").toBool();
            component.enforceHash = !componentObject.contains("enforceHash")
                || componentObject.value("enforceHash").toBool();
            component.acceptedSha256 = jsonStrings(componentObject, "acceptedSha256");
            if(!component.id.isEmpty() && !component.path.isEmpty())
                value.runtimeComponents.append(component);
        }
        const QJsonObject capabilities = object.value("capabilities").toObject();
        for(auto it = capabilities.constBegin(); it != capabilities.constEnd(); ++it)
        {
            SProfileCapability profileCapability;
            profileCapability.id = it.key();
            if(it.value().isString())
                profileCapability.status = capabilityStatusFromString(it.value().toString());
            else
            {
                const QJsonObject capabilityObject = it.value().toObject();
                profileCapability.status = capabilityStatusFromString(capabilityObject.value("status").toString());
                profileCapability.reason = capabilityObject.value("reason").toString();
                profileCapability.runtimeManifestId = capabilityObject.value("runtimeManifestId").toString();
            }
            value.capabilities.insert(profileCapability.id, profileCapability);
        }
        const QJsonObject providers = object.value("providers").toObject();
        for(auto it = providers.constBegin(); it != providers.constEnd(); ++it)
        {
            SProfileProviderBinding binding;
            if(it.value().isString())
                binding.id = it.value().toString().trimmed();
            else
            {
                const QJsonObject providerObject = it.value().toObject();
                binding.id = providerObject.value("id").toString().trimmed();
                binding.interfaceVersion = providerObject.value("interfaceVersion").toInt(1);
            }
            if(!binding.id.isEmpty())
                value.providers.insert(it.key(), binding);
        }
    }
    if(value.id.isEmpty() || value.displayName.isEmpty() || value.resourceDirectories.isEmpty())
    {
        if(error)
            *error = "A profile requires id, displayName, and resourceDirectories";
        return false;
    }
    *profile = value;
    return true;
}

QString absoluteFromRoot(const QString& root, const QString& relative)
{
    if(QDir::isAbsolutePath(relative))
        return QDir::cleanPath(relative);
    return QDir::cleanPath(QDir(root).absoluteFilePath(relative));
}

QString expandPath(QString value, const QString& gameRoot, const QString& modRoot)
{
    value.replace("{gameRoot}", gameRoot, Qt::CaseInsensitive);
    value.replace("{modRoot}", modRoot, Qt::CaseInsensitive);
    return QDir::cleanPath(value);
}

bool candidateMeetsRequirements(const QString& candidate, const SResourceProfile& profile)
{
    if(candidate.isEmpty() || !QDir(candidate).exists() ||
       QFileInfo(candidate).fileName().contains("backup", Qt::CaseInsensitive))
        return false;
    for(const QString& file : profile.requiredFiles)
        if(!QFileInfo(absoluteFromRoot(candidate, file)).isFile())
            return false;
    for(const QString& directory : profile.requiredDirectories)
        if(!QFileInfo(absoluteFromRoot(candidate, directory)).isDir())
            return false;
    return true;
}

int candidateScore(const QString& candidate, const SResourceProfile& profile)
{
    int score = 0;
    const QString name = QFileInfo(candidate).fileName();
    for(const QString& text : profile.directoryNameContains)
        if(name.contains(text, Qt::CaseInsensitive))
            score += 10;
    for(const QString& signature : profile.signatureFiles)
        if(QFileInfo(absoluteFromRoot(candidate, signature)).isFile())
            score += 100;
    return score;
}

void appendUnique(QStringList* values, const QString& path)
{
    const QString clean = QDir::cleanPath(path);
    for(const QString& existing : *values)
        if(existing.compare(clean, Qt::CaseInsensitive) == 0)
            return;
    values->append(clean);
}
}

QVector<SResourceProfile> builtInResourceProfiles()
{
    SResourceProfile jmv;
    jmv.schemaVersion = 2;
    jmv.id = "jamais-vu";
    jmv.displayName = "Jamais Vu (JMV)";
    jmv.source = "built-in";
    jmv.fixedModRoots = QStringList() << "Starter/MODS/JamaisVu135";
    jmv.searchParent = "Starter/MODS";
    jmv.directoryNameContains = QStringList() << "JamaisVu";
    jmv.requiredFiles = QStringList() << "main.lua";
    jmv.requiredDirectories = QStringList() << "res" << "Maps";
    jmv.signatureFiles = QStringList() << "res/databaseJmv.res";
    jmv.resourceDirectories = QStringList() << "{modRoot}/res" << "{gameRoot}/Res";
    jmv.mapDirectories = QStringList() << "{modRoot}/Maps" << "{gameRoot}/Maps";
    jmv.runtimeManifestId = "jmv-development-stack-v1";
    for(const QString& role : {QString("database"), QString("characterAppearance"),
                               QString("equipmentLayout"), QString("unitAnimation"),
                               QString("semanticValidation")})
        jmv.providers.insert(role, {"jmv.v1", 1});
    jmv.runtimeComponents
        << runtimeComponent("starter-engine", "{gameRoot}/Starter/Engine/game.exe",
                            registeredRuntimeHashes(ei::runtime::ERuntimeProfile::Jmv));
    jmv.capabilities.insert("mob.losslessEditing", capability("mob.losslessEditing", EProfileCapabilityStatus::Unsupported,
        "Copy-only hardening is active until UI edits are migrated to provenance transactions."));
    jmv.capabilities.insert("mob.perspectiveDiff", capability("mob.perspectiveDiff", EProfileCapabilityStatus::Experimental,
        "Structural diff is available headlessly; perspective overlay is under development."));
    jmv.capabilities.insert("runtime.directMapLaunch", capability("runtime.directMapLaunch", EProfileCapabilityStatus::Experimental,
        "A fingerprinted temporary JMV bridge is implemented after the public console route was rejected; runtime validation is pending.", jmv.runtimeManifestId));
    jmv.capabilities.insert("runtime.multiMobLaunch", capability("runtime.multiMobLaunch", EProfileCapabilityStatus::Unsupported,
        "JMV supplemental MOB discovery order has not been proven.", jmv.runtimeManifestId));
    jmv.capabilities.insert("runtime.appearanceResolver", capability("runtime.appearanceResolver", EProfileCapabilityStatus::Available,
        "Profile-database and strict embedded-MOB appearance precedence is implemented.", jmv.runtimeManifestId));
    jmv.capabilities.insert("runtime.equipmentResolver", capability("runtime.equipmentResolver", EProfileCapabilityStatus::Available,
        "Ordinary, 1H+shield, dual-1H, and single-2H layouts are available; dual-2H and 2H+shield remain diagnostic-only.", jmv.runtimeManifestId));
    jmv.capabilities.insert("runtime.semanticValidation", capability("runtime.semanticValidation", EProfileCapabilityStatus::Experimental,
        "Runtime validation requires manual JMV game tests.", jmv.runtimeManifestId));

    SResourceProfile base;
    base.schemaVersion = 2;
    base.id = "base-game";
    base.displayName = "Base game only";
    base.source = "built-in";
    base.resourceDirectories = QStringList() << "{gameRoot}/Res";
    base.mapDirectories = QStringList() << "{gameRoot}/Maps";
    base.runtimeManifestId = "base-ei-v1";
    for(const QString& role : {QString("database"), QString("characterAppearance"),
                               QString("equipmentLayout"), QString("unitAnimation"),
                               QString("semanticValidation")})
        base.providers.insert(role, {"base.standard.v1", 1});
    base.runtimeComponents << runtimeComponent(
        "base-game", "{gameRoot}/game.exe",
        registeredRuntimeHashes(ei::runtime::ERuntimeProfile::Base));
    base.capabilities.insert("mob.losslessEditing", capability("mob.losslessEditing", EProfileCapabilityStatus::Unsupported,
        "Copy-only hardening is active until UI edits are migrated to provenance transactions."));
    base.capabilities.insert("mob.perspectiveDiff", capability("mob.perspectiveDiff", EProfileCapabilityStatus::Experimental,
        "Structural diff is available headlessly; perspective overlay is under development."));
    base.capabilities.insert("runtime.directMapLaunch", capability("runtime.directMapLaunch", EProfileCapabilityStatus::Experimental,
        "Exact-hash Base EI bridge injection and staged-map redirection are an experimental feasibility probe.", base.runtimeManifestId));
    base.capabilities.insert("runtime.multiMobLaunch", capability("runtime.multiMobLaunch", EProfileCapabilityStatus::Unsupported,
        "Direct multi-MOB launch is not in Base EI scope."));
    base.capabilities.insert("runtime.appearanceResolver", capability("runtime.appearanceResolver", EProfileCapabilityStatus::Available,
        "Profile-database and strict embedded-MOB appearance precedence is implemented."));
    base.capabilities.insert("runtime.equipmentResolver", capability("runtime.equipmentResolver", EProfileCapabilityStatus::Available,
        "Ordinary armor and the Base single 1H/ranged weapon rule are implemented."));
    base.capabilities.insert("runtime.semanticValidation", capability("runtime.semanticValidation", EProfileCapabilityStatus::Experimental,
        "Runtime validation requires manual Base EI game tests."));

    return {jmv, base};
}

QVector<SResourceProfile> loadResourceProfiles(const QString& externalDirectory)
{
    QVector<SResourceProfile> profiles = builtInResourceProfiles();
    const QString directoryPath = externalDirectory.isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()).filePath("resource-profiles")
        : externalDirectory;
    const QDir directory(directoryPath);
    for(const QFileInfo& file : directory.entryInfoList({"*.json"}, QDir::Files, QDir::Name))
    {
        QFile input(file.absoluteFilePath());
        if(!input.open(QIODevice::ReadOnly))
            continue;
        SResourceProfile profile;
        if(!profileFromJson(input.readAll(), file.absoluteFilePath(), &profile, nullptr))
            continue;
        bool replaced = false;
        for(SResourceProfile& existing : profiles)
        {
            if(existing.id.compare(profile.id, Qt::CaseInsensitive) == 0)
            {
                existing = profile;
                replaced = true;
                break;
            }
        }
        if(!replaced)
            profiles.append(profile);
    }
    return profiles;
}

SResourceProfileEvaluation evaluateResourceProfile(const QString& gameRoot,
                                                   const SResourceProfile& profile)
{
    SResourceProfileEvaluation result;
    result.gameRoot = QDir::cleanPath(QFileInfo(gameRoot).absoluteFilePath());
    result.profileId = profile.id;
    result.profileName = profile.displayName;
    result.runtimeManifestId = profile.runtimeManifestId;
    result.capabilities = profile.capabilities;
    result.providers = profile.providers;
    if(!QFileInfo(QDir(result.gameRoot).filePath("game.exe")).isFile())
    {
        result.messages.append("The selected folder does not contain game.exe.");
        return result;
    }

    const bool needsModRoot = !profile.fixedModRoots.isEmpty() || !profile.searchParent.isEmpty();
    if(needsModRoot)
    {
        QStringList candidates;
        for(const QString& relative : profile.fixedModRoots)
            appendUnique(&candidates, absoluteFromRoot(result.gameRoot, relative));
        const QDir parent(absoluteFromRoot(result.gameRoot, profile.searchParent));
        for(const QFileInfo& directory : parent.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            appendUnique(&candidates, directory.absoluteFilePath());

        int bestScore = -1;
        for(const QString& candidate : candidates)
        {
            if(!candidateMeetsRequirements(candidate, profile))
                continue;
            const int score = candidateScore(candidate, profile);
            if(score > bestScore)
            {
                result.modRoot = candidate;
                bestScore = score;
            }
        }
        if(result.modRoot.isEmpty())
        {
            result.messages.append(QString("%1 was not found below the selected game installation.")
                                   .arg(profile.displayName));
            return result;
        }
    }

    for(const QString& path : profile.resourceDirectories)
    {
        const QString expanded = expandPath(path, result.gameRoot, result.modRoot);
        const QDir directory(expanded);
        if(!directory.exists())
        {
            result.messages.append("Missing resource directory: " + expanded);
            continue;
        }
        const int count = directory.entryList({"*.res"}, QDir::Files, QDir::Name | QDir::IgnoreCase).size();
        if(count == 0)
        {
            result.messages.append("No RES archives found in: " + expanded);
            continue;
        }
        result.resourceDirectories.append(expanded);
        result.archiveCount += count;
    }
    for(const QString& path : profile.mapDirectories)
    {
        const QString expanded = expandPath(path, result.gameRoot, result.modRoot);
        if(QDir(expanded).exists())
            result.mapDirectories.append(expanded);
        else
            result.messages.append("Optional map directory not found: " + expanded);
    }
    bool runtimeComplete = true;
    bool runtimeValidated = !profile.runtimeComponents.isEmpty();
    for(const SRuntimeComponent& component : profile.runtimeComponents)
    {
        SRuntimeComponentEvaluation evaluation;
        evaluation.id = component.id;
        evaluation.path = expandPath(component.path, result.gameRoot, result.modRoot);
        evaluation.enforceHash = component.enforceHash;
        const QFileInfo componentInfo(evaluation.path);
        evaluation.exists = componentInfo.isFile();
        if(evaluation.exists)
        {
            evaluation.size = componentInfo.size();
            evaluation.sha256 = sha256File(evaluation.path);
            evaluation.hashAccepted = component.acceptedSha256.isEmpty();
            for(const QString& accepted : component.acceptedSha256)
                if(accepted.compare(evaluation.sha256, Qt::CaseInsensitive) == 0)
                    evaluation.hashAccepted = true;
        }
        if(component.required && !evaluation.exists)
        {
            runtimeComplete = false;
            result.messages.append("Missing runtime component: " + evaluation.path);
        }
        if(component.required && component.enforceHash
           && (!evaluation.exists || !evaluation.hashAccepted || component.acceptedSha256.isEmpty()))
        {
            runtimeValidated = false;
            if(evaluation.exists)
                result.messages.append(QString("Runtime hash mismatch [%1]: %2 (SHA-256 %3)")
                    .arg(component.id, evaluation.path, evaluation.sha256));
        }
        result.runtimeComponents.append(evaluation);
    }
    result.runtimeStackComplete = runtimeComplete;
    result.runtimeStackValidated = runtimeValidated && runtimeComplete;
    result.valid = result.resourceDirectories.size() == profile.resourceDirectories.size() &&
                   result.archiveCount > 0 && result.runtimeStackComplete;
    return result;
}

QStringList discoverGameInstallations()
{
    QStringList result;
    QStringList candidates;
    for(const QStorageInfo& storage : QStorageInfo::mountedVolumes())
    {
        if(!storage.isValid() || !storage.isReady())
            continue;
        const QString root = storage.rootPath();
        candidates << QDir(root).filePath("Evil Islands")
                   << QDir(root).filePath("GOG Games/Evil Islands")
                   << QDir(root).filePath("Games/Evil Islands");
    }
    for(const QString& candidate : candidates)
    {
        if(QFileInfo(QDir(candidate).filePath("game.exe")).isFile())
            appendUnique(&result, QFileInfo(candidate).absoluteFilePath());
    }
    return result;
}

QString profileCapabilityStatusName(EProfileCapabilityStatus status)
{
    switch(status)
    {
    case EProfileCapabilityStatus::Experimental: return "experimental";
    case EProfileCapabilityStatus::Available: return "available";
    case EProfileCapabilityStatus::Validated: return "validated";
    case EProfileCapabilityStatus::Unsupported: return "unsupported";
    }
    return "unsupported";
}
