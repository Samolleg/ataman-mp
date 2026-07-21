#include "objects\object_base.h"
#include <QJsonArray>
#include <QCache>
#include <QFileInfo>
#include <QString>
#include "mob\mob.h"
#include "landscape.h"
#include "view.h"
#include "log.h"
#include "resourcemanager.h"
#include "resource_name.h"
#include "render_performance.h"
#include "render_queue.h"
#include "settings.h"
#include "property.h"

#include <climits>

namespace {
struct CachedFigureAssembly
{
    ~CachedFigureAssembly()
    {
        qDeleteAll(parts);
    }
    QVector<CPart*> parts;
};

QCache<QString, CachedFigureAssembly>& transientAssemblyCache()
{
    static QCache<QString, CachedFigureAssembly> cache;
    static bool initialized = false;
    if(!initialized)
    {
        // Costs are KiB of retained CPU vertex data. This keeps the cache
        // useful for dense humanoid maps without allowing old profiles/maps
        // to retain an unbounded amount of assembly geometry.
        cache.setMaxCost(128 * 1024);
        initialized = true;
    }
    return cache;
}

QString meshCacheScope(const QString& modelName)
{
    CSettings* settings = CObjectList::getInstance()->settings();
    const COptString* profile = settings
        ? dynamic_cast<const COptString*>(settings->opt("resourceProfile")) : nullptr;
    const COptStringList* roots = settings
        ? dynamic_cast<const COptStringList*>(settings->opt(eOptSetResource, "resourceRoots")) : nullptr;
    QString scope = (profile ? profile->value() : QString()) + '|';
    QVector<QString> sourcePaths = roots ? roots->value() : QVector<QString>();
    if(sourcePaths.isEmpty() && settings)
    {
        for(const char* optionName : {"figPaths", "texPaths"})
        {
            const COptStringList* paths = dynamic_cast<const COptStringList*>(
                settings->opt(eOptSetResource, optionName));
            if(paths)
                sourcePaths += paths->value();
        }
    }
    for(const QString& root : sourcePaths)
            scope += QFileInfo(root).absoluteFilePath().toLower() + ';';
    return scope + '|' + modelName.toLower();
}

void cloneAssembly(QVector<CPart*>& destination, const QVector<CPart*>& source)
{
    destination.reserve(source.size());
    for(CPart* part : source)
        destination.append(new CPart(*part));
}

void buildCachedAssembly(ei::CFigure* figure, const QString& scope,
                         const QString& modelName, const QStringList& visibleParts,
                         const QVector3D& complexion, QVector<CPart*>& destination)
{
    const QString key = scope + '|' + modelName.toLower() + '|'
        + visibleParts.join(';').toLower() + '|'
        + QString::number(complexion.x(), 'g', 8) + ','
        + QString::number(complexion.y(), 'g', 8) + ','
        + QString::number(complexion.z(), 'g', 8);
    QCache<QString, CachedFigureAssembly>& cache = transientAssemblyCache();
    CachedFigureAssembly* cached = cache.object(key);
    if(!cached)
    {
        cached = new CachedFigureAssembly;
        QStringList parts = visibleParts;
        QVector3D renderComplexion = complexion;
        figure->getVertexData(cached->parts, renderComplexion, parts);
        quint64 retainedBytes = 0;
        for(CPart* part : cached->parts)
            retainedBytes += quint64(part->vertData().size()) * sizeof(SVertexData);
        const int costKiB = qMax(1, int(qMin<quint64>(retainedBytes / 1024,
                                                      quint64(INT_MAX))));
        cloneAssembly(destination, cached->parts);
        cache.insert(key, cached, costKiB);
        return;
    }
    cloneAssembly(destination, cached->parts);
}
}

CObjectBase::CObjectBase(): 
  m_modelName("")
  ,m_complection(1.0f, 1.0f, 1.0f)
  ,m_minPoint(0.0f, 0.0f, 0.0f)
  ,m_texture(nullptr)
  ,m_pFigure(nullptr)
  ,m_bDeleted(false)
  ,m_bboxDirty(true)
{

}

CObjectBase::CObjectBase(const CObjectBase &base):
    CNode(base)
  ,m_modelName(base.m_modelName)
  ,m_bDeleted(base.m_bDeleted)
  ,m_bboxDirty(true)
{
    m_complection = base.m_complection;
    m_texture = base.m_texture;
    m_secondaryTexture = base.m_secondaryTexture;
    m_partTextures = base.m_partTextures;
    m_pFigure = base.m_pFigure;
    m_minPoint = base.m_minPoint;
    m_bodyParts = base.m_bodyParts;
    m_figureComplexion = base.m_figureComplexion;
    m_transientSkeleton = base.m_transientSkeleton;

    m_aPart.clear();
    for(auto& part:base.m_aPart)
    {
        m_aPart.append(new CPart(*part));
    }
}

CObjectBase::CObjectBase(QJsonObject data):
    m_texture(nullptr)
    ,m_pFigure(nullptr)
  ,m_bDeleted(false)
  ,m_bboxDirty(true)
{

    //m_mapID = data["Id"].toVariant().toUInt(); //TODO: generate mapID
    //m_mapID = 333777;

    m_name = data["Map name"].toString();
    m_comment = data["Comments"].toString();

    QJsonArray arrComplection = data["Complection"].toArray();
    if (arrComplection.size() == 3)
        m_complection = QVector3D(arrComplection[0].toVariant().toFloat(), arrComplection[1].toVariant().toFloat(), arrComplection[2].toVariant().toFloat());

    //TODO: generate position (use mouse diff if object mmore than one
    QJsonArray arrPos = data["Position"].toArray();
    if (arrPos.size() == 3)
        m_position = QVector3D(arrPos[0].toVariant().toFloat(), arrPos[1].toVariant().toFloat(), arrPos[2].toVariant().toFloat());

    QJsonArray arrQuot = data["Rotation"].toArray();
    if (arrQuot.size() == 4)
    {
        QVector4D rot(arrQuot[0].toVariant().toFloat(), arrQuot[1].toVariant().toFloat(), arrQuot[2].toVariant().toFloat(), arrQuot[3].toVariant().toFloat());
        CNode::setRot(rot);
    }

}

CObjectBase::CObjectBase(CNode* node):
    CNode(node)
    ,m_texture(nullptr)
    ,m_pFigure(nullptr)
    ,m_bDeleted(false)
    ,m_bboxDirty(true)
{
}

CObjectBase::~CObjectBase()
{
    for(auto& part: m_aPart)
        delete part;
}

void CObjectBase::updateFigure(ei::CFigure* fig)
{
    m_bboxDirty = true;
    if(!fig)
    {
        ei::log(eLogWarning, "Cannot update object figure; placeholder is unavailable for model: " + m_modelName);
        for(auto& part: m_aPart)
            delete part;
        m_aPart.clear();
        m_pFigure = nullptr;
        m_transientSkeleton = SAnimationSkeleton();
        return;
    }
    m_pFigure = fig;
    recalcFigure();
}

void CObjectBase::loadFigure()
{
    updateFigure(CObjectList::getInstance()->getFigure(m_modelName));
}

void CObjectBase::loadTexture()
{
//    QString texName("default0");
//    setTexture(CTextureList::getInstance()->texture(texName));
}

void CObjectBase::setTexture(QOpenGLTexture* texture)
{
    Q_ASSERT(texture);
    m_texture = texture;
}

void CObjectBase::draw(bool isActive, QOpenGLShaderProgram* program)
{
    Q_UNUSED(isActive);
    if(isMarkDeleted())
        return;

    if (m_state == ENodeState::eHidden) //dont draw hidden objects
        return;

    //Q_ASSERT(m_texture);
    if(m_texture == nullptr)
    {
        loadTexture();
        ei::log(eLogWarning, "empty texture, set default");
        return;
    }

    QMatrix4x4 matrix;
    matrix.setToIdentity();
    matrix.translate(m_drawPosition);
    QMatrix4x4 rtMatrix;
    rtMatrix.setToIdentity();
    rtMatrix.rotate(m_rotation);
    matrix = matrix * rtMatrix;

    RenderQueueContext& queue = RenderQueueContext::instance();
    const bool queued = queue.collecting();
    if(!queued)
        program->setUniformValue("u_modelMmatrix", matrix);

    if (!queued && m_state == ENodeState::eSelect)
        program->setUniformValue("u_highlight", true);

    if(!queued)
        program->setUniformValue("qt_Texture0", 0);
    QOpenGLTexture* boundTexture = nullptr;
    for(auto& part: m_aPart)
    {
        QOpenGLTexture* texture = m_partTextures.value(part->name().toLower(), nullptr);
        if(!texture)
            texture = part->textureNumber() == 2 && m_secondaryTexture
                ? m_secondaryTexture : m_texture;
        if(queued)
        {
            queue.submit(part, texture, matrix,
                         CTextureList::getInstance()->materialClass(texture),
                         m_state == ENodeState::eSelect);
            continue;
        }
        if(texture != boundTexture)
        {
            texture->bind(0);
            boundTexture = texture;
            RenderPerformanceContext& performance = RenderPerformanceContext::instance();
            if(performance.collecting())
                ++performance.counters().textureBinds;
        }
        part->draw(program);
    }
    if(!queued)
    {
        program->setUniformValue("u_partAnimated", false);
        program->setUniformValue("u_highlight", false);
    }
}

void CObjectBase::drawSelect(QOpenGLShaderProgram* program)
{
    if(isMarkDeleted())
        return;

    if (m_state == ENodeState::eHidden) //skip hidden object for select
        return;

    Q_ASSERT(m_texture);
    m_texture->bind(0);
    if(m_texture == nullptr)
        return;

    //if (!m_parent)
    {
        QMatrix4x4 matrix;
        matrix.setToIdentity();
        matrix.translate(m_drawPosition);
        QMatrix4x4 rtMatrix;
        rtMatrix.setToIdentity();
        rtMatrix.rotate(m_rotation);
        matrix = matrix * rtMatrix;
        RenderQueueContext& queue = RenderQueueContext::instance();
        if(queue.collectingSelection())
        {
            const QVector4D pickingColor = m_pickingColor.toVec4();
            for(CPart* part : m_aPart)
                queue.submitSelection(part, matrix, pickingColor);
            return;
        }
        program->setUniformValue("u_modelMmatrix", matrix);
        program->setUniformValue("u_color", m_pickingColor.toVec4());
    }

    for(auto& part: m_aPart)
        part->drawSelect(program);
    program->setUniformValue("u_partAnimated", false);
}

QMap<QString, QVector<QVector2D>> CObjectBase::renderedBodyPartUvTriangles() const
{
    QMap<QString, QVector<QVector2D>> result;
    for(const CPart* part : m_aPart)
    {
        if(!part)
            continue;
        const QString key = part->name().toLower();
        // The final-character atlas is the primary body texture. Heads/hair,
        // helmets, weapons, shields, and texture-number-2 members use another
        // texture and would be misleading if projected over this image.
        if(m_partTextures.contains(key)
                || (part->textureNumber() == 2 && m_secondaryTexture))
            continue;
        QVector<QVector2D> coordinates;
        coordinates.reserve(part->vertData().size());
        for(const SVertexData& vertex : part->vertData())
            coordinates.append(vertex.texCoord * part->textureUvScale());
        if(coordinates.size() >= 3)
            result.insert(part->name(), coordinates);
    }
    return result;
}

const SAnimationSkeleton& CObjectBase::transientAnimationSkeleton() const
{
    return m_transientSkeleton;
}

void CObjectBase::rebuildTransientAnimationSkeleton()
{
    m_transientSkeleton = SAnimationSkeleton();
    if(!m_pFigure)
        return;
    SAnimationSkeleton full;
    QVector3D complexion = m_figureComplexion;
    m_pFigure->getAssemblyHierarchy(full.parents, full.restOffsets, complexion);
    QSet<QString> required;
    for(const CPart* part : m_aPart)
    {
        QString component = part->name().toLower();
        QSet<QString> chain;
        while(!component.isEmpty() && !chain.contains(component))
        {
            chain.insert(component);
            required.insert(component);
            component = full.parents.value(component);
        }
    }
    for(const QString& component : required)
    {
        if(full.parents.contains(component))
            m_transientSkeleton.parents.insert(component, full.parents.value(component));
        if(full.restOffsets.contains(component))
            m_transientSkeleton.restOffsets.insert(component, full.restOffsets.value(component));
    }
}

void CObjectBase::applyTransientPartPoses(const QMap<QString, QMatrix4x4>& poses)
{
    for(CPart* part : m_aPart)
    {
        QMatrix4x4 identity; identity.setToIdentity();
        part->setPoseMatrix(poses.value(part->name().toLower(), identity));
    }
}

void CObjectBase::applyTransientPartMorphs(const QMap<QString, QVector<QVector3D>>& morphs)
{
    for(CPart* part : m_aPart)
        part->setMorphDeltas(morphs.value(part->name().toLower()));
}

void CObjectBase::serializeJson(QJsonObject& obj)
{
    obj.insert("Model name", m_modelName);
    obj.insert("Id", QJsonValue::fromVariant(m_mapID));
    QJsonArray aComplection;
    aComplection.append(QJsonValue::fromVariant(m_complection.x()));
    aComplection.append(QJsonValue::fromVariant(m_complection.y()));
    aComplection.append(QJsonValue::fromVariant(m_complection.z()));
    obj.insert("Complection", aComplection);

    QJsonArray pos;
    pos.append(QJsonValue::fromVariant(m_position.x()));
    pos.append(QJsonValue::fromVariant(m_position.y()));
    pos.append(QJsonValue::fromVariant(m_position.z()));
    obj.insert("Position", pos);

    QJsonArray rot;
    rot.append(QJsonValue::fromVariant(m_rotation.x()));
    rot.append(QJsonValue::fromVariant(m_rotation.y()));
    rot.append(QJsonValue::fromVariant(m_rotation.z()));
    rot.append(QJsonValue::fromVariant(m_rotation.scalar()));
    obj.insert("Rotation", rot);

    return;
}

uint CObjectBase::serialize(util::CMobParser &parser)
{
    Q_UNUSED(parser);
    Q_ASSERT("What are you doing here?" && false);
    return 0;
}



bool CObjectBase::updatePos(QVector3D& pos)
{
    m_position = pos;
    CLandscape::getInstance()->projectPosition(this);
    return true;
}

void CObjectBase::setConstitution(QVector3D &vec)
{
    m_complection = vec;
    recalcFigure();
    CLandscape::getInstance()->projectPosition(this);
}

QJsonObject CObjectBase::toJson()
{
    QJsonObject obj;
    obj.insert("Id", QJsonValue::fromVariant(m_mapID));
    obj.insert("Node type", nodeType());

    obj.insert("Map name", m_name);
    obj.insert("Comments", m_comment);

    QJsonArray aComplection;
    aComplection.append(QJsonValue::fromVariant(m_complection.x()));
    aComplection.append(QJsonValue::fromVariant(m_complection.y()));
    aComplection.append(QJsonValue::fromVariant(m_complection.z()));
    obj.insert("Complection", aComplection);

    QJsonArray pos;
    pos.append(QJsonValue::fromVariant(m_position.x()));
    pos.append(QJsonValue::fromVariant(m_position.y()));
    pos.append(QJsonValue::fromVariant(m_position.z()));
    obj.insert("Position", pos);

    QJsonArray rot;
    rot.append(QJsonValue::fromVariant(m_rotation.x()));
    rot.append(QJsonValue::fromVariant(m_rotation.y()));
    rot.append(QJsonValue::fromVariant(m_rotation.z()));
    rot.append(QJsonValue::fromVariant(m_rotation.scalar()));
    obj.insert("Rotation", rot);
    return obj;
}

CBox CObjectBase::getBBox()
{
    if(!m_bboxDirty)
        return m_cachedBBox;
    QVector3D min;
    QVector3D max;
    bool bInit = false;
    QMatrix4x4 rtMatrix;
    rtMatrix.setToIdentity();
    rtMatrix.rotate(m_rotation);
    const auto fillPoint = [&min, &max](const QVector3D& vec)
    {
        //x
        if(vec.x() < min.x())
            min.setX(vec.x());
        else if(vec.x() > max.x())
            max.setX(vec.x());
        //y
        if(vec.y() < min.y())
            min.setY(vec.y());
        else if(vec.y() > max.y())
            max.setY(vec.y());
        //z
        if(vec.z() < min.z())
            min.setZ(vec.z());
        else if(vec.z() > max.z())
            max.setZ(vec.z());
    };

    for(auto& part: m_aPart)
    {
        auto& arrVert = part->vertData();
        for(int i(0); i < arrVert.size(); ++i)
        {
            const QVector3D rotatedPos = rtMatrix*arrVert[i].position;
            if(!bInit)
            {
                min = rotatedPos;
                max = rotatedPos;
                bInit = true;
                continue;
            }
            fillPoint(rotatedPos);
        }
    }

   m_cachedBBox = CBox(min, max);
   m_bboxDirty = false;
   return m_cachedBBox;
}

void CObjectBase::setRot(const QQuaternion& quat)
{
    CNode::setRot(quat);
    m_bboxDirty = true;
    recalcMinPos();
}

void CObjectBase::collectParams(QList<QSharedPointer<IPropertyBase>>& aProp, ENodeType paramType)
{
    Q_UNUSED(paramType);
    propUint mapId(eObjParam_NID, m_mapID);
    util::addParam(aProp, &mapId);
    propStr comment(eObjParam_COMMENTS, m_comment);
    util::addParam(aProp, &comment);
    //prop3D pos(eObjParam_POSITION, m_position);
    propFloat posX(eObjParam_POSITION_X, m_position.x());
    util::addParam(aProp, &posX);
    propFloat posY(eObjParam_POSITION_Y, m_position.y());
    util::addParam(aProp, &posY);
    propFloat posZ(eObjParam_POSITION_Z, m_position.z());
    util::addParam(aProp, &posZ);
}

void CObjectBase::collectlogicParams(QList<QSharedPointer<IPropertyBase>>& aProp, ENodeType paramType)
{
    Q_UNUSED(aProp);
    Q_UNUSED(paramType);
    Q_ASSERT(false && "todo: implement");
}

void CObjectBase::getParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType)
{
    switch (propType){
    case eObjParam_NID:
    {
        prop.reset(new propUint(propType, m_mapID));
        break;
    }
    case eObjParam_NAME:
    {
        prop.reset(new propStr(propType, m_name));
        break;
    }
    case eObjParam_POSITION:
    {
        prop.reset(new prop3D(propType, m_position));
        break;
    }
    case eObjParam_POSITION_X:
    {
        prop.reset(new propFloat(propType, m_position.x()));
        break;
    }
    case eObjParam_POSITION_Y:
    {
        prop.reset(new propFloat(propType, m_position.y()));
        break;
    }
    case eObjParam_POSITION_Z:
    {
        prop.reset(new propFloat(propType, m_position.z()));
        break;
    }
    case eObjParam_COMMENTS:
    {
        prop.reset(new propStr(propType, m_comment));
        break;
    }
    default:
        Q_ASSERT(false);
    }
}

void CObjectBase::applyParam(const QSharedPointer<IPropertyBase>& prop)
{
    switch (prop->type()) {
    case eObjParam_NID:
    {
        m_mapID = dynamic_cast<const propUint*>(prop.get())->value();
        break;
    }
    case eObjParam_NAME:
    {
        m_name = dynamic_cast<const propStr*>(prop.get())->value();
        break;
    }
    case eObjParam_COMMENTS:
    {
        m_comment = dynamic_cast<const propStr*>(prop.get())->value();
        break;
    }
    case eObjParam_POSITION:
    {
        QVector3D pos = dynamic_cast<const prop3D*>(prop.get())->value();
        updatePos(pos);
        break;
    }
    case eObjParam_POSITION_X:
    {
        float val = dynamic_cast<const propFloat*>(prop.get())->value();
        QVector3D pos(position());
        pos.setX(val);
        updatePos(pos);
        break;
    }
    case eObjParam_POSITION_Y:
    {
        float val = dynamic_cast<const propFloat*>(prop.get())->value();
        QVector3D pos(position());
        pos.setY(val);
        updatePos(pos);
        break;
    }
    case eObjParam_POSITION_Z:
    {
        float val = dynamic_cast<const propFloat*>(prop.get())->value();
        QVector3D pos(position());
        pos.setZ(val);
        updatePos(pos);
        break;
    }
    default:
        //Q_ASSERT(false);
        break;
    }
}

void CObjectBase::getLogicParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType)
{
    Q_UNUSED(prop);
    Q_UNUSED(propType);
    Q_ASSERT(false);
    return;
}

void CObjectBase::applyLogicParam(const QSharedPointer<IPropertyBase>& prop)
{
    Q_UNUSED(prop);
    Q_ASSERT(false);
}

void CObjectBase::recalcFigure()
{
    m_bboxDirty = true;
    for(auto& part: m_aPart)
        delete part;
    m_aPart.clear();
    m_figureComplexion = m_complection;
    CPart::setCacheScope(meshCacheScope(m_modelName));
    m_pFigure->getVertexData(m_aPart, m_complection, m_bodyParts);
    rebuildTransientAnimationSkeleton();
    recalcMinPos();
}

void CObjectBase::applyTransientVisual(const QString& modelName, const QString& textureName,
                                       const QStringList& visibleParts, const QVector3D& complexion,
                                       const QMap<QString, QString>& partTextureNames,
                                       const QString& secondaryTextureName,
                                       const QStringList& bodyTextureLayers)
{
    QString textureFingerprint;
    for(auto it = partTextureNames.constBegin(); it != partTextureNames.constEnd(); ++it)
        textureFingerprint += it.key().toLower() + '=' + it.value().toLower() + ';';
    const QString fingerprint = modelName.toLower() + '|' + textureName.toLower() + '|'
        + secondaryTextureName.toLower() + '|'
        + bodyTextureLayers.join(';').toLower() + '|'
        + visibleParts.join(';').toLower() + '|' + QString::number(complexion.x(), 'g', 8)
        + ',' + QString::number(complexion.y(), 'g', 8) + ',' + QString::number(complexion.z(), 'g', 8)
        + '|' + textureFingerprint;
    if(fingerprint == m_transientVisualFingerprint)
        return;
    m_transientVisualFingerprint = fingerprint;
    m_pFigure = CObjectList::getInstance()->getFigure(modelName);
    // The caller already resolved profile-vs-import precedence. The
    // model-aware lookup normalizes a supplied .mmp suffix and resolves only
    // aliases belonging to that model/profile resource catalog.
    m_texture = bodyTextureLayers.isEmpty()
        ? CTextureList::getInstance()->texture(textureName, modelName)
        : CTextureList::getInstance()->layeredTexture(bodyTextureLayers, modelName);
    m_secondaryTexture = secondaryTextureName.trimmed().isEmpty() ? nullptr
        : CTextureList::getInstance()->texture(secondaryTextureName, modelName);
    m_partTextures.clear();
    for(auto it = partTextureNames.constBegin(); it != partTextureNames.constEnd(); ++it)
        m_partTextures.insert(it.key().toLower(),
            CTextureList::getInstance()->texture(it.value(), modelName));
    if(!bodyTextureLayers.isEmpty())
        for(const QString& partName : visibleParts)
            if(ei::usesUnlayeredHumanoidTexture(partName)
                    && !m_partTextures.contains(partName.toLower()))
                m_partTextures.insert(partName.toLower(),
                    CTextureList::getInstance()->texture(textureName, modelName));
    m_bboxDirty = true;
    for(CPart* part : m_aPart)
        delete part;
    m_aPart.clear();
    if(!m_pFigure)
        return;
    m_figureComplexion = complexion;
    const QString cacheScope = meshCacheScope(modelName);
    CPart::setCacheScope(cacheScope);
    buildCachedAssembly(m_pFigure, cacheScope, modelName, visibleParts,
                        complexion, m_aPart);
    for(CPart* part : m_aPart)
    {
        const QString partTexture = ei::usesUnlayeredHumanoidTexture(part->name())
                && !bodyTextureLayers.isEmpty() ? textureName : partTextureNames.value(part->name(),
            part->textureNumber() == 2 && !secondaryTextureName.trimmed().isEmpty()
                ? secondaryTextureName : textureName);
        float uvScale = ei::textureUvScale(partTexture);
        if(!part->vertData().isEmpty())
        {
            float minU = part->vertData().first().texCoord.x();
            float maxU = minU;
            float minV = part->vertData().first().texCoord.y();
            float maxV = minV;
            for(const SVertexData& vertex : part->vertData())
            {
                minU = qMin(minU, vertex.texCoord.x());
                maxU = qMax(maxU, vertex.texCoord.x());
                minV = qMin(minV, vertex.texCoord.y());
                maxV = qMax(maxV, vertex.texCoord.y());
            }
            uvScale = ei::textureUvScaleForBounds(partTexture, minU, maxU, minV, maxV);
        }
        part->setTextureUvScale(uvScale);
    }
    rebuildTransientAnimationSkeleton();
    recalcMinPos();
}

void CObjectBase::recalcMinPos()
{
    float min(0.0f);
    bool hasVertex = false;
    QVector3D rotatedPos;
    QMatrix4x4 rtMatrix;
    rtMatrix.setToIdentity();
    rtMatrix.rotate(m_rotation);
    for(auto& part: m_aPart)
        for(auto& vert : part->vertData())
        {
            rotatedPos = rtMatrix*vert.position; //get vector, rotated with matrix
            if(!hasVertex || rotatedPos.z() < min)
                min = rotatedPos.z();
            hasVertex = true;
        }

    // Empty or rejected assemblies must not acquire the historical +1000 Z
    // projection sentinel. Keep their serialized position stable so the
    // resolver diagnostic/placeholder remains selectable and inspectable.
    if(!hasVertex)
        min = 0.0f;
    m_minPoint = QVector3D(0.0f, 0.0f, min);
    CLandscape::getInstance()->projectPosition(this);
}


