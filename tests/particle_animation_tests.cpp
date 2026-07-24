#include <QtTest>

#include "particle_animation.h"

class ParticleAnimationTests : public QObject
{
    Q_OBJECT

private slots:
    void atlasUsesNativeTopRowFirstInsets();
    void isaacPreviewStreamIsDeterministic();
    void nativeSubmittedScaleIsBillboardRadius();
    void authoredStrengthScalesEveryNativeMutatorField();
    void schedulerUsesAttemptsRatherThanSlots();
    void prewarmedEmittersAreDeterministicAndBounded();
    void fireOnlyNeverReachesSmokeFrames();
    void tornadoUsesNativePopulationAndPhasePartitions();
    void lightningBlastIsFiniteAndRepeatingForPreview();
    void nukeUsesFiniteNativeBurstAndImpactPhases();
    void castingUsesDeterministicSyntheticCarrierControl();
    void bansheeCastingUsesFiniteElevatedRing();
    void mushroomUsesFiniteRisingCloud();
    void blood1UsesFiniteSyntheticCarrierBurst();
    void firewallUsesTendriFirePhaseRange();
};

void ParticleAnimationTests::atlasUsesNativeTopRowFirstInsets()
{
    const float inset = 1.0f / 512.0f;
    QCOMPARE(ei::particle::atlasUv(0),
             QVector4D(inset, 1.0f - inset,
                       0.25f - 2.0f * inset, -0.25f + 2.0f * inset));
    QCOMPARE(ei::particle::atlasUv(7),
             QVector4D(0.75f + inset, 0.75f - inset,
                       0.25f - 2.0f * inset, -0.25f + 2.0f * inset));
    QCOMPARE(ei::particle::atlasUv(14),
             QVector4D(0.50f + inset, 0.25f - inset,
                       0.25f - 2.0f * inset, -0.25f + 2.0f * inset));
}

void ParticleAnimationTests::nativeSubmittedScaleIsBillboardRadius()
{
    QCOMPARE(ei::particle::billboardDiameter(0.8f), 1.6f);
    QCOMPARE(ei::particle::billboardDiameter(0.32f), 0.64f);
}

void ParticleAnimationTests::isaacPreviewStreamIsDeterministic()
{
    ei::particle::Isaac32 pinned(1234u);
    QCOMPARE(pinned.next(), quint32(0x760A7513u));
    QCOMPARE(pinned.next(), quint32(0xAF6737FBu));
    QCOMPARE(pinned.next(), quint32(0xF25CDBD8u));
    QCOMPARE(pinned.next(), quint32(0x9B07A5AAu));

    ei::particle::Isaac32 first(1234u);
    ei::particle::Isaac32 second(1234u);
    ei::particle::Isaac32 different(1235u);
    bool differs = false;
    for(int i = 0; i < 1024; ++i)
    {
        const quint32 value = first.next();
        QCOMPARE(value, second.next());
        differs = differs || value != different.next();
    }
    QVERIFY(differs);
}

void ParticleAnimationTests::authoredStrengthScalesEveryNativeMutatorField()
{
    const auto full = ei::particle::parametersForScale(1.0f);
    QCOMPARE(full.lowerSpawnBound, QVector3D(-0.2f, -0.2f, 0.0f));
    QCOMPARE(full.upperSpawnBound, QVector3D(0.2f, 0.2f, 0.0f));
    QCOMPARE(full.initialScale, 0.8f);
    QCOMPARE(full.verticalVelocity, 0.3f);
    QCOMPARE(full.displacementThreshold, 1000.0f);
    QCOMPARE(full.smallParticleExpansion, 0.24f);

    const auto campfire = ei::particle::parametersForScale(0.4f);
    QVERIFY(qAbs(campfire.lowerSpawnBound.x() + 0.08f) < 0.000001f);
    QVERIFY(qAbs(campfire.lowerSpawnBound.y() + 0.08f) < 0.000001f);
    QCOMPARE(campfire.lowerSpawnBound.z(), 0.0f);
    QVERIFY(qAbs(campfire.upperSpawnBound.x() - 0.08f) < 0.000001f);
    QVERIFY(qAbs(campfire.upperSpawnBound.y() - 0.08f) < 0.000001f);
    QCOMPARE(campfire.upperSpawnBound.z(), 0.0f);
    QVERIFY(qAbs(campfire.initialScale - 0.32f) < 0.000001f);
    QVERIFY(qAbs(campfire.verticalVelocity - 0.12f) < 0.000001f);
    QCOMPARE(campfire.displacementThreshold, 400.0f);
    QVERIFY(qAbs(campfire.smallParticleExpansion - 0.096f) < 0.000001f);
}

void ParticleAnimationTests::schedulerUsesAttemptsRatherThanSlots()
{
    ei::particle::TendriEmitter emitter;
    const QVector3D origin(1.0f, 2.0f, 3.0f);
    emitter.advanceTo(10, 7u, 0x2001u, origin, 0.8f,
                      QVector3D(), 0.0f, 0);
    QCOMPARE(emitter.activeCount(), 0);
    emitter.advanceTo(11, 7u, 0x2001u, origin, 0.8f,
                      QVector3D(), 0.0f, 0);
    QVERIFY(emitter.activeCount() >= 1); // >50 probability guarantees fallback
    QVERIFY(emitter.activeCount() <= ei::particle::kSpawnAttempts);
}

void ParticleAnimationTests::prewarmedEmittersAreDeterministicAndBounded()
{
    ei::particle::TendriEmitter first;
    ei::particle::TendriEmitter second;
    const QVector3D origin(4.0f, -2.0f, 1.0f);
    const QVector3D wind(-0.72819996f, -0.68536466f, 0.0f);
    first.advanceTo(100, 42u, 0x2001u, origin, 0.8f, wind, 0.2f, 60);
    second.advanceTo(100, 42u, 0x2001u, origin, 0.8f, wind, 0.2f, 60);
    const auto a = first.samples(0.375f);
    const auto b = second.samples(0.375f);
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > ei::particle::kSpawnAttempts);
    QVERIFY(a.size() <= ei::particle::kActiveLimit);
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(a[i].frame >= 0 && a[i].frame < ei::particle::kTerminalFrame);
    }
}

void ParticleAnimationTests::fireOnlyNeverReachesSmokeFrames()
{
    ei::particle::TendriEmitter emitter;
    emitter.advanceTo(200, 99u, 0x2003u, QVector3D(), 0.8f,
                      QVector3D(), 0.0f, 60);
    const auto samples = emitter.samples(0.5f);
    QVERIFY(!samples.isEmpty());
    for(const auto& sample : samples)
        QVERIFY(sample.frame >= 0 && sample.frame <= 7);
}

void ParticleAnimationTests::tornadoUsesNativePopulationAndPhasePartitions()
{
    ei::particle::TornadoEmitter emitter;
    emitter.advanceTo(1000, 8202u, QVector3D(2.0f, 3.0f, 0.0f), 1.0f, 100);
    const auto samples = emitter.samples();
    QVERIFY(samples.size() >= 300);
    QVERIFY(samples.size() <= 432);
    bool hasCloud = false;
    bool hasDebris = false;
    for(const auto& sample : samples)
    {
        QVERIFY(sample.position.z() >= 1.0f);
        QVERIFY(sample.position.z() <= 13.0f);
        QVERIFY(sample.alpha >= 0.0f && sample.alpha <= 1.0f);
        hasCloud = hasCloud || sample.frame < 12;
        hasDebris = hasDebris || sample.frame >= 12;
    }
    QVERIFY(hasCloud);
    QVERIFY(hasDebris);
}

void ParticleAnimationTests::lightningBlastIsFiniteAndRepeatingForPreview()
{
    ei::particle::LightningBlastEmitter first;
    ei::particle::LightningBlastEmitter second;
    const QVector3D origin(1.0f, 1.0f, 0.0f);
    first.advanceTo(5, 8240u, origin, 1.0f);
    second.advanceTo(5, 8240u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(!a.isEmpty());
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].frame, b[i].frame);
    }
    first.advanceTo(70, 8240u, origin, 1.0f);
    QCOMPARE(first.activeCount(), 0);
    first.advanceTo(81, 8240u, origin, 1.0f);
    QVERIFY(first.activeCount() > 0);
}

void ParticleAnimationTests::nukeUsesFiniteNativeBurstAndImpactPhases()
{
    ei::particle::NukeEmitter first;
    ei::particle::NukeEmitter second;
    const QVector3D origin(3.0f, 4.0f, 1.0f);
    first.advanceTo(20, 8204u, origin, 1.0f);
    second.advanceTo(20, 8204u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(!a.isEmpty());
    bool hasImpactPhase = false;
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 14);
        hasImpactPhase = hasImpactPhase || a[i].frame >= 4;
    }
    QVERIFY(hasImpactPhase);
    first.advanceTo(79, 8204u, origin, 1.0f);
    QCOMPARE(first.activeCount(), 0);
    first.advanceTo(81, 8204u, origin, 1.0f);
    QVERIFY(first.activeCount() > 0);
}

void ParticleAnimationTests::castingUsesDeterministicSyntheticCarrierControl()
{
    ei::particle::CastingEmitter first;
    ei::particle::CastingEmitter second;
    const QVector3D origin(-2.0f, 1.0f, 0.0f);
    first.advanceTo(30, 8203u, origin, 1.0f);
    second.advanceTo(30, 8203u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(!a.isEmpty());
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(a[i].frame >= 1 && a[i].frame <= 15);
    }
    first.advanceTo(71, 8203u, origin, 1.0f);
    QCOMPARE(first.activeCount(), 0);
    first.advanceTo(73, 8203u, origin, 1.0f);
    QVERIFY(first.activeCount() > 0);
}

void ParticleAnimationTests::bansheeCastingUsesFiniteElevatedRing()
{
    ei::particle::BansheeCastingEmitter emitter;
    const QVector3D origin(1.0f, 2.0f, 0.5f);
    emitter.advanceTo(6, 8205u, origin, 1.0f);
    const auto samples = emitter.samples();
    QVERIFY(!samples.isEmpty());
    for(const auto& sample : samples)
    {
        QVERIFY(sample.position.z() > origin.z());
        QVERIFY(sample.frame >= 0 && sample.frame <= 15);
    }
    emitter.advanceTo(31, 8205u, origin, 1.0f);
    QCOMPARE(emitter.activeCount(), 0);
    emitter.advanceTo(33, 8205u, origin, 1.0f);
    QVERIFY(emitter.activeCount() > 0);
}

void ParticleAnimationTests::mushroomUsesFiniteRisingCloud()
{
    ei::particle::MushroomEmitter first;
    ei::particle::MushroomEmitter second;
    const QVector3D origin(2.0f, -1.0f, 0.5f);
    first.advanceTo(24, 8206u, origin, 1.0f);
    second.advanceTo(24, 8206u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 100);
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(a[i].position.z() > origin.z());
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 14);
    }
    first.advanceTo(63, 8206u, origin, 1.0f);
    QCOMPARE(first.activeCount(), 0);
    first.advanceTo(65, 8206u, origin, 1.0f);
    QVERIFY(first.activeCount() > 0);
}

void ParticleAnimationTests::blood1UsesFiniteSyntheticCarrierBurst()
{
    ei::particle::Blood1Emitter emitter;
    emitter.advanceTo(4, 8207u, QVector3D(), 1.0f);
    const auto samples = emitter.samples();
    QVERIFY(!samples.isEmpty());
    for(const auto& sample : samples)
    {
        QCOMPARE(sample.frame, 12);
        QVERIFY(sample.scale > 0.0f);
        QVERIFY(sample.alpha >= 0.0f && sample.alpha <= 1.0f);
    }
    emitter.advanceTo(31, 8207u, QVector3D(), 1.0f);
    QCOMPARE(emitter.activeCount(), 0);
    emitter.advanceTo(33, 8207u, QVector3D(), 1.0f);
    QVERIFY(emitter.activeCount() > 0);
}

void ParticleAnimationTests::firewallUsesTendriFirePhaseRange()
{
    ei::particle::TendriEmitter emitter;
    emitter.advanceTo(100, 8208u, 0x2010u, QVector3D(), 1.0f,
                      QVector3D(), 0.0f, 60);
    const auto samples = emitter.samples(0.5f);
    QVERIFY(!samples.isEmpty());
    for(const auto& sample : samples)
        QVERIFY(sample.frame >= 0 && sample.frame <= 7);
}

QTEST_APPLESS_MAIN(ParticleAnimationTests)
#include "particle_animation_tests.moc"
