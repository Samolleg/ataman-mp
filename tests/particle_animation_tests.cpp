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
    void bloodVariantsUseDedicatedAtlasPhases();
    void a4BloodUsesOneShotRadialGroundBurst();
    void firewallUsesTendriFirePhaseRange();
    void fireArrowUsesActivatedZeroSpeedControl();
    void acidRayUsesStationaryWidthAndSizeTable();
    void blueGasUsesSyntheticCarrierCylinder();
    void linkUsesNativeDefaultArc();
    void sphereShellsUseSyntheticCarrierBound();
    void sphereFireUsesFireSizeFactor();
    void antimagicUsesShortDenseShell();
    void modifiersUseSubtypeAnchorsAndFiniteTrails();
    void clayRingUsesFiniteGroundRing();
    void teleportUsesFiniteSignedColumn();
    void zoneExitUsesNativeStandaloneColumn();
    void pathUsesTruthfulStationaryCarrierPoint();
    void pathDestinationDistinguishesRingAndFailureCross();
    void moshkaUsesSyntheticCarrierSwarm();
    void portalStarUsesPersistentNativePhaseZero();
    void portalUsesHeadAndRotatingRisingTrails();
    void cylindersUseOppositeFiniteHelices();
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

void ParticleAnimationTests::bloodVariantsUseDedicatedAtlasPhases()
{
    for(int frame = 12; frame <= 15; ++frame)
    {
        ei::particle::Blood1Emitter emitter;
        const uint kind = frame == 12 ? 8207u : 8240u + uint(frame - 12);
        emitter.advanceTo(4, kind, QVector3D(), 1.0f, frame);
        const auto samples = emitter.samples();
        QVERIFY(!samples.isEmpty());
        for(const auto& sample : samples)
            QCOMPARE(sample.frame, frame);
    }
}

void ParticleAnimationTests::a4BloodUsesOneShotRadialGroundBurst()
{
    ei::particle::A4BloodEmitter first;
    ei::particle::A4BloodEmitter second;
    const QVector3D origin(1.0f, -2.0f, 0.0f);
    first.advanceTo(0, 8244u, origin);
    second.advanceTo(0, 8244u, origin);
    first.advanceTo(1, 8244u, origin);
    second.advanceTo(1, 8244u, origin);
    QCOMPARE(first.sourceCount(), second.sourceCount());
    QVERIFY(first.sourceCount() >= 4 && first.sourceCount() <= 8);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), 45);
    QCOMPARE(a.size(), b.size());
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, origin);
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].frame, 12);
        QCOMPARE(a[i].alpha, 1.0f);
        QVERIFY(a[i].scale >= 0.2f && a[i].scale <= 0.4f);
    }
    first.advanceTo(4, 8244u, origin);
    QCOMPARE(first.activeCount(), 0);
    first.advanceTo(33, 8244u, origin);
    QCOMPARE(first.activeCount(), 45);
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

void ParticleAnimationTests::fireArrowUsesActivatedZeroSpeedControl()
{
    ei::particle::FireArrowEmitter first;
    ei::particle::FireArrowEmitter second;
    const QVector3D origin(2.0f, 3.0f, 4.0f);
    first.advanceTo(0, 8209u, origin, 1.0f);
    second.advanceTo(0, 8209u, origin, 1.0f);
    first.advanceTo(20, 8209u, origin, 1.0f);
    second.advanceTo(20, 8209u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 1);
    QCOMPARE(a[0].position, origin);
    QVERIFY(qAbs(a[0].scale - 0.24f) < 0.000001f);
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].frame, b[i].frame);
        QCOMPARE(a[i].position, origin);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 14);
    }
}

void ParticleAnimationTests::acidRayUsesStationaryWidthAndSizeTable()
{
    ei::particle::AcidRayEmitter emitter;
    const QVector3D origin(-1.0f, 2.0f, 0.5f);
    emitter.advanceTo(0, 8210u, origin, 1.0f);
    emitter.advanceTo(20, 8210u, origin, 1.0f);
    const auto samples = emitter.samples();
    QVERIFY(!samples.isEmpty());
    QVERIFY(samples.size() <= 15);
    for(const auto& sample : samples)
    {
        QCOMPARE(sample.position, origin);
        QVERIFY(sample.scale >= 0.0f);
        QVERIFY(sample.frame >= 0 && sample.frame <= 14);
    }
}

void ParticleAnimationTests::blueGasUsesSyntheticCarrierCylinder()
{
    ei::particle::BlueGasEmitter first;
    ei::particle::BlueGasEmitter second;
    const QVector3D origin(4.0f, -2.0f, 1.0f);
    first.advanceTo(0, 8211u, origin, 1.0f);
    second.advanceTo(0, 8211u, origin, 1.0f);
    first.advanceTo(30, 8211u, origin, 1.0f);
    second.advanceTo(30, 8211u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 50);
    QVERIFY(a.size() <= 500);
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(a[i].scale > 0.0f);
        QVERIFY(a[i].alpha >= 0.0f && a[i].alpha <= 1.0f);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 14);
    }
}

void ParticleAnimationTests::linkUsesNativeDefaultArc()
{
    ei::particle::LinkEmitter first;
    ei::particle::LinkEmitter second;
    const QVector3D origin(6.0f, -3.0f, 2.0f);
    first.advanceTo(0, 8212u, origin, 1.0f);
    second.advanceTo(0, 8212u, origin, 1.0f);
    first.advanceTo(40, 8212u, origin, 1.0f);
    second.advanceTo(40, 8212u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 100);
    QVERIFY(a.size() <= 500);
    float highest = origin.z();
    float farthest = origin.x();
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        highest = qMax(highest, a[i].position.z());
        farthest = qMin(farthest, a[i].position.x());
        QVERIFY(a[i].scale > 0.0f);
        QVERIFY(a[i].alpha >= 0.0f && a[i].alpha <= 1.0f);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 15);
    }
    QVERIFY(highest > origin.z() + 1.5f);
    QVERIFY(farthest < origin.x() - 5.0f);
}

void ParticleAnimationTests::sphereShellsUseSyntheticCarrierBound()
{
    ei::particle::SphereAcidEmitter acid;
    ei::particle::SphereElectricityEmitter electricity;
    const QVector3D origin(-2.0f, 4.0f, 1.5f);
    acid.advanceTo(0, 8213u, origin, 1.0f);
    electricity.advanceTo(0, 8214u, origin, 1.0f);
    acid.advanceTo(30, 8213u, origin, 1.0f);
    electricity.advanceTo(30, 8214u, origin, 1.0f);
    const auto acidSamples = acid.samples();
    const auto electricitySamples = electricity.samples();
    QVERIFY(acidSamples.size() > 40);
    QVERIFY(electricitySamples.size() > 40);
    QVERIFY(acidSamples.size() <= 500);
    QVERIFY(electricitySamples.size() <= 500);
    for(const auto& sample : acidSamples)
    {
        QVERIFY(qAbs((sample.position - origin).length() - 0.78f) < 0.0001f);
        QVERIFY(sample.scale > 0.0f);
        QCOMPARE(sample.alpha, 1.0f);
        QVERIFY(sample.frame >= 0 && sample.frame <= 14);
    }
    for(const auto& sample : electricitySamples)
    {
        QVERIFY(qAbs((sample.position - origin).length() - 0.78f) < 0.0001f);
        QVERIFY(sample.scale > 0.0f);
        QCOMPARE(sample.alpha, 1.0f);
        QVERIFY(sample.frame >= 0 && sample.frame <= 14);
    }
}

void ParticleAnimationTests::sphereFireUsesFireSizeFactor()
{
    ei::particle::SphereFireEmitter emitter;
    const QVector3D origin(1.0f, -2.0f, 0.5f);
    emitter.advanceTo(0, 8215u, origin, 1.0f);
    emitter.advanceTo(30, 8215u, origin, 1.0f);
    const auto samples = emitter.samples();
    QVERIFY(samples.size() > 40);
    QVERIFY(samples.size() <= 500);
    for(const auto& sample : samples)
    {
        QVERIFY(qAbs((sample.position - origin).length() - 0.78f) < 0.0001f);
        QVERIFY(sample.scale > 0.0f);
        QCOMPARE(sample.alpha, 1.0f);
        QVERIFY(sample.frame >= 0 && sample.frame <= 14);
    }
}

void ParticleAnimationTests::antimagicUsesShortDenseShell()
{
    ei::particle::AntimagicEmitter first;
    ei::particle::AntimagicEmitter second;
    const QVector3D origin(-1.0f, 4.0f, 0.25f);
    first.advanceTo(0, 8218u, origin, 1.0f);
    second.advanceTo(0, 8218u, origin, 1.0f);
    first.advanceTo(30, 8218u, origin, 1.0f);
    second.advanceTo(30, 8218u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 40);
    QVERIFY(a.size() < 100);
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(qAbs((a[i].position - origin).length() - 0.78f) < 0.0001f);
        QVERIFY(a[i].scale > 0.0f);
        QCOMPARE(a[i].alpha, 1.0f);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 14);
    }
}

void ParticleAnimationTests::modifiersUseSubtypeAnchorsAndFiniteTrails()
{
    const QVector3D origin(2.0f, -3.0f, 0.5f);
    for(int subtype = 1; subtype <= 12; ++subtype)
    {
        ei::particle::ModifierEmitter emitter;
        emitter.advanceTo(0, 8218u + uint(subtype), origin, 1.0f, subtype);
        emitter.advanceTo(1, 8218u + uint(subtype), origin, 1.0f, subtype);
        const auto first = emitter.samples();
        QCOMPARE(first.size(), 80);
        int anchors = 0;
        for(const auto& sample : first)
        {
            QVERIFY(sample.frame >= 0 && sample.frame <= 15);
            QVERIFY(sample.scale > 0.0f);
            QVERIFY(sample.alpha >= 0.0f && sample.alpha <= 1.0f);
            if(sample.frame == subtype - 1)
                ++anchors;
        }
        QVERIFY(anchors >= 10);
        emitter.advanceTo(30, 8218u + uint(subtype), origin, 1.0f, subtype);
        QCOMPARE(emitter.activeCount(), 0);
        emitter.advanceTo(49, 8218u + uint(subtype), origin, 1.0f, subtype);
        QVERIFY(emitter.activeCount() > 0);
    }

    ei::particle::ModifierEmitter worldZAxis;
    ei::particle::ModifierEmitter carrierYAxis;
    worldZAxis.advanceTo(0, 8219u, origin, 1.0f, 1);
    carrierYAxis.advanceTo(0, 8223u, origin, 1.0f, 5);
    worldZAxis.advanceTo(1, 8219u, origin, 1.0f, 1);
    carrierYAxis.advanceTo(1, 8223u, origin, 1.0f, 5);
    float zAxisYSpread = 0.0f;
    float yAxisYSpread = 0.0f;
    for(const auto& sample : worldZAxis.samples())
        if(sample.frame == 0)
            zAxisYSpread = qMax(zAxisYSpread, qAbs(sample.position.y() - origin.y()));
    for(const auto& sample : carrierYAxis.samples())
        if(sample.frame == 4)
            yAxisYSpread = qMax(yAxisYSpread, qAbs(sample.position.y() - origin.y()));
    QVERIFY(zAxisYSpread > 0.1f);
    QVERIFY(yAxisYSpread < 0.0001f);

    ei::particle::ModifierEmitter inward;
    ei::particle::ModifierEmitter outward;
    inward.advanceTo(0, 8219u, origin, 1.0f, 1);
    outward.advanceTo(0, 8219u, origin, -1.0f, 1);
    inward.advanceTo(8, 8219u, origin, 1.0f, 1);
    outward.advanceTo(8, 8219u, origin, -1.0f, 1);
    float inwardRadius = 0.0f;
    float outwardRadius = 0.0f;
    const QVector3D center = origin + QVector3D(0.0f, 0.0f, 1.0f);
    for(const auto& sample : inward.samples())
        if(sample.frame == 0)
            inwardRadius = qMax(inwardRadius, (sample.position - center).length());
    for(const auto& sample : outward.samples())
        if(sample.frame == 0)
            outwardRadius = qMax(outwardRadius, (sample.position - center).length());
    QVERIFY(outwardRadius > inwardRadius);
}

void ParticleAnimationTests::clayRingUsesFiniteGroundRing()
{
    ei::particle::ClayRingEmitter first;
    ei::particle::ClayRingEmitter second;
    const QVector3D origin(3.0f, 2.0f, -0.25f);
    first.advanceTo(0, 8216u, origin, 1.0f);
    second.advanceTo(0, 8216u, origin, 1.0f);
    first.advanceTo(10, 8216u, origin, 1.0f);
    second.advanceTo(10, 8216u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 100);
    QVERIFY(a.size() <= 500);
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(a[i].scale > 0.0f);
        QVERIFY(a[i].alpha >= 0.0f && a[i].alpha <= 1.0f);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 3);
    }
    first.advanceTo(20, 8216u, origin, 1.0f);
    QCOMPARE(first.activeCount(), 0);
    first.advanceTo(66, 8216u, origin, 1.0f);
    QVERIFY(first.activeCount() > 0);
}

void ParticleAnimationTests::teleportUsesFiniteSignedColumn()
{
    ei::particle::TeleportEmitter positive;
    ei::particle::TeleportEmitter negative;
    const QVector3D origin(-4.0f, 1.0f, 0.0f);
    positive.advanceTo(0, 8217u, origin, 1.0f);
    negative.advanceTo(0, 8217u, origin, -1.0f);
    positive.advanceTo(20, 8217u, origin, 1.0f);
    negative.advanceTo(20, 8217u, origin, -1.0f);
    const auto positiveSamples = positive.samples();
    const auto negativeSamples = negative.samples();
    QVERIFY(positiveSamples.size() > 100);
    QVERIFY(negativeSamples.size() > 100);
    QVERIFY(positive.activeCount() <= 320);
    QVERIFY(negative.activeCount() <= 320);
    for(const auto& sample : positiveSamples)
    {
        QVERIFY(sample.scale > 0.0f);
        QVERIFY(sample.alpha >= 0.0f && sample.alpha <= 1.0f);
        QVERIFY(sample.frame >= 0 && sample.frame <= 15);
    }
    for(const auto& sample : negativeSamples)
    {
        QVERIFY(sample.scale > 0.0f);
        QVERIFY(sample.alpha >= 0.0f && sample.alpha <= 1.0f);
        QVERIFY(sample.frame >= 0 && sample.frame <= 15);
    }
    positive.advanceTo(50, 8217u, origin, 1.0f);
    QCOMPARE(positive.activeCount(), 0);
    positive.advanceTo(66, 8217u, origin, 1.0f);
    QVERIFY(positive.activeCount() > 0);
}

void ParticleAnimationTests::zoneExitUsesNativeStandaloneColumn()
{
    ei::particle::ZoneExitEmitter first;
    ei::particle::ZoneExitEmitter second;
    const QVector3D origin(2.0f, -5.0f, 0.25f);
    first.advanceTo(20, 8248u, origin, 1.0f);
    second.advanceTo(20, 8248u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 250);
    QVERIFY(first.activeCount() <= 600);
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        QCOMPARE(a[i].position.x(), origin.x());
        QCOMPARE(a[i].position.y(), origin.y());
        QVERIFY(a[i].position.z() >= origin.z() + 0.15f);
        QVERIFY(a[i].scale > 0.0f);
        QVERIFY(a[i].alpha >= 0.0f && a[i].alpha <= 1.0f);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 15);
    }
}

void ParticleAnimationTests::pathUsesTruthfulStationaryCarrierPoint()
{
    ei::particle::PathEmitter emitter;
    const QVector3D origin(-1.0f, 3.0f, 0.0f);
    emitter.advanceTo(2, 8249u, origin);
    const auto samples = emitter.samples();
    QCOMPARE(samples.size(), 1);
    QCOMPARE(samples[0].position, origin + QVector3D(0.0f, 0.0f, 0.15f));
    QCOMPARE(samples[0].scale, ei::particle::billboardDiameter(0.07f));
    QCOMPARE(samples[0].frame, 15);
    QCOMPARE(samples[0].color,
             QVector3D(64.0f / 255.0f, 1.0f, 64.0f / 255.0f));
    QVERIFY(samples[0].alpha > 0.0f);
    emitter.advanceTo(12, 8249u, origin);
    QCOMPARE(emitter.activeCount(), 0);
}

void ParticleAnimationTests::pathDestinationDistinguishesRingAndFailureCross()
{
    ei::particle::PathDestinationEmitter destination;
    ei::particle::PathDestinationEmitter failed;
    const QVector3D origin(0.5f, -0.5f, 1.0f);
    destination.advanceTo(2, 8250u, origin, false);
    failed.advanceTo(2, 8251u, origin, true);
    const auto ring = destination.samples();
    const auto cross = failed.samples();
    QCOMPARE(ring.size(), 128);
    QCOMPARE(cross.size(), 256);
    for(const auto& sample : ring)
    {
        QCOMPARE(sample.position.z(), origin.z() + 0.1f);
        QCOMPARE(sample.frame, 15);
        QCOMPARE(sample.color,
                 QVector3D(64.0f / 255.0f, 1.0f, 64.0f / 255.0f));
    }
    for(const auto& sample : cross)
    {
        QCOMPARE(sample.position.z(), origin.z() + 0.1f);
        QCOMPARE(sample.color,
                 QVector3D(1.0f, 64.0f / 255.0f, 64.0f / 255.0f));
    }
    destination.advanceTo(10, 8250u, origin, false);
    QCOMPARE(destination.activeCount(), 0);
}

void ParticleAnimationTests::moshkaUsesSyntheticCarrierSwarm()
{
    ei::particle::MoshkaEmitter first;
    ei::particle::MoshkaEmitter second;
    const QVector3D origin(4.0f, 2.0f, -0.25f);
    first.advanceTo(10, 8252u, origin, 1.0f);
    second.advanceTo(10, 8252u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 100);
    QVERIFY(first.activeCount() <= 800);
    float farthest = 0.0f;
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        QCOMPARE(a[i].color, b[i].color);
        farthest = qMax(farthest, (a[i].position - origin).length());
        QVERIFY(a[i].scale > 0.0f);
        QVERIFY(a[i].alpha >= 0.0f && a[i].alpha <= 1.0f);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 15);
    }
    QVERIFY(farthest > 0.2f);
}

void ParticleAnimationTests::portalStarUsesPersistentNativePhaseZero()
{
    ei::particle::PortalStarEmitter first;
    ei::particle::PortalStarEmitter second;
    const QVector3D origin(1.0f, -2.0f, 0.5f);
    first.advanceTo(10, 8253u, origin, 2.0f);
    second.advanceTo(10, 8253u, origin, 2.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), 1);
    QCOMPARE(a.size(), b.size());
    QCOMPARE(a[0].position, origin);
    QCOMPARE(a[0].position, b[0].position);
    QCOMPARE(a[0].scale, b[0].scale);
    QCOMPARE(a[0].frame, 0);
    QCOMPARE(a[0].alpha, 1.0f);
    QVERIFY(a[0].scale >= ei::particle::billboardDiameter(1.6f));
    QVERIFY(a[0].scale <= ei::particle::billboardDiameter(2.4f));
}

void ParticleAnimationTests::portalUsesHeadAndRotatingRisingTrails()
{
    ei::particle::PortalEmitter first;
    ei::particle::PortalEmitter second;
    const QVector3D origin(-3.0f, 1.5f, 0.25f);
    first.advanceTo(20, 8254u, origin, 1.0f);
    second.advanceTo(20, 8254u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 100);
    QVERIFY(first.activeCount() <= 800);
    bool foundHead = false;
    bool foundLowerTrail = false;
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 15);
        QVERIFY(a[i].alpha >= 0.0f && a[i].alpha <= 96.0f / 255.0f);
        if(a[i].frame == 15 && qAbs(a[i].position.z() - origin.z() - 6.4f) < 0.001f)
            foundHead = true;
        if(a[i].position.z() < origin.z() + 4.0f)
            foundLowerTrail = true;
    }
    QVERIFY(foundHead);
    QVERIFY(foundLowerTrail);
}

void ParticleAnimationTests::cylindersUseOppositeFiniteHelices()
{
    ei::particle::CylinderEmitter forward;
    ei::particle::CylinderEmitter reverse;
    const QVector3D origin(0.0f, 0.0f, 1.0f);
    forward.advanceTo(10, 8255u, origin, 2.0f, false);
    reverse.advanceTo(10, 8256u, origin, 2.0f, true);
    const auto a = forward.samples();
    const auto b = reverse.samples();
    QVERIFY(a.size() > 2);
    QVERIFY(b.size() > 2);
    QVERIFY(forward.activeCount() <= 600);
    QVERIFY(reverse.activeCount() <= 600);
    bool differs = a.size() != b.size();
    for(int i = 0; i < qMin(a.size(), b.size()); ++i)
    {
        QVERIFY(a[i].scale > 0.0f);
        QVERIFY(a[i].alpha >= 0.0f && a[i].alpha <= 1.0f);
        QVERIFY(a[i].frame >= 0);
        if(a[i].position != b[i].position)
            differs = true;
    }
    QVERIFY(differs);
    forward.advanceTo(35, 8255u, origin, 2.0f, false);
    QCOMPARE(forward.activeCount(), 0);
    forward.advanceTo(66, 8255u, origin, 2.0f, false);
    QVERIFY(forward.activeCount() > 2);
}

QTEST_APPLESS_MAIN(ParticleAnimationTests)
#include "particle_animation_tests.moc"
