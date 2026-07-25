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
    void vulcanSmokeUsesSeparateCloudAndPillarLaws();
    void fireballUsesStationaryNativeHeadPath();
    void fireBlastUsesFiniteFifteenSourceExplosion();
    void healingUsesSyntheticCarrierSpiralsAndTails();
    void poisonFogsUseNativeGridAndSubtypeColor();
    void geyserUsesThreeCyclingBallisticSources();
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
    void fireAndAcidStarsUseFinitePhaseFifteenFlash();
    void sparksUseSourceFlashAndNineTrails();
    void visionStarsUseSubtypePopulationsAndOffsets();
    void regenerationUsesTwoSmoothedCarrierSides();
    void silenceUsesTwoAnchorsAndFiniteTrails();
    void feeblemindUsesExpandingRotatingLinkPhases();
    void feetCloudsUseNativeColorsAndDecayingDrift();
    void ballStarsUsePhaseThreeCarrierBall();
    void curseStarsUseHealingPhasesAndDampedMotion();
    void curseHolderUsesShortDenseTornadoControls();
    void startTransUsesFiniteAnchorAndReturningTrails();
    void castingGatherUsesElementSpecificSourcesAndFlash();
    void castingRoundUsesSchoolSpecificOrbitControls();
    void castingFailedUsesPersistentRisingTeleportRing();
    void persistentEmittersBoundInitialWallClockReplay();
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

void ParticleAnimationTests::fireAndAcidStarsUseFinitePhaseFifteenFlash()
{
    ei::particle::StarFlashEmitter emitter;
    const QVector3D origin(1.0f, 2.0f, 3.0f);
    emitter.advanceTo(1, 8257u, origin, 2.0f);
    QCOMPARE(emitter.activeCount(), 1);
    QCOMPARE(emitter.samples().size(), 0);
    emitter.advanceTo(2, 8257u, origin, 2.0f);
    auto samples = emitter.samples();
    QCOMPARE(samples.size(), 1);
    QCOMPARE(samples[0].position, origin);
    QCOMPARE(samples[0].scale, ei::particle::billboardDiameter(2.0f));
    QCOMPARE(samples[0].alpha, 1.0f);
    QCOMPARE(samples[0].frame, 15);
    emitter.advanceTo(3, 8257u, origin, 2.0f);
    QCOMPARE(emitter.samples()[0].scale, ei::particle::billboardDiameter(1.8f));
    emitter.advanceTo(4, 8257u, origin, 2.0f);
    QCOMPARE(emitter.activeCount(), 0);
    emitter.advanceTo(18, 8257u, origin, 2.0f);
    QCOMPARE(emitter.samples().size(), 1);
}

void ParticleAnimationTests::sparksUseSourceFlashAndNineTrails()
{
    ei::particle::SparksEmitter first;
    ei::particle::SparksEmitter second;
    const QVector3D origin(-2.0f, 0.5f, 1.0f);
    first.advanceTo(1, 8259u, origin, 1.0f);
    QCOMPARE(first.activeCount(), 10);
    QCOMPARE(first.samples().size(), 9);
    first.advanceTo(2, 8259u, origin, 1.0f);
    second.advanceTo(2, 8259u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), 10);
    QCOMPARE(a.size(), b.size());
    bool foundSource = false;
    bool foundTrail = false;
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 4);
        if(a[i].frame == 0 && a[i].position == origin)
            foundSource = true;
        if(a[i].frame > 0 && a[i].position != origin)
            foundTrail = true;
    }
    QVERIFY(foundSource);
    QVERIFY(foundTrail);
    first.advanceTo(8, 8259u, origin, 1.0f);
    QCOMPARE(first.activeCount(), 0);
    first.advanceTo(26, 8259u, origin, 1.0f);
    QCOMPARE(first.activeCount(), 10);
}

void ParticleAnimationTests::visionStarsUseSubtypePopulationsAndOffsets()
{
    const QVector3D origin(3.0f, -1.0f, 0.25f);
    ei::particle::VisionStarEmitter single;
    ei::particle::VisionStarEmitter pentagon;
    ei::particle::VisionStarEmitter elevated;
    single.advanceTo(3, 8260u, origin, 1.0f, 0);
    pentagon.advanceTo(3, 8261u, origin, 1.0f, 1);
    elevated.advanceTo(3, 8268u, origin, 1.0f, 3);
    QCOMPARE(single.activeCount(), 1);
    QCOMPARE(single.samples().size(), 1);
    QCOMPARE(single.samples()[0].frame, 0);
    QCOMPARE(pentagon.activeCount(), 5);
    QCOMPARE(pentagon.samples().size(), 5);
    for(const auto& sample : pentagon.samples())
    {
        QCOMPARE(sample.frame, 1);
        const QVector2D planar(sample.position.x() - origin.x(),
                               sample.position.y() - origin.y());
        QVERIFY(qAbs(planar.length() - 0.5f) < 0.001f);
        QCOMPARE(sample.position.z(), origin.z());
    }
    QCOMPARE(elevated.activeCount(), 1);
    QCOMPARE(elevated.samples().size(), 1);
    QCOMPARE(elevated.samples()[0].frame, 3);
    QVERIFY(qAbs(elevated.samples()[0].position.z() - origin.z() - 0.7f) < 0.001f);
    single.advanceTo(50, 8260u, origin, 1.0f, 0);
    QCOMPARE(single.activeCount(), 1);
    QVERIFY(single.samples()[0].scale > 0.0f);
}

void ParticleAnimationTests::regenerationUsesTwoSmoothedCarrierSides()
{
    ei::particle::RegenerationEmitter first;
    ei::particle::RegenerationEmitter second;
    const QVector3D origin(2.0f, -1.0f, 0.5f);
    first.advanceTo(2, 8263u, origin, 2.0f);
    second.advanceTo(2, 8263u, origin, 2.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(first.activeCount(), 2);
    QCOMPARE(a.size(), 2);
    QCOMPARE(a.size(), b.size());
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].frame, 3);
        QCOMPARE(a[i].alpha, 1.0f);
        QVERIFY(a[i].scale >= ei::particle::billboardDiameter(0.4f));
        QVERIFY(a[i].scale <= ei::particle::billboardDiameter(0.6f));
        QVERIFY(qAbs(a[i].position.y() - origin.y()) < 0.0001f);
        QVERIFY(a[i].position.z() > origin.z());
    }
    QVERIFY(a[0].position.x() < origin.x());
    QVERIFY(a[1].position.x() > origin.x());
}

void ParticleAnimationTests::silenceUsesTwoAnchorsAndFiniteTrails()
{
    ei::particle::SilenceEmitter first;
    ei::particle::SilenceEmitter second;
    const QVector3D origin(-2.0f, 3.0f, 0.25f);
    first.advanceTo(2, 8264u, origin, 1.0f);
    second.advanceTo(2, 8264u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() >= 4);
    QVERIFY(first.activeCount() <= 300);
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].frame, 6);
        QCOMPARE(a[i].alpha, 1.0f);
        QVERIFY(a[i].scale > 0.0f);
    }
    first.advanceTo(40, 8264u, origin, 1.0f);
    QVERIFY(first.activeCount() > 2);
    QVERIFY(first.activeCount() < 40);
}

void ParticleAnimationTests::feeblemindUsesExpandingRotatingLinkPhases()
{
    ei::particle::FeeblemindEmitter first;
    ei::particle::FeeblemindEmitter second;
    const QVector3D origin(1.0f, 4.0f, -0.5f);
    first.advanceTo(2, 8265u, origin, 1.0f);
    second.advanceTo(2, 8265u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), 5);
    QCOMPARE(a.size(), b.size());
    QCOMPARE(first.activeCount(), 7);
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].frame, b[i].frame);
        QCOMPARE(a[i].alpha, 1.0f);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 15);
        QVERIFY(a[i].scale > 0.0f);
        QVERIFY(qAbs(a[i].position.z() - origin.z() - 0.05f) < 0.0001f);
        const QVector2D offset(a[i].position.x() - origin.x(),
                               a[i].position.y() - origin.y());
        QVERIFY(qAbs(offset.length() - 0.055f) < 0.0001f);
    }
    first.advanceTo(40, 8265u, origin, 1.0f);
    QVERIFY(first.activeCount() > 20);
    QVERIFY(first.activeCount() <= 300);
}

void ParticleAnimationTests::feetCloudsUseNativeColorsAndDecayingDrift()
{
    ei::particle::FeetCloudEmitter blue;
    ei::particle::FeetCloudEmitter red;
    const QVector3D origin(-1.0f, -2.0f, 0.75f);
    blue.advanceTo(2, 8266u, origin, 1.0f, false);
    red.advanceTo(2, 8267u, origin, 1.0f, true);
    const auto blueSamples = blue.samples();
    const auto redSamples = red.samples();
    QCOMPARE(blueSamples.size(), 8);
    QCOMPARE(redSamples.size(), 8);
    QCOMPARE(blue.activeCount(), 16);
    QCOMPARE(red.activeCount(), 16);
    for(const auto& sample : blueSamples)
    {
        QCOMPARE(sample.color, QVector3D(0.0f, 0.0f, 1.0f));
        QCOMPARE(sample.scale, ei::particle::billboardDiameter(0.4f));
        QCOMPARE(sample.alpha, 1.0f);
        QVERIFY(sample.frame >= 0 && sample.frame <= 15);
        QCOMPARE(sample.position.z(), origin.z());
    }
    for(const auto& sample : redSamples)
        QCOMPARE(sample.color, QVector3D(1.0f, 0.0f, 0.0f));
    blue.advanceTo(40, 8266u, origin, 1.0f, false);
    QVERIFY(blue.activeCount() > 50);
    QVERIFY(blue.activeCount() <= 300);
}

void ParticleAnimationTests::ballStarsUsePhaseThreeCarrierBall()
{
    ei::particle::BallStarsEmitter first;
    ei::particle::BallStarsEmitter second;
    const QVector3D origin(2.0f, -3.0f, 0.5f);
    first.advanceTo(21, 8269u, origin, 2.0f);
    second.advanceTo(21, 8269u, origin, 2.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(first.activeCount(), 21);
    QCOMPARE(a.size(), 20);
    QCOMPARE(a.size(), b.size());
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].frame, 3);
        QCOMPARE(a[i].alpha, 1.0f);
        QVERIFY((a[i].position - origin).length() < 1.6f);
        QVERIFY(a[i].scale >= ei::particle::billboardDiameter(1.12f));
        QVERIFY(a[i].scale <= ei::particle::billboardDiameter(3.12f));
    }
}

void ParticleAnimationTests::curseStarsUseHealingPhasesAndDampedMotion()
{
    ei::particle::CurseStarsEmitter first;
    ei::particle::CurseStarsEmitter second;
    const QVector3D origin(-1.0f, 4.0f, 0.25f);
    first.advanceTo(21, 8271u, origin, 1.0f);
    second.advanceTo(21, 8271u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(first.activeCount(), 168);
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 100);
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 15);
        QVERIFY(a[i].scale > 0.0f);
        QVERIFY(a[i].alpha > 0.0f && a[i].alpha <= 1.0f);
    }
}

void ParticleAnimationTests::curseHolderUsesShortDenseTornadoControls()
{
    ei::particle::TornadoEmitter normal;
    ei::particle::TornadoEmitter curse;
    const QVector3D origin(0.0f, 0.0f, 2.0f);
    normal.advanceTo(100, 8202u, origin, 1.0f, 100, false);
    curse.advanceTo(100, 8272u, origin, 1.0f, 100, true);
    const auto normalSamples = normal.samples();
    const auto curseSamples = curse.samples();
    QVERIFY(normal.activeCount() >= 300 && normal.activeCount() <= 400);
    QVERIFY(curse.activeCount() >= 300 && curse.activeCount() <= 400);
    QVERIFY(!normalSamples.isEmpty());
    QVERIFY(!curseSamples.isEmpty());
    float normalMaximumZ = origin.z();
    float curseMaximumZ = origin.z();
    for(const auto& sample : normalSamples)
        normalMaximumZ = qMax(normalMaximumZ, sample.position.z());
    for(const auto& sample : curseSamples)
        curseMaximumZ = qMax(curseMaximumZ, sample.position.z());
    QVERIFY(curseMaximumZ <= origin.z() + 1.0f + 4.8f + 0.001f);
    QVERIFY(normalMaximumZ > curseMaximumZ + 2.0f);
}

void ParticleAnimationTests::startTransUsesFiniteAnchorAndReturningTrails()
{
    ei::particle::StartTransEmitter first;
    ei::particle::StartTransEmitter second;
    const QVector3D origin(1.0f, 2.0f, 3.0f);
    first.advanceTo(10, 8273u, origin, 2.0f);
    second.advanceTo(10, 8273u, origin, 2.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 10);
    bool foundAnchor = false;
    bool foundTrail = false;
    for(int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a[i].position, b[i].position);
        QCOMPARE(a[i].scale, b[i].scale);
        QCOMPARE(a[i].alpha, b[i].alpha);
        QCOMPARE(a[i].frame, b[i].frame);
        QVERIFY(a[i].frame >= 0 && a[i].frame <= 15);
        if(a[i].frame == 12 && a[i].position == origin)
            foundAnchor = true;
        if(a[i].position != origin)
            foundTrail = true;
    }
    QVERIFY(foundAnchor);
    QVERIFY(foundTrail);
    first.advanceTo(52, 8273u, origin, 2.0f);
    QCOMPARE(first.activeCount(), 0);
    first.advanceTo(74, 8273u, origin, 2.0f);
    QVERIFY(first.activeCount() > 10);
}

void ParticleAnimationTests::vulcanSmokeUsesSeparateCloudAndPillarLaws()
{
    const QVector3D origin(2.0f, 3.0f, 4.0f);
    ei::particle::VulcanSmokeEmitter emitter;
    emitter.advanceTo(100, 8197u, origin, 1.0f);
    emitter.advanceTo(120, 8197u, origin, 1.0f);
    QVERIFY(emitter.activeCount() > 20);
    QVERIFY(emitter.activeCount() <= 400);
    bool foundUpperCloud = false;
    bool foundPillar = false;
    for(const ei::particle::EffectRenderSample& sample : emitter.samples())
    {
        foundUpperCloud |= sample.position.z() - origin.z() > 3.5f
            && sample.scale > 3.0f;
        foundPillar |= sample.position.z() - origin.z() < 2.0f
            && sample.scale < 3.0f;
        QVERIFY(sample.frame >= 0 && sample.frame < 16);
    }
    QVERIFY(foundUpperCloud);
    QVERIFY(foundPillar);
}

void ParticleAnimationTests::fireballUsesStationaryNativeHeadPath()
{
    ei::particle::FireballEmitter first;
    ei::particle::FireballEmitter second;
    const QVector3D origin(1.0f, -2.0f, 3.0f);
    first.advanceTo(15, 8192u, origin, 2.0f);
    second.advanceTo(15, 8192u, origin, 2.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(first.activeCount(), 15);
    QCOMPARE(a.size(), b.size());
    for(int index = 0; index < a.size(); ++index)
    {
        QCOMPARE(a[index].position, origin);
        QCOMPARE(a[index].position, b[index].position);
        QCOMPARE(a[index].scale, b[index].scale);
        QCOMPARE(a[index].frame, b[index].frame);
        QCOMPARE(a[index].alpha, 1.0f);
        QVERIFY(a[index].frame >= 0 && a[index].frame <= 14);
    }
    QCOMPARE(a.last().scale, ei::particle::billboardDiameter(1.92f));
}

void ParticleAnimationTests::fireBlastUsesFiniteFifteenSourceExplosion()
{
    ei::particle::FireBlastEmitter first;
    ei::particle::FireBlastEmitter second;
    const QVector3D origin(0.0f, 0.0f, 1.0f);
    first.advanceTo(7, 8194u, origin, 1.0f);
    second.advanceTo(7, 8194u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QVERIFY(!first.generating());
    QVERIFY(first.activeCount() > 100 && first.activeCount() <= 600);
    QCOMPARE(a.size(), b.size());
    for(int index = 0; index < a.size(); ++index)
    {
        QCOMPARE(a[index].position, b[index].position);
        QCOMPARE(a[index].scale, b[index].scale);
        QCOMPARE(a[index].frame, b[index].frame);
        QCOMPARE(a[index].alpha, b[index].alpha);
        QVERIFY(a[index].frame >= 0 && a[index].frame <= 15);
        QVERIFY(a[index].alpha >= 0.0f && a[index].alpha <= 1.0f);
    }
    first.advanceTo(47, 8194u, origin, 1.0f);
    QCOMPARE(first.activeCount(), 0);
    first.advanceTo(49, 8194u, origin, 1.0f);
    QVERIFY(first.activeCount() > 40);
}

void ParticleAnimationTests::healingUsesSyntheticCarrierSpiralsAndTails()
{
    ei::particle::HealingEmitter first;
    ei::particle::HealingEmitter second;
    const QVector3D origin(3.0f, 4.0f, 2.0f);
    first.advanceTo(10, 8198u, origin, 1.0f);
    second.advanceTo(10, 8198u, origin, 1.0f);
    QCOMPARE(first.sourceCount(), 5);
    const auto a = first.samples();
    const auto b = second.samples();
    QCOMPARE(a.size(), b.size());
    QVERIFY(a.size() > 20);
    bool anchorPhase = false;
    bool tailPhase = false;
    for(int index = 0; index < a.size(); ++index)
    {
        QCOMPARE(a[index].position, b[index].position);
        QCOMPARE(a[index].scale, b[index].scale);
        QCOMPARE(a[index].frame, b[index].frame);
        QCOMPARE(a[index].alpha, b[index].alpha);
        anchorPhase |= a[index].frame < 8;
        tailPhase |= a[index].frame >= 8;
    }
    QVERIFY(anchorPhase);
    QVERIFY(tailPhase);
    first.advanceTo(55, 8198u, origin, 1.0f);
    QCOMPARE(first.activeCount(), 0);
}

void ParticleAnimationTests::poisonFogsUseNativeGridAndSubtypeColor()
{
    ei::particle::PoisonFogEmitter poison;
    ei::particle::PoisonFogEmitter aggression;
    const QVector3D origin(-2.0f, 1.0f, 0.5f);
    poison.advanceTo(1, 8199u, origin, 1.0f, false);
    aggression.advanceTo(1, 8200u, origin, 1.0f, true);
    const auto poisonSamples = poison.samples();
    const auto aggressionSamples = aggression.samples();
    QVERIFY(!poisonSamples.isEmpty());
    QVERIFY(!aggressionSamples.isEmpty());
    for(const auto& sample : poisonSamples)
    {
        QCOMPARE(sample.color, QVector3D(1.0f, 1.0f, 1.0f));
        QCOMPARE(sample.alpha, 160.0f / 255.0f);
        QVERIFY(sample.frame >= 0 && sample.frame < 15);
        QVERIFY(sample.position.z() > origin.z());
        const QVector2D offset(sample.position.x() - origin.x(),
                               sample.position.y() - origin.y());
        QVERIFY(offset.length() < 1.0f);
    }
    for(const auto& sample : aggressionSamples)
    {
        QCOMPARE(sample.color,
                 QVector3D(1.0f, 175.0f / 255.0f, 127.0f / 255.0f));
        QCOMPARE(sample.alpha, 1.0f);
    }
}

void ParticleAnimationTests::geyserUsesThreeCyclingBallisticSources()
{
    ei::particle::GeyserEmitter first;
    ei::particle::GeyserEmitter second;
    const QVector3D origin(0.0f, 0.0f, 0.0f);
    first.advanceTo(32, 8201u, origin, 1.0f);
    second.advanceTo(32, 8201u, origin, 1.0f);
    const auto a = first.samples();
    const auto b = second.samples();
    QVERIFY(first.activeCount() > 20 && first.activeCount() <= 500);
    QCOMPARE(a.size(), b.size());
    bool foundFlash = false;
    bool foundStream = false;
    for(int index = 0; index < a.size(); ++index)
    {
        QCOMPARE(a[index].position, b[index].position);
        QCOMPARE(a[index].scale, b[index].scale);
        QCOMPARE(a[index].frame, b[index].frame);
        QCOMPARE(a[index].alpha, b[index].alpha);
        foundFlash |= a[index].frame == 15 && a[index].position == origin;
        foundStream |= a[index].frame < 12 && a[index].position != origin;
        QVERIFY(a[index].frame >= 0 && a[index].frame <= 15);
    }
    QVERIFY(foundFlash);
    QVERIFY(foundStream);
}

void ParticleAnimationTests::castingGatherUsesElementSpecificSourcesAndFlash()
{
    const QVector3D origin(3.0f, 4.0f, 5.0f);
    ei::particle::CastingGatherEmitter fire;
    ei::particle::CastingGatherEmitter electricity;
    ei::particle::CastingGatherEmitter divination;
    fire.advanceTo(100, 8231u, 8231u, origin, 1.0f);
    electricity.advanceTo(100, 8232u, 8232u, origin, 1.0f);
    divination.advanceTo(100, 8234u, 8234u, origin, 1.0f);
    QCOMPARE(fire.activeCount(), 20);
    QCOMPARE(electricity.activeCount(), 20);
    QCOMPARE(divination.activeCount(), 7);

    bool foundFlash = false;
    for(const ei::particle::EffectRenderSample& sample : divination.samples())
    {
        if(sample.frame == 0 && sample.position == origin)
        {
            foundFlash = true;
            QVERIFY(sample.scale > 0.0f);
        }
    }
    QVERIFY(foundFlash);

    fire.advanceTo(101, 8231u, 8231u, origin, 1.0f);
    electricity.advanceTo(101, 8232u, 8232u, origin, 1.0f);
    bool fireHeldPhase = false;
    bool electricityAdvanced = false;
    for(const ei::particle::EffectRenderSample& sample : fire.samples())
        fireHeldPhase |= sample.frame == 14;
    for(const ei::particle::EffectRenderSample& sample : electricity.samples())
        electricityAdvanced |= sample.frame == 13;
    QVERIFY(fireHeldPhase);
    QVERIFY(electricityAdvanced);
}

void ParticleAnimationTests::castingRoundUsesSchoolSpecificOrbitControls()
{
    const QVector3D origin(1.0f, 2.0f, 3.0f);
    ei::particle::CastingStarRoundEmitter illusion;
    ei::particle::CastingStarRoundEmitter domination;
    illusion.advanceTo(200, 8235u, 8235u, origin, 2.0f);
    domination.advanceTo(200, 8236u, 8236u, origin, 2.0f);
    QCOMPARE(illusion.activeCount(), 10);
    QCOMPARE(domination.activeCount(), 10);

    int illusionAnchors = 0;
    int dominationAnchors = 0;
    float dominationAnchorHeight = 0.0f;
    for(const ei::particle::EffectRenderSample& sample : illusion.samples())
        illusionAnchors += sample.frame == 0 ? 1 : 0;
    for(const ei::particle::EffectRenderSample& sample : domination.samples())
    {
        if(sample.frame == 0)
        {
            ++dominationAnchors;
            dominationAnchorHeight = sample.position.z() - origin.z();
        }
    }
    QCOMPARE(illusionAnchors, 2);
    QCOMPARE(dominationAnchors, 1);
    QVERIFY(qAbs(dominationAnchorHeight - 3.2f) < 0.0001f);
}

void ParticleAnimationTests::castingFailedUsesPersistentRisingTeleportRing()
{
    const QVector3D origin(0.0f, 0.0f, 2.0f);
    ei::particle::CastingFailedEmitter emitter;
    emitter.advanceTo(300, 8239u, origin, 1.5f);
    QCOMPARE(emitter.activeCount(), 25);
    const QVector<ei::particle::EffectRenderSample> first = emitter.samples();
    QVERIFY(!first.isEmpty());
    QCOMPARE(first[0].frame, 1);
    const float firstScale = first[0].scale;
    emitter.advanceTo(301, 8239u, origin, 1.5f);
    QCOMPARE(emitter.activeCount(), 50);
    const QVector<ei::particle::EffectRenderSample> second = emitter.samples();
    QCOMPARE(second[0].frame, 2);
    QVERIFY(second[0].scale < firstScale);
    QVERIFY(second[0].position.z() < origin.z());
}

void ParticleAnimationTests::persistentEmittersBoundInitialWallClockReplay()
{
    constexpr qint64 wallClockTick = 32000000000LL;
    const QVector3D origin(0.0f, 0.0f, 0.0f);
    ei::particle::MoshkaEmitter moshka;
    ei::particle::VisionStarEmitter vision;
    ei::particle::RegenerationEmitter regeneration;
    ei::particle::SilenceEmitter silence;
    ei::particle::FeeblemindEmitter feeblemind;
    ei::particle::FeetCloudEmitter feet;
    ei::particle::BallStarsEmitter ballStars;
    ei::particle::CurseStarsEmitter curseStars;
    ei::particle::FireballEmitter fireball;
    ei::particle::FireBlastEmitter fireBlast;
    ei::particle::HealingEmitter healing;
    ei::particle::PoisonFogEmitter poisonFog;
    ei::particle::GeyserEmitter geyser;
    ei::particle::VulcanSmokeEmitter vulcanSmoke;
    ei::particle::CastingGatherEmitter castingGather;
    ei::particle::CastingStarRoundEmitter castingRound;
    ei::particle::CastingFailedEmitter castingFailed;
    moshka.advanceTo(wallClockTick, 8252u, origin, 1.0f);
    vision.advanceTo(wallClockTick, 8260u, origin, 1.0f, 0);
    regeneration.advanceTo(wallClockTick, 8263u, origin, 1.0f);
    silence.advanceTo(wallClockTick, 8264u, origin, 1.0f);
    feeblemind.advanceTo(wallClockTick, 8265u, origin, 1.0f);
    feet.advanceTo(wallClockTick, 8266u, origin, 1.0f, false);
    ballStars.advanceTo(wallClockTick, 8269u, origin, 1.0f);
    curseStars.advanceTo(wallClockTick, 8271u, origin, 1.0f);
    fireball.advanceTo(wallClockTick, 8192u, origin, 1.0f);
    fireBlast.advanceTo(wallClockTick, 8194u, origin, 1.0f);
    healing.advanceTo(wallClockTick, 8198u, origin, 1.0f);
    poisonFog.advanceTo(wallClockTick, 8199u, origin, 1.0f, false);
    geyser.advanceTo(wallClockTick, 8201u, origin, 1.0f);
    vulcanSmoke.advanceTo(wallClockTick, 8197u, origin, 1.0f);
    castingGather.advanceTo(wallClockTick, 8231u, 8231u, origin, 1.0f);
    castingRound.advanceTo(wallClockTick, 8235u, 8235u, origin, 1.0f);
    castingFailed.advanceTo(wallClockTick, 8239u, origin, 1.0f);
    QVERIFY(moshka.activeCount() > 0 && moshka.activeCount() <= 800);
    QCOMPARE(vision.activeCount(), 1);
    QCOMPARE(regeneration.activeCount(), 2);
    QVERIFY(silence.activeCount() > 2 && silence.activeCount() < 50);
    QVERIFY(feeblemind.activeCount() > 20 && feeblemind.activeCount() <= 300);
    QVERIFY(feet.activeCount() > 50 && feet.activeCount() <= 300);
    QCOMPARE(ballStars.activeCount(), 21);
    QCOMPARE(curseStars.activeCount(), 168);
    QCOMPARE(fireball.activeCount(), 15);
    QVERIFY(fireBlast.activeCount() <= 600);
    QVERIFY(healing.activeCount() <= 500);
    QVERIFY(poisonFog.activeCount() > 0);
    QVERIFY(geyser.activeCount() > 0 && geyser.activeCount() <= 500);
    QVERIFY(vulcanSmoke.activeCount() > 0 && vulcanSmoke.activeCount() <= 400);
    QVERIFY(castingGather.activeCount() > 0 && castingGather.activeCount() <= 500);
    QVERIFY(castingRound.activeCount() > 0 && castingRound.activeCount() <= 600);
    QVERIFY(castingFailed.activeCount() > 0 && castingFailed.activeCount() <= 500);
}

QTEST_APPLESS_MAIN(ParticleAnimationTests)
#include "particle_animation_tests.moc"
