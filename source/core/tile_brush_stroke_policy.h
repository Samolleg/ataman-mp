#ifndef TILE_BRUSH_STROKE_POLICY_H
#define TILE_BRUSH_STROKE_POLICY_H

// Tracks ownership of live tile-brush mutations. A press may miss the
// projected surface while a later move or release becomes valid, so the first
// valid sample (not necessarily the press) must be able to open the stroke.
class CTileBrushStrokePolicy
{
public:
    bool ensureForValidSample(bool sampleValid, bool historyAvailable)
    {
        if(!sampleValid || !historyAvailable || m_active)
            return false;
        m_active = true;
        return true;
    }

    bool finish()
    {
        const bool wasActive = m_active;
        m_active = false;
        return wasActive;
    }

    bool isActive() const { return m_active; }

private:
    bool m_active{false};
};

#endif
