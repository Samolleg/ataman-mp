#include "ai_graph_document.h"

#include <QHash>
#include <QQueue>
#include <QtEndian>

namespace {
constexpr quint64 HeaderSize = 8;
constexpr quint64 WeightBytesPerCell = 16;
constexpr quint64 PositionBytesPerCell = 1;
constexpr quint64 ComponentBytesPerCell = 2;
constexpr quint64 BytesPerCellAllLayers =
    CAiGraphDocument::LayerCount
    * (WeightBytesPerCell + PositionBytesPerCell + ComponentBytesPerCell);

quint16 readLe16(const char* bytes)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(bytes));
}

quint32 readLe32(const char* bytes)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes));
}

void appendLe16(QByteArray* bytes, quint16 value)
{
    char encoded[2];
    qToLittleEndian<quint16>(value, reinterpret_cast<uchar*>(encoded));
    bytes->append(encoded, sizeof(encoded));
}

void appendLe32(QByteArray* bytes, quint32 value)
{
    char encoded[4];
    qToLittleEndian<quint32>(value, reinterpret_cast<uchar*>(encoded));
    bytes->append(encoded, sizeof(encoded));
}
}

void CAiGraphDocument::clear()
{
    m_width = 0;
    m_height = 0;
    m_valid = false;
    m_rawBytes.clear();
    m_layers.clear();
}

bool CAiGraphDocument::parse(const QByteArray& data, QString* error,
                             QStringList* warnings)
{
    clear();
    if(error)
        error->clear();
    if(warnings)
        warnings->clear();
    const auto fail = [&](const QString& message)
    {
        if(error)
            *error = message;
        clear();
        return false;
    };

    if(data.size() < qint64(HeaderSize))
        return fail(QStringLiteral("AI graph is shorter than its 8-byte header."));

    const qint32 width = qint32(readLe32(data.constData()));
    const qint32 height = qint32(readLe32(data.constData() + 4));
    if(width <= 0 || height <= 0 || width > 256 || height > 256)
        return fail(QStringLiteral("AI graph dimensions %1 x %2 are outside the supported 1..256 range.")
                    .arg(width).arg(height));

    const quint64 cells = quint64(width) * quint64(height);
    const quint64 requiredSize = HeaderSize + cells * BytesPerCellAllLayers;
    if(quint64(data.size()) < requiredSize)
        return fail(QStringLiteral("AI graph is truncated: expected at least %1 bytes, found %2.")
                    .arg(requiredSize).arg(data.size()));
    if(quint64(data.size()) > requiredSize && warnings)
        warnings->append(QStringLiteral("AI graph has %1 preserved trailing byte(s).")
                         .arg(quint64(data.size()) - requiredSize));

    m_width = width;
    m_height = height;
    m_rawBytes = data;
    m_layers.resize(LayerCount);
    quint64 offset = HeaderSize;
    int invalidLocalPositions = 0;
    for(int layer = 0; layer < LayerCount; ++layer)
    {
        SAiGraphLayer& destination = m_layers[layer];
        destination.weights.resize(int(cells) * DirectionCount);
        destination.packedPositions.resize(int(cells));
        destination.components.resize(int(cells));
        for(int y = 0; y < height; ++y)
        {
            for(int x = 0; x < width; ++x)
                for(int direction = 0; direction < DirectionCount; ++direction)
                {
                    const int cell = y * width + x;
                    destination.weights[cell * DirectionCount + direction] =
                        qint16(readLe16(data.constData() + offset));
                    offset += 2;
                }

            for(int x = 0; x < width; ++x)
            {
                const int cell = y * width + x;
                const quint8 packed = quint8(data.at(int(offset++)));
                destination.packedPositions[cell] = char(packed);
                if((packed & 0x0f) > 7 || ((packed >> 4) & 0x0f) > 7)
                    ++invalidLocalPositions;
            }

            for(int x = 0; x < width; ++x)
            {
                const int cell = y * width + x;
                destination.components[cell] = readLe16(data.constData() + offset);
                offset += 2;
            }
        }
    }
    if(invalidLocalPositions > 0 && warnings)
        warnings->append(QStringLiteral("AI graph contains %1 local position(s) outside the expected 0..7 cell range.")
                         .arg(invalidLocalPositions));
    m_valid = true;
    return true;
}

bool CAiGraphDocument::initializePreview(int width, int height,
                                         const QVector<SAiGraphLayer>& layers,
                                         QString* error)
{
    clear();
    if(error)
        error->clear();
    if(width <= 0 || height <= 0 || width > 256 || height > 256
       || layers.size() != LayerCount)
    {
        if(error)
            *error = QStringLiteral("Invalid decoded AI graph dimensions or layer count.");
        return false;
    }
    const int cells = width * height;
    for(const SAiGraphLayer& layer : layers)
        if(layer.weights.size() != cells * DirectionCount
           || layer.packedPositions.size() != cells
           || layer.components.size() != cells)
        {
            if(error)
                *error = QStringLiteral("Decoded AI graph layer sizes are inconsistent.");
            return false;
        }
    m_width = width;
    m_height = height;
    m_layers = layers;
    m_valid = true;
    return true;
}

QByteArray CAiGraphDocument::serialize(QString* error) const
{
    if(error)
        error->clear();
    if(!m_valid || m_width <= 0 || m_height <= 0
       || m_layers.size() != LayerCount)
    {
        if(error)
            *error = QStringLiteral("AI graph document is not serializable.");
        return QByteArray();
    }

    const int cells = m_width * m_height;
    QByteArray bytes;
    bytes.reserve(int(HeaderSize + quint64(cells) * BytesPerCellAllLayers));
    appendLe32(&bytes, quint32(m_width));
    appendLe32(&bytes, quint32(m_height));
    for(const SAiGraphLayer& layer : m_layers)
    {
        if(layer.weights.size() != cells * DirectionCount
           || layer.packedPositions.size() != cells
           || layer.components.size() != cells)
        {
            if(error)
                *error = QStringLiteral("AI graph layer sizes are inconsistent.");
            return QByteArray();
        }
        for(int y = 0; y < m_height; ++y)
        {
            for(int x = 0; x < m_width; ++x)
            {
                const int cell = y * m_width + x;
                for(int direction = 0; direction < DirectionCount; ++direction)
                    appendLe16(&bytes, quint16(layer.weights.at(
                        cell * DirectionCount + direction)));
            }
            for(int x = 0; x < m_width; ++x)
                bytes.append(layer.packedPositions.at(y * m_width + x));
            for(int x = 0; x < m_width; ++x)
                appendLe16(&bytes, layer.components.at(y * m_width + x));
        }
    }
    return bytes;
}

bool CAiGraphDocument::validateRuntimeCompatibility(
    int expectedWidth, int expectedHeight, QString* error) const
{
    if(error)
        error->clear();
    const auto fail = [error](const QString& message) {
        if(error)
            *error = message;
        return false;
    };
    if(!m_valid || m_width != expectedWidth || m_height != expectedHeight
       || m_layers.size() != LayerCount)
        return fail(QStringLiteral("AI graph dimensions do not match the staged map."));

    const int cells = m_width * m_height;
    for(int layerIndex = 0; layerIndex < LayerCount; ++layerIndex)
    {
        const SAiGraphLayer& layer = m_layers.at(layerIndex);
        if(layer.weights.size() != cells * DirectionCount
           || layer.packedPositions.size() != cells
           || layer.components.size() != cells)
            return fail(QStringLiteral("AI graph layer %1 has inconsistent plane sizes.")
                        .arg(layerIndex));
        for(int cell = 0; cell < cells; ++cell)
        {
            const quint8 packed = quint8(layer.packedPositions.at(cell));
            if((packed & 0x0f) > 7 || ((packed >> 4) & 0x0f) > 7)
                return fail(QStringLiteral("AI graph layer %1 cell %2 has an invalid local position.")
                            .arg(layerIndex).arg(cell));
            if(layer.components.at(cell) == 0)
                return fail(QStringLiteral("AI graph layer %1 cell %2 has no component ID.")
                            .arg(layerIndex).arg(cell));
            const int x = cell % m_width;
            const int y = cell / m_width;
            for(int direction = 0; direction < DirectionCount; ++direction)
            {
                const qint16 weight = layer.weights.at(
                    cell * DirectionCount + direction);
                if(weight == -1)
                    continue;
                if(weight < 0)
                    return fail(QStringLiteral("AI graph layer %1 cell %2 has an unsupported negative weight.")
                                .arg(layerIndex).arg(cell));
                const QPoint delta = directionDelta(direction);
                const int nx = x + delta.x(), ny = y + delta.y();
                if(nx < 0 || ny < 0 || nx >= m_width || ny >= m_height)
                    return fail(QStringLiteral("AI graph layer %1 links outside the map at cell %2.")
                                .arg(layerIndex).arg(cell));
                int opposite = -1;
                for(int candidate = 0; candidate < DirectionCount; ++candidate)
                    if(directionDelta(candidate) == -delta)
                    { opposite = candidate; break; }
                const int neighbor = ny * m_width + nx;
                if(opposite < 0 || layer.weights.at(
                       neighbor * DirectionCount + opposite) != weight)
                    return fail(QStringLiteral("AI graph layer %1 has a non-reciprocal edge at cell %2.")
                                .arg(layerIndex).arg(cell));
                if(layer.components.at(neighbor) != layer.components.at(cell))
                    return fail(QStringLiteral("AI graph layer %1 links different components at cell %2.")
                                .arg(layerIndex).arg(cell));
            }
        }

        QVector<bool> visited(cells, false);
        QHash<quint16, int> componentRegion;
        int region = 0;
        for(int start = 0; start < cells; ++start)
        {
            if(visited.at(start))
                continue;
            const quint16 component = layer.components.at(start);
            if(componentRegion.contains(component))
                return fail(QStringLiteral("AI graph layer %1 reuses component %2 for disconnected regions.")
                            .arg(layerIndex).arg(component));
            componentRegion.insert(component, region++);
            QQueue<int> pending;
            pending.enqueue(start);
            visited[start] = true;
            while(!pending.isEmpty())
            {
                const int cell = pending.dequeue();
                const int x = cell % m_width, y = cell / m_width;
                for(int direction = 0; direction < DirectionCount; ++direction)
                {
                    if(layer.weights.at(cell * DirectionCount + direction) < 0)
                        continue;
                    const QPoint delta = directionDelta(direction);
                    const int neighbor = (y + delta.y()) * m_width
                        + x + delta.x();
                    if(!visited.at(neighbor))
                    {
                        visited[neighbor] = true;
                        pending.enqueue(neighbor);
                    }
                }
            }
        }
    }
    return true;
}

int CAiGraphDocument::index(int x, int y) const
{
    return y * m_width + x;
}

bool CAiGraphDocument::validCell(int layer, int x, int y) const
{
    return m_valid && layer >= 0 && layer < m_layers.size()
        && x >= 0 && x < m_width && y >= 0 && y < m_height;
}

qint16 CAiGraphDocument::weight(int layer, int x, int y, int direction) const
{
    if(!validCell(layer, x, y) || direction < 0 || direction >= DirectionCount)
        return qint16(-1);
    return m_layers[layer].weights[index(x, y) * DirectionCount + direction];
}

quint8 CAiGraphDocument::packedPosition(int layer, int x, int y) const
{
    if(!validCell(layer, x, y))
        return 0;
    return quint8(m_layers[layer].packedPositions.at(index(x, y)));
}

int CAiGraphDocument::localX(int layer, int x, int y) const
{
    return packedPosition(layer, x, y) & 0x0f;
}

int CAiGraphDocument::localY(int layer, int x, int y) const
{
    return (packedPosition(layer, x, y) >> 4) & 0x0f;
}

quint16 CAiGraphDocument::component(int layer, int x, int y) const
{
    if(!validCell(layer, x, y))
        return 0;
    return m_layers[layer].components.at(index(x, y));
}

QPoint CAiGraphDocument::directionDelta(int direction)
{
    static const QPoint deltas[DirectionCount] = {
        QPoint(0, -1), QPoint(-1, -1), QPoint(-1, 0), QPoint(-1, 1),
        QPoint(0, 1), QPoint(1, 1), QPoint(1, 0), QPoint(1, -1)
    };
    return direction >= 0 && direction < DirectionCount
        ? deltas[direction] : QPoint();
}

QString CAiGraphDocument::layerLabel(int layer)
{
    static const char* labels[LayerCount] = {
        "Flying / levitating", "Narrow / prone", "Narrow / crawling",
        "Narrow / human-sized", "Narrow / tall", "Wide / low",
        "Wide / tall", "Very large"
    };
    return layer >= 0 && layer < LayerCount
        ? QString::fromLatin1(labels[layer]) : QStringLiteral("Unknown");
}

double CAiGraphDocument::canonicalHeight(int layer)
{
    static const double heights[LayerCount] = {2.0, 0.4, 1.0, 1.6, 2.2, 1.7, 3.0, 6.0};
    return layer >= 0 && layer < LayerCount ? heights[layer] : 0.0;
}
