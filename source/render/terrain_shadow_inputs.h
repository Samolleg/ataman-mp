#pragma once
#include <QByteArray>
#include <cstring>

// SEC geometry is a 33x33 block of eight-byte vertices after the five-byte
// header. Packed texture IDs/materials do not change sun visibility. Retain
// conservative invalidation for unknown data, geometry and overlay coverage.
inline bool terrainShadowInputsChanged(const QByteArray& before,
                                      const QByteArray& after)
{
    const int vertices = 33 * 33 * 8;
    const int tiles = 16 * 16 * 2;
    if(before.size() < 5 || after.size() < 5)
        return true;
    const unsigned char type = static_cast<unsigned char>(after.at(4));
    if((type != 0 && type != 3) || before.at(4) != after.at(4))
        return true;
    const int required = 5 + (type == 3 ? 2 : 1) * vertices
        + (type == 3 ? 3 : 1) * tiles;
    if(before.size() < required || after.size() < required
            || std::memcmp(before.constData(), after.constData(), 5 + vertices))
        return true;
    if(type == 3)
    {
        const int offset = 5 + 2 * vertices + tiles;
        for(int tile = 0; tile < tiles; tile += 2)
        {
            const auto missing = [offset, tile](const QByteArray& bytes) {
                return static_cast<unsigned char>(bytes.at(offset + tile)) == 255
                    && static_cast<unsigned char>(bytes.at(offset + tile + 1)) == 255;
            };
            if(missing(before) != missing(after))
                return true;
        }
    }
    return false;
}
