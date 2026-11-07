#ifndef CSELECTFRAME_H
#define CSELECTFRAME_H

#include <QVector>
#include <QVector2D>
#include <QRect>
#include <QOpenGLBuffer>
#include <QMatrix4x4>

//forward declarations
class QOpenGLShaderProgram;

///
/// \brief The CSelectFrame class draws a frame for selecting objects
///
class CSelectFrame
{
public:
    CSelectFrame();
    ~CSelectFrame();
    void draw(QOpenGLShaderProgram* program);
    void updateFrame(QPointF& bottomLeft, QPointF& topRight);
    void updateNativeFrame(const QPointF& a,const QPointF& b) {m_aPoints={QVector3D(a.x(),a.y(),0),QVector3D(a.x(),b.y(),0),QVector3D(b.x(),b.y(),0),QVector3D(b.x(),a.y(),0)};}
    const QVector<QVector3D>& points() const {return m_aPoints;}
    void reset();

private:
    QOpenGLBuffer m_vertexBuf; //vertex buffer for drawing links between look point and patrol point
    QOpenGLBuffer m_indexBuf;
    QMatrix4x4 m_matrix;
    QVector<QVector3D> m_aPoints; //use QVector3D?
};

#endif // CSELECTFRAME_H
