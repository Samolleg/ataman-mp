#ifndef LANDSCAPE_EDIT_POLICY_H
#define LANDSCAPE_EDIT_POLICY_H

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

#endif // LANDSCAPE_EDIT_POLICY_H
