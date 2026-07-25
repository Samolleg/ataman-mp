#include "mob_family_validation.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <limits>

#include "script_reference_analyzer.h"

namespace {
QString foldedPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}

QString logicalMobName(const QString& path)
{
    return QFileInfo(path).fileName().toLower();
}

QString severityName(EMobFamilyFindingSeverity severity)
{
    switch(severity)
    {
    case EMobFamilyFindingSeverity::Info: return "INFO";
    case EMobFamilyFindingSeverity::Warning: return "WARNING";
    case EMobFamilyFindingSeverity::Critical: return "CRITICAL";
    case EMobFamilyFindingSeverity::Incomplete: return "INCOMPLETE";
    }
    return "WARNING";
}

QString stripScriptComments(const QString& script)
{
    QString output;
    output.reserve(script.size());
    bool quoted = false;
    bool lineComment = false;
    bool blockComment = false;
    bool escaped = false;
    for(int i = 0; i < script.size(); ++i)
    {
        const QChar ch = script.at(i);
        const QChar next = i + 1 < script.size() ? script.at(i + 1) : QChar();
        if(lineComment)
        {
            if(ch == '\n' || ch == '\r')
            {
                lineComment = false;
                output.append(ch);
            }
            else
                output.append(' ');
            continue;
        }
        if(blockComment)
        {
            if(ch == '*' && next == '/')
            {
                output.append("  ");
                ++i;
                blockComment = false;
            }
            else
                output.append(ch == '\n' || ch == '\r' ? ch : QChar(' '));
            continue;
        }
        if(!quoted && ch == '/' && next == '/')
        {
            output.append("  ");
            ++i;
            lineComment = true;
            continue;
        }
        if(!quoted && ch == '/' && next == '*')
        {
            output.append("  ");
            ++i;
            blockComment = true;
            continue;
        }
        output.append(ch);
        if(quoted && ch == '\\' && !escaped)
        {
            escaped = true;
            continue;
        }
        if(ch == '"' && !escaped)
            quoted = !quoted;
        escaped = false;
    }
    return output;
}

QStringList splitTopLevel(const QString& value, QChar separator)
{
    QStringList result;
    int depth = 0;
    bool quoted = false;
    int start = 0;
    bool escaped = false;
    for(int i = 0; i < value.size(); ++i)
    {
        const QChar ch = value.at(i);
        if(quoted && ch == '\\' && !escaped)
        {
            escaped = true;
            continue;
        }
        if(ch == '"' && !escaped)
            quoted = !quoted;
        escaped = false;
        if(quoted)
            continue;
        if(ch == '(') ++depth;
        else if(ch == ')') --depth;
        else if(ch == separator && depth == 0)
        {
            result.append(value.mid(start, i - start).trimmed());
            start = i + 1;
        }
    }
    result.append(value.mid(start).trimmed());
    return result;
}

bool evaluateConstantString(QString expression, QString* output)
{
    expression = expression.trimmed();
    while(expression.startsWith('(') && expression.endsWith(')'))
        expression = expression.mid(1, expression.size() - 2).trimmed();
    if(expression.size() >= 2 && expression.startsWith('"') && expression.endsWith('"'))
    {
        QString value = expression.mid(1, expression.size() - 2);
        value.replace("\\\"", "\"");
        value.replace("\\\\", "\\");
        if(output) *output = value;
        return true;
    }
    const QRegularExpression strCat("^StrCat\\s*\\((.*)\\)$",
                                    QRegularExpression::CaseInsensitiveOption |
                                    QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = strCat.match(expression);
    QStringList pieces;
    if(match.hasMatch())
        pieces = splitTopLevel(match.captured(1), ',');
    else
    {
        pieces = splitTopLevel(expression, '+');
        if(pieces.size() == 1)
            return false;
    }
    QString value;
    for(const QString& piece : pieces)
    {
        QString part;
        if(!evaluateConstantString(piece, &part))
            return false;
        value += part;
    }
    if(output) *output = value;
    return true;
}

struct ScriptMobReference
{
    QString expression;
    QString fileName;
    int line = 0;
    bool resolved = false;
};

QVector<ScriptMobReference> addMobReferences(const QString& script)
{
    QVector<ScriptMobReference> result;
    const QString cleaned = stripScriptComments(script);
    const QRegularExpression call("\\bAddMob\\s*\\(", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator iterator = call.globalMatch(cleaned);
    while(iterator.hasNext())
    {
        const QRegularExpressionMatch match = iterator.next();
        const int expressionStart = match.capturedEnd();
        int depth = 1;
        bool quoted = false;
        bool escaped = false;
        int cursor = expressionStart;
        for(; cursor < cleaned.size() && depth > 0; ++cursor)
        {
            const QChar ch = cleaned.at(cursor);
            if(quoted && ch == '\\' && !escaped)
            {
                escaped = true;
                continue;
            }
            if(ch == '"' && !escaped)
                quoted = !quoted;
            escaped = false;
            if(quoted)
                continue;
            if(ch == '(') ++depth;
            else if(ch == ')') --depth;
        }
        ScriptMobReference reference;
        reference.expression = cleaned.mid(expressionStart, qMax(0, cursor - expressionStart - 1)).trimmed();
        reference.line = cleaned.left(match.capturedStart()).count('\n') + 1;
        QString resolved;
        reference.resolved = evaluateConstantString(reference.expression, &resolved);
        if(reference.resolved)
        {
            reference.fileName = QFileInfo(resolved).fileName();
            if(!reference.fileName.endsWith(".mob", Qt::CaseInsensitive))
                reference.fileName += ".mob";
        }
        result.append(reference);
    }
    return result;
}

QString findCaseInsensitive(const QString& directory, const QString& name)
{
    QDir dir(directory);
    if(!dir.exists())
        return QString();
    const QFileInfoList files = dir.entryInfoList(QStringList() << "*.mob", QDir::Files | QDir::Readable);
    for(const QFileInfo& file : files)
        if(file.fileName().compare(name, Qt::CaseInsensitive) == 0)
            return file.absoluteFilePath();
    return QString();
}

QString resolveMob(const QString& name, const QString& requestingPath,
                   const QStringList& roots)
{
    QFileInfo direct(name);
    if(direct.isAbsolute() && direct.exists())
        return direct.absoluteFilePath();
    if(!requestingPath.isEmpty())
    {
        const QString found = findCaseInsensitive(QFileInfo(requestingPath).absolutePath(), QFileInfo(name).fileName());
        if(!found.isEmpty()) return found;
    }
    for(const QString& root : roots)
    {
        const QString found = findCaseInsensitive(root, QFileInfo(name).fileName());
        if(!found.isEmpty()) return found;
    }
    return QString();
}

QString resolveOverlayMob(const QString& name, const QString& requestingPath,
                          const SMobFamilyValidationRequest& request)
{
    const QString logical = logicalMobName(name);
    auto matchingPath = [&logical](const QString& candidate) -> QString
    {
        const QFileInfo info(candidate);
        return info.exists() && logicalMobName(candidate) == logical
            ? info.absoluteFilePath() : QString();
    };
    QString found = matchingPath(request.preferredMobPath);
    if(!found.isEmpty()) return found;
    for(int index = request.seedMobPaths.size() - 1; index >= 0; --index)
    {
        found = matchingPath(request.seedMobPaths.at(index));
        if(!found.isEmpty()) return found;
    }
    for(const QString& root : request.workspaceRoots)
    {
        found = findCaseInsensitive(root, QFileInfo(name).fileName());
        if(!found.isEmpty()) return found;
    }
    if(!requestingPath.isEmpty())
    {
        found = findCaseInsensitive(QFileInfo(requestingPath).absolutePath(),
                                    QFileInfo(name).fileName());
        if(!found.isEmpty()) return found;
    }
    return resolveMob(name, QString(), request.mapRoots);
}

QStringList shadowedOverlayPaths(const QString& logicalName, const QString& activePath,
                                 const SMobFamilyValidationRequest& request)
{
    QStringList candidates = request.seedMobPaths;
    if(!request.mappedPrimaryMobPath.isEmpty())
        candidates.append(request.mappedPrimaryMobPath);
    for(const QString& root : request.workspaceRoots)
    {
        const QString path = findCaseInsensitive(root, logicalName);
        if(!path.isEmpty()) candidates.append(path);
    }
    for(const QString& root : request.mapRoots)
    {
        const QString path = findCaseInsensitive(root, logicalName);
        if(!path.isEmpty()) candidates.append(path);
    }
    QStringList result;
    const QString activeKey = foldedPath(activePath);
    for(const QString& candidate : candidates)
    {
        if(logicalMobName(candidate) != logicalName.toLower()
           || foldedPath(candidate) == activeKey)
            continue;
        const QString absolute = QFileInfo(candidate).absoluteFilePath();
        if(!result.contains(absolute, Qt::CaseInsensitive))
            result.append(absolute);
    }
    return result;
}

bool rangeContains(const SMobIdRangeSnapshot& range, quint32 id)
{
    return range.minimum <= range.maximum && id >= range.minimum && id <= range.maximum;
}

bool rangesOverlap(const SMobIdRangeSnapshot& a, const SMobIdRangeSnapshot& b)
{
    if(a.minimum > a.maximum || b.minimum > b.maximum)
        return false;
    return qMax(a.minimum, b.minimum) <= qMin(a.maximum, b.maximum);
}

QString ownerText(const SMobNidOwner& owner)
{
    return QString("%1: %2 (OBJTYPE %3, prototype %4)")
        .arg(owner.mobName,
             owner.objectName.isEmpty() ? owner.sectionPath : owner.objectName,
             owner.hasObjectType ? QString::number(owner.objectType) : QString("unknown"),
             owner.prototype.isEmpty() ? QString("-") : owner.prototype);
}
}

int CMobFamilyValidationReport::criticalCount() const
{
    return std::count_if(findings.begin(), findings.end(), [](const SMobFamilyFinding& finding)
    { return finding.severity == EMobFamilyFindingSeverity::Critical; });
}

int CMobFamilyValidationReport::warningCount() const
{
    return std::count_if(findings.begin(), findings.end(), [](const SMobFamilyFinding& finding)
    { return finding.severity == EMobFamilyFindingSeverity::Warning; });
}

int CMobFamilyValidationReport::incompleteCount() const
{
    return std::count_if(findings.begin(), findings.end(), [](const SMobFamilyFinding& finding)
    { return finding.severity == EMobFamilyFindingSeverity::Incomplete; });
}

bool CMobFamilyValidationReport::hasActionableFindings() const
{
    return criticalCount() > 0 || warningCount() > 0 || incompleteCount() > 0;
}

QString CMobFamilyValidationReport::summary() const
{
    const int outsideActiveFamily = std::count_if(
        members.cbegin(), members.cend(), [](const SMobFamilyMember& member)
        { return !member.activeRuntimeFamily; });
    if(outsideActiveFamily > 0)
    {
        return QString("Workspace context %1: %2 active-family MOB(s), %3 other loaded MOB(s), %4 critical, %5 warning, %6 incomplete")
            .arg(familyLabel.isEmpty() ? QString("standalone") : familyLabel)
            .arg(members.size() - outsideActiveFamily).arg(outsideActiveFamily)
            .arg(criticalCount()).arg(warningCount()).arg(incompleteCount());
    }
    return QString("Family %1: %2 MOB(s), %3 critical, %4 warning, %5 incomplete")
        .arg(familyLabel.isEmpty() ? QString("standalone") : familyLabel)
        .arg(members.size()).arg(criticalCount()).arg(warningCount()).arg(incompleteCount());
}

QString CMobFamilyValidationReport::toText(int findingLimit) const
{
    QString text;
    QTextStream stream(&text);
    stream << summary() << "\n";
    int shown = 0;
    for(const SMobFamilyFinding& finding : findings)
    {
        if(findingLimit >= 0 && shown >= findingLimit)
            break;
        stream << severityName(finding.severity) << " " << finding.category << ": "
               << finding.message << "\n";
        for(const QString& location : finding.locations)
            stream << "  - " << location << "\n";
        ++shown;
    }
    if(findingLimit >= 0 && findings.size() > shown)
        stream << "...and " << findings.size() - shown << " more finding(s).\n";
    return text.trimmed();
}

QByteArray CMobFamilyValidationReport::findingDigest() const
{
    QByteArray normalized;
    for(const SMobFamilyFinding& finding : findings)
    {
        normalized += finding.code.toUtf8() + '|' + QByteArray::number(finding.nid) + '|';
        QStringList locations = finding.locations;
        locations.sort(Qt::CaseInsensitive);
        normalized += locations.join('|').toUtf8() + '\n';
    }
    return QCryptographicHash::hash(normalized, QCryptographicHash::Sha256);
}

QString CMobFamilyValidationReport::describeProposedNid(quint32 nid,
                                                         const QString& owningMobPath) const
{
    QStringList details;
    const QVector<SMobNidOwner> owners = nidOwners.value(nid);
    for(const SMobNidOwner& owner : owners)
        details.append(QString("Already used by %1").arg(ownerText(owner)));
    const QString ownerFolded = foldedPath(owningMobPath);
    for(const SMobFamilyMember& member : members)
    {
        if(member.missing || foldedPath(member.path) == ownerFolded)
            continue;
        for(const SMobIdRangeSnapshot& range : member.ranges)
        {
            if(range.effective && rangeContains(range, nid))
                details.append(QString("Reserved by %1 range %2-%3")
                    .arg(member.fileName).arg(range.minimum).arg(range.maximum));
        }
    }
    return details.join('\n');
}

bool CMobFamilyValidationReport::suggestReplacementNid(const QString& owningMobPath,
                                                        quint32* nid,
                                                        bool* sharedRange,
                                                        const QSet<quint32>& additionallyOccupied) const
{
    if(sharedRange) *sharedRange = false;
    const QString ownerFolded = foldedPath(owningMobPath);
    const SMobFamilyMember* owner = nullptr;
    for(const SMobFamilyMember& member : members)
        if(!member.missing && foldedPath(member.path) == ownerFolded)
        {
            owner = &member;
            break;
        }
    if(!owner)
        return false;

    QVector<SMobIdRangeSnapshot> ranges;
    for(const SMobIdRangeSnapshot& range : owner->ranges)
        if(range.effective && range.minimum <= range.maximum)
            ranges.append(range);
    std::sort(ranges.begin(), ranges.end(), [](const SMobIdRangeSnapshot& a,
                                               const SMobIdRangeSnapshot& b)
    { return a.minimum < b.minimum; });

    auto isCoveredByOtherMob = [&](quint32 candidate)
    {
        for(const SMobFamilyMember& member : members)
        {
            if(member.missing || foldedPath(member.path) == ownerFolded)
                continue;
            for(const SMobIdRangeSnapshot& range : member.ranges)
                if(range.effective && rangeContains(range, candidate))
                    return true;
        }
        return false;
    };
    auto findCandidate = [&](bool permitShared, quint32* result)
    {
        for(const SMobIdRangeSnapshot& range : ranges)
        {
            for(quint64 value = range.minimum; value <= quint64(range.maximum); ++value)
            {
                const quint32 candidate = quint32(value);
                if(candidate == 0 || nidOwners.contains(candidate)
                   || additionallyOccupied.contains(candidate))
                    continue;
                if(!permitShared && isCoveredByOtherMob(candidate))
                    continue;
                *result = candidate;
                return true;
            }
        }
        return false;
    };
    quint32 result = 0;
    if(findCandidate(false, &result))
    {
        if(nid) *nid = result;
        return true;
    }
    if(findCandidate(true, &result))
    {
        if(nid) *nid = result;
        if(sharedRange) *sharedRange = true;
        return true;
    }
    return false;
}

CMobFamilyValidationReport CMobFamilyValidator::validate(
    const SMobFamilyValidationRequest& request)
{
    CMobFamilyValidationReport report;
    report.familyLabel = request.familyLabel;
    report.profileId = request.profileId;

    struct Pending
    {
        QString path;
        QString reason;
        QString requestedBy;
        int scriptLine;
        QString referenceExpression;
    };
    QQueue<Pending> pending;
    for(const QString& seed : request.seedMobPaths)
        pending.enqueue({seed, "explicit seed", QString(), 0, QString()});
    QString effectivePrimaryPath = request.mappedPrimaryMobPath;
    if(!request.explicitComposition && !request.preferredMobPath.isEmpty()
       && request.seedMobPaths.contains(request.preferredMobPath, Qt::CaseInsensitive))
    {
        const QString mappedLogical = logicalMobName(request.mappedPrimaryMobPath);
        bool mappedPrimaryLoaded = false;
        for(const QString& seed : request.seedMobPaths)
            mappedPrimaryLoaded = mappedPrimaryLoaded
                || logicalMobName(seed) == mappedLogical;
        const QString mappedStem = QFileInfo(request.mappedPrimaryMobPath).completeBaseName();
        const QString preferredStem = QFileInfo(request.preferredMobPath).completeBaseName();
        if(!mappedPrimaryLoaded && !mappedStem.isEmpty()
           && preferredStem.startsWith(mappedStem, Qt::CaseInsensitive))
            effectivePrimaryPath = request.preferredMobPath;
    }
    if(!request.explicitComposition && !effectivePrimaryPath.isEmpty())
        pending.enqueue({effectivePrimaryPath,
                         effectivePrimaryPath == request.mappedPrimaryMobPath
                            ? "map.txt primary" : "loaded primary overlay",
                         QString(), 0, QString()});
    if(request.includeProfileWideMobs &&
       (request.profileId.contains("jamais", Qt::CaseInsensitive) ||
        request.profileId.compare("jmv", Qt::CaseInsensitive) == 0))
    {
        const QString autorun = resolveOverlayMob("AutoRunSpellAddon123.mob", QString(), request);
        if(!autorun.isEmpty())
            pending.enqueue({autorun, "profile-wide autorun", QString(), 0, QString()});
    }

    QSet<QString> visited;
    while(!pending.isEmpty())
    {
        const Pending item = pending.dequeue();
        QString path = resolveOverlayMob(item.path, item.requestedBy, request);
        const QString key = path.isEmpty()
            ? QString("missing:%1:%2").arg(item.requestedBy, item.path).toLower()
            : logicalMobName(path);
        if(visited.contains(key))
            continue;
        visited.insert(key);

        SMobFamilyMember member;
        member.path = path.isEmpty() ? item.path : QFileInfo(path).absoluteFilePath();
        member.fileName = QFileInfo(member.path).fileName();
        member.logicalName = member.fileName.toLower();
        member.shadowedPaths = path.isEmpty() ? QStringList()
            : shadowedOverlayPaths(member.logicalName, member.path, request);
        const QString pathKey = foldedPath(member.path);
        if(request.inMemoryMobData.contains(pathKey))
            member.sourceRole = "in-memory workspace";
        else if(request.seedMobPaths.contains(member.path, Qt::CaseInsensitive))
            member.sourceRole = request.explicitComposition ? "collection" : "opened workspace";
        else if(!item.requestedBy.isEmpty()
                && QFileInfo(item.requestedBy).absolutePath().compare(
                    QFileInfo(member.path).absolutePath(), Qt::CaseInsensitive) == 0)
            member.sourceRole = "workspace dependency";
        else
            member.sourceRole = item.reason == "profile-wide autorun"
                ? "profile-wide" : "installed fallback";
        member.inclusionReason = item.reason;
        member.requestedBy = item.requestedBy;
        QStringList competingOpenSources;
        for(const QString& shadowed : member.shadowedPaths)
            if(request.seedMobPaths.contains(shadowed, Qt::CaseInsensitive))
                competingOpenSources.append(shadowed);
        if(!competingOpenSources.isEmpty())
        {
            SMobFamilyFinding finding;
            finding.severity = EMobFamilyFindingSeverity::Warning;
            finding.code = "family.source_ambiguous";
            finding.category = "Source ambiguity";
            finding.message = QString("Multiple opened MOBs use logical name %1; %2 is authoritative")
                .arg(member.fileName, member.path);
            finding.locations << member.path << competingOpenSources;
            report.findings.append(finding);
        }
        if(path.isEmpty())
        {
            member.missing = true;
            SMobFamilyFinding finding;
            finding.severity = EMobFamilyFindingSeverity::Incomplete;
            finding.code = "family.missing_mob";
            finding.category = "Incomplete family";
            const QString missingFile = member.fileName.isEmpty() ? item.path : member.fileName;
            if(item.scriptLine > 0 && !item.requestedBy.isEmpty())
            {
                const QString sourceName = QFileInfo(item.requestedBy).fileName();
                const QString expression = QString("AddMob(%1)").arg(item.referenceExpression);
                finding.message = QString(
                    "MOB file \"%1\" was not found. Referenced by %2 script line %3: %4")
                    .arg(missingFile, sourceName)
                    .arg(item.scriptLine)
                    .arg(expression);
                finding.locations << QString("Reference source: %1").arg(item.requestedBy)
                                  << QString("Script line %1: %2")
                                         .arg(item.scriptLine)
                                         .arg(expression);
            }
            else
            {
                finding.message = QString(
                    "MOB file \"%1\" was not found. Requested as %2")
                    .arg(missingFile, item.reason);
                if(!item.requestedBy.isEmpty())
                    finding.locations << QString("Reference source: %1").arg(item.requestedBy);
            }
            finding.locations << "Searched the active workspace and configured profile Maps sources";
            member.error = finding.message;
            report.members.append(member);
            report.findings.append(finding);
            continue;
        }

        CMobDocument document;
        CValidationReport documentReport;
        const QString overrideKey = foldedPath(path);
        const auto overrideIt = request.inMemoryMobData.constFind(overrideKey);
        member.inMemory = overrideIt != request.inMemoryMobData.constEnd();
        const bool loaded = member.inMemory
            ? document.loadBytes(overrideIt.value(), path, &documentReport)
            : document.load(QFileInfo(path), &documentReport);
        if(!loaded)
        {
            member.error = documentReport.toText();
            report.members.append(member);
            SMobFamilyFinding finding;
            finding.severity = EMobFamilyFindingSeverity::Incomplete;
            finding.code = "family.unreadable_mob";
            finding.category = "Incomplete family";
            finding.message = QString("%1 could not be parsed structurally").arg(member.fileName);
            finding.locations << path;
            report.findings.append(finding);
            continue;
        }
        member.objects = document.objects();
        member.ranges = document.idRanges();
        member.primaryMob = !document.isQuestMob();
        member.sourceSha256 = QCryptographicHash::hash(document.originalBytes(),
                                                        QCryptographicHash::Sha256);
        document.embeddedScript(&member.script, nullptr);
        report.members.append(member);
        const int memberIndex = report.members.size() - 1;

        const QVector<ScriptMobReference> references = addMobReferences(member.script);
        for(const ScriptMobReference& reference : references)
        {
            if(!reference.resolved)
            {
                SMobFamilyFinding finding;
                finding.severity = EMobFamilyFindingSeverity::Incomplete;
                finding.code = "family.dynamic_addmob";
                finding.category = "Incomplete family";
                finding.message = QString("Could not resolve AddMob expression at line %1: %2")
                    .arg(reference.line).arg(reference.expression);
                finding.locations << path;
                report.findings.append(finding);
                continue;
            }
            const QString target = resolveOverlayMob(reference.fileName, path, request);
            pending.enqueue({target.isEmpty() ? reference.fileName : target,
                             QString("AddMob at line %1").arg(reference.line), path,
                             reference.line, reference.expression});
        }

        if(report.members.at(memberIndex).ranges.isEmpty())
        {
            SMobFamilyFinding finding;
            finding.severity = EMobFamilyFindingSeverity::Warning;
            finding.code = "range.missing";
            finding.category = "ID range";
            finding.message = QString("%1 declares no ID ranges").arg(member.fileName);
            finding.locations << path;
            report.findings.append(finding);
        }
    }

    QStringList loadedPrimaryPaths;
    for(const SMobFamilyMember& member : report.members)
    {
        if(member.primaryMob
           && request.seedMobPaths.contains(member.path, Qt::CaseInsensitive))
            loadedPrimaryPaths.append(member.path);
    }
    if(loadedPrimaryPaths.size() > 1)
    {
        SMobFamilyFinding finding;
        finding.severity = EMobFamilyFindingSeverity::Warning;
        finding.code = "workspace.multiple_primary_mobs";
        finding.category = "Workspace composition";
        finding.message = QString(
            "%1 primary SC_OBJECT_DB_FILE MOBs are loaded; map ownership and Test Current Map are ambiguous")
            .arg(loadedPrimaryPaths.size());
        finding.locations = loadedPrimaryPaths;
        report.findings.append(finding);
    }

    // A request can temporarily contain multiple map sets if a second load is
    // admitted while the first is still finalising. Validate everything that
    // is genuinely loaded, but distinguish the active map.txt/AddMob family
    // from unrelated workspace documents.
    if(!request.explicitComposition && !effectivePrimaryPath.isEmpty())
    {
        QMap<QString, int> memberByLogicalName;
        for(int index = 0; index < report.members.size(); ++index)
        {
            report.members[index].activeRuntimeFamily = false;
            if(!report.members.at(index).logicalName.isEmpty())
                memberByLogicalName.insert(report.members.at(index).logicalName, index);
        }

        QQueue<int> familyPending;
        const QString primaryLogicalName = logicalMobName(effectivePrimaryPath);
        if(memberByLogicalName.contains(primaryLogicalName))
            familyPending.enqueue(memberByLogicalName.value(primaryLogicalName));
        for(int index = 0; index < report.members.size(); ++index)
            if(report.members.at(index).sourceRole == "profile-wide")
                familyPending.enqueue(index);

        while(!familyPending.isEmpty())
        {
            const int index = familyPending.dequeue();
            if(index < 0 || index >= report.members.size()
               || report.members.at(index).activeRuntimeFamily)
                continue;
            report.members[index].activeRuntimeFamily = true;
            for(const ScriptMobReference& reference : addMobReferences(report.members.at(index).script))
            {
                if(!reference.resolved)
                    continue;
                const QString targetLogicalName = logicalMobName(reference.fileName);
                if(memberByLogicalName.contains(targetLogicalName))
                    familyPending.enqueue(memberByLogicalName.value(targetLogicalName));
            }
        }

        QStringList unrelatedPaths;
        for(const SMobFamilyMember& member : report.members)
            if(!member.activeRuntimeFamily && !member.missing)
                unrelatedPaths.append(member.path);
        if(!unrelatedPaths.isEmpty())
        {
            SMobFamilyFinding finding;
            finding.severity = EMobFamilyFindingSeverity::Warning;
            finding.code = "workspace.unrelated_loaded_mobs";
            finding.category = "Mixed loaded map sets";
            finding.message = QString(
                "%1 loaded MOB(s) are outside active runtime family %2; overlaps with them are workspace-context findings")
                .arg(unrelatedPaths.size()).arg(request.familyLabel);
            finding.locations = unrelatedPaths;
            report.findings.append(finding);
        }
    }

    for(const SMobFamilyMember& member : report.members)
    {
        if(member.missing || !member.error.isEmpty())
            continue;
        QVector<SMobIdRangeSnapshot> effective;
        QMap<quint32, QVector<const SMobObjectSnapshot*>> ownersWithinMob;
        for(const SMobIdRangeSnapshot& range : member.ranges)
        {
            if(range.minimum > range.maximum)
            {
                SMobFamilyFinding finding;
                finding.severity = EMobFamilyFindingSeverity::Warning;
                finding.code = "range.invalid";
                finding.category = "ID range";
                finding.message = QString("%1 declares reversed range %2-%3")
                    .arg(member.fileName).arg(range.minimum).arg(range.maximum);
                finding.locations << member.path + ":" + range.sectionPath;
                report.findings.append(finding);
            }
            if(range.effective)
                effective.append(range);
        }
        for(const SMobObjectSnapshot& object : member.objects)
        {
            if(!object.hasNid)
                continue;
            ownersWithinMob[object.nid].append(&object);
            SMobNidOwner owner;
            owner.nid = object.nid;
            owner.mobPath = member.path;
            owner.mobName = member.fileName;
            owner.sectionPath = object.sectionPath;
            owner.objectName = object.name;
            owner.prototype = object.prototype;
            owner.objectType = object.objectType;
            owner.hasObjectType = object.hasObjectType;
            owner.useInScript = object.useInScript;
            owner.activeRuntimeFamily = member.activeRuntimeFamily;
            owner.hasParentId = object.hasParentId;
            owner.parentId = object.parentId;
            report.nidOwners[object.nid].append(owner);
            bool inside = effective.isEmpty();
            for(const SMobIdRangeSnapshot& range : effective)
                inside = inside || rangeContains(range, object.nid);
            if(!inside)
            {
                SMobFamilyFinding finding;
                finding.severity = EMobFamilyFindingSeverity::Warning;
                finding.code = "range.object_outside";
                finding.category = "ID range";
                finding.message = QString("NID %1 in %2 is outside that MOB's effective ranges")
                    .arg(object.nid).arg(member.fileName);
                finding.locations << object.sectionPath;
                finding.hasNid = true;
                finding.nid = object.nid;
                report.findings.append(finding);
            }
        }
        for(auto ownerIt = ownersWithinMob.constBegin();
            ownerIt != ownersWithinMob.constEnd(); ++ownerIt)
        {
            if(ownerIt.value().size() < 2)
                continue;
            SMobFamilyFinding finding;
            finding.severity = EMobFamilyFindingSeverity::Warning;
            finding.code = "nid.duplicate_within_mob";
            finding.category = "Duplicate object ID";
            finding.message = QString("NID %1 is used by %2 objects in %3")
                .arg(ownerIt.key()).arg(ownerIt.value().size()).arg(member.fileName);
            finding.hasNid = true;
            finding.nid = ownerIt.key();
            for(const SMobObjectSnapshot* object : ownerIt.value())
                finding.locations.append(QString("%1: %2")
                    .arg(object->sectionPath, object->name));
            report.findings.append(finding);
        }
    }

    for(int leftIndex = 0; leftIndex < report.members.size(); ++leftIndex)
    {
        const SMobFamilyMember& left = report.members.at(leftIndex);
        if(left.missing || !left.error.isEmpty()) continue;
        for(int rightIndex = leftIndex + 1; rightIndex < report.members.size(); ++rightIndex)
        {
            const SMobFamilyMember& right = report.members.at(rightIndex);
            if(right.missing || !right.error.isEmpty()) continue;
            for(const SMobIdRangeSnapshot& a : left.ranges)
            {
                if(!a.effective) continue;
                for(const SMobIdRangeSnapshot& b : right.ranges)
                {
                    if(!b.effective || !rangesOverlap(a, b)) continue;
                    SMobFamilyFinding finding;
                    finding.severity = EMobFamilyFindingSeverity::Info;
                    const bool crossFamily = left.activeRuntimeFamily != right.activeRuntimeFamily;
                    finding.code = crossFamily ? "range.workspace_overlap" : "range.legacy_overlap";
                    finding.category = crossFamily ? "Cross-family workspace range overlap"
                                                   : "Legacy range overlap";
                    finding.message = QString("%1 %2-%3 overlaps %4 %5-%6%7")
                        .arg(left.fileName).arg(a.minimum).arg(a.maximum)
                        .arg(right.fileName).arg(b.minimum).arg(b.maximum)
                        .arg(crossFamily
                            ? QString(" (separately loaded map sets; not one %1 family)").arg(request.familyLabel)
                            : QString());
                    finding.locations << left.path + ":" + a.sectionPath
                                      << right.path + ":" + b.sectionPath;
                    report.findings.append(finding);
                }
            }
        }
    }

    QVector<uint> collidingIds;
    for(auto it = report.nidOwners.constBegin(); it != report.nidOwners.constEnd(); ++it)
        if(it.value().size() >= 2)
            collidingIds.append(it.key());
    QSet<quint32> typedReferences;
    QSet<quint32> scriptReferences;
    for(const SMobFamilyMember& member : report.members)
    {
        if(member.missing || !member.error.isEmpty()) continue;
        for(const SMobObjectSnapshot& object : member.objects)
            if(object.hasParentId)
                typedReferences.insert(object.parentId);
        for(const SMobScriptReference& reference :
            CScriptReferenceAnalyzer::findObjectIds(member.script, collidingIds))
            scriptReferences.insert(reference.objectId);
    }
    for(auto it = report.nidOwners.constBegin(); it != report.nidOwners.constEnd(); ++it)
    {
        if(it.value().size() < 2)
            continue;
        bool safeStatic = true;
        QVector<quint32> ids;
        ids.append(it.key());
        for(const SMobNidOwner& owner : it.value())
        {
            safeStatic = safeStatic && owner.hasObjectType && owner.objectType == 54 &&
                         !owner.useInScript &&
                         (!owner.hasParentId || owner.parentId == 0
                          || owner.parentId == std::numeric_limits<quint32>::max());
        }
        if(scriptReferences.contains(it.key()) || typedReferences.contains(it.key()))
            safeStatic = false;
        SMobFamilyFinding finding;
        bool hasActiveFamilyOwner = false;
        bool hasOutsideFamilyOwner = false;
        for(const SMobNidOwner& owner : it.value())
        {
            hasActiveFamilyOwner = hasActiveFamilyOwner || owner.activeRuntimeFamily;
            hasOutsideFamilyOwner = hasOutsideFamilyOwner || !owner.activeRuntimeFamily;
        }
        const bool crossFamily = hasActiveFamilyOwner && hasOutsideFamilyOwner;
        finding.severity = safeStatic ? EMobFamilyFindingSeverity::Info
                                      : EMobFamilyFindingSeverity::Critical;
        finding.code = crossFamily ? "nid.workspace_collision"
                                   : (safeStatic ? "nid.static_alias" : "nid.collision");
        finding.category = crossFamily ? "Cross-family workspace NID collision"
                                       : (safeStatic ? "Known static alias" : "NID collision");
        finding.message = crossFamily
            ? QString("NID %1 is owned by %2 objects across separately loaded map sets; they are not one %3 runtime family")
                .arg(it.key()).arg(it.value().size()).arg(request.familyLabel)
            : QString("NID %1 is owned by %2 objects across the runtime family")
                .arg(it.key()).arg(it.value().size());
        finding.hasNid = true;
        finding.nid = it.key();
        for(const SMobNidOwner& owner : it.value())
            finding.locations.append(ownerText(owner));
        report.findings.append(finding);
    }

    std::stable_sort(report.findings.begin(), report.findings.end(),
        [](const SMobFamilyFinding& a, const SMobFamilyFinding& b)
        {
            const auto rank = [](EMobFamilyFindingSeverity severity)
            {
                switch(severity)
                {
                case EMobFamilyFindingSeverity::Critical: return 4;
                case EMobFamilyFindingSeverity::Warning: return 3;
                case EMobFamilyFindingSeverity::Incomplete: return 2;
                case EMobFamilyFindingSeverity::Info: return 1;
                }
                return 0;
            };
            return rank(a.severity) > rank(b.severity);
        });
    return report;
}
