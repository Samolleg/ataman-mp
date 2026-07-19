#include "mmp_codec.h"

#include <QtEndian>

namespace ei {

bool decodePnt3(const QByteArray& compressed, int decodedSize,
                QByteArray* decoded, QString* diagnostic)
{
    if(!decoded || decodedSize < 0)
    {
        if(diagnostic) *diagnostic = "Invalid PNT3 output request.";
        return false;
    }
    decoded->clear();
    decoded->reserve(decodedSize);
    int cursor = 0;
    int literalStart = 0;
    while(cursor + int(sizeof(quint32)) <= compressed.size())
    {
        const quint32 value = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(compressed.constData() + cursor));
        cursor += int(sizeof(quint32));
        // Corpus run sizes are 16-byte aligned (the FIG/MMP pipeline works in
        // four-pixel blocks). Low-alpha BGRA literals can otherwise look like
        // small run counts; keep non-aligned values as literal image data.
        if(value > 1000000u || value == 0u || (value & 0x0fu) != 0u)
            continue;

        const int literalEnd = cursor - int(sizeof(quint32));
        const int literalSize = literalEnd - literalStart;
        if(literalEnd < literalStart || decoded->size() > decodedSize - literalSize)
        {
            if(diagnostic) *diagnostic = "PNT3 literal data exceeds the decoded image bounds.";
            decoded->clear();
            return false;
        }
        // A BGRA literal dword can numerically resemble a run marker. It is a
        // marker only when the resulting run fits the remaining image.
        if(value > quint32(decodedSize - decoded->size() - literalSize))
            continue;
        decoded->append(compressed.constData() + literalStart, literalSize);
        decoded->append(QByteArray(int(value), '\0'));
        literalStart = cursor;
    }
    if(cursor != compressed.size())
    {
        if(diagnostic) *diagnostic = "PNT3 payload is not dword-aligned.";
        decoded->clear();
        return false;
    }
    if(decoded->size() > decodedSize - (cursor - literalStart))
    {
        if(diagnostic) *diagnostic = "PNT3 literal data exceeds the decoded image bounds.";
        decoded->clear();
        return false;
    }
    decoded->append(compressed.constData() + literalStart, cursor - literalStart);
    decoded->append(QByteArray(decodedSize - decoded->size(), '\0'));
    return true;
}

}
