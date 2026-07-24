#ifndef PARTICLE_ANIMATION_H
#define PARTICLE_ANIMATION_H

#include <QtGlobal>
#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include <QtMath>

#include <algorithm>
#include <array>

namespace ei {
namespace particle {

constexpr int kLogicalQuantMilliseconds = 55;
constexpr int kSpawnAttempts = 6;
constexpr int kSpawnProbability = 88;
constexpr int kActiveLimit = 500;
constexpr int kLifetimeTicks = 32;
constexpr int kTerminalFrame = 15;
constexpr float kEarlyScale = 0.8500000238418579f;
constexpr float kLateScale = 1.059999942779541f;
constexpr float kLateMotion = 0.15000000596046448f;

// 3DfpFPU slot +0x60 treats the submitted Tendri scale as the billboard
// half-extent. The editor quad spans -0.5..+0.5, so it needs the diameter.
inline float billboardDiameter(float submittedScale)
{
    return 2.0f * submittedScale;
}

struct TendriParameters
{
    QVector3D lowerSpawnBound;
    QVector3D upperSpawnBound;
    float initialScale;
    float verticalVelocity;
    float displacementThreshold;
    float smallParticleExpansion;
};

inline TendriParameters parametersForScale(float authoredScale)
{
    const float initialScale = 0.800000011920929f * authoredScale;
    return {QVector3D(-0.20000000298023224f * authoredScale,
                      -0.20000000298023224f * authoredScale, 0.0f),
            QVector3D(0.20000000298023224f * authoredScale,
                      0.20000000298023224f * authoredScale, 0.0f),
            initialScale,
            0.30000001192092896f * authoredScale,
            1000.0f * authoredScale,
            initialScale * 0.30000001192092896f};
}

enum class Family
{
    FireAndSmoke,
    FireOnly,
    SmokeOnly
};

inline Family familyForKind(quint32 kind)
{
    if(kind == 0x2004u || kind == 0x2005u)
        return Family::SmokeOnly;
    if(kind == 0x2003u)
        return Family::FireOnly;
    return Family::FireAndSmoke;
}

// The native atlas builder stores top-row-first rectangles. The shader's
// local Y runs from one at the lower edge to zero at the upper edge, hence
// the negative V extent reproduces the native (v0, v1) ordering directly.
inline QVector4D atlasUv(int frame)
{
    const int bounded = qBound(0, frame, 15);
    constexpr float cell = 0.25f;
    constexpr float inset = 1.0f / 512.0f;
    const float u0 = float(bounded & 3) * cell + inset;
    const float v0 = 1.0f - float(bounded >> 2) * cell - inset;
    const float u1 = float((bounded & 3) + 1) * cell - inset;
    const float v1 = 1.0f - float((bounded >> 2) + 1) * cell + inset;
    return QVector4D(u0, v0, u1 - u0, v1 - v0);
}

class Isaac32
{
public:
    explicit Isaac32(quint32 seed = 0) { reset(seed); }

    void reset(quint32 seed)
    {
        m_memory.fill(0);
        m_results.fill(0);
        quint32 value = seed ^ 0x50415254u; // editor-local deterministic seed
        for(quint32& word : m_results)
        {
            value += 0x9E3779B9u;
            quint32 mixed = value;
            mixed ^= mixed >> 16;
            mixed *= 0x7FEB352Du;
            mixed ^= mixed >> 15;
            mixed *= 0x846CA68Bu;
            word = mixed ^ (mixed >> 16);
        }
        initialize(true);
    }

    quint32 next()
    {
        if(m_remaining == 0)
        {
            refill();
            m_remaining = 256;
        }
        return m_results[--m_remaining];
    }

    float unit()
    {
        return float(double(next()) * 2.3283064370807974e-10);
    }

private:
    static void mix(quint32& a, quint32& b, quint32& c, quint32& d,
                    quint32& e, quint32& f, quint32& g, quint32& h)
    {
        a ^= b << 11; d += a; b += c;
        b ^= c >> 2;  e += b; c += d;
        c ^= d << 8;  f += c; d += e;
        d ^= e >> 16; g += d; e += f;
        e ^= f << 10; h += e; f += g;
        f ^= g >> 4;  a += f; g += h;
        g ^= h << 8;  b += g; h += a;
        h ^= a >> 9;  c += h; a += b;
    }

    void initialize(bool useSeed)
    {
        quint32 a = 0x9E3779B9u, b = a, c = a, d = a;
        quint32 e = a, f = a, g = a, h = a;
        for(int i = 0; i < 4; ++i)
            mix(a, b, c, d, e, f, g, h);
        for(int i = 0; i < 256; i += 8)
        {
            if(useSeed)
            {
                a += m_results[i]; b += m_results[i + 1];
                c += m_results[i + 2]; d += m_results[i + 3];
                e += m_results[i + 4]; f += m_results[i + 5];
                g += m_results[i + 6]; h += m_results[i + 7];
            }
            mix(a, b, c, d, e, f, g, h);
            m_memory[i] = a; m_memory[i + 1] = b;
            m_memory[i + 2] = c; m_memory[i + 3] = d;
            m_memory[i + 4] = e; m_memory[i + 5] = f;
            m_memory[i + 6] = g; m_memory[i + 7] = h;
        }
        if(useSeed)
        {
            for(int i = 0; i < 256; i += 8)
            {
                a += m_memory[i]; b += m_memory[i + 1];
                c += m_memory[i + 2]; d += m_memory[i + 3];
                e += m_memory[i + 4]; f += m_memory[i + 5];
                g += m_memory[i + 6]; h += m_memory[i + 7];
                mix(a, b, c, d, e, f, g, h);
                m_memory[i] = a; m_memory[i + 1] = b;
                m_memory[i + 2] = c; m_memory[i + 3] = d;
                m_memory[i + 4] = e; m_memory[i + 5] = f;
                m_memory[i + 6] = g; m_memory[i + 7] = h;
            }
        }
        m_a = m_b = m_c = 0;
        refill();
        m_remaining = 256;
    }

    void refill()
    {
        ++m_c;
        m_b += m_c;
        for(int i = 0; i < 256; ++i)
        {
            const quint32 x = m_memory[i];
            switch(i & 3)
            {
            case 0: m_a ^= m_a << 13; break;
            case 1: m_a ^= m_a >> 6; break;
            case 2: m_a ^= m_a << 2; break;
            default: m_a ^= m_a >> 16; break;
            }
            m_a += m_memory[(i + 128) & 255];
            const quint32 y = m_memory[(x >> 2) & 255] + m_a + m_b;
            m_memory[i] = y;
            m_b = m_memory[(y >> 10) & 255] + x;
            m_results[i] = m_b;
        }
    }

    std::array<quint32, 256> m_memory{};
    std::array<quint32, 256> m_results{};
    quint32 m_a{0}, m_b{0}, m_c{0};
    int m_remaining{0};
};

struct TendriParticle
{
    QVector3D position;
    QVector3D previousPosition;
    QVector3D drift;
    float scale{1.0f};
    float previousScale{1.0f};
    float verticalVelocity{0.3f};
    int lifetime{kLifetimeTicks};
    int frame{0};
    int previousFrame{0};
    int frameDelay{1};
    bool retiring{false};
};

struct RenderSample
{
    QVector3D position;
    float scale{1.0f};
    int frame{0};
};

struct EffectRenderSample
{
    QVector3D position;
    float scale{1.0f};
    float alpha{1.0f};
    int frame{0};
    QVector3D color{1.0f, 1.0f, 1.0f};
};

class TendriEmitter
{
public:
    void reset(quint32 seed, quint32 kind, const QVector3D& origin,
               float authoredScale, const QVector3D& windDirection,
               float windStrength, int prewarmTicks)
    {
        m_seed = seed;
        m_kind = kind;
        m_rng.reset(seed);
        m_particles.clear();
        for(int tick = 0; tick < prewarmTicks; ++tick)
            advanceOne(origin, authoredScale, windDirection, windStrength);
        m_initialized = true;
    }

    void advanceTo(qint64 logicalTick, quint32 seed, quint32 kind,
                   const QVector3D& origin, float authoredScale,
                   const QVector3D& windDirection, float windStrength,
                   int initialPrewarmTicks)
    {
        if(!m_initialized || seed != m_seed || kind != m_kind
                || logicalTick < m_lastTick || logicalTick - m_lastTick > 4096)
        {
            reset(seed, kind, origin, authoredScale, windDirection,
                  windStrength, initialPrewarmTicks);
            m_lastTick = logicalTick;
            return;
        }
        while(m_lastTick < logicalTick)
        {
            advanceOne(origin, authoredScale, windDirection, windStrength);
            ++m_lastTick;
        }
    }

    QVector<RenderSample> samples(float fraction) const
    {
        QVector<RenderSample> result;
        result.reserve(m_particles.size());
        const float alpha = qBound(0.0f, fraction, 1.0f);
        for(const TendriParticle& particle : m_particles)
        {
            RenderSample sample;
            sample.position = particle.previousPosition
                + (particle.position - particle.previousPosition) * alpha;
            sample.scale = particle.previousScale
                + (particle.scale - particle.previousScale) * alpha;
            sample.frame = particle.previousFrame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    bool update(TendriParticle& particle, const QVector3D& windDirection,
                float windStrength)
    {
        if(--particle.lifetime == 0)
            return false;
        if(--particle.frameDelay == 0)
        {
            ++particle.frame;
            if(particle.frame >= kTerminalFrame)
                return false;
            particle.frameDelay = 1;
            if(particle.frame > 7)
            {
                if(m_kind == 0x2003u || m_kind == 0x2010u)
                    return false;
                particle.frameDelay = 4;
                if((m_rng.next() & 3u) == 0)
                    return false;
            }
            if(particle.frame == 8)
                particle.verticalVelocity *= 0.5f;
        }
        particle.position.setZ(particle.position.z() + particle.verticalVelocity);
        if(particle.frame < 2)
            particle.position += particle.drift;
        if(particle.frame < 7)
            particle.scale *= kEarlyScale;
        else
        {
            particle.scale *= kLateScale;
            particle.position += windDirection
                * (particle.scale * windStrength * kLateMotion);
        }
        return true;
    }

    void spawn(const QVector3D& origin, float authoredScale)
    {
        const TendriParameters parameters = parametersForScale(authoredScale);
        TendriParticle particle;
        particle.frame = familyForKind(m_kind) == Family::SmokeOnly ? 8 : 0;
        particle.previousFrame = particle.frame;
        particle.scale = parameters.initialScale;
        particle.verticalVelocity = parameters.verticalVelocity;
        float expansion = 0.0f;
        if((m_rng.next() % 100u) + 1u <= 40u)
        {
            particle.scale *= 0.5f;
            expansion = parameters.smallParticleExpansion;
        }
        const float minX = parameters.lowerSpawnBound.x() - expansion;
        const float maxX = parameters.upperSpawnBound.x() + expansion;
        const float minY = parameters.lowerSpawnBound.y() - expansion;
        const float maxY = parameters.upperSpawnBound.y() + expansion;
        particle.drift.setX((minX + (maxX - minX) * m_rng.unit()) * 0.5f);
        particle.drift.setY((minY + (maxY - minY) * m_rng.unit()) * 0.5f);
        particle.position = origin + particle.drift;
        particle.position.setZ(origin.z() + parameters.verticalVelocity * m_rng.unit());
        particle.previousPosition = particle.position;
        particle.previousScale = particle.scale;
        m_particles.append(particle);
    }

    void advanceOne(const QVector3D& origin, float authoredScale,
                    const QVector3D& windDirection, float windStrength)
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            TendriParticle& particle = m_particles[i];
            if(particle.retiring)
            {
                m_particles.removeAt(i);
                continue;
            }
            particle.previousPosition = particle.position;
            particle.previousScale = particle.scale;
            particle.previousFrame = particle.frame;
            if(!update(particle, windDirection, windStrength))
            {
                particle.position = particle.previousPosition;
                particle.scale = particle.previousScale;
                particle.frame = particle.previousFrame;
                particle.retiring = true;
            }
        }

        int spawned = 0;
        for(int attempt = 0; attempt < kSpawnAttempts
                && m_particles.size() < kActiveLimit; ++attempt)
        {
            if((m_rng.next() % 100u) + 1u <= quint32(kSpawnProbability))
            {
                spawn(origin, authoredScale);
                ++spawned;
            }
        }
        if(spawned == 0 && kSpawnProbability > 50
                && m_particles.size() < kActiveLimit)
            spawn(origin, authoredScale);
    }

    Isaac32 m_rng;
    QVector<TendriParticle> m_particles;
    quint32 m_seed{0};
    quint32 m_kind{0};
    qint64 m_lastTick{0};
    bool m_initialized{false};
};

struct MushroomParticle
{
    QVector3D position;
    QVector3D velocity;
    float scale{0.1f};
    float alpha{0.25f};
    int lifetime{16};
    int frame{0};
    int phaseDelay{2};
};

class MushroomEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 64;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || cycleTick < m_cycleTick)
            reset(seed, origin, authoredScale);
        m_origin = origin;
        m_scale = qMax(0.001f, authoredScale);
        while(m_cycleTick < cycleTick) { advanceOne(); ++m_cycleTick; }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const MushroomParticle& p : m_particles)
        {
            EffectRenderSample sample;
            sample.position = p.position;
            sample.scale = billboardDiameter(p.scale);
            sample.alpha = p.alpha;
            sample.frame = p.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high) { return low + (high - low) * m_rng.unit(); }

    void reset(quint32 seed, const QVector3D& origin, float scale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x4D555348u);
        m_origin = origin;
        m_scale = qMax(0.001f, scale);
        m_sourceAge = m_cycleTick = 0;
        m_sourceHeight = m_sourceRadius = 0.0f;
        m_particles.clear();
        m_initialized = true;
    }

    void spawn()
    {
        QVector3D direction(randomRange(-1.0f, 1.0f), randomRange(-1.0f, 1.0f),
                            randomRange(0.1f, 1.0f));
        if(direction.lengthSquared() < 0.0001f) direction.setZ(1.0f);
        direction.normalize();
        MushroomParticle p;
        p.position = m_origin + QVector3D(direction.x() * m_sourceRadius,
                                           direction.y() * m_sourceRadius,
                                           m_sourceHeight);
        p.velocity = direction * (0.06f * m_scale);
        p.velocity.setZ(p.velocity.z() + 0.12f * m_scale);
        p.scale = (0.1f + 0.008f * float(m_sourceAge)) * m_scale;
        p.lifetime = 16 + int(m_rng.next() & 3u);
        p.frame = qBound(0, m_sourceAge / 5 + int(m_rng.next() % 3u) - 1, 14);
        m_particles.append(p);
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            MushroomParticle& p = m_particles[i];
            if(--p.lifetime == 0) { m_particles.removeAt(i); continue; }
            if(--p.phaseDelay == 0 && p.frame < 14)
            { ++p.frame; p.phaseDelay = p.frame < 8 ? 2 : 1; }
            if(p.frame < 8) p.scale *= 1.1f;
            p.alpha = p.lifetime > 4 ? qMin(1.0f, p.alpha + 64.0f / 255.0f)
                                      : qMax(0.0f, p.alpha - 63.0f / 255.0f);
            p.position += p.velocity;
            const QVector3D horizontal(m_origin.x() - p.position.x(),
                                       m_origin.y() - p.position.y(), 0.0f);
            p.velocity = p.velocity * 0.7f + horizontal * 0.02f;
        }
        if(m_sourceAge < 30)
            for(int attempt = 0; attempt < 10; ++attempt) spawn();
        ++m_sourceAge;
        m_sourceRadius = qLn(float(m_sourceAge + 1)) * 0.2f * m_scale;
        m_sourceHeight += 0.12f * m_scale;
    }

    Isaac32 m_rng;
    QVector<MushroomParticle> m_particles;
    QVector3D m_origin;
    float m_scale{1.0f}, m_sourceHeight{0.0f}, m_sourceRadius{0.0f};
    quint32 m_seed{0};
    int m_sourceAge{0}, m_cycleTick{0};
    bool m_initialized{false};
};

struct BloodParticle
{
    QVector3D position;
    QVector3D velocity;
    float scale{1.0f};
    int lifetime{6};
};

class Blood1Emitter
{
public:
    static constexpr int kPreviewCycleTicks = 32;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin, float bound,
                   int frame = 12)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        frame = qBound(12, frame, 15);
        if(!m_initialized || seed != m_seed || cycleTick < m_cycleTick || frame != m_frame)
            reset(seed, origin, bound, frame);
        m_origin = origin;
        m_bound = qMax(0.001f, bound);
        while(m_cycleTick < cycleTick) { advanceOne(); ++m_cycleTick; }
    }

    QVector<EffectRenderSample> samples() const
    {
        static constexpr std::array<float, 7> alpha = {
            0.0f, 64.0f / 255.0f, 128.0f / 255.0f,
            192.0f / 255.0f, 1.0f, 1.0f, 1.0f
        };
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const BloodParticle& p : m_particles)
        {
            EffectRenderSample sample;
            sample.position = p.position;
            sample.scale = billboardDiameter(p.scale);
            sample.alpha = alpha[size_t(qBound(0, p.lifetime, 6))];
            sample.frame = m_frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high) { return low + (high - low) * m_rng.unit(); }

    void reset(quint32 seed, const QVector3D& origin, float bound, int frame)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x424C4431u);
        m_origin = origin;
        m_bound = qMax(0.001f, bound);
        m_frame = frame;
        m_sourceAge = m_cycleTick = 0;
        m_particles.clear();
        m_initialized = true;
    }

    QVector3D randomDirection()
    {
        for(int attempt = 0; attempt < 10; ++attempt)
        {
            QVector3D v(randomRange(-1.0f, 1.0f), randomRange(-1.0f, 1.0f),
                        randomRange(-1.0f, 1.0f));
            if(v.lengthSquared() > 0.0001f && v.lengthSquared() <= 1.0f)
                return v.normalized();
        }
        return QVector3D(0.0f, 0.0f, 1.0f);
    }

    void spawn()
    {
        BloodParticle p;
        p.position = m_origin;
        p.velocity = randomDirection() * (0.166666672f * m_bound);
        p.scale = m_bound;
        m_particles.append(p);
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            BloodParticle& p = m_particles[i];
            if(--p.lifetime == 0) { m_particles.removeAt(i); continue; }
            p.position += p.velocity;
            p.scale *= 0.8f;
            p.velocity *= 0.9f;
            p.velocity.setZ(p.velocity.z() - p.scale * 0.05f);
        }
        if(m_sourceAge <= 5)
            for(int i = 0, count = qMax(1, qRound(m_bound * 8.0f)) * 10;
                i < count; ++i) spawn();
        ++m_sourceAge;
    }

    Isaac32 m_rng;
    QVector<BloodParticle> m_particles;
    QVector3D m_origin;
    float m_bound{1.0f};
    quint32 m_seed{0};
    int m_sourceAge{0}, m_cycleTick{0};
    int m_frame{12};
    bool m_initialized{false};
};

struct A4BloodSource
{
    float radius{0.1f};
    float angle{0.0f};
    int age{0};
};

struct A4BloodParticle
{
    QVector3D position;
    QVector3D velocity;
    float scale{0.1f};
    float verticalAcceleration{-0.1f};
    float terminalVelocity{-0.4f};
};

class A4BloodEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 32;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || cycleTick < m_cycleTick)
            reset(seed, origin);
        m_origin = origin;
        while(m_cycleTick < cycleTick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const A4BloodParticle& particle : m_particles)
        {
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = 1.0f;
            sample.frame = 12;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }
    int sourceCount() const { return m_sources.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(quint32 seed, const QVector3D& origin)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x4134424Cu);
        m_origin = origin;
        m_sources.clear();
        m_particles.clear();
        const int count = 4 + int(m_rng.next() % 5u);
        m_sources.resize(count);
        m_generating = true;
        m_cycleTick = 0;
        m_initialized = true;
    }

    void updateSources()
    {
        for(A4BloodSource& source : m_sources)
        {
            if(source.age == 0)
            {
                source.radius = randomRange(0.1f, 0.3f);
                source.angle = randomRange(0.0f, 6.0f);
            }
            else
                m_generating = false;
            ++source.age;
        }
    }

    void spawn()
    {
        const A4BloodSource& source =
            m_sources[int(m_rng.next() % quint32(m_sources.size()))];
        const float radial = randomRange(-0.5f * source.radius,
                                          0.5f * source.radius);
        A4BloodParticle particle;
        particle.position = m_origin;
        particle.scale = randomRange(0.1f, 0.2f);
        particle.velocity = QVector3D(qSin(source.angle) * radial,
                                      qCos(source.angle) * radial, 0.0f);
        particle.verticalAcceleration = randomRange(-0.18f, -0.06f);
        particle.terminalVelocity = randomRange(-0.6f, -0.2f);
        m_particles.append(particle);
    }

    void advanceOne()
    {
        updateSources();
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            A4BloodParticle& particle = m_particles[i];
            if(particle.position.z() < 0.0f)
            {
                m_particles.removeAt(i);
                continue;
            }
            particle.position += particle.velocity;
            particle.velocity.setZ(qMax(particle.terminalVelocity,
                particle.velocity.z() + particle.verticalAcceleration));
        }
        if(m_generating)
            for(int attempt = 0; attempt < 45 && m_particles.size() < 300; ++attempt)
                spawn();
    }

    Isaac32 m_rng;
    QVector<A4BloodSource> m_sources;
    QVector<A4BloodParticle> m_particles;
    QVector3D m_origin;
    quint32 m_seed{0};
    int m_cycleTick{0};
    bool m_generating{true};
    bool m_initialized{false};
};

struct WeatherRenderSample
{
    QVector3D position;
    float scale{1.0f};
    float alpha{1.0f};
    int frame{0};
};

struct TornadoParticle
{
    float height{0.0f};
    float alphaRamp{0.1f};
    float angle{0.0f};
    float radius{1.0f};
    float angularVelocity{0.0f};
    float verticalVelocity{0.0f};
    int lifetime{100};
    int frame{0};
    bool large{true};
};

class TornadoEmitter
{
public:
    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale, int prewarmTicks)
    {
        if(!m_initialized || seed != m_seed || logicalTick < m_lastTick
                || logicalTick - m_lastTick > 4096)
        {
            m_seed = seed;
            m_rng.reset(seed ^ 0x544F524Eu);
            m_particles.clear();
            for(int i = 0; i < prewarmTicks; ++i)
                advanceOne();
            m_lastTick = logicalTick;
            m_initialized = true;
        }
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        while(m_lastTick < logicalTick)
        {
            advanceOne();
            ++m_lastTick;
        }
    }

    QVector<WeatherRenderSample> samples() const
    {
        QVector<WeatherRenderSample> result;
        result.reserve(m_particles.size());
        const float heightScale = 12.0f * m_authoredScale;
        const QVector3D base = m_origin + QVector3D(0.0f, 0.0f, 1.0f);
        for(const TornadoParticle& particle : m_particles)
        {
            WeatherRenderSample sample;
            sample.position = base + QVector3D(qSin(particle.angle) * particle.radius,
                                                qCos(particle.angle) * particle.radius,
                                                particle.height * heightScale);
            const float submittedRadius = particle.large
                ? m_authoredScale * (particle.radius + 0.3f)
                : m_authoredScale * 0.15f;
            sample.scale = billboardDiameter(submittedRadius);
            sample.alpha = particle.alphaRamp * qSqrt(qMax(0.0f, 1.0f - particle.height));
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void spawn()
    {
        TornadoParticle particle;
        particle.height = m_rng.unit();
        particle.large = (m_rng.next() % 3u) != 0u;
        particle.lifetime = 100 + int(m_rng.next() % 9u);
        const float baseShape = 1.0f - particle.height;
        const float radiusShape = baseShape * baseShape * baseShape * baseShape;
        particle.radius = randomRange(0.8f, 2.0f) * (radiusShape + 0.2f) * 2.0f;
        particle.angle = randomRange(-float(M_PI), float(M_PI));
        particle.angularVelocity = randomRange(float(M_PI) * 0.06f,
                                                float(M_PI) * 0.07f)
            * (1.1f - baseShape * baseShape) * 1.5f;
        particle.frame = particle.large ? int(m_rng.next() % 12u)
                                        : 12 + int(m_rng.next() & 3u);
        m_particles.append(particle);
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            TornadoParticle& particle = m_particles[i];
            if(--particle.lifetime == 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            particle.height += particle.verticalVelocity;
            particle.verticalVelocity += 0.0001f;
            if(particle.height > 1.0f)
            {
                m_particles.removeAt(i);
                continue;
            }
            particle.angle += particle.angularVelocity;
            particle.angularVelocity *= 1.01f;
            particle.radius *= 0.99f;
            if(particle.lifetime > 8)
                particle.alphaRamp = qMin(1.0f, particle.alphaRamp + 0.1f);
            else
                particle.alphaRamp = qMax(0.1f, particle.alphaRamp);
            particle.frame = particle.large ? (particle.frame + 1) % 12
                                            : 12 + ((particle.frame + 1) & 3);
        }
        for(int attempt = 0; attempt < 4 && m_particles.size() < 800; ++attempt)
            spawn();
    }

    Isaac32 m_rng;
    QVector<TornadoParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    qint64 m_lastTick{0};
    bool m_initialized{false};
};

struct LightningSource
{
    QVector3D position;
    QVector3D previous;
    QVector3D velocity;
    int age{0};
};

struct LightningParticle
{
    QVector3D position;
    QVector3D velocity;
    int source{-1};
    int lifetime{0};
    int frame{0};
    float scale{0.0f};
    float alpha{1.0f};
    bool followSource{false};
    bool oneFrameFlash{false};
    bool cloud{false};
};

class LightningBlastEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 80;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || cycleTick < m_cycleTick)
            reset(seed, origin, authoredScale);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        while(m_cycleTick < cycleTick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<WeatherRenderSample> samples() const
    {
        QVector<WeatherRenderSample> result;
        result.reserve(m_particles.size());
        for(const LightningParticle& particle : m_particles)
        {
            if(particle.scale <= 0.0f)
                continue;
            WeatherRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    QVector3D randomDisk()
    {
        const float radius = qSqrt(m_rng.unit());
        const float angle = randomRange(0.0f, float(M_PI) * 2.0f);
        return QVector3D(qCos(angle) * radius, qSin(angle) * radius, 0.0f);
    }

    void reset(quint32 seed, const QVector3D& origin, float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x4C425354u);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        m_sources.clear();
        m_particles.clear();
        m_sourcePhase = 0;
        m_generating = true;
        m_cycleTick = 0;
        for(int i = 0; i < 10; ++i)
        {
            QVector3D direction = i == 0 ? QVector3D() : randomDisk();
            LightningSource source;
            source.position = origin + direction * (m_authoredScale * 0.3f);
            source.position.setZ(origin.z() + 0.3f);
            source.previous = source.position;
            direction.setZ(0.5f);
            source.velocity = direction.normalized() * 0.5f;
            m_sources.append(source);
        }
        m_initialized = true;
    }

    void spawn()
    {
        LightningParticle particle;
        if(m_sourcePhase < m_sources.size() * 2)
        {
            particle.source = m_sourcePhase / 2;
            particle.position = m_sources[particle.source].position;
            particle.lifetime = 10;
            particle.followSource = (m_sourcePhase & 1) == 0;
            particle.oneFrameFlash = !particle.followSource;
            if(particle.oneFrameFlash)
            {
                particle.frame = 12 + int(m_rng.next() & 3u);
                particle.scale = m_authoredScale * 0.2f * randomRange(0.5f, 1.4f);
                if(particle.source == 0)
                    particle.scale = m_authoredScale * 0.4f;
            }
            ++m_sourcePhase;
            m_particles.append(particle);
            return;
        }

        const int sourceAge = m_sources.isEmpty() ? 10 : m_sources[0].age;
        if(m_particles.size() < 200 && sourceAge < 5)
        {
            const float radiusPart = 1.0f - qPow(m_rng.unit(), 2.0f);
            const float angle = randomRange(0.0f, 6.28f);
            particle.velocity = QVector3D(radiusPart * qCos(angle),
                                          radiusPart * qSin(angle), 0.0f)
                * (m_authoredScale * 0.3f);
            particle.position = m_sources[0].position + particle.velocity;
            particle.position.setZ(m_sources[0].position.z()
                                   + (1.0f - radiusPart) * m_authoredScale * 0.1f);
            particle.frame = 8;
            particle.lifetime = int(9.0f - 3.0f * radiusPart);
            particle.scale = randomRange(0.1f, 0.14f) * m_authoredScale
                * (2.0f - radiusPart);
            particle.alpha = 0.25f;
            particle.cloud = true;
            m_particles.append(particle);
            return;
        }

        const int sourceIndex = int(m_rng.next() % quint32(m_sources.size()));
        const float part = m_rng.unit();
        particle.position = m_sources[sourceIndex].position * part
            + m_sources[sourceIndex].previous * (1.0f - part);
        particle.frame = 1;
        particle.lifetime = 6;
        particle.scale = randomRange(0.02f, 0.03f) * 3.0f;
        m_particles.append(particle);
    }

    void advanceOne()
    {
        static const int alphaTable[16] = {
            0, 0x50, 0xB0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        };
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            LightningParticle& particle = m_particles[i];
            if(--particle.lifetime < 0 || particle.oneFrameFlash)
            {
                m_particles.removeAt(i);
                continue;
            }
            if(particle.followSource)
            {
                particle.position = m_sources[particle.source].position;
                particle.scale = 0.1f;
                continue;
            }
            particle.alpha = float(alphaTable[qBound(0, particle.lifetime, 15)])
                / (255.0f * 4.0f);
            if(particle.cloud)
            {
                particle.frame = (particle.frame & ~3) + ((particle.frame + 1) & 3);
                particle.position += particle.velocity;
                particle.velocity *= 0.6f;
            }
            else
            {
                ++particle.frame;
            }
            particle.scale *= 0.9f;
        }

        if(!m_generating)
            return;
        const int attempts = m_cycleTick == 0 ? 250 : 50;
        for(int attempt = 0; attempt < attempts && m_particles.size() < 600; ++attempt)
        {
            if((m_rng.next() % 100u) + 1u <= 88u)
                spawn();
        }
        for(LightningSource& source : m_sources)
        {
            ++source.age;
            source.previous = source.position;
            source.position += source.velocity;
            source.velocity.setZ(source.velocity.z() - 0.075f);
        }
        if(!m_sources.isEmpty() && m_sources[0].age >= 10)
            m_generating = false;
    }

    Isaac32 m_rng;
    QVector<LightningSource> m_sources;
    QVector<LightningParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    int m_sourcePhase{0};
    int m_cycleTick{0};
    bool m_generating{false};
    bool m_initialized{false};
};

struct BurstParticle
{
    QVector3D position;
    QVector3D velocity;
    float scale{0.1f};
    float alpha{1.0f};
    int lifetime{1};
    int frame{0};
    bool impacted{false};
};

class NukeEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 80;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        const int tick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || tick < m_cycleTick)
            reset(seed, origin, authoredScale);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        while(m_cycleTick < tick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<WeatherRenderSample> samples() const
    {
        QVector<WeatherRenderSample> result;
        result.reserve(m_particles.size());
        for(const BurstParticle& particle : m_particles)
        {
            WeatherRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(quint32 seed, const QVector3D& origin, float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x4E554B45u);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        m_particles.clear();
        m_sourceAge = 0;
        m_attempts = 128;
        m_cycleTick = 0;
        m_generating = true;
        m_initialized = true;
    }

    void spawn()
    {
        const int phasesLeft = 11 - m_sourceAge;
        const float radius = randomRange(0.0f, m_authoredScale * 0.2f);
        const float angle = randomRange(0.0f, float(M_PI) * 2.0f);
        BurstParticle particle;
        particle.position = m_origin + QVector3D(radius * qSin(angle),
                                                  radius * qCos(angle),
                                                  randomRange(0.0f, m_authoredScale * 2.0f));
        particle.scale = m_authoredScale * 0.2f;
        particle.velocity = QVector3D(qSin(angle), qCos(angle),
                                      m_authoredScale * 0.1f * float(phasesLeft));
        particle.frame = m_sourceAge / 3;
        particle.lifetime = 30 + phasesLeft;
        float alpha = 4.0f;
        for(int i = 0; i < m_sourceAge; ++i)
            alpha *= 4.0f;
        particle.alpha = qMin(255.0f, alpha) / 255.0f;
        m_particles.append(particle);
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            BurstParticle& particle = m_particles[i];
            if(--particle.lifetime == 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            if(particle.impacted)
            {
                if(particle.frame < 14)
                    ++particle.frame;
                else
                {
                    m_particles.removeAt(i);
                    continue;
                }
                particle.position += QVector3D(particle.velocity.x(),
                                               particle.velocity.y(), 0.0f);
                particle.velocity.setX(particle.velocity.x() * 0.85f);
                particle.velocity.setY(particle.velocity.y() * 0.85f);
                particle.scale *= 1.13f;
                particle.alpha = qMax(0.0f, particle.alpha - 16.0f / 255.0f);
            }
            else
            {
                particle.velocity.setZ(particle.velocity.z() * 0.85f - 0.055f);
                particle.position.setZ(particle.position.z() + particle.velocity.z());
                particle.alpha = qMin(1.0f, particle.alpha * 4.0f);
                if(particle.velocity.z() < 0.0f
                        && particle.position.z() < m_origin.z() + 0.5f * m_authoredScale)
                {
                    const float speed = qAbs(particle.velocity.z());
                    particle.velocity.setX(particle.velocity.x() * speed);
                    particle.velocity.setY(particle.velocity.y() * speed);
                    particle.impacted = true;
                }
            }
        }

        if(!m_generating)
            return;
        for(int attempt = 0; attempt < m_attempts && m_particles.size() < 400; ++attempt)
        {
            if((m_rng.next() % 100u) + 1u <= 88u)
                spawn();
        }
        ++m_sourceAge;
        if(m_attempts > 8)
            m_attempts /= 2;
        if(m_sourceAge >= 13)
            m_generating = false;
    }

    Isaac32 m_rng;
    QVector<BurstParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    int m_sourceAge{0};
    int m_attempts{128};
    int m_cycleTick{0};
    bool m_generating{false};
    bool m_initialized{false};
};

struct RingSource
{
    QVector3D base;
    float radius{0.0f};
    float angle{0.0f};
    float zVelocity{0.0f};
    int lifetime{0};
};

class CastingEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 72;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        const int tick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || tick < m_cycleTick)
            reset(seed, origin, authoredScale);
        while(m_cycleTick < tick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<WeatherRenderSample> samples() const
    {
        QVector<WeatherRenderSample> result;
        result.reserve(m_particles.size());
        for(const BurstParticle& particle : m_particles)
        {
            WeatherRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(quint32 seed, const QVector3D& origin, float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x43415354u);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        m_speedFactor = m_authoredScale / 18.0f;
        m_sources.clear();
        m_particles.clear();
        for(int i = 0; i < 3; ++i)
        {
            const float zRandom = randomRange(-m_authoredScale * 0.2f,
                                              m_authoredScale * 0.2f);
            RingSource source;
            source.base = m_origin + QVector3D(0.0f, 0.0f,
                                               -m_authoredScale + zRandom);
            source.angle = float(i) * float(M_PI) * 2.0f / 3.0f;
            source.zVelocity = (2.0f * m_authoredScale - zRandom) / 36.0f;
            source.lifetime = 45;
            m_sources.append(source);
        }
        m_cycleTick = 0;
        m_generating = true;
        m_initialized = true;
    }

    QVector3D sourcePosition(const RingSource& source) const
    {
        return source.base + QVector3D(source.radius * qSin(source.angle),
                                       source.radius * qCos(source.angle), 0.0f);
    }

    void spawnTail()
    {
        const int sourceIndex = int(m_rng.next() % quint32(m_sources.size()));
        const RingSource& source = m_sources[sourceIndex];
        const float spread = source.radius * 0.1f + 0.001f;
        BurstParticle particle;
        particle.position = sourcePosition(source)
            + QVector3D(randomRange(-spread, spread), randomRange(-spread, spread),
                        randomRange(-spread, 0.0f));
        particle.scale = m_authoredScale * 0.1f;
        particle.frame = 1;
        particle.lifetime = 7;
        m_particles.append(particle);
    }

    void spawnClosingRing()
    {
        const RingSource& source = m_sources[0];
        const float wanted = m_speedFactor * 20.0f;
        const float nextRadius = source.radius + (source.radius - wanted) * 0.5f;
        const float low = qMin(nextRadius, source.radius);
        const float high = qMax(nextRadius, source.radius);
        const float radius = randomRange(low, high);
        const float angle = randomRange(0.0f, float(M_PI) * 2.0f);
        const int baseLifetime = qMin(source.lifetime, 5);
        BurstParticle particle;
        particle.position = m_origin + QVector3D(radius * qSin(angle),
                                                  radius * qCos(angle),
                                                  source.base.z() - m_origin.z());
        particle.scale = m_authoredScale * 0.1f;
        particle.frame = 1;
        particle.lifetime = baseLifetime + 1 + int(m_rng.next() % 4u);
        m_particles.append(particle);
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            BurstParticle& particle = m_particles[i];
            if(--particle.lifetime == 0)
                m_particles.removeAt(i);
            else
                particle.frame = qMin(15, particle.frame + 1);
        }
        if(!m_generating)
            return;

        const bool closingRing = m_sources[0].lifetime < 9;
        const int attempts = closingRing ? 72 : 27;
        for(int attempt = 0; attempt < attempts && m_particles.size() < 500; ++attempt)
        {
            if((m_rng.next() % 100u) + 1u <= 88u)
                closingRing ? spawnClosingRing() : spawnTail();
        }

        for(RingSource& source : m_sources)
        {
            if(--source.lifetime == 0)
            {
                m_generating = false;
                continue;
            }
            if(source.lifetime >= 27)
                source.radius += m_speedFactor;
            else if(source.lifetime >= 9)
                source.radius -= m_speedFactor;
            else
            {
                const float wanted = m_speedFactor * 20.0f;
                source.radius = wanted + (source.radius - wanted) * 0.5f;
            }
            if(source.lifetime >= 9)
            {
                source.angle += float(M_PI) / 18.0f;
                source.base.setZ(source.base.z() + source.zVelocity);
            }
        }
    }

    Isaac32 m_rng;
    QVector<RingSource> m_sources;
    QVector<BurstParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    float m_speedFactor{0.0f};
    quint32 m_seed{0};
    int m_cycleTick{0};
    bool m_generating{false};
    bool m_initialized{false};
};

class BansheeCastingEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 32;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        const int tick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || tick < m_cycleTick)
            reset(seed, origin, authoredScale);
        while(m_cycleTick < tick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<WeatherRenderSample> samples() const
    {
        QVector<WeatherRenderSample> result;
        result.reserve(m_particles.size());
        for(const BurstParticle& particle : m_particles)
        {
            WeatherRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(quint32 seed, const QVector3D& origin, float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x42414E53u);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        m_speedFactor = m_authoredScale / 18.0f;
        m_radius = 0.0f;
        m_lifetime = 8;
        m_particles.clear();
        m_cycleTick = 0;
        m_generating = true;
        m_initialized = true;
    }

    void spawn()
    {
        const float wanted = m_speedFactor * 30.0f;
        const float nextRadius = m_radius + (m_radius - wanted) * 0.5f;
        const float low = qMin(nextRadius, m_radius);
        const float high = qMax(nextRadius, m_radius);
        const float radius = high > low + 0.001f ? randomRange(low, high) : nextRadius;
        const float angle = randomRange(0.0f, float(M_PI) * 2.0f);
        const int baseLifetime = qMin(m_lifetime, 5);
        BurstParticle particle;
        particle.position = m_origin + QVector3D(radius * qSin(angle),
                                                  radius * qCos(angle),
                                                  m_authoredScale * 1.5f);
        particle.scale = m_authoredScale * 0.15f;
        particle.frame = 0;
        particle.lifetime = baseLifetime + 1 + int(m_rng.next() % 3u);
        m_particles.append(particle);
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            BurstParticle& particle = m_particles[i];
            if(--particle.lifetime == 0)
                m_particles.removeAt(i);
            else
                particle.frame = qMin(15, particle.frame + 1);
        }
        if(!m_generating)
            return;
        for(int attempt = 0; attempt < 72 && m_particles.size() < 500; ++attempt)
        {
            if((m_rng.next() % 100u) + 1u <= 88u)
                spawn();
        }
        if(--m_lifetime == 0)
        {
            m_generating = false;
            return;
        }
        const float wanted = m_speedFactor * 30.0f;
        m_radius = wanted + (m_radius - wanted) * 0.5f;
    }

    Isaac32 m_rng;
    QVector<BurstParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    float m_speedFactor{0.0f};
    float m_radius{0.0f};
    quint32 m_seed{0};
    int m_lifetime{8};
    int m_cycleTick{0};
    bool m_generating{false};
    bool m_initialized{false};
};

struct FireArrowParticle
{
    QVector3D position;
    float scale{0.1f};
    int lifetime{15};
    int frame{0};
    int phaseDelay{1};
    int special{0};
};

class FireArrowEmitter
{
public:
    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        if(!m_initialized || seed != m_seed || logicalTick < m_lastTick
                || logicalTick - m_lastTick > 4096)
            reset(logicalTick, seed, origin, authoredScale);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        while(m_lastTick < logicalTick)
        {
            advanceOne();
            ++m_lastTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const FireArrowParticle& particle : m_particles)
        {
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = 1.0f;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    void reset(qint64 logicalTick, quint32 seed, const QVector3D& origin,
               float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x46415252u);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        m_particles.clear();
        FireArrowParticle head;
        head.position = m_origin;
        head.scale = 0.3f * m_authoredScale * 0.4f;
        head.special = 2;
        m_particles.append(head);
        m_lastTick = logicalTick;
        m_initialized = true;
    }

    void spawnTrail()
    {
        FireArrowParticle particle;
        particle.position = m_origin;
        particle.scale = 0.3f * m_authoredScale * 0.2f;
        particle.special = (m_rng.next() % 100u) > 70u ? 1 : 0;
        particle.frame = 2 + int(m_rng.next() & 1u);
        m_particles.append(particle);
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            FireArrowParticle& particle = m_particles[i];
            if(particle.special == 2)
            {
                particle.position = m_origin;
                particle.frame = int(m_rng.next() % 3u);
                continue;
            }
            if(--particle.lifetime == 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            if(--particle.phaseDelay == 0)
            {
                if(++particle.frame >= 15)
                {
                    m_particles.removeAt(i);
                    continue;
                }
                particle.phaseDelay = 1;
            }
            if(particle.frame < 6)
                particle.scale *= 1.4f;
            else if(particle.special == 0)
                particle.scale *= 1.03f;
        }
        if(m_particles.size() < 500)
            spawnTrail();
    }

    Isaac32 m_rng;
    QVector<FireArrowParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    qint64 m_lastTick{0};
    bool m_initialized{false};
};

struct AcidRayParticle
{
    QVector3D position;
    float scale{0.1f};
    int lifetime{15};
    int frame{0};
};

class AcidRayEmitter
{
public:
    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        if(!m_initialized || seed != m_seed || logicalTick < m_lastTick
                || logicalTick - m_lastTick > 4096)
            reset(logicalTick, seed, origin, authoredScale);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        while(m_lastTick < logicalTick)
        {
            advanceOne();
            ++m_lastTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const AcidRayParticle& particle : m_particles)
        {
            if(particle.scale <= 0.0f)
                continue;
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = 1.0f;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    static float sizeAt(int age)
    {
        static constexpr std::array<float, 15> sizes = {
            1.10f, 0.85f, 0.93f, 1.00f, 0.97f,
            0.93f, 0.87f, 0.80f, 0.73f, 0.65f,
            0.55f, 0.43f, 0.30f, 0.20f, 0.00f
        };
        return sizes[size_t(qBound(0, age, 14))];
    }

    void reset(qint64 logicalTick, quint32 seed, const QVector3D& origin,
               float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x41434944u);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        m_particles.clear();
        m_lastTick = logicalTick;
        m_initialized = true;
    }

    void spawn()
    {
        AcidRayParticle particle;
        particle.position = m_origin;
        particle.scale = 0.3f * m_authoredScale * sizeAt(0);
        particle.frame = int(m_rng.next() & 1u);
        m_particles.append(particle);
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            AcidRayParticle& particle = m_particles[i];
            if(--particle.lifetime == 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            const int age = 15 - particle.lifetime;
            particle.scale = 0.3f * m_authoredScale * sizeAt(age);
            if(++particle.frame >= 15)
                m_particles.removeAt(i);
        }
        if(m_particles.size() < 500)
            spawn();
    }

    Isaac32 m_rng;
    QVector<AcidRayParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    qint64 m_lastTick{0};
    bool m_initialized{false};
};

struct BlueGasParticle
{
    QVector3D offset;
    float angularStep{0.0f};
    float verticalSpeed{0.0f};
    float scale{0.1f};
    float alpha{1.0f};
    int lifetime{45};
    int frame{0};
    int phaseDelay{3};
};

class BlueGasEmitter
{
public:
    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float syntheticCarrierBound)
    {
        if(!m_initialized || seed != m_seed || logicalTick < m_lastTick
                || logicalTick - m_lastTick > 4096)
            reset(logicalTick, seed, origin, syntheticCarrierBound);
        m_origin = origin;
        m_bound = qMax(0.001f, syntheticCarrierBound);
        while(m_lastTick < logicalTick)
        {
            advanceOne();
            ++m_lastTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const BlueGasParticle& particle : m_particles)
        {
            EffectRenderSample sample;
            sample.position = m_origin + particle.offset;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(qint64 logicalTick, quint32 seed, const QVector3D& origin,
               float syntheticCarrierBound)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x42474153u);
        m_origin = origin;
        m_bound = qMax(0.001f, syntheticCarrierBound);
        m_particles.clear();
        m_lastTick = logicalTick;
        m_initialized = true;
    }

    void spawn()
    {
        const float radius = 0.3f * m_bound;
        const float height = 2.0f * m_bound;
        const float radial = randomRange(0.0f, radius);
        const float angle = randomRange(0.0f, float(M_PI) * 2.0f);
        BlueGasParticle particle;
        particle.offset = QVector3D(qSin(angle) * radial, qCos(angle) * radial,
                                    randomRange(-0.3f * radius, 0.3f * radius));
        particle.angularStep = radius > 0.0f ? 0.3f * radial / radius : 0.0f;
        particle.verticalSpeed = randomRange(0.02f * height, 0.03f * height);
        particle.scale = radius * 0.7f;
        m_particles.append(particle);
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            BlueGasParticle& particle = m_particles[i];
            if(--particle.lifetime == 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            if(--particle.phaseDelay == 0)
            {
                if(++particle.frame >= 15)
                {
                    m_particles.removeAt(i);
                    continue;
                }
                particle.phaseDelay = 3;
            }
            const float sine = qSin(particle.angularStep);
            const float cosine = qCos(particle.angularStep);
            const float oldX = particle.offset.x();
            const float oldY = particle.offset.y();
            particle.offset.setX(oldX * cosine - oldY * sine);
            particle.offset.setY(oldX * sine + oldY * cosine);
            particle.offset.setZ(particle.offset.z() + particle.verticalSpeed);
            particle.verticalSpeed *= 1.02f;
            particle.scale *= 1.01f;
            if(particle.lifetime < 8)
                particle.alpha = qMax(0.0f, particle.alpha - 32.0f / 255.0f);
        }

        const int attempts = qMax(1, qRound(5.0f * m_bound));
        int spawned = 0;
        for(int attempt = 0; attempt < attempts && m_particles.size() < 500; ++attempt)
        {
            if((m_rng.next() % 100u) + 1u <= 80u)
            {
                spawn();
                ++spawned;
            }
        }
        if(spawned == 0 && m_particles.size() < 500)
            spawn();
    }

    Isaac32 m_rng;
    QVector<BlueGasParticle> m_particles;
    QVector3D m_origin;
    float m_bound{1.0f};
    quint32 m_seed{0};
    qint64 m_lastTick{0};
    bool m_initialized{false};
};

struct LinkParticle
{
    QVector3D position;
    QVector3D radialOffset;
    float stepCos{1.0f};
    float stepSin{0.0f};
    float progression{0.0f};
    float progressionRate{0.1f};
    float scale{0.1f};
    float alpha{0.0f};
    int frame{0};
};

class LinkEmitter
{
public:
    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        if(!m_initialized || seed != m_seed || logicalTick < m_lastTick
                || logicalTick - m_lastTick > 4096)
            reset(logicalTick, seed, origin, authoredScale);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        while(m_lastTick < logicalTick)
        {
            advanceOne();
            ++m_lastTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const LinkParticle& particle : m_particles)
        {
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(qint64 logicalTick, quint32 seed, const QVector3D& origin,
               float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x4C494E4Bu);
        m_origin = origin;
        m_authoredScale = qMax(0.001f, authoredScale);
        m_particles.clear();
        m_lastTick = logicalTick;
        m_initialized = true;
    }

    void spawn()
    {
        const float randomX = randomRange(-1.0f, 1.0f);
        const float randomY = randomRange(-1.0f, 1.0f);
        LinkParticle particle;
        particle.radialOffset = QVector3D(-0.2f * m_authoredScale * randomX,
                                          -0.2f * m_authoredScale * randomY,
                                          randomRange(-0.2f, 0.2f)
                                              * m_authoredScale);
        const float radialDistance = qSqrt(randomX * randomX + randomY * randomY);
        const float angularStep = 0.2f * radialDistance;
        particle.stepCos = qCos(angularStep);
        particle.stepSin = qSin(angularStep);
        particle.position = m_origin + particle.radialOffset;
        if((m_rng.next() & 3u) == 0u && radialDistance < 0.5f)
        {
            particle.progressionRate = randomRange(0.10f, 0.12f);
            particle.scale = m_authoredScale * randomRange(0.13f, 0.16f);
        }
        else
        {
            particle.progressionRate = randomRange(0.17f, 0.25f);
            particle.scale = m_authoredScale * randomRange(0.03f, 0.05f);
        }
        particle.frame = int(m_rng.next() % 16u);
        m_particles.append(particle);
    }

    void advanceOne()
    {
        constexpr float wallLength = 10.0f;
        const QVector3D wallDirection(-wallLength, 0.0f, 0.0f);
        const QVector3D forward(-1.0f, 0.0f, 0.0f);
        const QVector3D radialX(0.0f, 1.0f, 0.0f);
        const QVector3D radialY(0.0f, 0.0f, -1.0f);
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            LinkParticle& particle = m_particles[i];
            particle.frame = (particle.frame + 1) % 15;
            particle.progression += particle.progressionRate;
            if(particle.progression > wallLength)
            {
                m_particles.removeAt(i);
                continue;
            }
            const float oldX = particle.radialOffset.x();
            const float oldY = particle.radialOffset.y();
            particle.radialOffset.setX(oldX * particle.stepCos
                                       - oldY * particle.stepSin);
            particle.radialOffset.setY(oldX * particle.stepSin
                                       + oldY * particle.stepCos);
            const float part = particle.progression / wallLength;
            particle.position = m_origin
                + radialX * particle.radialOffset.x()
                + radialY * particle.radialOffset.y()
                + forward * particle.radialOffset.z()
                + wallDirection * part;
            particle.position.setZ(particle.position.z()
                                   + part * (1.0f - part) * wallLength * 0.9f);
            particle.alpha = qBound(0.0f,
                                    1000.0f * qMin(part, 1.0f - part) / 255.0f,
                                    1.0f);
        }

        int spawned = 0;
        for(int attempt = 0; attempt < 10 && m_particles.size() < 500; ++attempt)
        {
            if((m_rng.next() % 100u) + 1u <= 80u)
            {
                spawn();
                ++spawned;
            }
        }
        if(spawned == 0 && m_particles.size() < 500)
            spawn();
    }

    Isaac32 m_rng;
    QVector<LinkParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    qint64 m_lastTick{0};
    bool m_initialized{false};
};

struct SphereShellParticle
{
    QVector3D offset;
    QVector3D position;
    float sizeMultiplier{1.0f};
    float scale{0.01f};
    int lifetime{15};
    int frame{0};
};

class SphereShellEmitter
{
public:
    enum class Kind { Acid, Electricity, Fire, Antimagic };

    explicit SphereShellEmitter(Kind kind = Kind::Acid) : m_kind(kind) {}

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float syntheticCarrierBound)
    {
        if(!m_initialized || seed != m_seed || logicalTick < m_lastTick
                || logicalTick - m_lastTick > 4096)
            reset(logicalTick, seed, origin, syntheticCarrierBound);
        m_origin = origin;
        m_bound = qMax(0.001f, syntheticCarrierBound);
        while(m_lastTick < logicalTick)
        {
            advanceOne();
            ++m_lastTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const SphereShellParticle& particle : m_particles)
        {
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = 1.0f;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float sizeAt(int lifetime) const
    {
        if(m_kind == Kind::Antimagic)
        {
            static constexpr std::array<float, 6> sizes = {
                0.010f, 0.015f, 0.020f, 0.030f, 0.015f, 0.010f
            };
            return sizes[size_t(qBound(0, lifetime, 5))];
        }
        static constexpr std::array<float, 16> sizes = {
            0.010f, 0.020f, 0.030f, 0.035f,
            0.035f, 0.035f, 0.035f, 0.035f,
            0.035f, 0.035f, 0.035f, 0.035f,
            0.035f, 0.030f, 0.020f, 0.010f
        };
        return sizes[size_t(qBound(0, lifetime, 15))];
    }

    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    QVector3D randomBallDirection()
    {
        for(int attempt = 0; attempt < 10; ++attempt)
        {
            QVector3D value(randomRange(-1.0f, 1.0f),
                            randomRange(-1.0f, 1.0f),
                            randomRange(-1.0f, 1.0f));
            const float lengthSquared = value.lengthSquared();
            if(lengthSquared > 0.000001f && lengthSquared <= 1.0f)
                return value / qSqrt(lengthSquared);
        }
        return QVector3D(0.0f, 0.0f, 1.0f);
    }

    void reset(qint64 logicalTick, quint32 seed, const QVector3D& origin,
               float syntheticCarrierBound)
    {
        m_seed = seed;
        const quint32 salt = m_kind == Kind::Acid ? 0x53504143u
            : m_kind == Kind::Electricity ? 0x5350454Cu
            : m_kind == Kind::Fire ? 0x53504649u : 0x414E5449u;
        m_rng.reset(seed ^ salt);
        m_origin = origin;
        m_bound = qMax(0.001f, syntheticCarrierBound);
        m_particles.clear();
        m_lastTick = logicalTick;
        m_initialized = true;
    }

    void updateScale(SphereShellParticle& particle) const
    {
        const float radius = 1.2f * m_bound;
        const float typeFactor = m_kind == Kind::Acid ? 1.6f
            : m_kind == Kind::Fire ? 2.0f : 1.0f;
        particle.scale = particle.sizeMultiplier * radius
            * sizeAt(particle.lifetime) * typeFactor;
    }

    void spawn()
    {
        SphereShellParticle particle;
        if(m_rng.next() % 11u == 0u)
            particle.sizeMultiplier = m_kind == Kind::Electricity
                ? randomRange(3.5f, 4.5f)
                : m_kind == Kind::Antimagic ? randomRange(2.5f, 3.5f)
                : randomRange(1.5f, 2.5f);
        else
            particle.sizeMultiplier = randomRange(0.7f, 1.3f);
        const float radius = 1.2f * m_bound;
        particle.offset = randomBallDirection() * (0.65f * radius);
        particle.position = m_origin + particle.offset;
        particle.frame = int(m_rng.next() % 11u);
        particle.lifetime = m_kind == Kind::Antimagic ? 4 : 15;
        updateScale(particle);
        m_particles.append(particle);
    }

    void advanceOne()
    {
        constexpr float rotation = 0.01f;
        const float sine = qSin(rotation);
        const float cosine = qCos(rotation);
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            SphereShellParticle& particle = m_particles[i];
            if(--particle.lifetime == 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            particle.frame = (particle.frame + 1) % 15;
            const float oldX = particle.offset.x();
            const float oldY = particle.offset.y();
            particle.offset.setX(oldX * cosine - oldY * sine);
            particle.offset.setY(oldX * sine + oldY * cosine);
            particle.position = m_origin + particle.offset;
            updateScale(particle);
        }

        const float attemptMultiplier = m_kind == Kind::Antimagic ? 24.0f : 6.0f;
        const int attempts = qMax(1, qRound(attemptMultiplier * m_bound));
        int spawned = 0;
        for(int attempt = 0; attempt < attempts && m_particles.size() < 500; ++attempt)
        {
            if((m_rng.next() % 100u) + 1u <= 90u)
            {
                spawn();
                ++spawned;
            }
        }
        if(spawned == 0 && m_particles.size() < 500)
            spawn();
    }

    Isaac32 m_rng;
    QVector<SphereShellParticle> m_particles;
    QVector3D m_origin;
    float m_bound{1.0f};
    quint32 m_seed{0};
    qint64 m_lastTick{0};
    Kind m_kind{Kind::Acid};
    bool m_initialized{false};
};

class SphereAcidEmitter : public SphereShellEmitter
{
public:
    SphereAcidEmitter() : SphereShellEmitter(Kind::Acid) {}
};

class SphereElectricityEmitter : public SphereShellEmitter
{
public:
    SphereElectricityEmitter() : SphereShellEmitter(Kind::Electricity) {}
};

class SphereFireEmitter : public SphereShellEmitter
{
public:
    SphereFireEmitter() : SphereShellEmitter(Kind::Fire) {}
};

class AntimagicEmitter : public SphereShellEmitter
{
public:
    AntimagicEmitter() : SphereShellEmitter(Kind::Antimagic) {}
};

struct ModifierSource
{
    QVector3D position;
    QVector3D previous;
    QVector3D direction;
    int age{0};
};

struct ModifierParticle
{
    QVector3D position;
    QVector3D segmentPoint;
    QVector3D jitter;
    float scale{0.06f};
    float alpha{0.0f};
    int lifetime{5};
    int frame{12};
    int phaseDelay{2};
    int source{-1};
    bool anchor{false};
};

class ModifierEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 48;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float signedAuthoredScale, int subtype)
    {
        subtype = qBound(1, subtype, 12);
        const float scale = qFuzzyIsNull(signedAuthoredScale)
            ? 1.0f : signedAuthoredScale;
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || cycleTick < m_cycleTick
                || subtype != m_subtype || !qFuzzyCompare(scale, m_authoredScale))
            reset(seed, origin, scale, subtype);
        m_origin = origin;
        while(m_cycleTick < cycleTick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const ModifierParticle& particle : m_particles)
        {
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    static float birthAlpha(int age)
    {
        static constexpr std::array<int, 11> values = {
            0, 32, 64, 128, 255, 207, 159, 111, 63, 15, 0
        };
        return float(values[size_t(qBound(0, age, 10))]) / 255.0f;
    }

    static float anchorAlpha(int age, bool negative)
    {
        static constexpr std::array<int, 13> positive = {
            0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 128, 0
        };
        static constexpr std::array<int, 13> negativeValues = {
            0, 128, 255, 255, 255, 255, 255, 255, 192, 128, 64, 32, 0
        };
        const auto& values = negative ? negativeValues : positive;
        return float(values[size_t(qBound(0, age, 12))]) / 255.0f;
    }

    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    QVector3D randomSourceDirection(const QVector3D& axis)
    {
        for(int attempt = 0; attempt < 32; ++attempt)
        {
            QVector3D value(randomRange(-1.0f, 1.0f),
                            randomRange(-1.0f, 1.0f),
                            randomRange(-1.0f, 1.0f));
            if(!axis.isNull())
                value -= axis * QVector3D::dotProduct(axis, value);
            if(value.lengthSquared() > 0.000001f)
                return value.normalized();
        }
        return axis.isNull() ? QVector3D(0.0f, 0.0f, 1.0f)
                             : QVector3D(1.0f, 0.0f, 0.0f);
    }

    void reset(quint32 seed, const QVector3D& origin, float signedAuthoredScale,
               int subtype)
    {
        m_seed = seed;
        m_rng.reset(seed ^ (0x4D4F4400u + quint32(subtype)));
        m_origin = origin;
        m_authoredScale = signedAuthoredScale;
        m_subtype = subtype;
        m_particles.clear();
        m_sources.clear();
        const float radius = qMax(0.001f, qAbs(m_authoredScale));
        const float diameter = 2.0f * radius;
        const int sourceCount = qMax(1, qRound(qAbs(m_authoredScale) * 10.0f));
        QVector3D axis;
        if(m_subtype <= 4)
            axis = QVector3D(0.0f, 0.0f, 1.0f);
        else if(m_subtype <= 8)
            axis = QVector3D(0.0f, 1.0f, 0.0f);
        float centerZ = radius;
        if(m_subtype == 2 || m_subtype == 3)
            centerZ += 0.4f * diameter;
        else if(m_subtype == 4)
            centerZ -= 0.4f * diameter;
        const float initialLength = m_authoredScale < 0.0f
            ? diameter / 23.0f : diameter;
        m_sources.reserve(sourceCount);
        for(int i = 0; i < sourceCount; ++i)
        {
            ModifierSource source;
            source.direction = randomSourceDirection(axis) * initialLength;
            source.position = m_origin + source.direction
                + QVector3D(0.0f, 0.0f, centerZ);
            source.previous = source.position;
            m_sources.append(source);
        }
        m_generating = true;
        m_firstCreationPass = true;
        m_cycleTick = 0;
        m_initialized = true;
    }

    void updateSources()
    {
        const float radius = qMax(0.001f, qAbs(m_authoredScale));
        const float diameter = 2.0f * radius;
        float centerZ = radius;
        if(m_subtype == 2 || m_subtype == 3)
            centerZ += 0.4f * diameter;
        else if(m_subtype == 4)
            centerZ -= 0.4f * diameter;
        const float factor = m_authoredScale < 0.0f ? 1.4f : 1.0f / 1.4f;
        for(ModifierSource& source : m_sources)
        {
            const int oldAge = source.age++;
            if(oldAge >= 10)
            {
                m_generating = false;
                continue;
            }
            source.previous = source.position;
            source.position = m_origin + source.direction
                + QVector3D(0.0f, 0.0f, centerZ);
            source.direction *= factor;
        }
    }

    void spawn(int number)
    {
        if(m_sources.isEmpty())
            return;
        ModifierParticle particle;
        const int age = m_sources[0].age;
        particle.alpha = birthAlpha(age);
        if(age == 0 && number < m_sources.size())
        {
            particle.position = m_sources[number].position;
            particle.scale = m_authoredScale < 0.0f ? 0.2f : 0.125f;
            particle.lifetime = 25;
            particle.frame = m_subtype - 1;
            particle.source = number;
            particle.anchor = true;
            m_particles.append(particle);
            return;
        }

        const int sourceIndex = int(m_rng.next() % quint32(m_sources.size()));
        const float part = m_rng.unit();
        const ModifierSource& source = m_sources[sourceIndex];
        particle.segmentPoint = source.position * part
            + source.previous * (1.0f - part);
        particle.jitter = QVector3D(randomRange(-0.04f, 0.04f),
                                    randomRange(-0.04f, 0.04f),
                                    randomRange(-0.04f, 0.04f));
        particle.position = source.previous;
        particle.frame = 12 + int(m_rng.next() & 3u);
        m_particles.append(particle);
    }

    void updateParticles()
    {
        const bool negative = m_authoredScale < 0.0f;
        const float anchorFactor = negative ? 0.9345793724060059f : 1.0700000524520874f;
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            ModifierParticle& particle = m_particles[i];
            if(--particle.lifetime == 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            if(particle.anchor)
            {
                const ModifierSource& source = m_sources[particle.source];
                particle.position = source.position;
                particle.scale *= anchorFactor;
                particle.alpha = anchorAlpha(source.age, negative);
                continue;
            }
            if(--particle.phaseDelay == 0)
            {
                if(++particle.frame >= 16)
                    particle.frame = 12;
                particle.phaseDelay = 2;
            }
            particle.jitter *= 0.9f;
            particle.position = particle.segmentPoint + particle.jitter;
            particle.scale *= 0.9f;
            particle.alpha = qMax(0.0f, particle.alpha - 48.0f / 255.0f);
        }
    }

    void spawnPass()
    {
        if(!m_generating)
            return;
        for(int attempt = 0; attempt < 80 && m_particles.size() < 500; ++attempt)
            spawn(attempt);
    }

    void advanceOne()
    {
        if(m_firstCreationPass)
        {
            spawnPass();
            m_firstCreationPass = false;
            return;
        }
        updateSources();
        updateParticles();
        spawnPass();
    }

    Isaac32 m_rng;
    QVector<ModifierSource> m_sources;
    QVector<ModifierParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    int m_subtype{1};
    int m_cycleTick{0};
    bool m_generating{true};
    bool m_firstCreationPass{true};
    bool m_initialized{false};
};

struct ClayRingSource
{
    QVector3D position;
    QVector3D previous;
    QVector3D direction;
    int lifetime{14};
};

struct ClayRingParticle
{
    QVector3D position;
    QVector3D segmentPoint;
    QVector3D jitter;
    float scale{0.1f};
    float alpha{0.0f};
    int lifetime{5};
    int frame{1};
    int phaseDelay{2};
    int source{-1};
    bool anchor{false};
};

class ClayRingEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 64;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float signedAuthoredScale)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || cycleTick < m_cycleTick)
            reset(seed, origin, signedAuthoredScale);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(signedAuthoredScale)
            ? 1.0f : signedAuthoredScale;
        while(m_cycleTick < cycleTick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const ClayRingParticle& particle : m_particles)
        {
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    static float radiusAt(int index)
    {
        static constexpr std::array<float, 16> radii = {
            0.000f, 0.025f, 0.056f, 0.092f,
            0.136f, 0.188f, 0.251f, 0.326f,
            0.417f, 0.525f, 0.656f, 0.812f,
            1.000f, 1.000f, 1.000f, 1.000f
        };
        return radii[size_t(qBound(0, index, 15))];
    }

    static float alphaAt(int index)
    {
        static constexpr std::array<int, 16> alpha = {
            0, 128, 255, 255, 255, 255, 255, 255,
            255, 160, 80, 32, 0, 0, 0, 0
        };
        return float(alpha[size_t(qBound(0, index, 15))]) / 255.0f;
    }

    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(quint32 seed, const QVector3D& origin, float signedAuthoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x434C4159u);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(signedAuthoredScale)
            ? 1.0f : signedAuthoredScale;
        m_sources.clear();
        m_particles.clear();
        m_sources.reserve(10);
        for(int i = 0; i < 10; ++i)
        {
            const float angle = 0.62832f * float(i);
            ClayRingSource source;
            source.position = source.previous = origin;
            source.direction = QVector3D(qSin(angle), qCos(angle), 0.0f);
            m_sources.append(source);
        }
        m_generating = true;
        m_cycleTick = 0;
        m_initialized = true;
    }

    void updateSources()
    {
        for(ClayRingSource& source : m_sources)
        {
            if(--source.lifetime < 0)
            {
                m_generating = false;
                continue;
            }
            source.previous = source.position;
            const int index = m_authoredScale < 0.0f
                ? source.lifetime : 14 - source.lifetime;
            source.position = m_origin
                + source.direction * (m_authoredScale * radiusAt(index));
            source.position.setZ(m_origin.z() + 1.0f);
            const float oldX = source.direction.x();
            const float oldY = source.direction.y();
            source.direction.setX(oldX * 0.95f + oldY * 0.31225f);
            source.direction.setY(-oldX * 0.31225f + oldY * 0.95f);
        }
    }

    void spawn(int number)
    {
        ClayRingParticle particle;
        const int sourceLife = m_sources.isEmpty() ? 0 : m_sources[0].lifetime;
        particle.alpha = alphaAt(sourceLife);
        if(sourceLife == 13 && number < m_sources.size())
        {
            particle.position = m_sources[number].position;
            particle.scale = 0.2f;
            particle.lifetime = 15;
            particle.frame = 0;
            particle.source = number;
            particle.anchor = true;
            m_particles.append(particle);
            return;
        }

        const int sourceIndex = int(m_rng.next() % quint32(m_sources.size()));
        const float part = m_rng.unit();
        const ClayRingSource& source = m_sources[sourceIndex];
        particle.segmentPoint = source.position * part + source.previous * (1.0f - part);
        particle.jitter = QVector3D(randomRange(-0.1f, 0.1f),
                                    randomRange(-0.1f, 0.1f),
                                    randomRange(-0.1f, 0.1f));
        particle.position = source.previous;
        m_particles.append(particle);
    }

    void advanceOne()
    {
        updateSources();
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            ClayRingParticle& particle = m_particles[i];
            if(--particle.lifetime == 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            if(particle.anchor)
            {
                const int sourceLife = m_sources.isEmpty() ? 0 : m_sources[0].lifetime;
                particle.alpha = alphaAt(sourceLife);
                particle.position = m_sources[particle.source].position;
                particle.scale = 0.2f;
                continue;
            }
            if(--particle.phaseDelay == 0)
            {
                if(++particle.frame >= 16)
                {
                    m_particles.removeAt(i);
                    continue;
                }
                particle.phaseDelay = 2;
            }
            particle.jitter *= 0.9f;
            particle.position = particle.segmentPoint + particle.jitter;
            particle.scale *= 0.9f;
            particle.alpha = qMax(0.0f, particle.alpha - 48.0f / 255.0f);
        }

        if(!m_generating || m_sources.isEmpty())
            return;
        for(int attempt = 0; attempt < 80 && m_particles.size() < 500; ++attempt)
            spawn(attempt);
    }

    Isaac32 m_rng;
    QVector<ClayRingSource> m_sources;
    QVector<ClayRingParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    int m_cycleTick{0};
    bool m_generating{true};
    bool m_initialized{false};
};

struct TeleportParticle
{
    QVector3D position;
    float verticalOffset{0.0f};
    float verticalFactor{1.2f};
    float visibleScale{0.0f};
    float targetScale{0.2f};
    float alpha{1.0f};
    float alphaStep{-19.0f / 255.0f};
    int lifetime{14};
    int frame{0};
};

class TeleportEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 64;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float signedAuthoredScale)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || cycleTick < m_cycleTick)
            reset(seed, origin, signedAuthoredScale);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(signedAuthoredScale)
            ? 1.0f : signedAuthoredScale;
        while(m_cycleTick < cycleTick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const TeleportParticle& particle : m_particles)
        {
            if(particle.visibleScale <= 0.0f)
                continue;
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.visibleScale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(quint32 seed, const QVector3D& origin, float signedAuthoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x54454C45u);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(signedAuthoredScale)
            ? 1.0f : signedAuthoredScale;
        m_particles.clear();
        m_sourceAge = 0;
        m_generating = true;
        m_cycleTick = 0;
        m_initialized = true;
    }

    void spawn()
    {
        const float radius = qAbs(m_authoredScale);
        const float unitRadius = m_rng.unit();
        const float radialPart = (unitRadius * unitRadius + unitRadius) * 0.5f;
        const float angle = m_rng.unit() * 6.2832f;
        TeleportParticle particle;
        particle.position = m_origin
            + QVector3D(radius * radialPart * qSin(angle),
                        radius * radialPart * qCos(angle),
                        randomRange(-0.4f, -0.2f) * radius);
        particle.targetScale = 0.2f * (2.0f - radialPart);
        particle.frame = m_rng.unit() > radialPart
            ? 4 + int(m_rng.next() % 12u) : int(m_rng.next() & 3u);
        if(m_authoredScale > 0.0f)
        {
            particle.verticalOffset = radius * 0.15f * (4.0f - radialPart);
            particle.verticalFactor = 1.2f;
            particle.alpha = 1.0f;
            particle.alphaStep = -19.0f / 255.0f;
        }
        else
        {
            particle.verticalOffset = radius * 2.25f * (4.0f - radialPart);
            particle.verticalFactor = 1.0f / 1.2f;
            particle.alpha = 0.0f;
            particle.alphaStep = 19.0f / 255.0f;
        }
        particle.position.setZ(particle.position.z() + particle.verticalOffset);
        m_particles.append(particle);
    }

    void advanceOne()
    {
        if(++m_sourceAge > 35)
            m_generating = false;
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            TeleportParticle& particle = m_particles[i];
            if(--particle.lifetime == 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            ++particle.frame;
            if(particle.frame == 4)
                particle.frame = 0;
            if(particle.frame == 16)
                particle.frame = 4;
            particle.position.setZ(particle.position.z() - particle.verticalOffset);
            particle.verticalOffset *= particle.verticalFactor;
            particle.position.setZ(particle.position.z() + particle.verticalOffset);
            particle.visibleScale = particle.lifetime == 1 ? 0.0f : particle.targetScale;
            particle.alpha = qBound(0.0f, particle.alpha + particle.alphaStep, 1.0f);
        }
        if(!m_generating)
            return;
        for(int attempt = 0; attempt < 20 && m_particles.size() < 320; ++attempt)
            spawn();
    }

    Isaac32 m_rng;
    QVector<TeleportParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    int m_sourceAge{0};
    int m_cycleTick{0};
    bool m_generating{true};
    bool m_initialized{false};
};

struct ZoneExitParticle
{
    QVector3D position;
    float targetScale{0.0f};
    float visibleScale{0.0f};
    float alpha{0.0f};
    int lifetime{15};
    int frame{0};
};

class ZoneExitEmitter
{
public:
    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float)
    {
        if(!m_initialized || seed != m_seed || logicalTick < m_tick)
            reset(seed, origin);
        m_origin = origin;
        while(m_tick < logicalTick)
        {
            advanceOne();
            ++m_tick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const ZoneExitParticle& particle : m_particles)
        {
            if(particle.visibleScale <= 0.0f)
                continue;
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.visibleScale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    static float alphaAt(int lifetime)
    {
        static constexpr std::array<int, 16> alpha = {
            0, 80, 176, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 128, 64
        };
        return float(alpha[size_t(qBound(0, lifetime, 15))]) / 255.0f;
    }

    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(quint32 seed, const QVector3D& origin)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x5A4F4E45u);
        m_origin = origin;
        m_particles.clear();
        m_tick = 0;
        m_initialized = true;
    }

    void spawn()
    {
        const float r1 = randomRange(-1.0f, 1.0f);
        const float r2 = randomRange(-1.0f, 1.0f);
        ZoneExitParticle particle;
        // A bare PARTICL record leaves both planar basis parameters at zero.
        particle.position = QVector3D(m_origin.x(), m_origin.y(), m_origin.z() + 0.1f);
        particle.targetScale = (1.9f - r1 * r1 - r2 * r2)
            * 0.05f * randomRange(0.7f, 1.3f);
        particle.frame = int(m_rng.next() & 15u);
        m_particles.append(particle);
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            ZoneExitParticle& particle = m_particles[i];
            particle.alpha = alphaAt(particle.lifetime);
            if(--particle.lifetime < 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            particle.frame = (particle.frame & ~3) + ((particle.frame + 1) & 3);
            particle.visibleScale = particle.targetScale;
            particle.position.setZ(particle.position.z() + 0.05f);
        }
        for(int i = 0; i < 25 && m_particles.size() < 600; ++i)
            spawn();
    }

    Isaac32 m_rng;
    QVector<ZoneExitParticle> m_particles;
    QVector3D m_origin;
    quint32 m_seed{0};
    qint64 m_tick{0};
    bool m_initialized{false};
};

struct PathParticle
{
    QVector3D position;
    float alpha{0.0f};
    int lifetime{7};
};

class PathEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 24;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || cycleTick < m_cycleTick)
            reset(seed, origin);
        m_origin = origin;
        while(m_cycleTick < cycleTick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const PathParticle& particle : m_particles)
        {
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(0.07f);
            sample.alpha = particle.alpha;
            sample.frame = 15;
            sample.color = QVector3D(64.0f / 255.0f, 1.0f, 64.0f / 255.0f);
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    static float brightnessAt(int lifetime)
    {
        static constexpr std::array<float, 16> brightness = {
            0.0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
        };
        return brightness[size_t(qBound(0, lifetime, 15))];
    }

    void reset(quint32 seed, const QVector3D& origin)
    {
        m_seed = seed;
        m_origin = origin;
        m_particles.clear();
        m_generated = false;
        m_cycleTick = 0;
        m_initialized = true;
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            PathParticle& particle = m_particles[i];
            if(--particle.lifetime < 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            particle.alpha = brightnessAt(particle.lifetime);
        }
        if(!m_generated)
        {
            PathParticle particle;
            particle.position = m_origin + QVector3D(0.0f, 0.0f, 0.15f);
            m_particles.append(particle);
            m_generated = true;
        }
    }

    QVector<PathParticle> m_particles;
    QVector3D m_origin;
    quint32 m_seed{0};
    int m_cycleTick{0};
    bool m_generated{false};
    bool m_initialized{false};
};

struct PathDestinationParticle
{
    QVector3D position;
    QVector3D offset;
    float scale{0.0f};
    float alpha{0.0f};
    int lifetime{6};
};

class PathDestinationEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 24;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   bool failed)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || failed != m_failed
                || cycleTick < m_cycleTick)
            reset(seed, origin, failed);
        m_origin = origin;
        while(m_cycleTick < cycleTick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        const QVector3D color = m_failed
            ? QVector3D(1.0f, 64.0f / 255.0f, 64.0f / 255.0f)
            : QVector3D(64.0f / 255.0f, 1.0f, 64.0f / 255.0f);
        for(const PathDestinationParticle& particle : m_particles)
        {
            if(particle.scale <= 0.0f)
                continue;
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = 15;
            sample.color = color;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    static float alphaAt(int lifetime)
    {
        static constexpr std::array<int, 16> alpha = {
            0, 64, 144, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 128, 64
        };
        return float(alpha[size_t(qBound(0, lifetime, 15))]) / 255.0f;
    }

    void reset(quint32 seed, const QVector3D& origin, bool failed)
    {
        m_seed = seed;
        m_origin = origin;
        m_failed = failed;
        m_particles.clear();
        m_generated = false;
        m_cycleTick = 0;
        m_initialized = true;
    }

    void spawnBatch()
    {
        constexpr int ringCount = 128;
        const int count = m_failed ? 256 : ringCount;
        m_particles.reserve(count);
        for(int number = 0; number < count; ++number)
        {
            QVector3D offset;
            if(number < ringCount)
            {
                const float angle = float(number) * 6.29f / float(ringCount);
                offset = QVector3D(0.1f * qSin(angle), 0.1f * qCos(angle), 0.0f);
            }
            else
            {
                static constexpr std::array<float, 4> dx = {
                    0.707f, -0.707f, -0.707f, 0.707f
                };
                static constexpr std::array<float, 4> dy = {
                    0.707f, -0.707f, 0.707f, -0.707f
                };
                const int part = number - ringCount;
                const int arm = part / 32;
                const float length = float(part & 31) * (0.1f / 31.0f);
                offset = QVector3D(length * dx[size_t(arm)],
                                   length * dy[size_t(arm)], 0.0f);
            }
            PathDestinationParticle particle;
            particle.position = m_origin;
            particle.offset = offset;
            m_particles.append(particle);
        }
    }

    void advanceOne()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            PathDestinationParticle& particle = m_particles[i];
            if(--particle.lifetime < 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            particle.alpha = alphaAt(particle.lifetime);
            particle.position += particle.offset;
            particle.position.setZ(m_origin.z() + 0.1f);
            particle.scale = 0.07f;
            particle.offset *= 1.05f;
        }
        if(!m_generated)
        {
            spawnBatch();
            m_generated = true;
        }
    }

    QVector<PathDestinationParticle> m_particles;
    QVector3D m_origin;
    quint32 m_seed{0};
    int m_cycleTick{0};
    bool m_failed{false};
    bool m_generated{false};
    bool m_initialized{false};
};

struct MoshkaSource
{
    QVector3D position;
    QVector3D direction;
    QVector3D interpolationVelocity;
    int lifetime{0};
    int phase{0};
};

struct MoshkaParticle
{
    QVector3D position;
    QVector3D color{1.0f, 1.0f, 1.0f};
    float scale{0.0f};
    float alpha{0.0f};
    int lifetime{15};
    int frame{0};
    int source{-1};
};

class MoshkaEmitter
{
public:
    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        if(!m_initialized || seed != m_seed || logicalTick < m_tick)
            reset(seed, origin, authoredScale);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : qAbs(authoredScale);
        while(m_tick < logicalTick)
        {
            advanceOne();
            ++m_tick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const MoshkaParticle& particle : m_particles)
        {
            if(particle.scale <= 0.0f)
                continue;
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            sample.color = particle.color;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    static QVector3D colorAt(int phase)
    {
        static constexpr std::array<std::array<int, 3>, 12> colors = {{
            {{251, 254, 143}}, {{252, 227, 149}}, {{252, 201, 154}},
            {{255, 176, 154}}, {{255, 155, 163}}, {{252, 152, 176}},
            {{250, 150, 195}}, {{247, 149, 212}}, {{243, 147, 237}},
            {{239, 144, 249}}, {{239, 144, 249}}, {{239, 144, 249}}
        }};
        const auto& c = colors[size_t(qBound(0, phase, 11))];
        return QVector3D(float(c[0]) / 255.0f, float(c[1]) / 255.0f,
                         float(c[2]) / 255.0f);
    }

    static float alphaAt(int lifetime)
    {
        static constexpr std::array<int, 16> alpha = {
            0, 64, 112, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255
        };
        return float(alpha[size_t(qBound(0, lifetime, 15))]) / 255.0f;
    }

    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    QVector3D randomBallPoint()
    {
        QVector3D result;
        for(int safe = 0; safe < 10; ++safe)
        {
            result = QVector3D(randomRange(-1.0f, 1.0f),
                               randomRange(-1.0f, 1.0f),
                               randomRange(-1.0f, 1.0f));
            if(result.lengthSquared() < 1.0f)
                break;
        }
        return result;
    }

    float sizeAt(int phase) const
    {
        return qPow(0.93f, float(qBound(0, phase, 11)));
    }

    void reset(quint32 seed, const QVector3D& origin, float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x4D4F5348u);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : qAbs(authoredScale);
        m_sources.clear();
        m_sources.resize(80);
        for(MoshkaSource& source : m_sources)
            source.position = origin;
        m_particles.clear();
        m_firstCreationPass = true;
        m_tick = 0;
        m_initialized = true;
    }

    void updateSources()
    {
        for(MoshkaSource& source : m_sources)
        {
            if(--source.lifetime < 0)
            {
                source.phase = int(m_rng.next() % 3u);
                source.position = m_origin
                    + QVector3D(randomRange(-0.2f, 0.2f),
                                randomRange(-0.2f, 0.2f),
                                randomRange(-0.2f, 0.2f)) * m_authoredScale;
                source.direction = randomBallPoint() * (m_authoredScale * 0.2f);
                source.lifetime = 7 + int(m_rng.next() % 3u);
                source.interpolationVelocity = QVector3D();
                continue;
            }
            ++source.phase;
            if(m_rng.next() % 6u == 0u)
                source.direction += randomBallPoint() * (m_authoredScale * 0.05f);
            source.position += source.direction;
            source.interpolationVelocity = source.direction;
        }
    }

    void spawn()
    {
        if(m_particles.size() < 80)
        {
            MoshkaParticle anchor;
            anchor.source = m_particles.size();
            anchor.position = m_origin;
            anchor.lifetime = 15;
            anchor.frame = int(m_rng.next() & 3u);
            m_particles.append(anchor);
            return;
        }
        const int sourceIndex = int(m_rng.next() % quint32(m_sources.size()));
        const MoshkaSource& source = m_sources[sourceIndex];
        MoshkaParticle trail;
        trail.source = -1;
        trail.color = colorAt(source.phase);
        trail.lifetime = qMin(5, source.lifetime + 1);
        trail.frame = 4;
        trail.position = source.position - source.direction * m_rng.unit();
        trail.scale = sizeAt(source.phase) * 0.1f * m_authoredScale;
        m_particles.append(trail);
    }

    void updateParticles()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            MoshkaParticle& particle = m_particles[i];
            if(particle.source < 0)
            {
                if(--particle.lifetime <= 0)
                {
                    m_particles.removeAt(i);
                    continue;
                }
                ++particle.frame;
                particle.alpha = alphaAt(particle.lifetime);
                continue;
            }
            const MoshkaSource& source = m_sources[particle.source];
            particle.frame = (particle.frame + 1) & 3;
            particle.position = source.position;
            particle.scale = sizeAt(source.phase) * m_authoredScale * 0.15f;
            particle.color = colorAt(source.phase);
            particle.alpha = 1.0f;
            if(source.interpolationVelocity.isNull())
                particle.scale = m_authoredScale * 0.15f;
        }
    }

    void spawnPass()
    {
        for(int attempt = 0; attempt < 100 && m_particles.size() < 800; ++attempt)
            spawn();
    }

    void advanceOne()
    {
        if(m_firstCreationPass)
        {
            spawnPass();
            m_firstCreationPass = false;
            return;
        }
        updateSources();
        updateParticles();
        spawnPass();
    }

    Isaac32 m_rng;
    QVector<MoshkaSource> m_sources;
    QVector<MoshkaParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    qint64 m_tick{0};
    bool m_firstCreationPass{true};
    bool m_initialized{false};
};

struct PortalStarParticle
{
    QVector3D position;
    float scale{0.0f};
};

class PortalStarEmitter
{
public:
    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        if(!m_initialized || seed != m_seed || logicalTick < m_tick)
            reset(seed, origin, authoredScale);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : authoredScale;
        while(m_tick < logicalTick)
        {
            m_particle.position = m_origin;
            m_particle.scale = randomRange(0.8f, 1.2f) * m_authoredScale;
            ++m_tick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        if(m_particle.scale <= 0.0f)
            return {};
        EffectRenderSample sample;
        sample.position = m_particle.position;
        sample.scale = billboardDiameter(m_particle.scale);
        sample.alpha = 1.0f;
        sample.frame = 0;
        return {sample};
    }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(quint32 seed, const QVector3D& origin, float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x50535452u);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : authoredScale;
        m_particle = PortalStarParticle{};
        m_particle.position = origin;
        m_tick = 0;
        m_initialized = true;
    }

    Isaac32 m_rng;
    PortalStarParticle m_particle;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    qint64 m_tick{0};
    bool m_initialized{false};
};

struct PortalParticle
{
    QVector3D position;
    float radialX{0.0f};
    float radialY{0.0f};
    float cosine{1.0f};
    float sine{0.0f};
    float verticalStep{0.0f};
    float scale{0.0f};
    float targetScale{0.0f};
    float alpha{0.0f};
    int frame{0};
    int phaseDelay{3};
    bool head{false};
    bool terminal{false};
};

class PortalEmitter
{
public:
    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        if(!m_initialized || seed != m_seed || logicalTick < m_tick)
            reset(seed, origin, authoredScale);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : authoredScale;
        while(m_tick < logicalTick)
        {
            advanceOne();
            ++m_tick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const PortalParticle& particle : m_particles)
        {
            if(particle.scale <= 0.0f || particle.alpha <= 0.0f)
                continue;
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(quint32 seed, const QVector3D& origin, float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x504F5254u);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : authoredScale;
        m_particles.clear();
        m_firstCreationPass = true;
        m_tick = 0;
        m_initialized = true;
    }

    void spawn()
    {
        PortalParticle particle;
        if(m_particles.isEmpty())
        {
            particle.position = m_origin + QVector3D(0.0f, 0.0f, 6.4f);
            particle.frame = 15;
            particle.alpha = 96.0f / 255.0f;
            particle.head = true;
            m_particles.append(particle);
            return;
        }

        const float unitRadius = m_rng.unit();
        const float radialFraction = (unitRadius * unitRadius + unitRadius) * 0.5f;
        const float angle = randomRange(0.0f, 6.2832f);
        particle.radialX = 2.5f * radialFraction * qSin(angle);
        particle.radialY = 2.5f * radialFraction * qCos(angle);
        particle.position = m_origin
            + QVector3D(particle.radialX, particle.radialY, 0.0f);
        particle.targetScale = 0.3f * (2.0f - radialFraction);
        const bool inner = m_rng.unit() > radialFraction;
        const float angularStep = inner ? 0.1f : -0.1f;
        particle.cosine = qCos(angularStep);
        particle.sine = qSin(angularStep);
        particle.frame = inner ? 4 + int(m_rng.next() % 11u)
                               : int(m_rng.next() & 3u);
        particle.verticalStep = 0.0375f * (4.0f - radialFraction);
        particle.position.setZ(m_origin.z()
            + randomRange(-0.5f, 0.5f) * particle.verticalStep);
        particle.alpha = 64.0f / 255.0f;
        m_particles.append(particle);
    }

    void updateParticles()
    {
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            PortalParticle& particle = m_particles[i];
            if(particle.head)
            {
                particle.position = m_origin + QVector3D(0.0f, 0.0f, 6.4f);
                particle.scale = m_authoredScale >= 1.0f
                    ? randomRange(0.8f, 1.2f) * 1.5f : 0.0f;
                continue;
            }
            if(particle.terminal)
            {
                m_particles.removeAt(i);
                continue;
            }
            const float x = particle.cosine * particle.radialX
                + particle.sine * particle.radialY;
            const float y = -particle.sine * particle.radialX
                + particle.cosine * particle.radialY;
            particle.radialX = x;
            particle.radialY = y;
            particle.position.setX(m_origin.x() + x);
            particle.position.setY(m_origin.y() + y);
            particle.position.setZ(particle.position.z()
                                   + particle.verticalStep * m_authoredScale);
            particle.verticalStep *= 1.03f;
            if(--particle.phaseDelay == 0)
            {
                ++particle.frame;
                if(particle.frame == 4)
                    particle.frame = 0;
                if(particle.frame == 15)
                    particle.frame = 4;
                particle.phaseDelay = 3;
            }
            const float height = particle.position.z() - m_origin.z();
            if(height > 8.0f)
            {
                particle.scale = 0.0f;
                particle.alpha = 0.0f;
                particle.terminal = true;
                continue;
            }
            particle.scale = particle.targetScale;
            const int alpha = qMin(64, (55 + qRound((8.0f - height) * 25.0f)) >> 2);
            particle.alpha = float(qMax(0, alpha)) / 255.0f;
        }
    }

    void spawnPass(int attempts)
    {
        for(int i = 0; i < attempts && m_particles.size() < 800; ++i)
            spawn();
    }

    void advanceOne()
    {
        if(m_firstCreationPass)
        {
            spawnPass(50);
            m_firstCreationPass = false;
            return;
        }
        updateParticles();
        spawnPass(qMax(0, int(10.0f * m_authoredScale)));
    }

    Isaac32 m_rng;
    QVector<PortalParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    qint64 m_tick{0};
    bool m_firstCreationPass{true};
    bool m_initialized{false};
};

struct CylinderSource
{
    QVector3D offset;
    QVector3D velocity;
    float orbitCosine{1.0f};
    float orbitSine{0.0f};
    float verticalStep{0.0f};
    int lifetime{18};
};

struct CylinderParticle
{
    QVector3D position;
    QVector3D offset;
    QVector3D velocity;
    float scale{0.0f};
    float alpha{0.0f};
    int lifetime{0};
    int frame{0};
    int source{-1};
};

class CylinderEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 64;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale, bool reverse)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || reverse != m_reverse
                || cycleTick < m_cycleTick)
            reset(seed, origin, authoredScale, reverse);
        m_origin = origin;
        m_radius = qFuzzyIsNull(authoredScale) ? 1.0f : qAbs(authoredScale);
        while(m_cycleTick < cycleTick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const CylinderParticle& particle : m_particles)
        {
            if(particle.scale <= 0.0f || particle.alpha <= 0.0f)
                continue;
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    static float alphaAt(int lifetime)
    {
        static constexpr std::array<int, 32> alpha = {
            0, 32, 64, 128, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255
        };
        return float(alpha[size_t(qBound(0, lifetime, 31))]) / 255.0f;
    }

    void reset(quint32 seed, const QVector3D& origin, float authoredScale, bool reverse)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x43594C49u);
        m_origin = origin;
        m_radius = qFuzzyIsNull(authoredScale) ? 1.0f : qAbs(authoredScale);
        m_reverse = reverse;
        m_sources.clear();
        m_particles.clear();
        m_attempts = 30;
        m_generating = true;
        m_sourcesInitialized = false;
        m_firstCreationPass = true;
        m_cycleTick = 0;
        m_initialized = true;
    }

    void initializeSources()
    {
        m_sources.resize(2);
        const float circleRadius = 0.7f * m_radius;
        const float initialAngle = (m_reverse ? -1.0f : 1.0f)
            * (2.0f * float(M_PI) / 15.0f * 0.3f);
        for(int i = 0; i < 2; ++i)
        {
            const float angle = float(i) * float(M_PI);
            CylinderSource& source = m_sources[i];
            source.offset = QVector3D(circleRadius * qCos(angle),
                                      circleRadius * qSin(angle),
                                      0.2f * m_radius);
            source.orbitCosine = qCos(initialAngle);
            source.orbitSine = qSin(initialAngle);
            source.verticalStep = m_radius * (2.0f / 18.0f) * 0.04f;
            source.lifetime = 18;
        }
        m_sourcesInitialized = true;
    }

    bool spawn()
    {
        if(m_particles.size() < 2)
        {
            if(!m_sourcesInitialized)
                initializeSources();
            CylinderParticle anchor;
            anchor.source = m_particles.size();
            anchor.position = m_origin + m_sources[anchor.source].offset;
            m_particles.append(anchor);
            return true;
        }
        const int sourceIndex = int(m_rng.next() & 1u);
        const CylinderSource& source = m_sources[sourceIndex];
        const int lifetime = qMin(14, source.lifetime + 6);
        if(lifetime < 0 || (qFuzzyIsNull(source.velocity.x())
                && qFuzzyIsNull(source.velocity.y())))
            return false;
        CylinderParticle particle;
        particle.offset = source.offset - source.velocity * m_rng.unit();
        particle.velocity = source.velocity;
        particle.position = m_origin + particle.offset;
        particle.scale = 0.1f;
        particle.lifetime = lifetime;
        particle.frame = 1;
        m_particles.append(particle);
        return true;
    }

    void updateSources()
    {
        for(int i = 0; i < m_sources.size(); ++i)
        {
            CylinderSource& source = m_sources[i];
            if(i == 0)
            {
                if(source.lifetime & 1)
                    --m_attempts;
                --m_attempts;
            }
            if(source.lifetime-- <= 0)
            {
                m_generating = false;
                continue;
            }
            const QVector3D old = source.offset;
            QVector3D next;
            next.setX(old.x() * source.orbitCosine + old.y() * source.orbitSine);
            next.setY(-old.x() * source.orbitSine + old.y() * source.orbitCosine);
            next.setZ(old.z() + source.verticalStep);
            source.velocity = next - old;
            source.offset = next;
            source.verticalStep *= 1.25f;

            const float basisAngle = (m_reverse ? 1.0f : -1.0f)
                * (2.0f * float(M_PI) / 15.0f * 0.1f);
            const float c = qCos(basisAngle);
            const float s = qSin(basisAngle);
            const float oldC = source.orbitCosine;
            const float oldS = source.orbitSine;
            source.orbitCosine = oldC * c + oldS * s;
            source.orbitSine = -oldC * s + oldS * c;
        }
    }

    void updateParticles()
    {
        const float circleRadius = 0.7f * m_radius;
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            CylinderParticle& particle = m_particles[i];
            if(particle.source >= 0)
            {
                if(!m_generating)
                {
                    m_particles.removeAt(i);
                    continue;
                }
                const CylinderSource& source = m_sources[particle.source];
                particle.position = m_origin + source.offset;
                particle.scale = 0.3f;
                particle.alpha = alphaAt(source.lifetime);
                continue;
            }
            ++particle.frame;
            if(--particle.lifetime <= 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            particle.alpha = alphaAt(particle.lifetime);
            particle.offset += particle.velocity;
            const float horizontal = qSqrt(particle.offset.x() * particle.offset.x()
                                          + particle.offset.y() * particle.offset.y());
            if(horizontal > 0.0f)
            {
                particle.offset.setX(particle.offset.x() * circleRadius / horizontal);
                particle.offset.setY(particle.offset.y() * circleRadius / horizontal);
            }
            const float factor = particle.velocity.length() / circleRadius;
            particle.velocity = QVector3D(0.6f * factor * particle.offset.y(),
                                          -0.6f * factor * particle.offset.x(),
                                          0.6f * particle.velocity.z());
            particle.position = m_origin + particle.offset;
            particle.scale *= 0.9f;
        }
    }

    void spawnPass()
    {
        for(int i = 0; i < m_attempts && m_particles.size() < 600; ++i)
            spawn();
    }

    void advanceOne()
    {
        if(m_firstCreationPass)
        {
            spawnPass();
            m_firstCreationPass = false;
            return;
        }
        updateSources();
        updateParticles();
        if(m_generating && m_attempts > 0)
            spawnPass();
    }

    Isaac32 m_rng;
    QVector<CylinderSource> m_sources;
    QVector<CylinderParticle> m_particles;
    QVector3D m_origin;
    float m_radius{1.0f};
    quint32 m_seed{0};
    int m_attempts{30};
    int m_cycleTick{0};
    bool m_reverse{false};
    bool m_generating{true};
    bool m_sourcesInitialized{false};
    bool m_firstCreationPass{true};
    bool m_initialized{false};
};

struct StarFlashParticle
{
    QVector3D position;
    float scale{0.0f};
    float pendingScale{0.0f};
    int lifetime{3};
};

class StarFlashEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 16;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || cycleTick < m_cycleTick)
            reset(seed, origin, authoredScale);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : qAbs(authoredScale);
        while(m_cycleTick < cycleTick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        if(!m_active || m_particle.scale <= 0.0f)
            return {};
        EffectRenderSample sample;
        sample.position = m_particle.position;
        sample.scale = billboardDiameter(m_particle.scale);
        sample.alpha = 1.0f;
        sample.frame = 15;
        return {sample};
    }

    int activeCount() const { return m_active ? 1 : 0; }

private:
    void reset(quint32 seed, const QVector3D& origin, float authoredScale)
    {
        m_seed = seed;
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : qAbs(authoredScale);
        m_particle = StarFlashParticle{};
        m_active = false;
        m_spawned = false;
        m_cycleTick = 0;
        m_initialized = true;
    }

    void advanceOne()
    {
        if(!m_spawned)
        {
            m_particle.position = m_origin;
            m_particle.pendingScale = m_authoredScale;
            m_particle.scale = 0.0f;
            m_particle.lifetime = 3;
            m_active = true;
            m_spawned = true;
            return;
        }
        if(!m_active)
            return;
        if(--m_particle.lifetime <= 0)
        {
            m_particle.scale = 0.0f;
            m_active = false;
            return;
        }
        m_particle.position = m_origin;
        m_particle.scale = m_particle.pendingScale;
        m_particle.pendingScale *= 0.9f;
    }

    StarFlashParticle m_particle;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    int m_cycleTick{0};
    bool m_active{false};
    bool m_spawned{false};
    bool m_initialized{false};
};

struct SparksParticle
{
    QVector3D position;
    QVector3D velocity;
    float scale{0.0f};
    float alpha{1.0f};
    int lifetime{6};
    int frame{0};
    bool sourceFlash{false};
};

class SparksEmitter
{
public:
    static constexpr int kPreviewCycleTicks = 24;

    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale)
    {
        const int cycleTick = int(logicalTick % kPreviewCycleTicks);
        if(!m_initialized || seed != m_seed || cycleTick < m_cycleTick)
            reset(seed, origin, authoredScale);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : qAbs(authoredScale);
        while(m_cycleTick < cycleTick)
        {
            advanceOne();
            ++m_cycleTick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const SparksParticle& particle : m_particles)
        {
            if(particle.scale <= 0.0f || particle.alpha <= 0.0f)
                continue;
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = particle.alpha;
            sample.frame = particle.frame;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    QVector3D randomSphereDirection()
    {
        QVector3D direction(0.0f, 0.0f, 1.0f);
        for(int attempt = 0; attempt < 10; ++attempt)
        {
            direction = QVector3D(randomRange(-1.0f, 1.0f),
                                  randomRange(-1.0f, 1.0f),
                                  randomRange(-1.0f, 1.0f));
            const float lengthSquared = direction.lengthSquared();
            if(lengthSquared > 0.0f && lengthSquared < 1.0f)
                break;
        }
        return direction.lengthSquared() > 0.0f
            ? direction.normalized() : QVector3D(0.0f, 0.0f, 1.0f);
    }

    void reset(quint32 seed, const QVector3D& origin, float authoredScale)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x53504152u);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : qAbs(authoredScale);
        m_particles.clear();
        m_spawned = false;
        m_cycleTick = 0;
        m_initialized = true;
    }

    void spawnInitialPopulation()
    {
        for(int attempt = 0; attempt < 10; ++attempt)
        {
            SparksParticle particle;
            particle.position = m_origin;
            if(attempt == 0)
            {
                particle.frame = 0;
                particle.sourceFlash = true;
                particle.scale = 0.0f;
            }
            else
            {
                const float speedScale = randomRange(0.8f, 1.4f);
                particle.velocity = randomSphereDirection()
                    * (speedScale * speedScale * m_authoredScale * 0.2f);
                particle.position += particle.velocity * m_rng.unit();
                particle.scale = 0.2f * m_authoredScale
                    * randomRange(0.8f, 1.2f);
                particle.frame = 1 + int(m_rng.next() % 3u);
                particle.lifetime = 6;
            }
            m_particles.append(particle);
        }
        m_spawned = true;
    }

    void updateParticles()
    {
        static constexpr std::array<int, 8> alpha = {
            0, 64, 128, 255, 255, 255, 255, 255
        };
        for(int i = m_particles.size() - 1; i >= 0; --i)
        {
            SparksParticle& particle = m_particles[i];
            if(particle.sourceFlash)
            {
                if(qFuzzyIsNull(particle.scale))
                {
                    particle.scale = 0.55f * m_authoredScale
                        * randomRange(0.8f, 1.2f);
                    continue;
                }
                m_particles.removeAt(i);
                continue;
            }
            if(--particle.lifetime < 0)
            {
                m_particles.removeAt(i);
                continue;
            }
            particle.alpha = float(alpha[size_t(particle.lifetime)]) / 255.0f;
            particle.position += particle.velocity;
            particle.scale *= 0.9f;
            particle.velocity *= 0.6f;
            ++particle.frame;
        }
    }

    void advanceOne()
    {
        if(!m_spawned)
            spawnInitialPopulation();
        else
            updateParticles();
    }

    Isaac32 m_rng;
    QVector<SparksParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    int m_cycleTick{0};
    bool m_spawned{false};
    bool m_initialized{false};
};

struct VisionStarParticle
{
    QVector3D position;
    QVector3D offset;
    float scale{0.0f};
};

class VisionStarEmitter
{
public:
    void advanceTo(qint64 logicalTick, quint32 seed, const QVector3D& origin,
                   float authoredScale, int subtype)
    {
        subtype = qBound(0, subtype, 3);
        if(!m_initialized || seed != m_seed || logicalTick < m_tick
                || subtype != m_subtype)
            reset(seed, origin, authoredScale, subtype);
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : qAbs(authoredScale);
        while(m_tick < logicalTick)
        {
            advanceOne();
            ++m_tick;
        }
    }

    QVector<EffectRenderSample> samples() const
    {
        QVector<EffectRenderSample> result;
        result.reserve(m_particles.size());
        for(const VisionStarParticle& particle : m_particles)
        {
            if(particle.scale <= 0.0f)
                continue;
            EffectRenderSample sample;
            sample.position = particle.position;
            sample.scale = billboardDiameter(particle.scale);
            sample.alpha = 1.0f;
            sample.frame = m_subtype;
            result.append(sample);
        }
        return result;
    }

    int activeCount() const { return m_particles.size(); }

private:
    float randomRange(float low, float high)
    {
        return low + (high - low) * m_rng.unit();
    }

    void reset(quint32 seed, const QVector3D& origin, float authoredScale, int subtype)
    {
        m_seed = seed;
        m_rng.reset(seed ^ 0x56535452u ^ quint32(subtype));
        m_origin = origin;
        m_authoredScale = qFuzzyIsNull(authoredScale) ? 1.0f : qAbs(authoredScale);
        m_subtype = subtype;
        m_particles.clear();
        m_tick = 0;
        m_initialized = true;
    }

    void spawn(int index)
    {
        VisionStarParticle particle;
        particle.position = m_origin;
        if(m_subtype == 1)
        {
            const float angle = float(index) * (2.0f * float(M_PI) / 5.0f);
            particle.offset = QVector3D(0.5f * qCos(angle),
                                        0.5f * qSin(angle), 0.0f);
            particle.position += particle.offset;
        }
        m_particles.append(particle);
    }

    void updateParticles()
    {
        const float coefficient = m_subtype == 1 ? 0.17f : 0.3f;
        const float c = qCos(0.07f);
        const float s = qSin(0.07f);
        for(VisionStarParticle& particle : m_particles)
        {
            if(m_subtype == 1)
            {
                const float x = c * particle.offset.x() + s * particle.offset.y();
                const float y = -s * particle.offset.x() + c * particle.offset.y();
                particle.offset.setX(x);
                particle.offset.setY(y);
            }
            particle.position = m_origin + particle.offset;
            if(m_subtype == 3)
                particle.position.setZ(particle.position.z() + 0.7f);
            particle.scale = particle.scale * 0.95f
                + m_authoredScale * coefficient * randomRange(0.1f, 1.9f) * 0.05f;
        }
    }

    void spawnPass()
    {
        const int maximum = m_subtype == 1 ? 5 : 1;
        for(int attempt = 0; attempt < 3 && m_particles.size() < maximum; ++attempt)
            spawn(m_particles.size());
    }

    void advanceOne()
    {
        updateParticles();
        spawnPass();
    }

    Isaac32 m_rng;
    QVector<VisionStarParticle> m_particles;
    QVector3D m_origin;
    float m_authoredScale{1.0f};
    quint32 m_seed{0};
    qint64 m_tick{0};
    int m_subtype{0};
    bool m_initialized{false};
};

} // namespace particle
} // namespace ei

#endif // PARTICLE_ANIMATION_H
