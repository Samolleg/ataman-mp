#include <QtTest>

#include "figure_vertex_component.h"

class FigureVertexComponentTests : public QObject
{
    Q_OBJECT

private slots:
    void decodesVertexNormalAndUvIndicesInCorpusOrder();
    void rejectsOutOfRangeIndices();
};

void FigureVertexComponentTests::decodesVertexNormalAndUvIndicesInCorpusOrder()
{
    QVector<QVector3D> vertices = {
        QVector3D(10.0f, 0.0f, 0.0f),
        QVector3D(20.0f, 0.0f, 0.0f)
    };
    QVector<QVector4D> normals = {
        QVector4D(0.0f, 1.0f, 0.0f, 0.0f),
        QVector4D(0.0f, 0.0f, 1.0f, 0.0f)
    };
    QVector<QVector2D> textureCoordinates = {
        QVector2D(0.25f, 0.5f),
        QVector2D(0.75f, 1.0f)
    };

    US3 component;
    component.x = 1; // vertex index -- deliberately differs from normal
    component.y = 0; // normal index
    component.z = 1; // UV index

    SVertexData decoded;
    QVERIFY(ei::decodeFigureVertexComponent(
        component, vertices, normals, textureCoordinates, &decoded));
    QCOMPARE(decoded.position, vertices.at(1));
    QCOMPARE(decoded.normal, normals.at(0).toVector3D());
    QCOMPARE(decoded.texCoord, textureCoordinates.at(1));
    QCOMPARE(ei::figureSourceVertexIndex(component), 1);
}

void FigureVertexComponentTests::rejectsOutOfRangeIndices()
{
    QVector<QVector3D> vertices(2);
    QVector<QVector4D> normals(4);
    QVector<QVector2D> textureCoordinates(1);
    US3 component;
    component.x = 2;
    component.y = 3;
    component.z = 0;
    SVertexData decoded;
    QVERIFY(!ei::decodeFigureVertexComponent(
        component, vertices, normals, textureCoordinates, &decoded));
}

QTEST_APPLESS_MAIN(FigureVertexComponentTests)
#include "figure_vertex_component_tests.moc"
