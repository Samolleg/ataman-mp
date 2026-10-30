#ifndef FIGURE_H
#define FIGURE_H

#include <QDataStream>
#include <QVector>
#include <QVector4D>
#include <QMap>
#include "types.h"
#include "part.h"

namespace ei
{

struct SHeader{
    int vertBlocks = 0;
    int normalBlocks = 0;
    int uvCount = 0;
    int indexCount = 0;
    int vertexComponentCount = 0;
    int morphingComponentCount = 0;
    int unknown = 0;
    int group = 0;
    int textureNumber = 0;

    void read(QDataStream& stream);
};

///
/// \brief The CFigure class provides implementation of the internal 3D format of the game objects
///
class CFigure
{
public:
    CFigure();
    ~CFigure();
    //void getVertexData(QVector<SVertexData>& aVrtData, QVector3D& complection);
    void getVertexData(QVector<CPart*>& model, QVector3D& complection, QList<QString>& aBodyParts,
                       bool upload = true);
    void getMinimumBboxZ(float& value, QVector3D& complection);
    void uvCoords();
    void boundBox();
    void setComplex(float str, float dex, float tall);
    //TODO return methods of vertices, uv, normals, vert.indices, uv.indices
    bool readData(QDataStream& stream);
    bool completeCaptureGeometry() const {
        if(!m_completeCaptureGeometry)return false;
        for(auto* child:m_aChild)if(!child->completeCaptureGeometry())return false;
        return true;
    }
    void readAssemblyOffset(QDataStream& stream);
    void applyAssemblyOffset(QVector<QVector3D>* offset = nullptr);

    void addChild(ei::CFigure* fig) {m_aChild.append(fig); fig->m_parentName = m_name; }
    QList<ei::CFigure*>& children() {return m_aChild; }
    void setName (const QString& name) {m_name = name; }
    QString& name() {return m_name;}
    void setAssemblySourceOrder(int order) { m_assemblySourceOrder = order; }
    void setOffset(QVector<QVector3D>& offset) {m_offset = offset; }
    QVector<QVector3D>& offset() {return m_offset; }
    void getPartNames(QStringList& arrBodyParts);
    void getAssemblyHierarchy(QMap<QString, QString>& parents,
                              QMap<QString, QVector3D>& restOffsets,
                              QVector3D& complection);

private:
    void generateTriangles(QVector<SVertexData>& aVrtData, QVector<QVector3D>& aMorphVertex,
                           QVector<int>* sourceVertexIndices = nullptr);
    void calculateConstitution (QVector<SVertexData>& aVrtData, QVector3D& constitute,
                                QVector<int>* sourceVertexIndices = nullptr);
    void appendVertexData(QVector<CPart*>& model, QVector3D& complection,
                          QList<QString>& aBodyParts, bool upload);

private:
    //TODO: change vector to array
    //QVector<float> m_BoundBox;

    QVector<QVector4D> m_aNormal;
    QVector<QVector2D> m_aUvCoord;
    //QVector<QVector2D> m_aUvComp;   //converted uv coords to vertex data

    QVector<ushort> m_aVertInd;  //change to more container, not short
    //QVector<S3> m_vComponents;
    //QVector<int> m_normIndices;
    QVector<ushort> m_aUvInd;

    // vectors include morph components
    QVector<QVector<QVector3D>> m_aMorphVertex;    // [1 or 8 forms][vertex] XYZ
    //TODO: change vector to array or bbox class
    QVector<QVector3D> m_morphMin;   // 1 or 8 vectors; TODO bbox class
    QVector<QVector3D> m_morphMax;   // 1 or 8 vectors
    QVector<QVector3D> m_morphCenter; //1 or 8 vectors
    //QVector<float> m_morphRadius;  //8

    QVector<QVector3D> m_offset; //8x3 assembly offset from bon file
    QVector<ushort> m_aInd;
    QVector<US3> m_aVertComp;
    int m_textureNumber{0};
    QString m_name;
    QString m_parentName;
    int m_assemblySourceOrder{-1};
    QList<ei::CFigure*> m_aChild;
    bool m_completeCaptureGeometry{false};

};

} // ei namespace

#endif // FIGURE_H
