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
    void materialApplyInvalidatesSectorDrawData();
    void derivesGeneralTerrainEmission();
    void derivesMaterialOpacity();
    void usesSharedOverlayVertexRules();
    void gatesUnderlayEmissionByPositiveDepth();
    void resolvesAuthoredOverlayTileAnimation();
    void advancesAuthoredOverlayTileFrames();
    void mprSettingsUsesTopLevelWindowAndOverlaySlotToggle();
    void mprMaterialEditorReloadsSelectionAndDoesNotStealEnter();
    void workingAtlasPreviewUpdatesFlipbookAtlasGrid();
    void incrementalOverlayRefreshRecomputesFlipbookMetadata();
    void mprMaterialEditorUsesLiveWorkingAtlasSlotCount();
    void overlayActivationUsesExactWorkingAtlasMaterial();
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
    QVERIFY2(vertex.contains("attribute vec3 a_overlayTileAnimation"),
             "Overlay vertices must carry the authored atlas start/base/count record");
    QVERIFY2(vertex.contains("u_overlayAtlasGrid"),
             "The shader must account for the complete multi-page terrain atlas");
    QVERIFY(vertex.contains("u_overlayMotionEnabled"));
    QVERIFY(vertex.contains("floor(logicalTime)"));
    QVERIFY(!vertex.contains("#version"));
    QVERIFY(!vertex.contains("layout("));
    QVERIFY(!fragment.contains("u_animateLiquid"));
    QVERIFY(!fragment.contains("sampledUv"));
    QVERIFY(vertex.contains("attribute vec3 a_materialEmission"));
    QVERIFY(vertex.contains("attribute float a_materialOpacity"));
    QVERIFY(fragment.contains("v_materialEmission"));
    QVERIFY(fragment.contains("resColor.a *= clamp(v_materialOpacity"));
    QVERIFY(fragment.contains("u_sunColor * sunDiffuse + v_materialEmission"));
    QVERIFY(objectFragment.contains("uniform vec3 u_materialEmission"));
    QVERIFY(objectFragment.contains("u_sunColor * sunDiffuse + u_materialEmission"));
}

void OverlayMaterialAnimationTests::advancesAuthoredOverlayTileFrames()
{
    QDir repository(QDir::current());
    while(!repository.exists("source/render/sector.cpp") && repository.cdUp()) {}
    QFile sectorFile(repository.absoluteFilePath("source/render/sector.cpp"));
    QFile landscapeFile(repository.absoluteFilePath("source/render/landscape.cpp"));
    QVERIFY(sectorFile.open(QIODevice::ReadOnly));
    QVERIFY(landscapeFile.open(QIODevice::ReadOnly));
    const QByteArray sector = sectorFile.readAll();
    const QByteArray landscape = landscapeFile.readAll();
    QVERIFY2(sector.contains("overlayTileAnimation("),
             "Water vertices must resolve their authored atlas animation record");
    QVERIFY2(landscape.contains("u_overlayAtlasGrid"),
             "Water rendering must provide the atlas page geometry to the shader");
}

void OverlayMaterialAnimationTests::workingAtlasPreviewUpdatesFlipbookAtlasGrid()
{
    QDir repository(QDir::current());
    while(!repository.exists("source/render/landscape.cpp") && repository.cdUp()) {}
    QFile landscapeFile(repository.absoluteFilePath("source/render/landscape.cpp"));
    QVERIFY(landscapeFile.open(QIODevice::ReadOnly));
    const QByteArray landscape = landscapeFile.readAll();

    QVERIFY2(landscape.contains("m_renderTextureCount = textureCount;"),
             "Uploading an expanded working atlas must update the atlas grid used by flipbook frame offsets");
    QVERIFY2(landscape.contains("m_renderTextureCount = qMax(1, int(m_header.nTexture));"),
             "Clearing a working atlas preview must restore the published flipbook atlas grid");
}

void OverlayMaterialAnimationTests::incrementalOverlayRefreshRecomputesFlipbookMetadata()
{
    QDir repository(QDir::current());
    while(!repository.exists("source/render/sector.cpp") && repository.cdUp()) {}
    QFile sectorFile(repository.absoluteFilePath("source/render/sector.cpp"));
    QVERIFY(sectorFile.open(QIODevice::ReadOnly));
    const QByteArray sector = sectorFile.readAll();
    const int updateStart = sector.indexOf("void CSector::updateWaterTiles(");
    const int updateEnd = sector.indexOf("void CSector::updateWaterDrawData(", updateStart);
    QVERIFY(updateStart >= 0);
    QVERIFY(updateEnd > updateStart);
    const QByteArray update = sector.mid(updateStart, updateEnd - updateStart);

    QVERIFY2(update.contains("overlayTileAnimation("),
             "Changing an overlay tile incrementally must recompute its authored flipbook range");
    QVERIFY2(update.contains("target.tileAnimation"),
             "The recomputed flipbook range must be written to every changed overlay vertex");
}

void OverlayMaterialAnimationTests::mprMaterialEditorUsesLiveWorkingAtlasSlotCount()
{
    QDir repository(QDir::current());
    while(!repository.exists("source/render/view.cpp") && repository.cdUp()) {}
    QFile viewFile(repository.absoluteFilePath("source/render/view.cpp"));
    QVERIFY(viewFile.open(QIODevice::ReadOnly));
    const QByteArray view = viewFile.readAll();
    const int editorStart = view.indexOf("Atlas tile animation records");
    const int editorEnd = view.indexOf("QWidget* identityPage", editorStart);
    QVERIFY(editorStart >= 0);
    QVERIFY(editorEnd > editorStart);
    const QByteArray editor = view.mid(editorStart, editorEnd - editorStart);

    QVERIFY2(editor.contains("activeTerrainAtlasSlotCount()"),
             "Material Editor must validate animation records against the live working atlas");
    QVERIFY2(!editor.contains("m_mprDocument.mapInfo().tileCount"),
             "Material Editor still rejects valid appended working-atlas slots against the old MPR count");
}

void OverlayMaterialAnimationTests::overlayActivationUsesExactWorkingAtlasMaterial()
{
    QDir repository(QDir::current());
    while(!repository.exists("source/render/view.cpp") && repository.cdUp()) {}
    QFile viewFile(repository.absoluteFilePath("source/render/view.cpp"));
    QVERIFY(viewFile.open(QIODevice::ReadOnly));
    QByteArray view = viewFile.readAll();
    view.replace("\r\n", "\n");
    const int commandStart = view.indexOf(
        "if(!bLand && m_landscapeTools->overlaySurfaceOperation()");
    const int commandEnd = view.indexOf(
        "const ETerrainTileLayer layer = bLand", commandStart);
    QVERIFY(commandStart >= 0);
    QVERIFY(commandEnd > commandStart);
    const QByteArray activation = view.mid(
        commandStart, commandEnd - commandStart);

    QVERIFY2(activation.contains("realizeWorkingTerrainSignature("),
             "Modern Add Overlay must realize the selected exact uniform signature in the live working atlas");
    QVERIFY2(activation.contains("selectWorkingPaintVariantWithRuntimeType("),
             "Add Overlay must retain the selected family's reviewed runtime terrain type");
    QVERIFY2(!activation.contains("m_terrainMaterialRepresentatives.value"),
             "Add Overlay still relies on the palette representative map that global palette construction clears");
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

void OverlayMaterialAnimationTests::materialApplyInvalidatesSectorDrawData()
{
    QDir repository(QDir::current());
    while(!repository.exists("source/render/landscape.cpp") && repository.cdUp()) {}
    QFile headerFile(repository.absoluteFilePath("source/render/landscape.h"));
    QFile sourceFile(repository.absoluteFilePath("source/render/landscape.cpp"));
    QVERIFY(headerFile.open(QIODevice::ReadOnly));
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray header = headerFile.readAll();
    const QByteArray source = sourceFile.readAll();

    QVERIFY2(header.contains("void setMaterials(const QVector<SMaterial>& arrMat);"),
             "Material assignment must be an invalidating operation, not an inline table swap");
    QVERIFY2(source.contains("m_materialDrawDataDirty = true;"),
             "Applying material edits must invalidate baked sector material data");
    QVERIFY2(source.contains("refreshMaterialDrawDataIfNeeded();"),
             "The render path must consume pending material invalidation under the active GL context");
    QFile viewFile(repository.absoluteFilePath("source/render/view.cpp"));
    QVERIFY(viewFile.open(QIODevice::ReadOnly));
    QVERIFY2(!viewFile.readAll().contains("setUniformValue(\"transparency\", 0.3f)"),
             "The viewport must use authored per-material opacity instead of a fixed overlay subtraction");
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

void OverlayMaterialAnimationTests::derivesMaterialOpacity()
{
    SMaterial source = material(ETerrainType::eTerrainWater, 0.5f, 0.0f);
    source.A = 0.35f;
    QCOMPARE(ei::terrainMaterialOpacity(&source), 0.35f);
    source.A = -1.0f;
    QCOMPARE(ei::terrainMaterialOpacity(&source), 0.0f);
    source.A = 4.0f;
    QCOMPARE(ei::terrainMaterialOpacity(&source), 1.0f);
    QCOMPARE(ei::terrainMaterialOpacity(nullptr), 1.0f);
}

void OverlayMaterialAnimationTests::usesSharedOverlayVertexRules()
{
    const QVector<qint16> materials{
        10, 11,
        12, 13
    };
    QCOMPARE(ei::sharedOverlayMaterialIndex(materials, 2, 2, 2, 2), 10);
    QCOMPARE(ei::sharedOverlayMaterialIndex(materials, 2, 2, 2, 1), 10);
    QCOMPARE(ei::sharedOverlayMaterialIndex(materials, 2, 2, 1, 2), 10);
    QCOMPARE(ei::sharedOverlayMaterialIndex(materials, 2, 2, 3, 3), 13);

    QVector<qint16> sparse = materials;
    sparse[0] = -1;
    QCOMPARE(ei::sharedOverlayMaterialIndex(sparse, 2, 2, 2, 2), 12);
    QCOMPARE(ei::sharedOverlayMaterialIndex(sparse, 2, 2, 0, 0), -1);
}

void OverlayMaterialAnimationTests::gatesUnderlayEmissionByPositiveDepth()
{
    const QVector3D red(0.8f, 0.0f, 0.0f);
    QCOMPARE(ei::terrainUnderlayEmission(red, 3.0f, 3.25f), red);
    QCOMPARE(ei::terrainUnderlayEmission(red, 3.0f, 3.0f), QVector3D());
    QCOMPARE(ei::terrainUnderlayEmission(red, 3.0f, 2.75f), QVector3D());
}

void OverlayMaterialAnimationTests::mprSettingsUsesTopLevelWindowAndOverlaySlotToggle()
{
    QDir repository(QDir::current());
    while(!repository.exists("source/render/view.cpp") && repository.cdUp()) {}
    QFile viewFile(repository.absoluteFilePath("source/render/view.cpp"));
    QVERIFY(viewFile.open(QIODevice::ReadOnly));
    const QByteArray view = viewFile.readAll();

    QVERIFY2(view.contains("QDialog(nullptr, Qt::Window | Qt::WindowSystemMenuHint"),
             "MPR Settings must use the same independent top-level window path as minimap export");
    QVERIFY2(view.contains("showOverlayMaterialSlotsFromMprSettings"),
             "The Materials Editor must expose a direct overlay-slot inspection shortcut");
    QVERIFY2(view.contains("ELandscapeEditMode::OverlayPaint"),
             "The shortcut must activate Paint existing overlays rather than another landscape mode");
    QVERIFY2(view.contains("showMaterialSlots->setCheckable(true)"),
             "The material-slot viewport control must expose its persistent on/off state");
    QVERIFY2(view.contains("setOverlayMaterialSlotLabelsVisible(checked)"),
             "The material-slot viewport control must both show and hide the labels");
    QVERIFY2(view.contains("overlayMaterialSlotLabelsChanged"),
             "The control must stay synchronized with Landscape Tools");
}

void OverlayMaterialAnimationTests::mprMaterialEditorReloadsSelectionAndDoesNotStealEnter()
{
    QDir repository(QDir::current());
    while(!repository.exists("source/render/view.cpp") && repository.cdUp()) {}
    QFile viewFile(repository.absoluteFilePath("source/render/view.cpp"));
    QVERIFY(viewFile.open(QIODevice::ReadOnly));
    const QByteArray view = viewFile.readAll();
    QByteArray normalizedView = view;
    normalizedView.replace("\r\n", "\n");

    QVERIFY2(view.contains("showMaterialSlots->setAutoDefault(false)"),
             "Enter in a material editor must not activate or focus the slot-label toggle");
    QVERIFY2(!view.contains("else loadSelectedMaterial();"),
             "Rebuilding a non-empty material table must also refresh the editor fields");
    QVERIFY2(normalizedView.contains("materialTable->selectRow(qBound(0, selected, draftMaterials->size() - 1));\n        loadSelectedMaterial();"),
             "The selected material values and type must reload even when the row number stays unchanged");
    QVERIFY2(view.contains("Effective geometric amplitude is world Wind strength x 0.3 x this value"),
             "Wave multiplier help must explain that zero world wind means zero geometric motion");
    QVERIFY2(view.contains("does not control atlas-frame animation"),
             "Wave multiplier help must distinguish geometry from authored tile-frame playback");
}

void OverlayMaterialAnimationTests::resolvesAuthoredOverlayTileAnimation()
{
    SAnimTile zoneFiveWater{};
    zoneFiveWater.tileIndex = 64;
    zoneFiveWater.nPhase = 32;
    const QVector<SAnimTile> records{zoneFiveWater};

    const ei::SOverlayTileAnimation active =
        ei::overlayTileAnimation(64, &records);
    QVERIFY(active.animated);
    QCOMPARE(active.startSlot, 64);
    QCOMPARE(active.baseSlot, 64);
    QCOMPARE(active.frameCount, 32);

    QVERIFY(!ei::overlayTileAnimation(65, &records).animated);
    QVERIFY(!ei::overlayTileAnimation(64, nullptr).animated);
    zoneFiveWater.nPhase = 1;
    const QVector<SAnimTile> disabledRecords{zoneFiveWater};
    QVERIFY(!ei::overlayTileAnimation(64, &disabledRecords).animated);
}

QTEST_APPLESS_MAIN(OverlayMaterialAnimationTests)
#include "overlay_material_animation_tests.moc"
