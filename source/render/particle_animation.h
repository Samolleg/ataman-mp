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

} // namespace particle
} // namespace ei

#endif // PARTICLE_ANIMATION_H
