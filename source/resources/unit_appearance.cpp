#include "unit_appearance.h"

#include <QSet>
#include <QTextCodec>

namespace {

quint8 byteAt(const QByteArray& data, int offset)
{
    return quint8(uchar(data.at(offset)));
}

quint32 littleEndianDword(const QByteArray& data, int offset)
{
    return quint32(byteAt(data, offset))
            | (quint32(byteAt(data, offset + 1)) << 8)
            | (quint32(byteAt(data, offset + 2)) << 16)
            | (quint32(byteAt(data, offset + 3)) << 24);
}

bool readStringField(const QByteArray& data, int& offset, quint8 expectedField,
                     QString& value)
{
    if(offset + 2 > data.size() || byteAt(data, offset) != expectedField)
        return false;

    const quint8 firstLengthByte = byteAt(data, offset + 1);
    int lengthBytes = 1;
    quint32 encodedLength = firstLengthByte;
    if(firstLengthByte & 1)
    {
        if(offset + 5 > data.size())
            return false;
        encodedLength = littleEndianDword(data, offset + 1);
        lengthBytes = 4;
    }
    const quint32 byteLength = encodedLength >> 1;
    if(byteLength < 1 || byteLength > quint32(data.size())
            || quint32(offset + 1 + lengthBytes) + byteLength > quint32(data.size()))
        return false;

    QByteArray encoded = data.mid(offset + 1 + lengthBytes, int(byteLength));
    if(encoded.endsWith('\0'))
        encoded.chop(1);
    if(encoded.contains('\0'))
        return false;

    QTextCodec* cp1251 = QTextCodec::codecForName("CP1251");
    value = cp1251->toUnicode(encoded);
    offset += 1 + lengthBytes + int(byteLength);
    return !value.isEmpty();
}

}

namespace ei {

QMap<QString, int> unitPrototypeSkinIndices(const QByteArray& unitsDatabase)
{
    QMap<QString, int> result;

    // units.udb is a tagged tree. Monster rows are sections whose first fields
    // are: 0=name (string), 1=base race (string), 2=unknown (dword), and
    // 3=skin index (dword). Scanning for that complete typed prefix avoids
    // depending on unrelated or still-undocumented fields later in each row.
    for(int section = 0; section + 18 < unitsDatabase.size(); ++section)
    {
        if(byteAt(unitsDatabase, section) != 1)
            continue;
        const quint32 encodedSectionLength = littleEndianDword(unitsDatabase, section + 1);
        if(!(encodedSectionLength & 1) || (encodedSectionLength >> 1) < 16)
            continue;

        int field = section + 5;
        QString prototype;
        QString baseRace;
        if(!readStringField(unitsDatabase, field, 0, prototype)
                || !readStringField(unitsDatabase, field, 1, baseRace))
            continue;
        if(field + 12 > unitsDatabase.size()
                || byteAt(unitsDatabase, field) != 2
                || byteAt(unitsDatabase, field + 1) != 8
                || byteAt(unitsDatabase, field + 6) != 3
                || byteAt(unitsDatabase, field + 7) != 8)
            continue;

        const qint32 skinIndex = qint32(littleEndianDword(unitsDatabase, field + 8));
        if(skinIndex < 0 || skinIndex > 999)
            continue;
        result.insert(prototype.toLower(), skinIndex);
    }
    return result;
}

QString humanoidTextureResourceName(const QMap<QString, int>& skinIndices,
                                    const QString& prototypeName,
                                    const QString& modelName)
{
    QString model = modelName.trimmed().toLower();
    if(model.endsWith(".mod"))
        model.chop(4);
    static const QSet<QString> humanoidModels = {
        "unhuma", "unhufe", "unorma", "unorfe"
    };
    if(!humanoidModels.contains(model))
        return QString();

    const auto found = skinIndices.constFind(prototypeName.trimmed().toLower());
    if(found == skinIndices.constEnd())
        return QString();
    return QString("%1skin_%2.mmp").arg(model).arg(found.value(), 2, 10, QChar('0'));
}

}
