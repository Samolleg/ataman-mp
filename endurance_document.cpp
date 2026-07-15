#include "endurance_document.h"

#include <QDataStream>
#include <QFile>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QTextStream>

namespace {
QString severityName(EValidationSeverity severity)
{
    switch(severity)
    {
    case EValidationSeverity::Info: return "info";
    case EValidationSeverity::Warning: return "warning";
    case EValidationSeverity::Error: return "error";
    }
    return "error";
}

bool readWholeFile(const QFileInfo& info, QByteArray& output, CValidationReport* report)
{
    QFile file(info.filePath());
    if(!file.open(QIODevice::ReadOnly))
    {
        if(report)
            report->add("file.open", EValidationSeverity::Error, file.errorString(), info.filePath());
        return false;
    }
    output = file.readAll();
    if(output.isEmpty())
    {
        if(report)
            report->add("file.empty", EValidationSeverity::Error, "File is empty", info.filePath());
        return false;
    }
    return true;
}
}

QJsonObject SValidationFinding::toJson() const
{
    QJsonObject result;
    result.insert("code", code);
    result.insert("severity", severityName(severity));
    result.insert("message", message);
    if(!location.isEmpty())
        result.insert("location", location);
    return result;
}

void CValidationReport::add(const QString& code, EValidationSeverity severity, const QString& message, const QString& location)
{
    m_findings.append({code, severity, message, location});
}

bool CValidationReport::hasErrors() const
{
    for(const auto& finding : m_findings)
        if(finding.severity == EValidationSeverity::Error)
            return true;
    return false;
}

QJsonArray CValidationReport::toJson() const
{
    QJsonArray result;
    for(const auto& finding : m_findings)
        result.append(finding.toJson());
    return result;
}

QString CValidationReport::toText() const
{
    QString result;
    QTextStream stream(&result);
    if(m_findings.isEmpty())
        stream << "OK: no findings\n";
    for(const auto& finding : m_findings)
    {
        stream << severityName(finding.severity).toUpper() << " " << finding.code << ": " << finding.message;
        if(!finding.location.isEmpty())
            stream << " [" << finding.location << "]";
        stream << "\n";
    }
    return result;
}

bool CMobDocument::load(const QFileInfo& file, CValidationReport* report)
{
    m_originalBytes.clear();
    m_duplicateIds.clear();
    if(!readWholeFile(file, m_originalBytes, report))
        return false;

    if(m_originalBytes.size() < 16)
    {
        if(report)
            report->add("mob.envelope", EValidationSeverity::Error, "MOB is too small to contain a valid root section", file.filePath());
        return false;
    }

    const QString searchable = QString::fromLatin1(m_originalBytes);
    const QRegularExpression marker("NewObject\\s*-\\s*(\\d+)");
    QRegularExpressionMatchIterator it = marker.globalMatch(searchable);
    QMap<quint32, int> counts;
    while(it.hasNext())
    {
        bool ok = false;
        const quint32 id = it.next().captured(1).toUInt(&ok);
        if(ok)
            counts[id] += 1;
    }
    for(auto countIt = counts.constBegin(); countIt != counts.constEnd(); ++countIt)
    {
        if(countIt.value() > 1)
        {
            for(int n = 1; n < countIt.value(); ++n)
                m_duplicateIds.append(countIt.key());
            if(report)
                report->add("mob.duplicate_id", EValidationSeverity::Warning,
                            QString("ID %1 occurs %2 times; it was preserved").arg(countIt.key()).arg(countIt.value()),
                            file.fileName());
        }
    }
    if(report)
        report->add("mob.lossless", EValidationSeverity::Info, "Unchanged serialization is byte-identical", file.fileName());
    return true;
}

bool CMprDocument::load(const QFileInfo& file, CValidationReport* report)
{
    m_originalBytes.clear();
    if(!readWholeFile(file, m_originalBytes, report))
        return false;
    if(m_originalBytes.size() < 16)
    {
        if(report)
            report->add("mpr.envelope", EValidationSeverity::Error, "MPR is smaller than a RES header", file.filePath());
        return false;
    }
    QDataStream stream(m_originalBytes);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 signature = 0;
    stream >> signature;
    if(signature != 0x019CE23C)
    {
        if(report)
            report->add("mpr.signature", EValidationSeverity::Error, "Invalid MPR/RES signature", file.filePath());
        return false;
    }
    if(report)
        report->add("mpr.lossless", EValidationSeverity::Info, "Unchanged serialization is byte-identical", file.fileName());
    return true;
}
