#include <QtTest>

#include "figure_vertex_component.h"

class FigureVertexComponentTests : public QObject
{
    Q_OBJECT

private slots:
    void decodesNormalVertexAndUvIndicesInFig8Order();
};

void FigureVertexComponentTests::decodesNormalVertexAndUvIndicesInFig8Order()
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
    component.x = 0; // normal index
    component.y = 1; // vertex index -- deliberately differs from normal
    component.z = 1; // UV index

    const SVertexData decoded = ei::decodeFigureVertexComponent(
        component, vertices, normals, textureCoordinates);
    QCOMPARE(decoded.position, vertices.at(1));
    QCOMPARE(decoded.normal, normals.at(0).toVector3D());
    QCOMPARE(decoded.texCoord, textureCoordinates.at(1));
    QCOMPARE(ei::figureSourceVertexIndex(component), 1);
}

QTEST_APPLESS_MAIN(FigureVertexComponentTests)
#include "figure_vertex_component_tests.moc"
