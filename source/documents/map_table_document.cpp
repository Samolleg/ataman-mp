#include "map_table_document.h"

#include <QRegularExpression>
#include <QMap>
#include <QSet>
#include <QTextCodec>

#include <algorithm>

namespace {
const QRegularExpression kWhitespace(QStringLiteral("\\s+"));

QString trimmedTag(const QByteArray& bytes)
{
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    const QString line = codec ? codec->toUnicode(bytes) : QString::fromLatin1(bytes);
    const QString trimmed = line.trimmed();
    if(!trimmed.startsWith('#') || trimmed.startsWith("##"))
        return QString();
    return trimmed.split(kWhitespace, QString::SkipEmptyParts).value(0).toLower();
}

QStringList tokens(const QString& line)
{
    return line.trimmed().split(kWhitespace, QString::SkipEmptyParts);
}

bool tokenIsSafe(const QString& value)
{
    return !value.trimmed().isEmpty() && !value.contains(kWhitespace);
}

QString number(double value)
{
    return QString::number(value, 'g', 15);
}

QString rectangleText(const QRectF& rect)
{
    return QStringLiteral("%1 %2 %3 %4")
        .arg(number(rect.left()), number(rect.top()),
             number(rect.right()), number(rect.bottom()));
}

bool parseRectangle(const QString& line, QRectF* rectangle)
{
    const QStringList parts = tokens(line);
    if(parts.size() < 4)
        return false;
    bool ok[4] = {false, false, false, false};
    const double x1 = parts.at(0).toDouble(&ok[0]);
    const double y1 = parts.at(1).toDouble(&ok[1]);
    const double x2 = parts.at(2).toDouble(&ok[2]);
    const double y2 = parts.at(3).toDouble(&ok[3]);
    if(!ok[0] || !ok[1] || !ok[2] || !ok[3])
        return false;
    if(rectangle)
        *rectangle = QRectF(QPointF(x1, y1), QPointF(x2, y2));
    return true;
}

bool equalDouble(double left, double right)
{
    return qAbs(left - right) <= 0.000000001;
}

bool equalRect(const QRectF& left, const QRectF& right)
{
    return equalDouble(left.left(), right.left()) &&
        equalDouble(left.top(), right.top()) &&
        equalDouble(left.right(), right.right()) &&
        equalDouble(left.bottom(), right.bottom());
}
}

bool SMapTableMaps::operator==(const SMapTableMaps& other) const
{
    return present == other.present && sectorWidth == other.sectorWidth &&
        sectorHeight == other.sectorHeight &&
        minimapTexture == other.minimapTexture &&
        objectivesTexture == other.objectivesTexture;
}

bool SMapTableExit::operator==(const SMapTableExit& other) const
{
    return exitId == other.exitId && destinationZoneId == other.destinationZoneId &&
        destinationExitId == other.destinationExitId &&
        hasDeployRectangle == other.hasDeployRectangle &&
        (!hasDeployRectangle || equalRect(deployRectangle, other.deployRectangle)) &&
        hasRemoveRectangle == other.hasRemoveRectangle &&
        (!hasRemoveRectangle || equalRect(removeRectangle, other.removeRectangle)) &&
        hasPassTime == other.hasPassTime &&
        (!hasPassTime || equalDouble(passTime, other.passTime)) &&
        hasViewAngle == other.hasViewAngle &&
        (!hasViewAngle || equalDouble(viewAngle, other.viewAngle)) &&
        hasDeployAngle == other.hasDeployAngle &&
        (!hasDeployAngle || equalDouble(deployAngle, other.deployAngle));
}

QString SMapTableEditResult::toText() const
{
    QStringList lines;
    for(const QString& error : errors)
        lines.append(QStringLiteral("Error: ") + error);
    for(const QString& warning : warnings)
        lines.append(QStringLiteral("Warning: ") + warning);
    for(const QString& field : changedFields)
        lines.append(QStringLiteral("Changed: ") + field);
    return lines.join('\n');
}

bool CMapTableDocument::loadBytes(const QByteArray& bytes, QStringList* errors)
{
    m_originalBytes = bytes;
    m_lines.clear();
    m_spans.clear();
    m_zones.clear();
    m_dirty = false;

    int cursor = 0;
    while(cursor < bytes.size())
    {
        const int newline = bytes.indexOf('\n', cursor);
        const int contentEnd = newline < 0 ? bytes.size() : newline;
        int textEnd = contentEnd;
        QByteArray ending;
        if(newline >= 0)
        {
            if(textEnd > cursor && bytes.at(textEnd - 1) == '\r')
            {
                --textEnd;
                ending = QByteArrayLiteral("\r\n");
            }
            else
                ending = QByteArrayLiteral("\n");
        }
        SLine line;
        line.content = bytes.mid(cursor, textEnd - cursor);
        line.ending = ending;
        m_lines.append(line);
        cursor = newline < 0 ? bytes.size() : newline + 1;
    }
    if(bytes.isEmpty())
        m_lines.append(SLine());
    return reparse(errors);
}

QByteArray CMapTableDocument::serialize() const
{
    QByteArray result;
    for(const SLine& line : m_lines)
    {
        result.append(line.content);
        result.append(line.ending);
    }
    return result;
}

QByteArray CMapTableDocument::encodeLine(const QString& text, bool* ok) const
{
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    const QByteArray encoded = codec ? codec->fromUnicode(text) : text.toLatin1();
    const QString decoded = codec ? codec->toUnicode(encoded) : QString::fromLatin1(encoded);
    if(ok)
        *ok = decoded == text;
    return encoded;
}

QString CMapTableDocument::decodeLine(const QByteArray& bytes) const
{
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    return codec ? codec->toUnicode(bytes) : QString::fromLatin1(bytes);
}

QByteArray CMapTableDocument::preferredEnding() const
{
    int crlf = 0;
    int lf = 0;
    for(const SLine& line : m_lines)
    {
        if(line.ending == QByteArrayLiteral("\r\n"))
            ++crlf;
        else if(line.ending == QByteArrayLiteral("\n"))
            ++lf;
    }
    return crlf >= lf ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");
}

bool CMapTableDocument::reparse(QStringList* errors)
{
    m_spans.clear();
    m_zones.clear();
    QVector<int> starts;
    for(int index = 0; index < m_lines.size(); ++index)
    {
        if(trimmedTag(m_lines.at(index).content) == QStringLiteral("#zone"))
            starts.append(index);
    }

    const auto decoded = [this](int index) { return decodeLine(m_lines.at(index).content); };
    QVector<int> reviewStarts = starts;
    for(int spanIndex = 0; spanIndex < reviewStarts.size(); ++spanIndex)
    {
        int reviewStart = reviewStarts.at(spanIndex);
        while(reviewStart > 0)
        {
            const QString preceding = decoded(reviewStart - 1).trimmed();
            if(!preceding.isEmpty() && !preceding.startsWith("##"))
                break;
            --reviewStart;
        }
        reviewStarts[spanIndex] = reviewStart;
    }
    const auto nextValueLine = [this](int tagIndex, int limit) {
        for(int index = tagIndex + 1; index < limit; ++index)
        {
            const QString line = decodeLine(m_lines.at(index).content).trimmed();
            if(line.isEmpty() || line.startsWith("##"))
                continue;
            return line.startsWith('#') ? -1 : index;
        }
        return -1;
    };

    for(int spanIndex = 0; spanIndex < starts.size(); ++spanIndex)
    {
        SZoneSpan span;
        span.start = starts.at(spanIndex);
        span.end = spanIndex + 1 < starts.size() ? starts.at(spanIndex + 1) : m_lines.size();
        // Global quest records following the last zone are not part of it.
        // #allod is also authored inside zone definitions and must not be used
        // as a boundary marker.
        for(int index = span.start + 1; index < span.end; ++index)
        {
            const QString tag = trimmedTag(m_lines.at(index).content);
            if(tag == QStringLiteral("#quest"))
            {
                span.end = index;
                break;
            }
        }

        const QStringList header = tokens(decoded(span.start));
        if(header.size() < 4)
        {
            if(errors)
                errors->append(QString("Malformed #zone header at line %1").arg(span.start + 1));
            continue;
        }
        span.value.zoneId = header.at(1);
        span.value.allodId = header.at(2);
        span.value.zoneType = header.at(3);

        int firstExit = span.end;
        for(int index = span.start + 1; index < span.end; ++index)
        {
            const QString tag = trimmedTag(m_lines.at(index).content);
            if(tag == QStringLiteral("#exit"))
            {
                firstExit = index;
                break;
            }
            if(tag == QStringLiteral("#res"))
            {
                const int valueIndex = nextValueLine(index, span.end);
                if(valueIndex >= 0)
                {
                    const QStringList value = tokens(decoded(valueIndex));
                    if(value.size() >= 2)
                    {
                        span.value.mprName = value.at(0);
                        span.value.mobName = value.at(1);
                    }
                }
            }
            else if(tag == QStringLiteral("#maps"))
            {
                const int sizeIndex = nextValueLine(index, span.end);
                const int textureIndex = sizeIndex >= 0 ? nextValueLine(sizeIndex, span.end) : -1;
                if(sizeIndex >= 0 && textureIndex >= 0)
                {
                    const QStringList size = tokens(decoded(sizeIndex));
                    const QStringList texture = tokens(decoded(textureIndex));
                    bool widthOk = false;
                    bool heightOk = false;
                    if(size.size() >= 2 && texture.size() >= 2)
                    {
                        span.value.maps.sectorWidth = size.at(0).toInt(&widthOk);
                        span.value.maps.sectorHeight = size.at(1).toInt(&heightOk);
                        span.value.maps.minimapTexture = texture.at(0);
                        span.value.maps.objectivesTexture = texture.at(1);
                        span.value.maps.present = widthOk && heightOk;
                    }
                }
            }
        }

        int exitStart = firstExit;
        while(exitStart < span.end)
        {
            const QStringList exitHeader = tokens(decoded(exitStart));
            bool exitIdOk = false;
            SMapTableExit exit;
            exit.exitId = exitHeader.size() >= 2 ? exitHeader.at(1).toInt(&exitIdOk) : 0;
            int exitEnd = span.end;
            for(int index = exitStart + 1; index < span.end; ++index)
            {
                if(trimmedTag(m_lines.at(index).content) == QStringLiteral("#exit"))
                {
                    exitEnd = index;
                    break;
                }
            }
            const int targetIndex = nextValueLine(exitStart, exitEnd);
            if(exitIdOk && targetIndex >= 0)
            {
                const QStringList target = tokens(decoded(targetIndex));
                bool targetExitOk = false;
                if(target.size() >= 2)
                {
                    exit.destinationZoneId = target.at(0);
                    exit.destinationExitId = target.at(1).toInt(&targetExitOk);
                }
                if(targetExitOk)
                {
                    for(int index = targetIndex + 1; index < exitEnd; ++index)
                    {
                        const QString tag = trimmedTag(m_lines.at(index).content);
                        const int valueIndex = nextValueLine(index, exitEnd);
                        if(valueIndex < 0)
                            continue;
                        if(tag == QStringLiteral("#deploy"))
                            exit.hasDeployRectangle = parseRectangle(decoded(valueIndex), &exit.deployRectangle);
                        else if(tag == QStringLiteral("#remove"))
                            exit.hasRemoveRectangle = parseRectangle(decoded(valueIndex), &exit.removeRectangle);
                        else if(tag == QStringLiteral("#passtime"))
                        {
                            bool ok = false;
                            exit.passTime = decoded(valueIndex).trimmed().toDouble(&ok);
                            exit.hasPassTime = ok;
                        }
                        else if(tag == QStringLiteral("#view"))
                        {
                            bool ok = false;
                            exit.viewAngle = decoded(valueIndex).trimmed().toDouble(&ok);
                            exit.hasViewAngle = ok;
                        }
                        else if(tag == QStringLiteral("#deployangle"))
                        {
                            bool ok = false;
                            exit.deployAngle = decoded(valueIndex).trimmed().toDouble(&ok);
                            exit.hasDeployAngle = ok;
                        }
                    }
                    span.value.exits.append(exit);
                }
            }
            exitStart = exitEnd;
        }

        // Human-authored separators and descriptions conventionally precede
        // the #zone they describe. Keep serialization spans unchanged, but
        // present those comment-only preambles with the following zone in Raw
        // Review instead of misleadingly attaching them to the previous one.
        const int reviewStart = reviewStarts.at(spanIndex);
        int reviewEnd = span.end;
        if(spanIndex + 1 < reviewStarts.size())
            reviewEnd = qMin(reviewEnd, reviewStarts.at(spanIndex + 1));
        QByteArray raw;
        for(int index = reviewStart; index < reviewEnd; ++index)
        {
            raw.append(m_lines.at(index).content);
            raw.append(m_lines.at(index).ending);
        }
        QTextCodec* codec = QTextCodec::codecForName("CP1251");
        span.value.rawBlock = codec ? codec->toUnicode(raw) : QString::fromLatin1(raw);
        m_spans.append(span);
        m_zones.append(span.value);
    }
    return errors ? errors->isEmpty() : true;
}

SMapTableEditResult CMapTableDocument::validateZone(
    const SMapTableZone& zone, const QVector<SMapTableZone>& existing)
{
    SMapTableEditResult result;
    if(!tokenIsSafe(zone.zoneId))
        result.errors.append("Zone ID is required and cannot contain whitespace.");
    if(!tokenIsSafe(zone.allodId))
        result.errors.append("Allod ID is required and cannot contain whitespace.");
    if(!tokenIsSafe(zone.zoneType))
        result.errors.append("Zone type is required and cannot contain whitespace.");
    if(zone.mprName.isEmpty() != zone.mobName.isEmpty())
        result.errors.append("MPR and MOB resource names must either both be present or both be absent.");
    if(zone.zoneType.compare("game", Qt::CaseInsensitive) == 0 &&
       (zone.mprName.isEmpty() || zone.mobName.isEmpty()))
        result.errors.append("Game zones require both MPR and MOB resource names.");
    if(!zone.mprName.isEmpty() && (!tokenIsSafe(zone.mprName) || !tokenIsSafe(zone.mobName)))
        result.errors.append("MPR and MOB resource names cannot contain whitespace.");
    if(zone.maps.present)
    {
        if(zone.maps.sectorWidth <= 0 || zone.maps.sectorHeight <= 0)
            result.errors.append("Map sector width and height must be positive.");
        if(!tokenIsSafe(zone.maps.minimapTexture) || !tokenIsSafe(zone.maps.objectivesTexture))
            result.errors.append("Minimap and objectives texture names are required and cannot contain whitespace.");
    }
    QSet<int> exitIds;
    for(const SMapTableExit& exit : zone.exits)
    {
        if(exit.exitId <= 0)
            result.errors.append("Exit IDs must be positive.");
        if(exitIds.contains(exit.exitId))
            result.errors.append(QString("Exit ID %1 is duplicated.").arg(exit.exitId));
        exitIds.insert(exit.exitId);
        if(!tokenIsSafe(exit.destinationZoneId))
            result.errors.append(QString("Exit %1 requires a destination zone token.").arg(exit.exitId));
    }
    for(const SMapTableZone& candidate : existing)
    {
        if(candidate.zoneId.compare(zone.zoneId, Qt::CaseInsensitive) == 0)
            result.errors.append(QString("Zone ID %1 already exists.").arg(zone.zoneId));
    }
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    QStringList values = {zone.zoneId, zone.allodId, zone.zoneType, zone.mprName,
                          zone.mobName, zone.maps.minimapTexture,
                          zone.maps.objectivesTexture};
    for(const SMapTableExit& exit : zone.exits)
        values.append(exit.destinationZoneId);
    for(const QString& value : values)
    {
        if(codec && codec->toUnicode(codec->fromUnicode(value)) != value)
        {
            result.errors.append(QString("Value cannot be represented in CP1251: %1").arg(value));
            break;
        }
    }
    result.success = result.errors.isEmpty();
    return result;
}

SMapTableEditResult CMapTableDocument::replaceZone(
    const QString& originalZoneId, const SMapTableZone& replacement)
{
    SMapTableEditResult result;
    int spanIndex = -1;
    QVector<SMapTableZone> otherZones;
    for(int index = 0; index < m_spans.size(); ++index)
    {
        if(m_spans.at(index).value.zoneId.compare(originalZoneId, Qt::CaseInsensitive) == 0)
        {
            if(spanIndex >= 0)
            {
                result.errors.append(QString("Zone ID %1 is ambiguous.").arg(originalZoneId));
                return result;
            }
            spanIndex = index;
        }
        else
            otherZones.append(m_spans.at(index).value);
    }
    if(spanIndex < 0)
    {
        result.errors.append(QString("Zone ID %1 was not found.").arg(originalZoneId));
        return result;
    }
    result = validateZone(replacement, otherZones);
    if(!result.success)
        return result;

    const SMapTableZone original = m_spans.at(spanIndex).value;
    const bool headerChanged = original.zoneId != replacement.zoneId ||
        original.allodId != replacement.allodId || original.zoneType != replacement.zoneType;
    const bool resourcesChanged = original.mprName != replacement.mprName ||
        original.mobName != replacement.mobName;
    const bool mapsChanged = original.maps != replacement.maps;
    const bool exitsChanged = original.exits != replacement.exits;
    if(!headerChanged && !resourcesChanged && !mapsChanged && !exitsChanged)
    {
        result.success = true;
        return result;
    }

    const SZoneSpan sourceSpan = m_spans.at(spanIndex);
    QVector<SLine> block;
    for(int index = sourceSpan.start; index < sourceSpan.end; ++index)
        block.append(m_lines.at(index));

    const auto tagAt = [&block](int index) {
        return index >= 0 && index < block.size() ? trimmedTag(block.at(index).content) : QString();
    };
    const auto findTag = [&block, &tagAt](const QString& tag, int begin, int end) {
        const int limit = qMin(end, block.size());
        for(int index = qMax(0, begin); index < limit; ++index)
        {
            if(tagAt(index) == tag)
                return index;
        }
        return -1;
    };
    const auto nextValue = [this, &block](int after, int end) {
        const int limit = qMin(end, block.size());
        for(int index = after + 1; index < limit; ++index)
        {
            const QString text = decodeLine(block.at(index).content).trimmed();
            if(text.isEmpty() || text.startsWith("##"))
                continue;
            return text.startsWith('#') ? -1 : index;
        }
        return -1;
    };
    const auto encodedLine = [this, &result](const QString& value) {
        bool ok = false;
        const QByteArray encoded = encodeLine(value, &ok);
        if(!ok)
            result.errors.append(QString("Value cannot be represented in CP1251: %1").arg(value));
        return encoded;
    };
    const auto makeLine = [this, &encodedLine](const QString& value) {
        SLine line;
        line.content = encodedLine(value);
        line.ending = preferredEnding();
        return line;
    };
    const auto eraseSection = [&block, &nextValue](int tagIndex, int valueCount, int end) {
        QVector<int> remove;
        remove.append(tagIndex);
        int cursor = tagIndex;
        for(int count = 0; count < valueCount; ++count)
        {
            cursor = nextValue(cursor, end);
            if(cursor >= 0)
                remove.append(cursor);
        }
        std::sort(remove.begin(), remove.end(), std::greater<int>());
        for(int index : remove)
            block.removeAt(index);
    };
    const auto setSection = [&block, &findTag, &nextValue, &makeLine, &eraseSection](
        const QString& tag, const QStringList& values, int begin, int end, int insertAt,
        int oldValueCount) {
        int tagIndex = findTag(tag, begin, end);
        if(values.isEmpty())
        {
            if(tagIndex >= 0)
                eraseSection(tagIndex, oldValueCount, end);
            return;
        }
        if(tagIndex < 0)
        {
            int cursor = qBound(0, insertAt, block.size());
            block.insert(cursor++, makeLine(tag));
            for(const QString& value : values)
                block.insert(cursor++, makeLine(value));
            return;
        }
        int cursor = tagIndex;
        for(const QString& value : values)
        {
            const int valueIndex = nextValue(cursor, block.size());
            if(valueIndex < 0)
            {
                block.insert(cursor + 1, makeLine(value));
                ++cursor;
            }
            else
            {
                block[valueIndex].content = makeLine(value).content;
                cursor = valueIndex;
            }
        }
    };

    if(headerChanged)
    {
        block[0].content = encodedLine(QStringLiteral("#zone %1 %2 %3")
            .arg(replacement.zoneId, replacement.allodId, replacement.zoneType));
        result.changedFields.append("zone header");
    }

    const auto firstExit = [&block, &tagAt]() {
        for(int index = 1; index < block.size(); ++index)
        {
            if(tagAt(index) == QStringLiteral("#exit"))
                return index;
        }
        return block.size();
    };
    if(resourcesChanged)
    {
        QStringList values;
        if(!replacement.mprName.isEmpty())
            values.append(replacement.mprName + ' ' + replacement.mobName);
        setSection(QStringLiteral("#res"), values, 1, firstExit(), firstExit(), 1);
        result.changedFields.append("map resources");
    }
    if(mapsChanged)
    {
        QStringList values;
        if(replacement.maps.present)
        {
            values.append(QStringLiteral("%1 %2").arg(replacement.maps.sectorWidth)
                .arg(replacement.maps.sectorHeight));
            values.append(replacement.maps.minimapTexture + ' ' +
                          replacement.maps.objectivesTexture);
        }
        setSection(QStringLiteral("#maps"), values, 1, firstExit(), firstExit(), 2);
        result.changedFields.append("minimap metadata");
    }

    if(exitsChanged)
    {
        QMap<int, SMapTableExit> requested;
        for(const SMapTableExit& exit : replacement.exits)
            requested.insert(exit.exitId, exit);

        for(int oldIndex = original.exits.size() - 1; oldIndex >= 0; --oldIndex)
        {
            const SMapTableExit oldExit = original.exits.at(oldIndex);
            int exitStart = -1;
            int exitEnd = block.size();
            for(int index = 1; index < block.size(); ++index)
            {
                if(tagAt(index) != QStringLiteral("#exit"))
                    continue;
                const QStringList header = tokens(decodeLine(block.at(index).content));
                bool ok = false;
                const int id = header.size() >= 2 ? header.at(1).toInt(&ok) : 0;
                if(ok && id == oldExit.exitId)
                {
                    exitStart = index;
                    for(int next = index + 1; next < block.size(); ++next)
                    {
                        if(tagAt(next) == QStringLiteral("#exit"))
                        {
                            exitEnd = next;
                            break;
                        }
                    }
                    break;
                }
            }
            if(exitStart < 0)
                continue;
            if(!requested.contains(oldExit.exitId))
            {
                for(int index = exitEnd - 1; index >= exitStart; --index)
                    block.removeAt(index);
                result.changedFields.append(QString("removed exit %1").arg(oldExit.exitId));
                continue;
            }
            const SMapTableExit wanted = requested.take(oldExit.exitId);
            if(oldExit == wanted)
                continue;

            int targetIndex = nextValue(exitStart, exitEnd);
            if(targetIndex < 0)
            {
                block.insert(exitStart + 1, makeLine(wanted.destinationZoneId + ' ' +
                    QString::number(wanted.destinationExitId)));
            }
            else
            {
                block[targetIndex].content = encodedLine(wanted.destinationZoneId + ' ' +
                    QString::number(wanted.destinationExitId));
            }
            exitEnd = block.size();
            for(int next = exitStart + 1; next < block.size(); ++next)
            {
                if(tagAt(next) == QStringLiteral("#exit"))
                {
                    exitEnd = next;
                    break;
                }
            }
            const auto optional = [&setSection, exitStart, &exitEnd](
                const QString& tag, bool present, const QString& value) {
                setSection(tag, present ? QStringList(value) : QStringList(),
                           exitStart + 1, exitEnd, exitEnd, 1);
            };
            optional(QStringLiteral("#deploy"), wanted.hasDeployRectangle,
                     rectangleText(wanted.deployRectangle));
            optional(QStringLiteral("#remove"), wanted.hasRemoveRectangle,
                     rectangleText(wanted.removeRectangle));
            optional(QStringLiteral("#passtime"), wanted.hasPassTime, number(wanted.passTime));
            optional(QStringLiteral("#view"), wanted.hasViewAngle, number(wanted.viewAngle));
            optional(QStringLiteral("#deployangle"), wanted.hasDeployAngle,
                     number(wanted.deployAngle));
            result.changedFields.append(QString("exit %1").arg(wanted.exitId));
        }

        for(const SMapTableExit& wanted : replacement.exits)
        {
            if(!requested.contains(wanted.exitId))
                continue;
            if(!block.isEmpty() && block.last().ending.isEmpty())
                block.last().ending = preferredEnding();
            block.append(makeLine(QStringLiteral("#exit %1").arg(wanted.exitId)));
            block.append(makeLine(wanted.destinationZoneId + ' ' +
                                  QString::number(wanted.destinationExitId)));
            if(wanted.hasDeployRectangle)
            {
                block.append(makeLine("#deploy"));
                block.append(makeLine(rectangleText(wanted.deployRectangle)));
            }
            if(wanted.hasRemoveRectangle)
            {
                block.append(makeLine("#remove"));
                block.append(makeLine(rectangleText(wanted.removeRectangle)));
            }
            if(wanted.hasPassTime)
            {
                block.append(makeLine("#passtime"));
                block.append(makeLine(number(wanted.passTime)));
            }
            if(wanted.hasViewAngle)
            {
                block.append(makeLine("#view"));
                block.append(makeLine(number(wanted.viewAngle)));
            }
            if(wanted.hasDeployAngle)
            {
                block.append(makeLine("#deployangle"));
                block.append(makeLine(number(wanted.deployAngle)));
            }
            result.changedFields.append(QString("added exit %1").arg(wanted.exitId));
        }
    }

    if(!result.errors.isEmpty())
    {
        result.success = false;
        return result;
    }
    const QVector<SLine> previousLines = m_lines;
    const bool previousDirty = m_dirty;
    for(int index = sourceSpan.end - 1; index >= sourceSpan.start; --index)
        m_lines.removeAt(index);
    for(int index = 0; index < block.size(); ++index)
        m_lines.insert(sourceSpan.start + index, block.at(index));
    m_dirty = true;
    QStringList parseErrors;
    if(!reparse(&parseErrors))
    {
        m_lines = previousLines;
        m_dirty = previousDirty;
        reparse(nullptr);
        result.errors.append(parseErrors);
        result.success = false;
        return result;
    }
    result.success = true;
    return result;
}

SMapTableEditResult CMapTableDocument::appendZone(const SMapTableZone& zone)
{
    SMapTableEditResult result = validateZone(zone, m_zones);
    if(!result.success)
        return result;

    QVector<SLine> added;
    const auto append = [this, &added, &result](const QString& value) {
        bool ok = false;
        SLine line;
        line.content = encodeLine(value, &ok);
        line.ending = preferredEnding();
        if(!ok)
            result.errors.append(QString("Value cannot be represented in CP1251: %1").arg(value));
        added.append(line);
    };
    append(QStringLiteral("#zone %1 %2 %3").arg(zone.zoneId, zone.allodId, zone.zoneType));
    if(!zone.mprName.isEmpty())
    {
        append("#res");
        append(zone.mprName + ' ' + zone.mobName);
    }
    if(zone.maps.present)
    {
        append("#maps");
        append(QStringLiteral("%1 %2").arg(zone.maps.sectorWidth).arg(zone.maps.sectorHeight));
        append(zone.maps.minimapTexture + ' ' + zone.maps.objectivesTexture);
    }
    for(const SMapTableExit& exit : zone.exits)
    {
        append(QStringLiteral("#exit %1").arg(exit.exitId));
        append(exit.destinationZoneId + ' ' + QString::number(exit.destinationExitId));
        if(exit.hasDeployRectangle) { append("#deploy"); append(rectangleText(exit.deployRectangle)); }
        if(exit.hasRemoveRectangle) { append("#remove"); append(rectangleText(exit.removeRectangle)); }
        if(exit.hasPassTime) { append("#passtime"); append(number(exit.passTime)); }
        if(exit.hasViewAngle) { append("#view"); append(number(exit.viewAngle)); }
        if(exit.hasDeployAngle) { append("#deployangle"); append(number(exit.deployAngle)); }
    }
    if(!result.errors.isEmpty())
    {
        result.success = false;
        return result;
    }

    int insertion = m_lines.size();
    for(int index = 0; index < m_lines.size(); ++index)
    {
        if(trimmedTag(m_lines.at(index).content) == QStringLiteral("#quest"))
        {
            insertion = index;
            break;
        }
    }
    const QVector<SLine> previousLines = m_lines;
    const bool previousDirty = m_dirty;
    if(insertion > 0 && m_lines.at(insertion - 1).ending.isEmpty())
        m_lines[insertion - 1].ending = preferredEnding();
    for(int index = 0; index < added.size(); ++index)
        m_lines.insert(insertion + index, added.at(index));
    m_dirty = true;
    QStringList parseErrors;
    if(!reparse(&parseErrors))
    {
        m_lines = previousLines;
        m_dirty = previousDirty;
        reparse(nullptr);
        result.errors.append(parseErrors);
        result.success = false;
        return result;
    }
    result.changedFields.append(QString("added zone %1").arg(zone.zoneId));
    result.warnings.append("New registrations include only fields explicitly supplied in the editor; review profile-specific weather, sky, camera, figure, cage, and LOD metadata before release.");
    result.success = true;
    return result;
}
