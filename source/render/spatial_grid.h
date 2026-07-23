#ifndef SPATIAL_GRID_H
#define SPATIAL_GRID_H

#include <QHash>
#include <QSet>
#include <QVector>
#include <QVector3D>
#include <QtMath>

#include <algorithm>

#include "view_frustum.h"

struct SSpatialQueryStats
{
    int cellsTested{0};
    int cellsVisible{0};
    int candidates{0};
    int entriesTested{0};
    int rejected{0};
};

// A deterministic XY grid aligned to EI's 32x32 terrain sectors. Entries are
// returned in insertion order so enabling spatial visibility cannot change the
// legacy transparent-object ordering before the explicit render queue lands.
template<typename T>
class CSpatialGrid
{
public:
    explicit CSpatialGrid(float cellSize = 32.0f, int maximumCellsPerEntry = 64)
        : m_cellSize(cellSize), m_maximumCellsPerEntry(maximumCellsPerEntry) {}

    void clear()
    {
        m_entries.clear();
        m_cells.clear();
        m_overflow.clear();
    }

    void insert(const T& item, const QVector3D& center, float radius,
                bool alwaysVisible = false)
    {
        const bool invalid = !qIsFinite(center.x()) || !qIsFinite(center.y())
            || !qIsFinite(center.z()) || !qIsFinite(radius);
        Entry entry{item, center, qMax(0.0f, radius), alwaysVisible || invalid};
        const int entryIndex = m_entries.size();
        m_entries.append(entry);
        if(entry.alwaysVisible)
        {
            m_overflow.append(entryIndex);
            return;
        }

        const int minX = qFloor((center.x() - entry.radius) / m_cellSize);
        const int maxX = qFloor((center.x() + entry.radius) / m_cellSize);
        const int minY = qFloor((center.y() - entry.radius) / m_cellSize);
        const int maxY = qFloor((center.y() + entry.radius) / m_cellSize);
        const qint64 cellCount = qint64(maxX - minX + 1) * qint64(maxY - minY + 1);
        if(cellCount <= 0 || cellCount > m_maximumCellsPerEntry)
        {
            m_overflow.append(entryIndex);
            return;
        }

        for(int y = minY; y <= maxY; ++y)
            for(int x = minX; x <= maxX; ++x)
            {
                Cell& cell = m_cells[cellKey(x, y)];
                cell.entries.append(entryIndex);
                const QVector3D minimum(float(x) * m_cellSize,
                    float(y) * m_cellSize, center.z() - entry.radius);
                const QVector3D maximum(float(x + 1) * m_cellSize,
                    float(y + 1) * m_cellSize, center.z() + entry.radius);
                if(!cell.initialized)
                {
                    cell.minimum = minimum;
                    cell.maximum = maximum;
                    cell.initialized = true;
                }
                else
                {
                    cell.minimum.setZ(qMin(cell.minimum.z(), minimum.z()));
                    cell.maximum.setZ(qMax(cell.maximum.z(), maximum.z()));
                }
            }
    }

    QVector<T> query(const CViewFrustum* frustum, SSpatialQueryStats* stats = nullptr) const
    {
        if(stats)
            *stats = SSpatialQueryStats();
        if(!frustum)
        {
            QVector<T> all;
            all.reserve(m_entries.size());
            for(const Entry& entry : m_entries)
                all.append(entry.item);
            if(stats)
            {
                stats->candidates = m_entries.size();
                stats->entriesTested = m_entries.size();
            }
            return all;
        }

        QSet<int> candidateSet;
        for(int entryIndex : m_overflow)
            candidateSet.insert(entryIndex);
        for(auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it)
        {
            if(stats)
                ++stats->cellsTested;
            const Cell& cell = it.value();
            if(!cell.initialized || !frustum->intersectsAabb(cell.minimum, cell.maximum))
                continue;
            if(stats)
                ++stats->cellsVisible;
            for(int entryIndex : cell.entries)
                candidateSet.insert(entryIndex);
        }

        QVector<int> ordered = candidateSet.values().toVector();
        std::sort(ordered.begin(), ordered.end());
        if(stats)
            stats->candidates = ordered.size();
        QVector<T> visible;
        visible.reserve(ordered.size());
        for(int entryIndex : ordered)
        {
            const Entry& entry = m_entries.at(entryIndex);
            if(stats)
                ++stats->entriesTested;
            if(entry.alwaysVisible || frustum->intersectsSphere(entry.center, entry.radius))
                visible.append(entry.item);
            else if(stats)
                ++stats->rejected;
        }
        return visible;
    }

    // Directional-shadow casters can sit outside the color frustum while
    // their projection along the light direction reaches a visible receiver.
    // Query cells conservatively with their bounding spheres, then apply the
    // exact swept-sphere test to entries. This avoids walking every MOB node
    // merely to find the comparatively small shadow-caster set.
    QVector<T> querySwept(const CViewFrustum* frustum, const QVector3D& sweep,
                          SSpatialQueryStats* stats = nullptr) const
    {
        if(!frustum)
            return query(nullptr, stats);
        if(stats)
            *stats = SSpatialQueryStats();

        QSet<int> candidateSet;
        for(int entryIndex : m_overflow)
            candidateSet.insert(entryIndex);
        for(auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it)
        {
            if(stats)
                ++stats->cellsTested;
            const Cell& cell = it.value();
            if(!cell.initialized)
                continue;
            const QVector3D center = (cell.minimum + cell.maximum) * 0.5f;
            const float radius = (cell.maximum - center).length();
            if(!frustum->intersectsSweptSphere(center, radius, sweep))
                continue;
            if(stats)
                ++stats->cellsVisible;
            for(int entryIndex : cell.entries)
                candidateSet.insert(entryIndex);
        }

        QVector<int> ordered = candidateSet.values().toVector();
        std::sort(ordered.begin(), ordered.end());
        if(stats)
            stats->candidates = ordered.size();
        QVector<T> visible;
        visible.reserve(ordered.size());
        for(int entryIndex : ordered)
        {
            const Entry& entry = m_entries.at(entryIndex);
            if(stats)
                ++stats->entriesTested;
            if(entry.alwaysVisible
                    || frustum->intersectsSweptSphere(entry.center, entry.radius, sweep))
                visible.append(entry.item);
            else if(stats)
                ++stats->rejected;
        }
        return visible;
    }

    int entryCount() const { return m_entries.size(); }
    int cellCount() const { return m_cells.size(); }
    int overflowCount() const { return m_overflow.size(); }

private:
    struct Entry
    {
        T item;
        QVector3D center;
        float radius;
        bool alwaysVisible;
    };
    struct Cell
    {
        QVector<int> entries;
        QVector3D minimum;
        QVector3D maximum;
        bool initialized{false};
    };

    static quint64 cellKey(int x, int y)
    {
        return (quint64(quint32(x)) << 32) | quint32(y);
    }

    float m_cellSize;
    int m_maximumCellsPerEntry;
    QVector<Entry> m_entries;
    QHash<quint64, Cell> m_cells;
    QVector<int> m_overflow;
};

#endif
