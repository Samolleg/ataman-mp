#ifndef PART_H
#define PART_H
#include <QString>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QSharedPointer>
#include "types.h"

class CSharedPartMesh;
class RenderQueueContext;

///
/// \brief The CPart class provides a display of each part(figure) of the object in the scene.
///
class CPart
{
public:
    CPart();
    CPart(const CPart& part);
    ~CPart();
    QVector<SVertexData>& vertData() {return m_aVertData; }
    const QVector<SVertexData>& vertData() const {return m_aVertData; }
    QString& name() {return m_name; }
    const QString& name() const {return m_name; }
    void setName(QString& name) {m_name = name; }
    void setVisible(bool bShow = true) {m_bShow = bShow;}
    void update(); //update shader buffers
    void draw(QOpenGLShaderProgram* program);
    void drawImmediate(QOpenGLShaderProgram* program);
    void drawSelect(QOpenGLShaderProgram* program);
    void setParentName(const QString& name) { m_parentName = name.toLower(); }
    const QString& parentName() const { return m_parentName; }
    void setRestOffset(const QVector3D& offset) { m_restOffset = offset; }
    const QVector3D& restOffset() const { return m_restOffset; }
    void setTextureNumber(int textureNumber) { m_textureNumber = textureNumber; }
    int textureNumber() const { return m_textureNumber; }
    void setTextureUvScale(float scale) { m_textureUvScale = scale; }
    float textureUvScale() const { return m_textureUvScale; }
    void setPoseMatrix(const QMatrix4x4& matrix) { m_poseMatrix = matrix; }
    void setSourceVertexIndices(const QVector<int>& indices) { m_sourceVertexIndices = indices; }
    void setMorphDeltas(const QVector<QVector3D>& deltas);
    bool isDrawable() const { return m_bShow && !m_aVertData.isEmpty(); }
    quintptr meshIdentity() const { return quintptr(m_mesh.data()); }
    static void setCacheScope(const QString& scope);
    static int sharedMeshCount();
    static quint64 sharedMeshBytes();

private:
    void attachSharedMesh(const QVector<SVertexData>& vertices);
    void attachPrivateMesh(const QVector<SVertexData>& vertices);

    QSharedPointer<CSharedPartMesh> m_mesh;

    QString m_name;
    QString m_parentName;
    QVector3D m_restOffset;
    int m_textureNumber{0};
    float m_textureUvScale{1.0f};
    QMatrix4x4 m_poseMatrix;
    QVector<SVertexData> m_aVertData;
    QVector<SVertexData> m_baseVertData;
    QVector<int> m_sourceVertexIndices;
    bool m_morphApplied{false};
    bool m_bShow;
};

#endif // PART_H
