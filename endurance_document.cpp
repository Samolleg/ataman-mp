#include "endurance_document.h"

#include <QDataStream>
#include <QFile>
#include <QJsonObject>
#include <QMap>
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

    QDataStream rootStream(m_originalBytes);
    rootStream.setByteOrder(QDataStream::LittleEndian);
    quint32 rootType = 0;
    quint32 rootLength = 0;
    rootStream >> rootType >> rootLength;
    if(rootType != 40960 || rootLength < 8 || rootLength > quint32(m_originalBytes.size()))
    {
        if(report)
            report->add("mob.root", EValidationSeverity::Error,
                        QString("Invalid MOB root header (type %1, length %2, file size %3)")
                            .arg(rootType).arg(rootLength).arg(m_originalBytes.size()),
                        file.filePath());
        return false;
    }

    quint32 topLevelOffset = rootLength;
    while(topLevelOffset < quint32(m_originalBytes.size()))
    {
        if(quint64(topLevelOffset) + 8 > quint64(m_originalBytes.size()))
        {
            if(report)
                report->add("mob.trailing_header", EValidationSeverity::Error,
                            "A trailing MOB section has an incomplete header", file.filePath());
            return false;
        }
        QDataStream trailingStream(m_originalBytes.mid(int(topLevelOffset), 8));
        trailingStream.setByteOrder(QDataStream::LittleEndian);
        quint32 trailingType = 0;
        quint32 trailingLength = 0;
        trailingStream >> trailingType >> trailingLength;
        if(trailingType != 826366246 || trailingLength < 8 ||
           quint64(topLevelOffset) + trailingLength > quint64(m_originalBytes.size()))
        {
            if(report)
                report->add("mob.trailing_section", EValidationSeverity::Error,
                            QString("Invalid trailing MOB section at offset %1 (type %2, length %3)")
                                .arg(topLevelOffset).arg(trailingType).arg(trailingLength),
                            file.filePath());
            return false;
        }
        topLevelOffset += trailingLength;
    }

    QMap<quint32, int> counts;
    const QByteArray marker("NewObject");
    int markerOffset = 0;
    while((markerOffset = m_originalBytes.indexOf(marker, markerOffset)) >= 0)
    {
        int cursor = markerOffset + marker.size();
        while(cursor < m_originalBytes.size() &&
              (m_originalBytes.at(cursor) == '\0' ||
               m_originalBytes.at(cursor) == ' ' ||
               m_originalBytes.at(cursor) == '\t'))
            ++cursor;
        if(cursor >= m_originalBytes.size() || m_originalBytes.at(cursor) != '-')
        {
            markerOffset += marker.size();
            continue;
        }
        ++cursor;
        while(cursor < m_originalBytes.size() &&
              (m_originalBytes.at(cursor) == '\0' ||
               m_originalBytes.at(cursor) == ' ' ||
               m_originalBytes.at(cursor) == '\t'))
            ++cursor;
        const int firstDigit = cursor;
        while(cursor < m_originalBytes.size() &&
              m_originalBytes.at(cursor) >= '0' && m_originalBytes.at(cursor) <= '9')
            ++cursor;
        bool ok = false;
        const quint32 id = m_originalBytes.mid(firstDigit, cursor - firstDigit).toUInt(&ok);
        if(ok)
            counts[id] += 1;
        markerOffset = qMax(cursor, markerOffset + marker.size());
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
    quint32 tableSize = 0;
    quint32 tableOffset = 0;
    quint32 namesLength = 0;
    stream >> signature >> tableSize >> tableOffset >> namesLength;
    if(signature != 0x019CE23C)
    {
        if(report)
            report->add("mpr.signature", EValidationSeverity::Error, "Invalid MPR/RES signature", file.filePath());
        return false;
    }
    const quint64 entrySize = 22;
    const quint64 tableBytes = quint64(tableSize) * entrySize;
    const quint64 namesOffset = quint64(tableOffset) + tableBytes;
    if(tableOffset < 16 || namesOffset > quint64(m_originalBytes.size()) ||
       namesOffset + namesLength > quint64(m_originalBytes.size()))
    {
        if(report)
            report->add("mpr.table", EValidationSeverity::Error,
                        "The RES table or filename buffer extends outside the MPR file",
                        file.filePath());
        return false;
    }

    if(!stream.device()->seek(tableOffset))
    {
        if(report)
            report->add("mpr.table_seek", EValidationSeverity::Error,
                        "Could not seek to the MPR resource table", file.filePath());
        return false;
    }
    for(quint32 index = 0; index < tableSize; ++index)
    {
        quint32 nextIndex = 0;
        quint32 dataSize = 0;
        quint32 dataOffset = 0;
        quint32 lastWriteTime = 0;
        quint16 nameLength = 0;
        quint32 nameOffset = 0;
        stream >> nextIndex >> dataSize >> dataOffset >> lastWriteTime >> nameLength >> nameOffset;
        Q_UNUSED(lastWriteTime);
        const bool nextIndexValid = nextIndex == quint32(-1) || nextIndex < tableSize;
        const bool dataValid = dataOffset >= 16 &&
            quint64(dataOffset) + dataSize <= quint64(tableOffset);
        const bool nameValid = quint64(nameOffset) + nameLength <= namesLength;
        if(stream.status() != QDataStream::Ok || !nextIndexValid || !dataValid || !nameValid)
        {
            if(report)
                report->add("mpr.entry", EValidationSeverity::Error,
                            QString("Invalid MPR resource-table entry %1").arg(index),
                            file.filePath());
            return false;
        }
    }
    if(report)
        report->add("mpr.lossless", EValidationSeverity::Info, "Unchanged serialization is byte-identical", file.fileName());
    return true;
}
