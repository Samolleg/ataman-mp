#ifndef NODE_H
#define NODE_H

#include "QVector"
#include <QQuaternion>
#include <QGLWidget>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QJsonObject>
#include <QByteArray>

#include "types.h"
#include "utils.h"
#include "figure.h"
//#include "property.h"

class CMob;
class IPropertyBase;

///
/// \brief The CNode class provides basic implementation of 3D scene node such as: unit, light source, particle, etc.
///
class CNode : public QObject
{
    Q_OBJECT
public:
    CNode();
    CNode(CNode* parent);
    CNode(const CNode& node);
    static uint s_freeId;
    virtual ~CNode();
    virtual void draw(bool isActive, QOpenGLShaderProgram* program = nullptr) = 0;
    // Rendering is split so the view can cull heavy object geometry while
    // retaining logic helpers whose paths may extend into the viewport.
    virtual void drawObjectVisual(bool isActive, QOpenGLShaderProgram* program = nullptr)
        { draw(isActive, program); }
    virtual void drawLogicVisual(bool isActive, QOpenGLShaderProgram* program = nullptr)
        { Q_UNUSED(isActive); Q_UNUSED(program); }
    virtual void drawSelect(QOpenGLShaderProgram* program = nullptr) = 0;
    virtual ENodeType nodeType() = 0;
    virtual uint deserialize(util::CMobParser& parser) {Q_UNUSED(parser); Q_ASSERT("pure virtual" && false); return 0;} //todo: define this method in CObjectBase
    virtual QString& modelName() = 0;
    virtual QString textureName() = 0;
    virtual QVector3D& minPosition() = 0;
    virtual void updateFigure(ei::CFigure* fig) = 0;
    virtual void loadFigure() = 0;
    virtual void setTexture(QOpenGLTexture* texture) = 0;
    virtual void loadTexture() = 0;
    virtual void serializeJson(QJsonObject& obj) = 0;
    virtual uint serialize(util::CMobParser& parser) = 0;
    virtual void collectParams(QList<QSharedPointer<IPropertyBase>>& aProp, ENodeType paramType) = 0;
    virtual void collectlogicParams(QList<QSharedPointer<IPropertyBase>>& aProp, ENodeType paramType) = 0;
    virtual void getParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType) = 0;
    virtual void applyParam(const QSharedPointer<IPropertyBase>& prop) = 0;
    virtual void getLogicParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType) = 0;
    virtual void applyLogicParam(const QSharedPointer<IPropertyBase>& prop) = 0;
    virtual bool updatePos(QVector3D& pos) = 0;
    virtual const QVector3D& complection() = 0;
    virtual void setConstitution(QVector3D& vec) = 0;
    virtual const QVector3D& constitution() = 0;
    virtual QJsonObject toJson() = 0;
    virtual CBox getBBox() = 0;
    virtual void markAsDeleted(bool bDeleted = true) = 0;
    virtual bool isMarkDeleted() = 0;
    virtual bool isOperationAxisAllow(EOperationAxisType type) = 0;

    virtual void setRot(const QQuaternion& quat) {m_rotation = quat;}
    virtual void setDrawPosition(QVector3D pos) {m_drawPosition = pos;}

    const uint& innerId() {return m_id; }
    const uint& mapId(){return m_mapID;}
    void setMapId(uint id) {m_mapID = id;}
    const QString& mapName(){return m_name;}
    void addChild(CNode* child) {m_aChild.append(child); child->setParent(this); }
    void setParent(CNode* parent) {m_parent = parent;}
    uchar* color() {return m_pickingColor.rgb; }
    bool isColorSuitable(const SColor& color) {return m_pickingColor == color; }
    void setPos(QVector3D& pos) {m_position = pos;}
    void setRot(const QVector4D& quat) {m_rotation = QQuaternion(quat);}
    QVector3D getEulerRotation();
    void move(float x, float y, float z);
    QVector3D& position() {return m_position; }
    QVector3D& drawPosition() {return m_drawPosition;}
    void setState(ENodeState state) {m_state = state;}
    ENodeState nodeState() const {return m_state;}

    void setRawObjectProvenance(const QByteArray& documentSha256,
                                const QString& sectionPath,
                                const QByteArray& subtreeSha256,
                                bool copiedObject = false)
    {
        m_rawSourceDocumentSha256 = documentSha256;
        m_rawSourceObjectPath = sectionPath;
        m_rawSourceSubtreeSha256 = subtreeSha256;
        m_rawCopiedObject = copiedObject;
        if(!copiedObject)
        {
            m_rawTemplateSourceSubtree.clear();
            m_rawTemplateCleanSubtree.clear();
        }
    }
    bool hasRawObjectProvenance() const
    {
        return m_rawSourceDocumentSha256.size() == 32
                && !m_rawSourceObjectPath.isEmpty()
                && m_rawSourceSubtreeSha256.size() == 32;
    }
    const QByteArray& rawSourceDocumentSha256() const { return m_rawSourceDocumentSha256; }
    const QString& rawSourceObjectPath() const { return m_rawSourceObjectPath; }
    const QByteArray& rawSourceSubtreeSha256() const { return m_rawSourceSubtreeSha256; }
    bool isRawCopiedObject() const { return m_rawCopiedObject; }
    void setRawObjectTemplateBytes(const QByteArray& sourceSubtree,
                                   const QByteArray& cleanSubtree)
    {
        m_rawTemplateSourceSubtree = sourceSubtree;
        m_rawTemplateCleanSubtree = cleanSubtree;
    }
    void setRawStructuralReplacementBytes(const QByteArray& sourceSubtree,
                                          const QByteArray& cleanSubtree)
    {
        setRawObjectTemplateBytes(sourceSubtree, cleanSubtree);
        m_rawStructuralReplacement = true;
    }
    bool hasRawStructuralReplacementBytes() const
    {
        return m_rawStructuralReplacement && hasRawObjectTemplateBytes();
    }
    bool hasRawObjectTemplateBytes() const
    {
        return !m_rawTemplateSourceSubtree.isEmpty()
                && !m_rawTemplateCleanSubtree.isEmpty();
    }
    const QByteArray& rawTemplateSourceSubtree() const { return m_rawTemplateSourceSubtree; }
    const QByteArray& rawTemplateCleanSubtree() const { return m_rawTemplateCleanSubtree; }

private:
    void init();

protected:
    QVector3D m_position; // offset from landscape
    QVector3D m_drawPosition; // absolute position for object
    //QVector3D m_rotation;
    uint m_mapID;
    QString m_name;
    QString m_comment;
    QQuaternion m_rotation;
    QVector<CNode*> m_aChild;
    CNode* m_parent;
    SColor m_pickingColor;
    ENodeState m_state;
    QByteArray m_rawSourceDocumentSha256;
    QString m_rawSourceObjectPath;
    QByteArray m_rawSourceSubtreeSha256;
    bool m_rawCopiedObject;
    QByteArray m_rawTemplateSourceSubtree;
    QByteArray m_rawTemplateCleanSubtree;
    bool m_rawStructuralReplacement;

private:
    uint m_id;

};

#endif // NODE_H
