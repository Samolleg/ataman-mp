#include "objects\object_base.h"
#include <QJsonArray>
#include <QCache>
#include <QHash>
#include <QSet>
#include <QString>
#include <QElapsedTimer>
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
#include "persistent_visual_cache.h"

#include <climits>
#include <algorithm>
#include <limits>

namespace {
ei::SVisualAssemblyCacheTelemetry s_visualAssemblyTelemetry;

struct CachedFigureAssembly
{
    ~CachedFigureAssembly()
    {
        qDeleteAll(parts);
    }
    QVector<CPart*> parts;
};

enum class EAssemblyCacheClass
{
    GeneralObject,
    CharacterVisual
};

QCache<QString, CachedFigureAssembly>& transientAssemblyCache(EAssemblyCacheClass cacheClass)
{
    static QCache<QString, CachedFigureAssembly> generalObjects;
    static QCache<QString, CachedFigureAssembly> characterVisuals;
    static bool initialized = false;
    if(!initialized)
    {
        // Static map geometry is loaded during the foreground MOB pass. Keep
        // it independent from completed humanoid geometry so thousands of
        // scenery objects cannot evict a recently resolved character set just
        // before the user returns to that map.
        generalObjects.setMaxCost(384 * 1024);
        characterVisuals.setMaxCost(768 * 1024);
        initialized = true;
    }
    return cacheClass == EAssemblyCacheClass::CharacterVisual
        ? characterVisuals : generalObjects;
}

QString meshCacheScope(const QString& modelName)
{
    CSettings* settings = CObjectList::getInstance()->settings();
    const COptString* profile = settings
        ? dynamic_cast<const COptString*>(settings->opt("resourceProfile")) : nullptr;
    return (profile ? profile->value() : QString()) + '|'
        + configuredResourceFingerprint(settings) + '|' + modelName.toLower();
}

QHash<QString, float>& transientPartUvScaleCache()
{
    static QHash<QString, float> cache;
    return cache;
}

QHash<QString, float>& transientProjectionMinZCache()
{
    static QHash<QString, float> cache;
    return cache;
}

void cloneAssembly(QVector<CPart*>& destination, const QVector<CPart*>& source)
{
    QElapsedTimer timer;
    timer.start();
    destination.reserve(source.size());
    for(CPart* part : source)
        destination.append(new CPart(*part));
    s_visualAssemblyTelemetry.cloneMs += timer.elapsed();
}

void buildCachedAssembly(ei::CFigure* figure, const QString& scope,
                         const QString& modelName, const QStringList& visibleParts,
                         const QVector3D& complexion, QVector<CPart*>& destination,
                         EAssemblyCacheClass cacheClass)
{
    const QString key = scope + '|' + modelName.toLower() + '|'
        + visibleParts.join(';').toLower() + '|'
        + QString::number(complexion.x(), 'g', 8) + ','
        + QString::number(complexion.y(), 'g', 8) + ','
        + QString::number(complexion.z(), 'g', 8);
    QCache<QString, CachedFigureAssembly>& cache = transientAssemblyCache(cacheClass);
    CachedFigureAssembly* cached = cache.object(key);
    if(!cached)
    {
        cached = new CachedFigureAssembly;
        ++s_visualAssemblyTelemetry.builds;
        QElapsedTimer buildTimer;
        buildTimer.start();
        QStringList parts = visibleParts;
        QVector3D renderComplexion = complexion;
        figure->getVertexData(cached->parts, renderComplexion, parts);
        s_visualAssemblyTelemetry.buildMs += buildTimer.elapsed();
        quint64 retainedBytes = 0;
        for(CPart* part : cached->parts)
            retainedBytes += quint64(part->vertData().size()) * sizeof(SVertexData);
        const int costKiB = qMax(1, int(qMin<quint64>(retainedBytes / 1024,
                                                      quint64(INT_MAX))));
        cloneAssembly(destination, cached->parts);
        cache.insert(key, cached, costKiB);
        return;
    }
    ++s_visualAssemblyTelemetry.memoryHits;
    cloneAssembly(destination, cached->parts);
}

float navigationCross(const QVector2D& origin, const QVector2D& a, const QVector2D& b)
{
    return (a.x() - origin.x()) * (b.y() - origin.y())
        - (a.y() - origin.y()) * (b.x() - origin.x());
}

QVector<QVector2D> navigationConvexHull(QVector<QVector2D> points)
{
    std::sort(points.begin(), points.end(), [](const QVector2D& left, const QVector2D& right) {
        return left.x() < right.x()
            || (qFuzzyCompare(left.x(), right.x()) && left.y() < right.y());
    });
    QVector<QVector2D> unique;
    for(const QVector2D& point : points)
        if(unique.isEmpty() || (point - unique.back()).lengthSquared() > 1.0e-8f)
            unique.append(point);
    if(unique.size() <= 2)
        return unique;
    QVector<QVector2D> hull;
    for(const QVector2D& point : unique)
    {
        while(hull.size() >= 2
              && navigationCross(hull.at(hull.size() - 2), hull.back(), point) <= 1.0e-6f)
            hull.removeLast();
        hull.append(point);
    }
    const int lowerSize = hull.size();
    for(int index = unique.size() - 2; index >= 0; --index)
    {
        const QVector2D point = unique.at(index);
        while(hull.size() > lowerSize
              && navigationCross(hull.at(hull.size() - 2), hull.back(), point) <= 1.0e-6f)
            hull.removeLast();
        hull.append(point);
    }
    hull.removeLast();
    return hull;
}

ENavigationVolumeKind navigationKindForPart(const QString& name)
{
    const QString upper = name.toUpper();
    if(upper.startsWith(QStringLiteral("BASE")))
        return ENavigationVolumeKind::Bridge;
    if(upper.startsWith(QStringLiteral("CROWN")))
        return ENavigationVolumeKind::Easy;
    if(upper.startsWith(QStringLiteral("EMPTY")))
        return ENavigationVolumeKind::Ignored;
    return ENavigationVolumeKind::Blocking;
}
}

namespace ei {

QString editorHelperFigureIdentity(const QString& modelName, const CFigure* figure)
{
    const QString normalized = modelName.trimmed().toLower();
    if(!normalized.isEmpty())
        return normalized;
    return QStringLiteral("editor-helper@%1").arg(quintptr(figure), 0, 16);
}

void resetVisualAssemblyCacheTelemetry()
{
    s_visualAssemblyTelemetry = SVisualAssemblyCacheTelemetry();
}

SVisualAssemblyCacheTelemetry visualAssemblyCacheTelemetry()
{
    return s_visualAssemblyTelemetry;
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
    m_partEmissions = base.m_partEmissions;
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
        // A null texture is an expected transient state while profile visuals
        // resolve in the background, and some editor-only helper objects never
        // own a texture. Resource lookup already logs actionable missing names
        // and model/candidate context; do not emit the same context-free warning
        // once per object per rendered frame.
        return;
    }

    QMatrix4x4 matrix;
    matrix.setToIdentity();
    matrix.translate(m_drawPosition);
    QMatrix4x4 rtMatrix;
    rtMatrix.setToIdentity();
    rtMatrix.rotate(m_rotation);
    matrix = matrix * rtMatrix;
    matrix.scale(editorVisualScale());

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
                         m_state == ENodeState::eSelect,
                         castsRenderShadow(), nodeType() == ENodeType::eUnit,
                         m_partEmissions.value(part->name().toLower()));
            continue;
        }
        program->setUniformValue("u_materialEmission",
                                 m_partEmissions.value(part->name().toLower()));
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
        program->setUniformValue("u_materialEmission", QVector3D());
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
        matrix.scale(editorVisualScale());
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

QVector<SNavigationCollisionProxy> CObjectBase::navigationCollisionProxies() const
{
    QVector<SNavigationCollisionProxy> result;
    const float scale = editorVisualScale();
    for(const CPart* part : m_aPart)
    {
        if(!part || !part->isDrawable())
            continue;

        QVector3D localMinimum;
        QVector3D localMaximum;
        bool authored = part->hasAuthoredNavigationBounds();
        if(authored)
        {
            localMinimum = part->authoredNavigationMinimum();
            localMaximum = part->authoredNavigationMaximum();
        }
        else
        {
            localMinimum = QVector3D(std::numeric_limits<float>::max(),
                                     std::numeric_limits<float>::max(),
                                     std::numeric_limits<float>::max());
            localMaximum = -localMinimum;
            for(const SVertexData& vertex : part->vertData())
            {
                localMinimum.setX(qMin(localMinimum.x(), vertex.position.x()));
                localMinimum.setY(qMin(localMinimum.y(), vertex.position.y()));
                localMinimum.setZ(qMin(localMinimum.z(), vertex.position.z()));
                localMaximum.setX(qMax(localMaximum.x(), vertex.position.x()));
                localMaximum.setY(qMax(localMaximum.y(), vertex.position.y()));
                localMaximum.setZ(qMax(localMaximum.z(), vertex.position.z()));
            }
        }

        QVector<QVector2D> projected;
        QVector<QVector3D> boxCorners;
        boxCorners.reserve(8);
        float minimumZ = std::numeric_limits<float>::max();
        float maximumZ = -std::numeric_limits<float>::max();
        for(int z = 0; z < 2; ++z)
            for(int y = 0; y < 2; ++y)
                for(int x = 0; x < 2; ++x)
                {
                    QVector3D local(x ? localMaximum.x() : localMinimum.x(),
                                    y ? localMaximum.y() : localMinimum.y(),
                                    z ? localMaximum.z() : localMinimum.z());
                    local = part->poseMatrix().map(local * scale);
                    const QVector3D world = m_rotation.rotatedVector(local) + m_drawPosition;
                    projected.append(world.toVector2D());
                    boxCorners.append(world);
                    minimumZ = qMin(minimumZ, world.z());
                    maximumZ = qMax(maximumZ, world.z());
                }
        SNavigationCollisionProxy proxy;
        proxy.footprint = navigationConvexHull(projected);
        proxy.boxCorners = boxCorners;
        proxy.minimumZ = minimumZ;
        proxy.maximumZ = maximumZ;
        proxy.kind = navigationKindForPart(part->name());
        proxy.authored = authored;
        proxy.objectId = m_mapID;
        proxy.partName = part->name();
        if(proxy.footprint.size() >= 3 && proxy.maximumZ > proxy.minimumZ)
            result.append(proxy);
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
    const float visualScale = editorVisualScale();
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
            const QVector3D rotatedPos = rtMatrix*(arrVert[i].position * visualScale);
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
    // Editor-only helpers intentionally have no serialized model name. They
    // still need distinct cache identities; otherwise the first helper built
    // supplies its geometry to every later helper.
    const QString figureIdentity = ei::editorHelperFigureIdentity(m_modelName, m_pFigure);
    const QString cacheScope = meshCacheScope(figureIdentity);
    CPart::setCacheScope(cacheScope);
    // Static world objects used to repeat constitution interpolation and
    // triangle generation for every instance. Reuse the exact immutable CPU
    // assembly, while each object still receives its own lightweight CPart
    // wrappers for visibility, pose and picking state.
    buildCachedAssembly(m_pFigure, cacheScope, figureIdentity, m_bodyParts,
                        m_complection, m_aPart, EAssemblyCacheClass::GeneralObject);
    rebuildTransientAnimationSkeleton();
    recalcMinPos();
}

void CObjectBase::applyTransientVisual(const QString& modelName, const QString& textureName,
                                       const QStringList& visibleParts, const QVector3D& complexion,
                                       const QMap<QString, QString>& partTextureNames,
                                       const QString& secondaryTextureName,
                                       const QStringList& bodyTextureLayers,
                                       const QMap<QString, QVector3D>& partEmissions)
{
    QString textureFingerprint;
    for(auto it = partTextureNames.constBegin(); it != partTextureNames.constEnd(); ++it)
        textureFingerprint += it.key().toLower() + '=' + it.value().toLower() + ';';
    QString fingerprint = modelName.toLower() + '|' + textureName.toLower() + '|'
        + secondaryTextureName.toLower() + '|'
        + bodyTextureLayers.join(';').toLower() + '|'
        + visibleParts.join(';').toLower() + '|' + QString::number(complexion.x(), 'g', 8)
        + ',' + QString::number(complexion.y(), 'g', 8) + ',' + QString::number(complexion.z(), 'g', 8)
        + '|' + textureFingerprint;
    QString emissionFingerprint;
    for(auto it = partEmissions.constBegin(); it != partEmissions.constEnd(); ++it)
        emissionFingerprint += it.key().toLower() + '='
            + QString::number(it.value().x(), 'g', 8) + ','
            + QString::number(it.value().y(), 'g', 8) + ','
            + QString::number(it.value().z(), 'g', 8) + ';';
    fingerprint += '|' + emissionFingerprint;
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
    m_partEmissions.clear();
    for(auto it = partTextureNames.constBegin(); it != partTextureNames.constEnd(); ++it)
        m_partTextures.insert(it.key().toLower(),
            CTextureList::getInstance()->texture(it.value(), modelName));
    for(auto it = partEmissions.constBegin(); it != partEmissions.constEnd(); ++it)
        m_partEmissions.insert(it.key().toLower(), it.value());
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
                        complexion, m_aPart, EAssemblyCacheClass::CharacterVisual);
    for(CPart* part : m_aPart)
    {
        const QString partTexture = ei::usesUnlayeredHumanoidTexture(part->name())
                && !bodyTextureLayers.isEmpty() ? textureName : partTextureNames.value(part->name(),
            part->textureNumber() == 2 && !secondaryTextureName.trimmed().isEmpty()
                ? secondaryTextureName : textureName);
        const QString uvCacheKey = QString::number(part->meshIdentity()) + '|'
            + partTexture.toLower();
        auto cachedUvScale = transientPartUvScaleCache().constFind(uvCacheKey);
        float uvScale = cachedUvScale == transientPartUvScaleCache().constEnd()
            ? ei::textureUvScale(partTexture) : cachedUvScale.value();
        if(cachedUvScale == transientPartUvScaleCache().constEnd()
                && !part->vertData().isEmpty())
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
        if(cachedUvScale == transientPartUvScaleCache().constEnd())
            transientPartUvScaleCache().insert(uvCacheKey, uvScale);
        part->setTextureUvScale(uvScale);
    }
    rebuildTransientAnimationSkeleton();
    const QString projectionKey = cacheScope + '|' + fingerprint + '|'
        + QString::number(m_rotation.scalar(), 'g', 8) + ','
        + QString::number(m_rotation.x(), 'g', 8) + ','
        + QString::number(m_rotation.y(), 'g', 8) + ','
        + QString::number(m_rotation.z(), 'g', 8) + '|'
        + QString::number(editorVisualScale(), 'g', 8);
    const auto cachedMinZ = transientProjectionMinZCache().constFind(projectionKey);
    if(cachedMinZ != transientProjectionMinZCache().constEnd())
    {
        m_minPoint = QVector3D(0.0f, 0.0f, cachedMinZ.value());
        CLandscape::getInstance()->projectPosition(this);
    }
    else
    {
        recalcMinPos();
        transientProjectionMinZCache().insert(projectionKey, m_minPoint.z());
    }
}

void CObjectBase::recalcMinPos()
{
    float min(0.0f);
    bool hasVertex = false;
    QVector3D rotatedPos;
    QMatrix4x4 rtMatrix;
    rtMatrix.setToIdentity();
    rtMatrix.rotate(m_rotation);
    const float visualScale = editorVisualScale();
    for(auto& part: m_aPart)
        for(auto& vert : part->vertData())
        {
            rotatedPos = rtMatrix*(vert.position * visualScale); //get vector, rotated with matrix
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


