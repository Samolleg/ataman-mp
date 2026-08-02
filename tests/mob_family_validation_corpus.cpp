#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QTextStream>

#include <algorithm>

#include "mob_family_validation.h"
#include "base_profile_database_provider.h"
#include "jmv_profile_database_provider.h"
#include "assembly_inventory.h"
#include "character_bodypart_runtime.h"

namespace {
int printBodyPartStats(const QVector<SMobFamilyMember>& members, QTextStream& out)
{
    int unreadable = 0;
    int unitCount = 0;
    int imported = 0;
    int missingRecords = 0;
    int emptyMasks = 0;
    QMap<int, int> partCounts;
    QMap<QString, int> tokenCounts;
    QMap<QString, int> modelCounts;
    QMap<QString, int> modelEmptyCounts;
    for(const SMobFamilyMember& member : members)
    {
        if(member.missing || !member.error.isEmpty())
        {
            ++unreadable;
            continue;
        }
        for(const SMobObjectSnapshot& object : member.objects)
        {
            if(!object.unit)
                continue;
            ++unitCount;
            imported += object.hasImportMode && object.importMode ? 1 : 0;
            const QString model = object.model.trimmed().toLower();
            const QString modelKey = model.isEmpty() ? QString("<missing>") : model;
            ++modelCounts[modelKey];
            if(object.bodyPartsSectionPath.isEmpty())
            {
                ++missingRecords;
                continue;
            }
            ++partCounts[object.bodyParts.size()];
            if(object.bodyParts.isEmpty())
                ++modelEmptyCounts[modelKey];
            emptyMasks += object.bodyParts.isEmpty() ? 1 : 0;
            for(const QString& token : object.bodyParts)
                ++tokenCounts[token];
        }
    }
    out << "BODYPART_STATS files=" << members.size()
        << " unreadable=" << unreadable << " units=" << unitCount
        << " imported=" << imported << " missingRecords=" << missingRecords
        << " emptyMasks=" << emptyMasks << "\n";
    for(auto it = partCounts.constBegin(); it != partCounts.constEnd(); ++it)
        out << "BODYPART_COUNT " << it.key() << "=" << it.value() << "\n";
    for(auto it = tokenCounts.constBegin(); it != tokenCounts.constEnd(); ++it)
        out << "BODYPART_TOKEN " << it.key() << "=" << it.value() << "\n";
    for(auto it = modelCounts.constBegin(); it != modelCounts.constEnd(); ++it)
        out << "BODYPART_MODEL " << it.key() << " units=" << it.value()
            << " empty=" << modelEmptyCounts.value(it.key()) << "\n";
    return unreadable;
}

QString itemPrototype(const QString& reference)
{
    return reference.section('[', 0, 0).section('.', 0, 0).trimmed().toLower();
}

bool hasHelmet(const QStringList& armor,
               const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database)
{
    for(const QString& reference : armor)
    {
        if(!database.isNull())
        {
            const auto direct = database->items.constFind(itemPrototype(reference));
            if(direct != database->items.constEnd()
               && direct->category.compare("armor", Qt::CaseInsensitive) == 0
               && direct->type.compare("helm", Qt::CaseInsensitive) == 0)
                return true;
        }
        if(reference.contains("helm", Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QMap<QString, QStringList> loadAssemblyInventory(const QString& path, QString* error)
{
    QMap<QString, QStringList> result;
    if(path.isEmpty())
        return result;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error) *error = file.errorString();
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if(parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if(error) *error = parseError.errorString();
        return result;
    }
    const QJsonObject root = document.object();
    for(auto model = root.constBegin(); model != root.constEnd(); ++model)
    {
        if(!model.value().isArray())
            continue;
        QStringList parts;
        for(const QJsonValue& value : model.value().toArray())
            if(value.isString())
                parts.append(value.toString().trimmed().toLower());
        parts.removeAll(QString());
        parts.removeDuplicates();
        result.insert(model.key().trimmed().toLower(), parts);
    }
    return result;
}

QSet<int> helmetGeometry(const QStringList& armor,
                         const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database)
{
    QSet<int> result;
    if(database.isNull())
        return result;
    for(const QString& reference : armor)
    {
        const QString folded = reference.toCaseFolded();
        const auto direct = database->items.constFind(itemPrototype(reference));
        if(direct != database->items.constEnd()
           && direct->category.compare("armor", Qt::CaseInsensitive) == 0
           && direct->type.compare("helm", Qt::CaseInsensitive) == 0
           && direct->textureType1 >= 0)
            result.insert(direct->textureType1);
        for(auto item = database->items.constBegin(); item != database->items.constEnd(); ++item)
        {
            if(item->category.compare("armor", Qt::CaseInsensitive) != 0
               || item->type.compare("helm", Qt::CaseInsensitive) != 0
               || item->textureType1 < 0)
                continue;
            const QString marker = item.key().toCaseFolded() + ".";
            if(folded.startsWith(marker) || folded.contains("." + marker))
                result.insert(item->textureType1);
        }
    }
    return result;
}

void printMissingHairTemplateStats(
    const QVector<SMobFamilyMember>& members,
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
    const QMap<QString, QStringList>& assemblyPartsByModel,
    const QStringList& assemblyResourceSources,
    const QString& assemblyResourceFingerprint,
    QTextStream& out)
{
    int evaluated = 0;
    int runtimeFallbackRisks = 0;
    int resolvedByInitialHair = 0;
    int resolvedByReplacement = 0;
    int missingAssemblyEvidence = 0;
    int unavailableRuntimeMembers = 0;
    for(const SMobFamilyMember& member : members)
    {
        for(const SMobObjectSnapshot& object : member.objects)
        {
            if(!object.unit || object.importMode || database.isNull())
                continue;
            const auto monster = database->monsters.constFind(
                object.prototype.trimmed().toLower());
            if(monster == database->monsters.constEnd() || monster->hairIndex < 0
               || !hasHelmet(monster->wears, database))
                continue;

            QString resolvedModel = object.model.trimmed().toLower();
            const auto race = database->raceModels.constFind(
                monster->baseRace.trimmed().toLower());
            if(race != database->raceModels.constEnd())
                resolvedModel = race->model.trimmed().toLower();
            QStringList assembly = assemblyPartsByModel.value(resolvedModel);
            if(assembly.isEmpty())
                assembly = ei::assemblyPartNamesFromResourceSources(
                    assemblyResourceSources, assemblyResourceFingerprint, resolvedModel);
            if(assembly.isEmpty())
            {
                ++missingAssemblyEvidence;
                continue;
            }
            ++evaluated;
            const QSet<int> helmetTtis = helmetGeometry(monster->wears, database);
            QList<int> sortedHelmetTtis = helmetTtis.values();
            std::sort(sortedHelmetTtis.begin(), sortedHelmetTtis.end());
            for(const int tti : sortedHelmetTtis)
            {
                const ei::SHelmetHairRuntimeDecision decision =
                    ei::helmetHairRuntimeDecision(resolvedModel, object.bodyParts,
                        assembly, monster->hairIndex, tti);
                if(!decision.helmetPartAvailable
                   || (!decision.nativeHairAvailable && !decision.replacementAvailable))
                {
                    ++unavailableRuntimeMembers;
                    continue;
                }
                resolvedByInitialHair += decision.helmetPartAvailable
                    && !decision.replacementAvailable
                    && decision.nativeHairInitiallyLoaded ? 1 : 0;
                resolvedByReplacement += decision.helmetPartAvailable
                    && decision.replacementAvailable ? 1 : 0;
                if(!decision.helmetPartAvailable || !decision.fallbackLoadsNativeHair)
                    continue;
                ++runtimeFallbackRisks;
                out << "HELMET_HAIR_FALLBACK file=" << member.fileName
                    << " nid=" << (object.hasNid ? QString::number(object.nid)
                                                  : QString("missing"))
                    << " prototype=" << object.prototype
                    << " model=" << resolvedModel
                    << " hair=" << decision.nativeHairPart
                    << " helmet=" << QString::number(tti).rightJustified(2, '0')
                    << " required=" << decision.requiredSerializedPart << "\n";
            }
        }
    }
    out << "HELMET_HAIR_FALLBACK_STATS evaluated=" << evaluated
        << " runtimeFallbackRisks=" << runtimeFallbackRisks
        << " resolvedByInitialHair=" << resolvedByInitialHair
        << " resolvedByReplacement=" << resolvedByReplacement
        << " missingAssemblyEvidence=" << missingAssemblyEvidence
        << " unavailableRuntimeMembers=" << unavailableRuntimeMembers << "\n";
}

QSharedPointer<const ei::SProfileDatabaseSnapshot> loadDatabase(
    const QString& profileId, const QStringList& resourceDirectories)
{
    if(resourceDirectories.isEmpty())
        return {};
    ei::SProfileDatabaseLoadRequest request;
    request.profileId = profileId;
    request.resourceDirectories = resourceDirectories;
    if(profileId.contains("jamais", Qt::CaseInsensitive)
       || profileId.compare("jmv", Qt::CaseInsensitive) == 0)
        return ei::JmvProfileDatabaseProvider().load(request);
    return ei::BaseProfileDatabaseProvider().load(request);
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Parser-backed Endurance runtime-family validation probe");
    parser.addHelpOption();
    QCommandLineOption seed(QStringList() << "s" << "seed", "Explicit MOB seed", "path");
    QCommandLineOption primary(QStringList() << "p" << "primary", "map.txt primary MOB", "path");
    QCommandLineOption root(QStringList() << "r" << "map-root", "MOB lookup root", "directory");
    QCommandLineOption workspaceRoot("workspace-root", "Workspace overlay root", "directory");
    QCommandLineOption preferred("preferred", "Preferred opened MOB source", "path");
    QCommandLineOption profile("profile", "Compatibility profile id", "id", "jmv");
    QCommandLineOption expectNid("expect-nid", "Require a critical collision for this NID", "nid");
    QCommandLineOption expectMember("expect-member", "Require this family member", "file");
    QCommandLineOption expectFinding("expect-finding",
        "Require a finding code, optionally restricted as code:nid", "code[:nid]");
    QCommandLineOption suggestFor("suggest-for", "Print deterministic repairs for owners in this MOB", "file");
    QCommandLineOption expectSuggestions("expect-suggestions", "Require comma-separated deterministic repairs", "ids");
    QCommandLineOption bodyPartStats("bodypart-stats",
        "Print parser-backed serialized unit/body-part inventory");
    QCommandLineOption statsOnly("stats-only",
        "Suppress the ordinary family report (use with --bodypart-stats)");
    QCommandLineOption auditRoot("audit-root",
        "Audit every readable *.mob in a directory independently", "directory");
    QCommandLineOption databaseResource("database-resource",
        "Read-only profile database resource directory", "directory");
    QCommandLineOption missingHairTemplateStats("missing-hair-template-stats",
        "Print database-equipped helmet/hair runtime decisions");
    QCommandLineOption assemblyInventory("assembly-inventory",
        "JSON object mapping model names to authored assembly members", "path");
    QCommandLineOption assemblyResource("assembly-resource",
        "Ordered RES archive used for assembly evidence (repeatable)", "path");
    parser.addOptions({seed, primary, root, workspaceRoot, preferred,
                       profile, expectNid, expectMember,
                       expectFinding, suggestFor, expectSuggestions,
                       bodyPartStats, statsOnly, auditRoot, databaseResource,
                       missingHairTemplateStats, assemblyInventory,
                       assemblyResource});
    parser.process(app);

    QString inventoryError;
    const QMap<QString, QStringList> assemblyPartsByModel =
        loadAssemblyInventory(parser.value(assemblyInventory), &inventoryError);
    const QStringList assemblyResourceSources = parser.values(assemblyResource);
    const QString assemblyResourceFingerprint =
        assemblyResourceSources.join(QChar(0x1f)).toCaseFolded();
    if(parser.isSet(assemblyInventory) && !inventoryError.isEmpty())
    {
        QTextStream(stderr) << "Cannot read assembly inventory: " << inventoryError << "\n";
        return 2;
    }

    const QSharedPointer<const ei::SProfileDatabaseSnapshot> database =
        loadDatabase(parser.value(profile), parser.values(databaseResource));

    if(parser.isSet(auditRoot))
    {
        QVector<SMobFamilyMember> members;
        const QDir directory(parser.value(auditRoot));
        for(const QFileInfo& file : directory.entryInfoList(
                QStringList() << "*.mob", QDir::Files | QDir::Readable, QDir::Name))
        {
            SMobFamilyMember member;
            member.path = file.absoluteFilePath();
            member.fileName = file.fileName();
            CMobDocument document;
            CValidationReport report;
            if(document.load(file, &report))
                member.objects = document.objects();
            else
                member.error = report.toText();
            members.append(member);
        }
        QTextStream out(stdout);
        const int unreadable = printBodyPartStats(members, out);
        if(parser.isSet(missingHairTemplateStats))
            printMissingHairTemplateStats(
                members, database, assemblyPartsByModel,
                assemblyResourceSources, assemblyResourceFingerprint, out);
        return unreadable == 0 ? 0 : 1;
    }

    SMobFamilyValidationRequest request;
    request.familyLabel = "corpus-probe";
    request.profileId = parser.value(profile);
    request.seedMobPaths = parser.values(seed);
    request.mappedPrimaryMobPath = parser.value(primary);
    request.mapRoots = parser.values(root);
    request.workspaceRoots = parser.values(workspaceRoot);
    request.preferredMobPath = parser.value(preferred);
    request.database = database;
    request.assemblyPartsByModel = assemblyPartsByModel;
    request.assemblyResourceSources = assemblyResourceSources;
    request.assemblyResourceFingerprint = assemblyResourceFingerprint;
    if(request.seedMobPaths.isEmpty() && request.mappedPrimaryMobPath.isEmpty())
    {
        QTextStream(stderr) << "At least one --seed or --primary is required.\n";
        return 2;
    }

    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
    QTextStream out(stdout);
    if(parser.isSet(bodyPartStats))
        printBodyPartStats(report.members, out);
    if(parser.isSet(missingHairTemplateStats))
        printMissingHairTemplateStats(
            report.members, database, assemblyPartsByModel,
            assemblyResourceSources, assemblyResourceFingerprint, out);
    if(!parser.isSet(statsOnly))
        out << report.toText() << "\n";
    bool passed = true;
    for(const QString& expected : parser.values(expectMember))
    {
        bool found = false;
        for(const SMobFamilyMember& member : report.members)
            found = found || member.fileName.compare(expected, Qt::CaseInsensitive) == 0;
        if(!found)
        {
            out << "EXPECTATION FAILED: missing family member " << expected << "\n";
            passed = false;
        }
    }
    for(const QString& expected : parser.values(expectNid))
    {
        bool valid = false;
        const quint32 nid = expected.toUInt(&valid);
        bool found = false;
        for(const SMobFamilyFinding& finding : report.findings)
            found = found || (finding.code == "nid.collision" && finding.hasNid
                              && finding.nid == nid);
        if(!valid || !found)
        {
            out << "EXPECTATION FAILED: no critical collision for NID " << expected << "\n";
            passed = false;
        }
    }
    for(const QString& expected : parser.values(expectFinding))
    {
        const int separator = expected.lastIndexOf(':');
        const QString code = separator > 0 ? expected.left(separator) : expected;
        bool restrictNid = false;
        const quint32 nid = separator > 0
            ? expected.mid(separator + 1).toUInt(&restrictNid) : 0;
        bool found = false;
        for(const SMobFamilyFinding& finding : report.findings)
            found = found || (finding.code == code
                && (!restrictNid || (finding.hasNid && finding.nid == nid)));
        if(!found)
        {
            out << "EXPECTATION FAILED: missing finding " << expected << "\n";
            passed = false;
        }
    }
    if(parser.isSet(suggestFor))
    {
        QSet<quint32> reserved;
        QStringList suggestions;
        for(auto it = report.nidOwners.constBegin(); it != report.nidOwners.constEnd(); ++it)
        {
            if(it.value().size() < 2)
                continue;
            for(const SMobNidOwner& owner : it.value())
            {
                if(owner.mobName.compare(parser.value(suggestFor), Qt::CaseInsensitive) != 0)
                    continue;
                quint32 replacement = 0;
                bool shared = false;
                if(report.suggestReplacementNid(owner.mobPath, &replacement, &shared, reserved))
                {
                    reserved.insert(replacement);
                    suggestions.append(QString::number(replacement));
                }
            }
        }
        out << "SUGGESTIONS " << suggestions.join(',') << "\n";
        if(parser.isSet(expectSuggestions)
           && suggestions.join(',') != parser.value(expectSuggestions))
        {
            out << "EXPECTATION FAILED: wanted suggestions "
                << parser.value(expectSuggestions) << "\n";
            passed = false;
        }
    }
    return passed ? 0 : 1;
}
