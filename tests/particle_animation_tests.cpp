#include <QtTest>

#include "particle_animation.h"

class ParticleAnimationTests : public QObject
{
    Q_OBJECT

private slots:
    void campfireUsesAuthoredFrameRanges();
    void smokeStartsAtFrameEight();
    void atlasUsesBottomUpGameFrameOrder();
    void samplesAreDeterministicAndBounded();
};

void ParticleAnimationTests::campfireUsesAuthoredFrameRanges()
{
    const auto flame = ei::particle::sample(
        ei::particle::Family::FireAndSmoke, 0, 0, 0);
    const auto smoke = ei::particle::sample(
        ei::particle::Family::FireAndSmoke, 12, 0, 0);
    QCOMPARE(flame.frame, 0);
    QVERIFY(!flame.smoke);
    QCOMPARE(smoke.frame, 9);
    QVERIFY(smoke.smoke);
}

void ParticleAnimationTests::smokeStartsAtFrameEight()
{
    const auto sample = ei::particle::sample(
        ei::particle::Family::SmokeOnly, 0, 0, 0);
    QCOMPARE(sample.frame, 8);
    QVERIFY(sample.smoke);
}

void ParticleAnimationTests::atlasUsesBottomUpGameFrameOrder()
{
    QCOMPARE(ei::particle::atlasUv(0), QVector4D(0.0f, 0.75f, 0.25f, 0.25f));
    QCOMPARE(ei::particle::atlasUv(7), QVector4D(0.75f, 0.50f, 0.25f, 0.25f));
    QCOMPARE(ei::particle::atlasUv(8), QVector4D(0.0f, 0.25f, 0.25f, 0.25f));
    QCOMPARE(ei::particle::atlasUv(14), QVector4D(0.50f, 0.0f, 0.25f, 0.25f));
}

void ParticleAnimationTests::samplesAreDeterministicAndBounded()
{
    for(int tick = 0; tick < 128; ++tick)
    {
        for(int slot = 0; slot < ei::particle::kEmitterSlots; ++slot)
        {
            const auto first = ei::particle::sample(
                ei::particle::Family::FireAndSmoke, tick, slot, 1234u);
            const auto second = ei::particle::sample(
                ei::particle::Family::FireAndSmoke, tick, slot, 1234u);
            QCOMPARE(first.frame, second.frame);
            QVERIFY(first.frame >= 0);
            QVERIFY(first.frame <= 14);
        }
    }
}

QTEST_APPLESS_MAIN(ParticleAnimationTests)
#include "particle_animation_tests.moc"
