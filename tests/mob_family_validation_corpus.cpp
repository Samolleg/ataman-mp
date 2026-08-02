#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QTextStream>

#include "mob_family_validation.h"

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
    parser.addOptions({seed, primary, root, workspaceRoot, preferred,
                       profile, expectNid, expectMember,
                       expectFinding, suggestFor, expectSuggestions,
                       bodyPartStats, statsOnly, auditRoot});
    parser.process(app);

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
    if(request.seedMobPaths.isEmpty() && request.mappedPrimaryMobPath.isEmpty())
    {
        QTextStream(stderr) << "At least one --seed or --primary is required.\n";
        return 2;
    }

    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
    QTextStream out(stdout);
    if(parser.isSet(bodyPartStats))
        printBodyPartStats(report.members, out);
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
