#ifndef LANDSCAPE_EDIT_POLICY_H
#define LANDSCAPE_EDIT_POLICY_H

#include <cmath>
#include <QPoint>
#include <QVector>

enum class ELandscapeEditMode
{
    Sculpt,
    TilePaint,
    OverlaySurface,
    OverlayPaint,
    OverlaySculpt
};

struct STerrainPaintIssueCapturePlan
{
    bool land{false};
    bool overlay{false};
};

inline STerrainPaintIssueCapturePlan terrainPaintIssueCapturePlan(
    ELandscapeEditMode activeMode)
{
    // A visual report is about the composed terrain cell, not whichever
    // paint tab happened to be active when marking began. Capture both
    // available grids and label each snapshot with its actual layer.
    (void)activeMode;
    STerrainPaintIssueCapturePlan plan;
    plan.land = true;
    plan.overlay = true;
    return plan;
}

inline bool terrainPaintIssueMarkerConsumesMouseMove(bool middleButtonHeld)
{
    // Left-drag and hover belong to the marker. Middle-drag remains owned by
    // the normal select operation so camera orbit keeps working.
    return !middleButtonHeld;
}

inline bool landscapeModeChangeRequiresBrushReset(
    ELandscapeEditMode previous, ELandscapeEditMode current)
{
    return previous != current;
}

inline int landscapeWheelStep(int angleDeltaY, int legacyDelta, int pixelDeltaY)
{
    // Some Windows wheel devices report only the legacy delta while precision
    // devices may report angle or pixel deltas. A zero angle delta must not be
    // mistaken for a downward step.
    const int delta = angleDeltaY != 0 ? angleDeltaY
        : (legacyDelta != 0 ? legacyDelta : pixelDeltaY);
    return delta > 0 ? 1 : (delta < 0 ? -1 : 0);
}

inline float landscapeSculptSubstepFraction(int spatialSteps)
{
    const int safeSteps = spatialSteps > 0 ? spatialSteps : 1;
    return 1.0f / float(safeSteps);
}

inline float landscapeSculptEffectiveStrength(float strength,
                                              float stampFraction)
{
    const float boundedStrength = strength < 0.0f ? 0.0f
        : (strength > 1.0f ? 1.0f : strength);
    if(boundedStrength <= 0.0f || stampFraction <= 0.0f)
        return 0.0f;
    if(boundedStrength >= 1.0f)
        return 1.0f;
    // Splitting one brush tick into several spatial samples must preserve the
    // established full-tick blend instead of multiplying it by sample count.
    return 1.0f - float(std::pow(1.0f - boundedStrength, stampFraction));
}

inline bool terrainPaintOutlineEnabled(bool outlineValid,
                                       bool manualTileSelection)
{
    // Manual painting still has a multi-cell footprint. Its physical-tile
    // preview supplements the footprint; it must not suppress it.
    (void)manualTileSelection;
    return outlineValid;
}

inline bool terrainPaintCellOutlineVisible(bool brushSelectionAvailable,
                                           bool previewTileAvailable)
{
    // A semantic family can be valid even when it has no physical thumbnail
    // in the legacy tile form (for example a generated working-atlas family).
    // The cell footprint belongs to the brush selection, not to that optional
    // lifted tile preview.
    (void)previewTileAvailable;
    return brushSelectionAvailable;
}

inline bool terrainContinuousSampleShouldUpdate(
    bool previousValid, const QPoint& previousPoint, int previousLayer,
    int previousMode, const QPoint& currentPoint, int currentLayer,
    int currentMode)
{
    return !previousValid || previousPoint != currentPoint
        || previousLayer != currentLayer || previousMode != currentMode;
}

inline float terrainPaintOutlineWorldCenter(int coordinate,
                                            bool manualTileSelection)
{
    // Semantic brushes are vertex-centered. Manual brushes are cell-centered
    // and therefore sit one world unit farther along the two-unit tile grid.
    return float(coordinate * 2 + (manualTileSelection ? 1 : 0));
}

struct STerrainPaintFootprintSnapshot
{
    QPoint anchor;
    int radius{0};
    bool square{false};
    bool cellCentered{false};

    bool contains(const QPoint& point) const
    {
        const int dx = qAbs(point.x() - anchor.x());
        const int dy = qAbs(point.y() - anchor.y());
        return square ? qMax(dx, dy) <= radius
                      : dx * dx + dy * dy <= radius * radius;
    }

    QVector<QPoint> cells() const
    {
        QVector<QPoint> result;
        for(int y = anchor.y() - radius; y <= anchor.y() + radius; ++y)
            for(int x = anchor.x() - radius; x <= anchor.x() + radius; ++x)
            {
                const QPoint point(x, y);
                if(contains(point))
                    result.append(point);
            }
        return result;
    }
};

inline STerrainPaintFootprintSnapshot terrainPaintFootprintSnapshot(
    const QPoint& cellAnchor, const QPoint& vertexAnchor, bool cellCentered,
    float radius, bool square)
{
    STerrainPaintFootprintSnapshot snapshot;
    snapshot.anchor = cellCentered ? cellAnchor : vertexAnchor;
    snapshot.radius = qMax(0, qRound(radius));
    snapshot.square = square;
    snapshot.cellCentered = cellCentered;
    return snapshot;
}

inline bool terrainPaintCommitMatchesDisplayedFootprint(
    bool displayedValid, const STerrainPaintFootprintSnapshot& displayed,
    const STerrainPaintFootprintSnapshot& fresh)
{
    return displayedValid
        && displayed.anchor == fresh.anchor
        && displayed.radius == fresh.radius
        && displayed.square == fresh.square
        && displayed.cellCentered == fresh.cellCentered;
}

#endif // LANDSCAPE_EDIT_POLICY_H
