#ifndef LANDSCAPE_EDIT_POLICY_H
#define LANDSCAPE_EDIT_POLICY_H

#include <cmath>

enum class ELandscapeEditMode
{
    Sculpt,
    TilePaint,
    OverlaySurface,
    OverlayPaint,
    OverlaySculpt
};

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

inline float landscapeSculptSubstepSeconds(int intervalMilliseconds,
                                           int spatialSteps)
{
    const int safeInterval = intervalMilliseconds > 0 ? intervalMilliseconds : 1;
    const int safeSteps = spatialSteps > 0 ? spatialSteps : 1;
    return float(safeInterval) / 1000.0f / float(safeSteps);
}

inline float landscapeSculptEffectiveStrength(float strength,
                                              float elapsedSeconds)
{
    const float boundedStrength = strength < 0.0f ? 0.0f
        : (strength > 1.0f ? 1.0f : strength);
    if(boundedStrength <= 0.0f || elapsedSeconds <= 0.0f)
        return 0.0f;
    if(boundedStrength >= 1.0f)
        return 1.0f;
    // Treat the UI strength as a per-second blend. Splitting a timer tick into
    // several spatial samples must not make flatten/smooth stronger.
    return 1.0f - float(std::pow(1.0f - boundedStrength, elapsedSeconds));
}

#endif // LANDSCAPE_EDIT_POLICY_H
