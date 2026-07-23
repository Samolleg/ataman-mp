#ifndef PARTICLE_ANIMATION_H
#define PARTICLE_ANIMATION_H

#include <QtGlobal>
#include <QVector4D>

namespace ei {
namespace particle {

constexpr int kLogicalTicksPerSecond = 15;
constexpr int kEmitterSlots = 6;
constexpr int kLifetimeTicks = 32;

enum class Family
{
    FireAndSmoke,
    FireOnly,
    SmokeOnly
};

struct FrameSample
{
    int frame{0};
    int ageTicks{0};
    float normalizedAge{0.0f};
    bool smoke{false};
};

inline Family familyForKind(quint32 kind)
{
    if(kind == 8196u || kind == 8197u)
        return Family::SmokeOnly;
    if(kind == 8195u)
        return Family::FireOnly;
    return Family::FireAndSmoke;
}

// The stock emitter maintains six records. Their births are distributed over
// the 32-tick record lifetime; this deterministic staggering gives the editor
// the same continuous plume without depending on frame rate or random state.
inline FrameSample sample(Family family, int logicalTick, int slot, quint32 seed)
{
    const int clampedSlot = qBound(0, slot, kEmitterSlots - 1);
    const int seedOffset = int(seed % quint32(kLifetimeTicks));
    const int stagger = (clampedSlot * kLifetimeTicks) / kEmitterSlots;
    const int age = ((logicalTick + seedOffset - stagger) % kLifetimeTicks
                     + kLifetimeTicks) % kLifetimeTicks;

    FrameSample result;
    result.ageTicks = age;
    result.normalizedAge = float(age) / float(kLifetimeTicks - 1);
    if(family == Family::FireOnly)
    {
        result.frame = age & 7;
        return result;
    }
    if(family == Family::SmokeOnly)
    {
        result.frame = 8 + ((age / 4) % 7);
        result.smoke = true;
        return result;
    }

    if(age < 8)
        result.frame = age;
    else
    {
        result.frame = qMin(14, 8 + (age - 8) / 4);
        result.smoke = true;
    }
    return result;
}

// game.exe addresses TendriSmoke as a 4x4 animation. Frames 0..7 occupy the
// lower two rows (flame) and 8..14 the upper two rows (smoke). The particle
// quad uses top-left QImage coordinates, so convert the game's bottom-up row.
inline QVector4D atlasUv(int frame)
{
    const int bounded = qBound(0, frame, 15);
    const int column = bounded & 3;
    const int rowFromBottom = bounded >> 2;
    const float cell = 0.25f;
    return QVector4D(float(column) * cell,
                     float(3 - rowFromBottom) * cell,
                     cell, cell);
}

inline float signedNoise(quint32 seed, int slot, quint32 salt)
{
    quint32 value = seed ^ (quint32(slot + 1) * 0x9e3779b9u) ^ salt;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return float(value & 0xffffu) / 32767.5f - 1.0f;
}

} // namespace particle
} // namespace ei

#endif // PARTICLE_ANIMATION_H
