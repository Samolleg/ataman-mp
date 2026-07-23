#include <QtTest>

#include "particle_animation.h"

class ParticleAnimationTests : public QObject
{
    Q_OBJECT

private slots:
    void atlasUsesNativeTopRowFirstInsets();
    void isaacPreviewStreamIsDeterministic();
    void schedulerUsesAttemptsRatherThanSlots();
    void prewarmedEmittersAreDeterministicAndBounded();
    void fireOnlyNeverReachesSmokeFrames();
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

QTEST_APPLESS_MAIN(ParticleAnimationTests)
#include "particle_animation_tests.moc"
