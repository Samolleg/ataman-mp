#ifndef LANDSCAPE_EDIT_POLICY_H
#define LANDSCAPE_EDIT_POLICY_H

enum class ELandscapeEditMode
{
    Sculpt,
    TilePaint,
    OverlayPaint
};

inline bool landscapeModeChangeRequiresBrushReset(
    ELandscapeEditMode previous, ELandscapeEditMode current)
{
    return previous != current;
}

#endif // LANDSCAPE_EDIT_POLICY_H

