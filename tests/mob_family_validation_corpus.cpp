#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QTextStream>

#include <algorithm>

#include "mob_family_validation.h"
#include "base_profile_database_provider.h"
#include "jmv_profile_database_provider.h"

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

QString canonicalList(QStringList values, bool omitHair = false)
{
    QStringList folded;
    for(const QString& value : values)
    {
        if(omitHair && (value.compare("hr", Qt::CaseInsensitive) == 0
                        || value.startsWith("hr.", Qt::CaseInsensitive)))
            continue;
        folded.append(value.trimmed().toCaseFolded());
    }
    std::sort(folded.begin(), folded.end());
    return folded.join(QChar(0x1f));
}

QString hairPeerKey(
    const SMobObjectSnapshot& object,
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database)
{
    QStringList armor = object.armor;
    if(!object.importMode && !database.isNull())
    {
        const auto monster = database->monsters.constFind(
            object.prototype.trimmed().toLower());
        if(monster != database->monsters.constEnd())
            armor = monster->wears;
    }
    return QStringList({object.prototype.trimmed().toCaseFolded(),
                        object.model.trimmed().toCaseFolded(),
                        object.importMode ? QString("import") : QString("database"),
                        canonicalList(armor),
                        canonicalList(object.bodyParts, true)})
        .join(QChar(0x1e));
}

void printMissingHairTemplateStats(
    const QVector<SMobFamilyMember>& members,
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
    QTextStream& out)
{
    int authoredHair = 0;
    int hairless = 0;
    int unknown = 0;
    int matchingControls = 0;
    int confirmedDependencies = 0;
    QSet<QString> hairControls;
    for(const SMobFamilyMember& member : members)
        for(const SMobObjectSnapshot& object : member.objects)
            if(object.unit && object.hasBodyParts && !object.bodyParts.isEmpty()
               && object.bodyParts.contains("hr", Qt::CaseInsensitive))
                hairControls.insert(hairPeerKey(object, database));
    for(const SMobFamilyMember& member : members)
    {
        for(const SMobObjectSnapshot& object : member.objects)
        {
            if(!object.unit || object.bodyParts.contains("hr", Qt::CaseInsensitive))
                continue;
            bool hasDottedHair = false;
            for(const QString& part : object.bodyParts)
                hasDottedHair = hasDottedHair
                    || part.startsWith("hr.", Qt::CaseInsensitive);
            if(hasDottedHair)
                continue;

            const ei::SMonsterDatabaseRecord* monster = nullptr;
            if(!object.importMode && !database.isNull())
            {
                const auto found = database->monsters.constFind(
                    object.prototype.trimmed().toLower());
                if(found != database->monsters.constEnd())
                    monster = &found.value();
            }
            const QStringList armor = object.importMode || !monster
                ? object.armor : monster->wears;
            if(!hasHelmet(armor, database))
                continue;

            QString resolvedModel = object.model.trimmed().toLower();
            if(monster && !database.isNull())
            {
                const auto race = database->raceModels.constFind(
                    monster->baseRace.trimmed().toLower());
                if(race != database->raceModels.constEnd())
                    resolvedModel = race->model.trimmed().toLower();
            }
            const QSet<int> helmetTtis = helmetGeometry(armor, database);
            QList<int> sortedHelmetTtis = helmetTtis.values();
            std::sort(sortedHelmetTtis.begin(), sortedHelmetTtis.end());
            QStringList helmetTtiText;
            for(int tti : sortedHelmetTtis)
                helmetTtiText.append(QString::number(tti));

            const QString hair = monster ? QString::number(monster->hairIndex)
                                         : QString("unknown");
            if(!monster) ++unknown;
            else if(monster->hairIndex >= 0) ++authoredHair;
            else ++hairless;
            const bool matchingControl =
                hairControls.contains(hairPeerKey(object, database));
            matchingControls += matchingControl ? 1 : 0;
            const bool confirmedDependency = !object.importMode && monster
                && resolvedModel == "unhuma" && monster->hairIndex == 1
                && helmetTtis.contains(6);
            confirmedDependencies += confirmedDependency ? 1 : 0;
            out << "MISSING_HAIR_TEMPLATE file=" << member.fileName
                << " nid=" << (object.hasNid ? QString::number(object.nid)
                                              : QString("missing"))
                << " prototype=" << object.prototype
                << " import=" << (object.importMode ? "true" : "false")
                << " resolvedModel=" << resolvedModel
                << " hairIndex=" << hair
                << " helmetGeometry=" << helmetTtiText.join(',')
                << " matchingControl=" << (matchingControl ? "true" : "false")
                << " confirmedDependency="
                << (confirmedDependency ? "true" : "false")
                << "\n";
        }
    }
    out << "MISSING_HAIR_TEMPLATE_STATS authoredHair=" << authoredHair
        << " hairless=" << hairless << " unknown=" << unknown
        << " matchingControls=" << matchingControls
        << " confirmedDependencies=" << confirmedDependencies << "\n";
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
        "Print helmeted missing-hr units classified by database hair index");
    parser.addOptions({seed, primary, root, workspaceRoot, preferred,
                       profile, expectNid, expectMember,
                       expectFinding, suggestFor, expectSuggestions,
                       bodyPartStats, statsOnly, auditRoot, databaseResource,
                       missingHairTemplateStats});
    parser.process(app);

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
            printMissingHairTemplateStats(members, database, out);
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
        printMissingHairTemplateStats(report.members, database, out);
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
