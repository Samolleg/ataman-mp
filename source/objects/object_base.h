#ifndef OBJECT_H
#define OBJECT_H

#include <QVector>
#include <QVector2D>
#include <QMap>
#include <QGLWidget>
#include <QOpenGLFunctions>
//#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
//#include <QOpenGLTexture>
#include "node.h"
#include "part.h"
#include "unit_animation.h"

class CMob;

namespace ei {
struct SVisualAssemblyCacheTelemetry
{
    int memoryHits{0};
    int diskHits{0};
    int builds{0};
    qint64 diskReadMs{0};
    qint64 buildMs{0};
    qint64 cloneMs{0};
};

void resetVisualAssemblyCacheTelemetry();
SVisualAssemblyCacheTelemetry visualAssemblyCacheTelemetry();
}

enum class ENavigationVolumeKind : quint8
{
    Blocking,
    Hard,
    Easy,
    Danger,
    Bridge,
    Ignored
};
struct SNavigationCollisionProxy
{
    QVector<QVector2D> footprint;
    QVector<QVector3D> boxCorners;
    float minimumZ{0.0f};
    float maximumZ{0.0f};
    ENavigationVolumeKind kind{ENavigationVolumeKind::Blocking};
    bool authored{false};
    quint32 objectId{0};
    QString partName;
};

class CObjectBase : public CNode
{
public:
    CObjectBase();
    CObjectBase(const CObjectBase& base);
    CObjectBase(QJsonObject data);
    CObjectBase(CNode* node);
    ~CObjectBase() override;

    void draw(bool isActive, QOpenGLShaderProgram* program) override;
    void drawSelect(QOpenGLShaderProgram* program = nullptr) override;
    ENodeType nodeType() override {return ENodeType::eBaseType; }    //todo: delete, use this class as abstract type
    QString& modelName() override {return m_modelName; }
    QString textureName() override {return QString("default0");}
    void updateFigure(ei::CFigure* fig) override;
    void loadFigure() override;
    void loadTexture() override;
    void setModelName(QString name) {m_modelName = name;}
    QVector3D& minPosition() override { return m_minPoint; }
    void setTexture(QOpenGLTexture* texture) override;
    void serializeJson(QJsonObject& obj) override;
    uint serialize(util::CMobParser& parser) override;
    void collectParams(QList<QSharedPointer<IPropertyBase>>& aProp, ENodeType paramType) override;
    void collectlogicParams(QList<QSharedPointer<IPropertyBase>>& aProp, ENodeType paramType) override;
    void getParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType) override;
    void applyParam(const QSharedPointer<IPropertyBase>& prop) override;
    void getLogicParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType) override;
    void applyLogicParam(const QSharedPointer<IPropertyBase>& prop) override;
    bool updatePos(QVector3D& pos) override;
    void setRot(const QQuaternion& quat) override;
    const QVector3D& constitution() override final {return m_complection;}
    const QVector3D& complection() override {return m_complection;}
    void setConstitution(QVector3D& vec) override;
    QJsonObject toJson() override;
    CBox getBBox() override final;
    void markAsDeleted(bool bDeleted = true) override {m_bDeleted = bDeleted;}
    bool isMarkDeleted() override {return m_bDeleted;}
    bool isOperationAxisAllow(EOperationAxisType type) override {Q_UNUSED(type); return true;};
    QMap<QString, QVector<QVector2D>> renderedBodyPartUvTriangles() const;
    QVector<SNavigationCollisionProxy> navigationCollisionProxies() const;

protected:
    // Editor-only helper figures (lights, particle anchors) can be presented
    // at a useful selection size without changing their serialized transform
    // or the authored effect they represent.
    virtual float editorVisualScale() const { return 1.0f; }
    void recalcFigure();
    void recalcMinPos();
    void applyTransientVisual(const QString& modelName, const QString& textureName,
                              const QStringList& visibleParts, const QVector3D& complexion,
                              const QMap<QString, QString>& partTextureNames = {},
                              const QString& secondaryTextureName = QString(),
                              const QStringList& bodyTextureLayers = {},
                              const QMap<QString, QVector3D>& partEmissions = {});
    void invalidateTransientVisual() { m_transientVisualFingerprint.clear(); }
    const SAnimationSkeleton& transientAnimationSkeleton() const;
    void applyTransientPartPoses(const QMap<QString, QMatrix4x4>& poses);
    void applyTransientPartMorphs(const QMap<QString, QVector<QVector3D>>& morphs);
    void rebuildTransientAnimationSkeleton();
    //void updateVisibility(QVector<QString>& aPart);

protected:
    QString m_modelName;
    QVector3D m_complection;    //x-grace, y-strength, z-tall
    QStringList m_bodyParts; // for preparing vertex data

private:
    QVector<CPart*> m_aPart;
    QVector3D m_minPoint;   // min position of model bound boxes(units half body under ground)
    QOpenGLTexture* m_texture;
    QOpenGLTexture* m_secondaryTexture{nullptr};
    QMap<QString, QOpenGLTexture*> m_partTextures;
    QMap<QString, QVector3D> m_partEmissions;
    ei::CFigure* m_pFigure;
    bool m_bDeleted;
    CBox m_cachedBBox;
    bool m_bboxDirty;
    QString m_transientVisualFingerprint;
    QVector3D m_figureComplexion{1.0f, 1.0f, 1.0f};
    SAnimationSkeleton m_transientSkeleton;
};

#endif // OBJECT_H
