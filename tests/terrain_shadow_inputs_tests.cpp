#include "terrain_shadow_inputs.h"
#include <QtTest>

class TerrainShadowInputsTests : public QObject
{
    Q_OBJECT
private slots:
    void geometryAndCoverage()
    {
        const int vertices = 33 * 33 * 8, tiles = 16 * 16 * 2;
        QByteArray land(5 + vertices + tiles, 0);
        QVERIFY(!terrainShadowInputsChanged(land, land));
        auto paint = land; paint[5 + vertices] = 12;
        QVERIFY(!terrainShadowInputsChanged(land, paint));
        auto height = land; height[5] = 1;
        QVERIFY(terrainShadowInputsChanged(land, height));
        QVERIFY(terrainShadowInputsChanged(height, land));
        QVERIFY(terrainShadowInputsChanged({}, land));
        QVERIFY(terrainShadowInputsChanged(land.left(land.size()-1), land));
        auto overlay = QByteArray(5 + 2 * vertices + 3 * tiles, 0); overlay[4] = 3;
        QVERIFY(terrainShadowInputsChanged(land, overlay));
        QVERIFY(terrainShadowInputsChanged(overlay, land));
        const int overlayTiles = 5 + 2 * vertices + tiles;
        auto texture = overlay; texture[overlayTiles] = 42;
        QVERIFY(!terrainShadowInputsChanged(overlay, texture));
        auto material = overlay; material[overlayTiles + tiles] = 9;
        QVERIFY(!terrainShadowInputsChanged(overlay, material));
        auto missing = overlay; missing[overlayTiles] = char(255); missing[overlayTiles+1] = char(255);
        QVERIFY(terrainShadowInputsChanged(overlay, missing));
        QVERIFY(terrainShadowInputsChanged(missing, overlay));
        auto unknown = land; unknown[4] = 7;
        QVERIFY(terrainShadowInputsChanged(unknown, unknown));
    }
};
QTEST_APPLESS_MAIN(TerrainShadowInputsTests)
#include "terrain_shadow_inputs_tests.moc"
