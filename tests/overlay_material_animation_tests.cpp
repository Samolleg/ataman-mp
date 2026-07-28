#include <QtTest>

#include "overlay_material_animation.h"

class OverlayMaterialAnimationTests : public QObject
{
    Q_OBJECT

private slots:
    void propagatesAuthoredParameters();
    void gatesByMaterialType();
    void clampsWindInputs();
    void explicitNeutralStateRestoresBaseGeometry();
    void editModeSwitchIsExactAndReversible();
    void serializedWarpSpeedDoesNotChangeRuntimeWarp();
    void noiseIsStableAndSectorPeriodic();
    void shaderContractsCoverDesktopAndAngle();
    void mapLifetimeResetsAnimationState();
    void derivesGeneralTerrainEmission();
};

static SMaterial material(ETerrainType type, float wave, float warp)
{
    SMaterial result{};
    result.type = type;
    result.waveMultiplier = wave;
    result.warpSpeed = warp;
    return result;
}

void OverlayMaterialAnimationTests::propagatesAuthoredParameters()
{
    const SMaterial source = material(ETerrainType::eTerrainWater, 0.6f, 0.25f);
    const ei::SOverlayMaterialAnimation result = ei::overlayMaterialAnimation(&source);
    QCOMPARE(result.waveMultiplier, 0.6f);
    QCOMPARE(result.authoredWarpSpeed, 0.25f);
    QVERIFY(result.animated);
}

void OverlayMaterialAnimationTests::gatesByMaterialType()
{
    const SMaterial terrain = material(ETerrainType::eTerrainNoWater, 1.0f, 0.0f);
    QVERIFY(!ei::overlayMaterialAnimation(&terrain).animated);
    QVERIFY(!ei::overlayMaterialAnimation(nullptr).animated);

    SMaterial untexturedWater = material(ETerrainType::eTerrainWater, 0.4f, 0.0f);
    untexturedWater.type = static_cast<ETerrainType>(2);
    QVERIFY(ei::overlayMaterialAnimation(&untexturedWater).animated);

    SMaterial grass = material(ETerrainType::eTerrainWater, 0.4f, 0.0f);
    grass.type = static_cast<ETerrainType>(4);
    QVERIFY(!ei::overlayMaterialAnimation(&grass).animated);
}

void OverlayMaterialAnimationTests::clampsWindInputs()
{
    const ei::SOverlayWindAnimation high = ei::overlayWindAnimation(
        QVector3D(3.0f, 4.0f, 0.0f), 4.0f, 1.0f, true);
    QCOMPARE(high.strength, 1.0f);
    QVERIFY(high.enabled);
    QVERIFY(high.direction.length() > 0.0f);

    const ei::SOverlayWindAnimation low = ei::overlayWindAnimation(
        QVector3D(1.0f, 0.0f, 0.0f), -2.0f, 1.0f, true);
    QCOMPARE(low.strength, 0.0f);
    QVERIFY(!low.enabled);
}

void OverlayMaterialAnimationTests::explicitNeutralStateRestoresBaseGeometry()
{
    const SMaterial source = material(ETerrainType::eTerrainWater, 0.8f, 0.0f);
    const ei::SOverlayMaterialAnimation water = ei::overlayMaterialAnimation(
        &source);
    const QVector3D base(12.0f, 7.0f, 3.0f);
    const QVector2D grid(12.0f, 7.0f);
    const float noise = ei::overlayWindNoise(12, 7);

    const ei::SOverlayWindAnimation neutral = ei::overlayWindAnimation(
        QVector3D(1.0f, 0.0f, 0.0f), 0.75f, 0.0f, false);
    QCOMPARE(ei::overlayVertexDisplacement(base, grid, noise, water, neutral),
             QVector3D());

    const ei::SOverlayWindAnimation timeZero = ei::overlayWindAnimation(
        QVector3D(1.0f, 0.0f, 0.0f), 0.75f, 0.0f, true);
    QVERIFY(!ei::overlayVertexDisplacement(base, grid, noise, water, timeZero).isNull());
}

void OverlayMaterialAnimationTests::editModeSwitchIsExactAndReversible()
{
    const SMaterial source = material(ETerrainType::eTerrainWater, 0.5f, 0.0f);
    const ei::SOverlayMaterialAnimation water = ei::overlayMaterialAnimation(&source);
    const QVector3D base(4.0f, 9.0f, 2.0f);
    const QVector2D grid(4.0f, 9.0f);
    const float noise = ei::overlayWindNoise(4, 9);
    const ei::SOverlayWindAnimation running = ei::overlayWindAnimation(
        QVector3D(0.2f, 0.8f, 0.0f), 0.6f, 17.25f, true);
    const ei::SOverlayWindAnimation editing = ei::overlayWindAnimation(
        QVector3D(0.2f, 0.8f, 0.0f), 0.6f, 17.25f, false);

    const QVector3D first = base
        + ei::overlayVertexDisplacement(base, grid, noise, water, running);
    const QVector3D neutral = base
        + ei::overlayVertexDisplacement(base, grid, noise, water, editing);
    const QVector3D resumed = base
        + ei::overlayVertexDisplacement(base, grid, noise, water, running);
    QVERIFY(first != base);
    QCOMPARE(neutral, base);
    QCOMPARE(resumed, first);
}

void OverlayMaterialAnimationTests::serializedWarpSpeedDoesNotChangeRuntimeWarp()
{
    const SMaterial firstSource = material(ETerrainType::eTerrainWater, 0.5f, 0.0f);
    const SMaterial secondSource = material(ETerrainType::eTerrainWater, 0.5f, 9.0f);
    const ei::SOverlayWindAnimation wind = ei::overlayWindAnimation(
        QVector3D(1.0f, 0.0f, 0.0f), 0.5f, 3.0f, true);
    const QVector3D base(2.0f, 3.0f, 4.0f);
    const QVector2D grid(2.0f, 3.0f);
    QCOMPARE(ei::overlayVertexDisplacement(base, grid, 0.2f,
                 ei::overlayMaterialAnimation(&firstSource), wind),
             ei::overlayVertexDisplacement(base, grid, 0.2f,
                 ei::overlayMaterialAnimation(&secondSource), wind));
}

void OverlayMaterialAnimationTests::noiseIsStableAndSectorPeriodic()
{
    QCOMPARE(ei::overlayWindNoise(0, 0), ei::overlayWindNoise(32, 32));
    QCOMPARE(ei::overlayWindNoise(0, 8), ei::overlayWindNoise(32, 8));
    QCOMPARE(ei::overlayWindNoise(11, 0), ei::overlayWindNoise(11, 32));
    QCOMPARE(ei::overlayWindNoise(16, 16), ei::overlayWindNoise(16, 16));
}

void OverlayMaterialAnimationTests::shaderContractsCoverDesktopAndAngle()
{
    QDir repository(QDir::current());
    while(!repository.exists("assets/shaders/landshader.vsh")
          && repository.cdUp()) {}
    const QString vertexPath = repository.absoluteFilePath(
        "assets/shaders/landshader.vsh");
    const QString fragmentPath = repository.absoluteFilePath(
        "assets/shaders/landshader.fsh");
    const QString objectFragmentPath = repository.absoluteFilePath(
        "assets/shaders/fshader.fsh");
    QVERIFY2(!vertexPath.isEmpty(), "terrain vertex shader fixture not found");
    QVERIFY2(!fragmentPath.isEmpty(), "terrain fragment shader fixture not found");
    QFile vertexFile(vertexPath);
    QFile fragmentFile(fragmentPath);
    QFile objectFragmentFile(objectFragmentPath);
    QVERIFY(vertexFile.open(QIODevice::ReadOnly));
    QVERIFY(fragmentFile.open(QIODevice::ReadOnly));
    QVERIFY(objectFragmentFile.open(QIODevice::ReadOnly));
    const QByteArray vertex = vertexFile.readAll();
    const QByteArray fragment = fragmentFile.readAll();
    const QByteArray objectFragment = objectFragmentFile.readAll();
    QVERIFY(vertex.contains("attribute vec4 a_overlayAnimation"));
    QVERIFY(vertex.contains("u_overlayMotionEnabled"));
    QVERIFY(vertex.contains("floor(logicalTime)"));
    QVERIFY(!vertex.contains("#version"));
    QVERIFY(!vertex.contains("layout("));
    QVERIFY(!fragment.contains("u_animateLiquid"));
    QVERIFY(!fragment.contains("sampledUv"));
    QVERIFY(vertex.contains("attribute vec3 a_materialEmission"));
    QVERIFY(fragment.contains("v_materialEmission"));
    QVERIFY(fragment.contains("u_sunColor * sunDiffuse + v_materialEmission"));
    QVERIFY(objectFragment.contains("uniform vec3 u_materialEmission"));
    QVERIFY(objectFragment.contains("u_sunColor * sunDiffuse + u_materialEmission"));
}

void OverlayMaterialAnimationTests::mapLifetimeResetsAnimationState()
{
    QDir repository(QDir::current());
    while(!repository.exists("source/render/view.cpp") && repository.cdUp()) {}
    QFile viewFile(repository.absoluteFilePath("source/render/view.cpp"));
    QVERIFY(viewFile.open(QIODevice::ReadOnly));
    const QByteArray view = viewFile.readAll();
    QVERIFY(view.contains("m_overlayAnimationTimer.restart();"));
    QVERIFY(view.contains("m_overlayAnimationTimer.invalidate();"));
    QVERIFY(view.contains("m_landProgram.setUniformValue(\"u_overlayMotionEnabled\", false);"));
}

void OverlayMaterialAnimationTests::derivesGeneralTerrainEmission()
{
    SMaterial lava = material(ETerrainType::eTerrainWater, 0.5f, 0.0f);
    lava.R = 0.7960785f;
    lava.G = 0.0f;
    lava.B = 0.0f;
    lava.selfIllumination = 1.0f;
    QCOMPARE(ei::terrainMaterialEmission(&lava),
             QVector3D(0.7960785f, 0.0f, 0.0f));

    SMaterial authoredOther = lava;
    authoredOther.type = static_cast<ETerrainType>(99);
    authoredOther.R = 0.75f;
    authoredOther.G = 0.5f;
    authoredOther.B = 0.25f;
    authoredOther.selfIllumination = 2.0f;
    QCOMPARE(ei::terrainMaterialEmission(&authoredOther),
             QVector3D(1.0f, 1.0f, 0.5f));

    authoredOther.selfIllumination = 0.0f;
    QCOMPARE(ei::terrainMaterialEmission(&authoredOther), QVector3D());
    QCOMPARE(ei::terrainMaterialEmission(nullptr), QVector3D());
}

QTEST_APPLESS_MAIN(OverlayMaterialAnimationTests)
#include "overlay_material_animation_tests.moc"
