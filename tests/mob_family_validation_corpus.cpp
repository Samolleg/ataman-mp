#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include "mob_family_validation.h"

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
    QCommandLineOption suggestFor("suggest-for", "Print deterministic repairs for owners in this MOB", "file");
    QCommandLineOption expectSuggestions("expect-suggestions", "Require comma-separated deterministic repairs", "ids");
    parser.addOptions({seed, primary, root, workspaceRoot, preferred,
                       profile, expectNid, expectMember,
                       suggestFor, expectSuggestions});
    parser.process(app);

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
