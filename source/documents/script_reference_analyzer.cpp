#include "script_reference_analyzer.h"

#include <QSet>
#include <algorithm>
#include <climits>

namespace {
bool isIdentifierStart(QChar character)
{
    return character == QLatin1Char('_') || character.isLetter();
}

bool isIdentifierPart(QChar character)
{
    return character == QLatin1Char('_') || character.isLetterOrNumber();
}

void skipQuotedText(const QString& script, int* position)
{
    const QChar quote = script.at(*position);
    ++*position;
    while(*position < script.size())
    {
        if(script.at(*position) == QLatin1Char('\\'))
        {
            *position += qMin(2, script.size() - *position);
            continue;
        }
        if(script.at(*position) == quote)
        {
            ++*position;
            return;
        }
        ++*position;
    }
}

void skipTrivia(const QString& script, int* position)
{
    for(;;)
    {
        while(*position < script.size() && script.at(*position).isSpace())
            ++*position;
        if(*position + 1 >= script.size() || script.at(*position) != QLatin1Char('/'))
            return;
        if(script.at(*position + 1) == QLatin1Char('/'))
        {
            *position += 2;
            while(*position < script.size() && script.at(*position) != QLatin1Char('\n'))
                ++*position;
            continue;
        }
        if(script.at(*position + 1) == QLatin1Char('*'))
        {
            *position += 2;
            while(*position + 1 < script.size() &&
                  !(script.at(*position) == QLatin1Char('*') &&
                    script.at(*position + 1) == QLatin1Char('/')))
                ++*position;
            if(*position + 1 < script.size())
                *position += 2;
            continue;
        }
        return;
    }
}

bool parseLookup(const QString& script, int afterName, bool stringId,
                 uint* objectId, int* idPosition, int* afterCall)
{
    int position = afterName;
    skipTrivia(script, &position);
    if(position >= script.size() || script.at(position) != QLatin1Char('('))
        return false;
    ++position;
    skipTrivia(script, &position);

    QChar quote;
    if(stringId)
    {
        if(position >= script.size() ||
           (script.at(position) != QLatin1Char('\'') &&
            script.at(position) != QLatin1Char('"')))
            return false;
        quote = script.at(position++);
    }

    const int digitsBegin = position;
    while(position < script.size() && script.at(position).isDigit())
        ++position;
    if(position == digitsBegin)
        return false;
    const int digitsEnd = position;

    if(stringId)
    {
        if(position >= script.size() || script.at(position) != quote)
            return false;
        ++position;
    }
    else if(position + 1 < script.size() && script.at(position) == QLatin1Char('.') &&
            script.at(position + 1) == QLatin1Char('0'))
    {
        ++position;
        while(position < script.size() && script.at(position) == QLatin1Char('0'))
            ++position;
    }

    skipTrivia(script, &position);
    if(position >= script.size() || script.at(position) != QLatin1Char(')'))
        return false;

    bool valid = false;
    const qulonglong value = script.mid(digitsBegin, digitsEnd - digitsBegin).toULongLong(&valid);
    if(!valid || value > UINT_MAX)
        return false;
    *objectId = uint(value);
    *idPosition = digitsBegin;
    *afterCall = position + 1;
    return true;
}

SMobScriptReference makeReference(const QString& script, uint objectId, int idPosition)
{
    SMobScriptReference reference;
    reference.objectId = objectId;
    reference.line = script.left(idPosition).count(QLatin1Char('\n')) + 1;
    const int lineStart = script.lastIndexOf(QLatin1Char('\n'), idPosition - 1) + 1;
    reference.column = idPosition - lineStart + 1;
    int lineEnd = script.indexOf(QLatin1Char('\n'), idPosition);
    if(lineEnd < 0)
        lineEnd = script.size();
    reference.context = script.mid(lineStart, lineEnd - lineStart).trimmed();
    if(reference.context.size() > 180)
        reference.context = reference.context.left(177) + "...";
    return reference;
}
}

QVector<SMobScriptReference> CScriptReferenceAnalyzer::findObjectIds(
    const QString& script)
{
    QVector<SMobScriptReference> references;
    if(script.isEmpty())
        return references;

    int position = 0;
    while(position < script.size())
    {
        if(script.at(position) == QLatin1Char('/') && position + 1 < script.size() &&
           (script.at(position + 1) == QLatin1Char('/') ||
            script.at(position + 1) == QLatin1Char('*')))
        {
            skipTrivia(script, &position);
            continue;
        }
        if(script.at(position) == QLatin1Char('\'') || script.at(position) == QLatin1Char('"'))
        {
            skipQuotedText(script, &position);
            continue;
        }
        if(!isIdentifierStart(script.at(position)))
        {
            ++position;
            continue;
        }

        const int nameBegin = position++;
        while(position < script.size() && isIdentifierPart(script.at(position)))
            ++position;
        const QString name = script.mid(nameBegin, position - nameBegin);
        const bool getObject = name.compare("GetObject", Qt::CaseInsensitive) == 0;
        const bool getObjectById = name.compare("GetObjectByID", Qt::CaseInsensitive) == 0;
        if(!getObject && !getObjectById)
            continue;

        uint objectId = 0;
        int idPosition = 0;
        int afterCall = position;
        if(parseLookup(script, position, getObjectById, &objectId, &idPosition, &afterCall))
        {
            references.append(makeReference(script, objectId, idPosition));
            position = afterCall;
        }
    }

    std::sort(references.begin(), references.end(),
        [](const SMobScriptReference& left, const SMobScriptReference& right) {
            if(left.objectId != right.objectId)
                return left.objectId < right.objectId;
            if(left.line != right.line)
                return left.line < right.line;
            return left.column < right.column;
        });
    return references;
}

QVector<SMobScriptReference> CScriptReferenceAnalyzer::findObjectIds(
    const QString& script, const QVector<uint>& objectIds)
{
    if(objectIds.isEmpty()) return {};
    QSet<uint> requested;
    for(uint id:objectIds) requested.insert(id);
    QVector<SMobScriptReference> result;
    for(const auto& reference:findObjectIds(script))
        if(requested.contains(reference.objectId)) result.append(reference);
    return result;
}
