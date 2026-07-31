#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include "unit.h"
#include "gl_dispatch.h"
#include "view.h"
#include "resourcemanager.h"
#include "landscape.h"
#include "settings.h"
#include "log.h"
#include "scene.h"
#include "property.h"
#include "profile_database.h"
#include "profile_visual_provider.h"
#include "persistent_visual_cache.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <limits>

CUnit::CUnit():
    m_prototypeName("")
    ,m_bImport(false)
{
    m_type = 50; //Human by default
    m_aLogic.clear();
    for(int i(0); i<5; ++i)
    {
        m_aLogic.append(new CLogic(this, i==0));
    }
    m_stat.reset(new SUnitStat());
}

CUnit::CUnit(const CUnit &unit):
    CWorldObj(unit)
{
    m_prototypeName = unit.m_prototypeName;
    m_prototypeNullTerminated = unit.m_prototypeNullTerminated;
    m_stat.reset(new SUnitStat(*unit.m_stat.get()));
    m_aQuestItem = unit.m_aQuestItem;
    m_aQuickItem = unit.m_aQuickItem;
    m_aSpell = unit.m_aSpell;
    m_aWeapon = unit.m_aWeapon;
    m_aArmor = unit.m_aArmor;
    m_bImport = unit.m_bImport;
    m_aLogic.clear();
    for(auto& pLogic : unit.m_aLogic)
        m_aLogic.append(new CLogic(this, *pLogic));
}

CUnit::CUnit(QJsonObject data):
    CWorldObj(data["World object"].toObject())
{
    //m_pMob = pMob;
    if(data.contains("Subtype"))
        m_type = (uint)data["Subtype"].toVariant().toUInt();
    m_prototypeName = data["Prototype name"].toString();
    m_stat.reset(new SUnitStat(data["Unit stats"].toObject()));
    const auto deSerializeStringList = [&data](QList<QString>& aList, QString name)
    {
        QJsonArray aItem = data[name].toArray();
        for(auto it=aItem.begin(); it<aItem.end(); ++it)
        {
            aList.append(it->toString());
        }
    };

    deSerializeStringList(m_aQuestItem, "Quest items");
    deSerializeStringList(m_aQuickItem, "Quick items");
    deSerializeStringList(m_aSpell, "Spells");
    deSerializeStringList(m_aWeapon, "Weapons");
    deSerializeStringList(m_aArmor, "Armors");
    m_bImport = data["Is use mob stats?"].toBool();
    QJsonArray arrLogic = data["Logics"].toArray();
    if(arrLogic.size() == 5)
    {
        for(auto it=arrLogic.begin(); it<arrLogic.end(); ++it)
        {
            CLogic* pLogic = new CLogic(this);
            pLogic->deSerializeJson(it->toObject());
            m_aLogic.append(pLogic);
        }
    }

}

CUnit::~CUnit()
{
    for(auto& logic: m_aLogic)
        delete logic;
}

void CUnit::draw(bool isActive, QOpenGLShaderProgram* program)
{
    drawObjectVisual(isActive, program);
    drawLogicVisual(isActive, program);
}

void CUnit::drawObjectVisual(bool isActive, QOpenGLShaderProgram* program)
{
    if(m_state == ENodeState::eHidden)
    {
        AnimationFrameContext::instance().counters().skippedOffscreen++;
        return;
    }
    if(!refreshTransientVisual())
    {
        CObjectBase::draw(isActive, program);
        return;
    }
    refreshAnimationPose();
    CObjectBase::draw(isActive, program);
}

void CUnit::drawLogicVisual(bool isActive, QOpenGLShaderProgram* program)
{
    if (m_aLogic.empty() || m_state == ENodeState::eHidden)
        return;

    editorLogic()->draw(isActive, program);
}

void CUnit::drawSelect(QOpenGLShaderProgram* program)
{
    if(m_state == ENodeState::eHidden)
        return;
    if(!refreshTransientVisual())
    {
        CObjectBase::drawSelect(program);
        return;
    }
    refreshAnimationPose();
    CObjectBase::drawSelect(program);
    if(CScene::getInstance()->getMode() == eEditModeLogic)
        editorLogic()->drawSelect(program);
}

void CUnit::preloadVisualResources()
{
    // Called with the viewport GL context current immediately after MOB
    // projection. Resolve the complete figure/texture/equipment plan and its
    // initial idle pose before the unit first enters the camera frustum; this
    // does not alter serialized MOB data.
    if(refreshTransientVisual(true))
        refreshAnimationPose();
}

bool CUnit::needsVisualResolution() const
{
    return m_resolvedDatabaseGeneration != ei::ProfileDatabaseContext::instance().generation()
        || m_resolvedVisualRevision != m_visualRevision;
}

bool CUnit::hasPreparedAppearance() const
{
    return m_preparedAppearanceValid
        && m_preparedAppearanceGeneration == ei::ProfileDatabaseContext::instance().generation()
        && m_preparedAppearanceRevision == m_visualRevision;
}

bool CUnit::needsAppearancePreparation() const
{
    const quint64 generation = ei::ProfileDatabaseContext::instance().generation();
    if(m_resolvedDatabaseGeneration == generation && m_resolvedVisualRevision == m_visualRevision)
        return false;
    if(m_appearancePreparationInFlight)
        return false;
    return !(m_preparedAppearanceValid
             && m_preparedAppearanceGeneration == generation
             && m_preparedAppearanceRevision == m_visualRevision);
}

bool CUnit::takeAppearancePreparationTask(SUnitAppearancePreparationTask* task)
{
    if(!task || !needsAppearancePreparation())
        return false;
    ei::ProfileDatabaseContext& context = ei::ProfileDatabaseContext::instance();
    const auto database = context.current();
    task->unitKey = reinterpret_cast<quintptr>(this);
    task->databaseGeneration = context.generation();
    task->visualRevision = m_visualRevision;
    task->providerId = context.providerForRole("characterAppearance");
    task->database = database;
    task->request.profileId = database.isNull() ? QString() : database->profileId;
    task->request.runtimeManifestId = database.isNull() ? QString() : database->runtimeManifestId;
    task->request.prototype = m_prototypeName;
    task->request.parentTemplate = m_parentTemplate;
    task->request.model = m_modelName;
    task->request.primaryTexture = m_primaryTexture;
    task->request.secondaryTexture = m_secondaryTexture;
    task->request.bodyParts = m_bodyParts;
    task->request.dataSource = m_bImport ? EUnitVisualDataSource::EmbeddedMob
                                        : EUnitVisualDataSource::ProfileDatabase;
    m_appearancePreparationInFlight = true;
    return true;
}

void CUnit::acceptAppearancePreparation(const SUnitAppearancePreparationResult& result)
{
    m_appearancePreparationInFlight = false;
    if(result.databaseGeneration != ei::ProfileDatabaseContext::instance().generation()
            || result.visualRevision != m_visualRevision)
        return;
    m_preparedAppearanceGeneration = result.databaseGeneration;
    m_preparedAppearanceRevision = result.visualRevision;
    m_preparedAppearanceResult = result.providerResult;
    m_preparedAppearance = result.appearance;
    m_preparedAppearanceDiagnostic = result.diagnostic;
    m_preparedAppearanceValid = true;
}

namespace {

quint64 s_unitCacheGeneration = std::numeric_limits<quint64>::max();
QHash<QString, SAnimationClip> s_idleClipCache;
QSet<QString> s_loggedVisualPlans;
QHash<QString, QJsonObject> s_resolvedVisualPlanCache;
constexpr quint32 kResolvedVisualPlanSchema = 1;

void prepareUnitGenerationCaches(quint64 generation)
{
    if(s_unitCacheGeneration == generation)
        return;
    s_unitCacheGeneration = generation;
    s_idleClipCache.clear();
    s_loggedVisualPlans.clear();
}

QStringList jsonStringList(const QJsonObject& object, const char* key)
{
    QStringList result;
    for(const QJsonValue& value : object.value(key).toArray())
        result.append(value.toString());
    return result;
}

bool optionalEquipmentPart(const QString& part)
{
    // base*/l.base* members are mutually-exclusive assembly variants, not the
    // avatar's equipped hand slot. Keep the ordinary lh3/rh3 anatomy (or its
    // glove replacement) and add only the resolver-selected weapon geometry.
    static const QRegularExpression optional(
        "(?:^(?:sling|quiver|arrows|basearrow\\d+|rh3\\.arrow\\d+|"
        "bwparta\\d+|lh3\\.bwpartb\\d+|bwtetiva[ab]\\d+|"
        "rh3\\.crbow\\d+main|crbow\\d+(?:part|tetiva)\\d+|"
        "baserh3|(?:l\\.)?base(?:sword|pike|club|axe|dagger|staff|"
        "dsword|dpike|dclub|daxe|dstaff)\\d+)$|"
        "\\.(?:armor|sword|axe|dagger|pike|club|bow|crossbow|shield|dsword|daxe|dpike|dclub|staff|dstaff)\\d+|hr\\.\\d+)$",
        QRegularExpression::CaseInsensitiveOption);
    return optional.match(part).hasMatch();
}

QString resolvedVisualPlanKey(const QJsonObject& identity);
bool readResolvedVisualPlan(const QString& key, QJsonObject* plan);
void writeResolvedVisualPlan(const QString& key, const QJsonObject& plan);
QMap<QString, QString> jsonStringMap(const QJsonObject& object);
QMap<QString, QVector3D> jsonVectorMap(const QJsonObject& object);

}

bool CUnit::refreshTransientVisual(bool force)
{
    ei::ProfileDatabaseContext& context = ei::ProfileDatabaseContext::instance();
    const quint64 generation = context.generation();
    prepareUnitGenerationCaches(generation);
    if(m_resolvedDatabaseGeneration == generation && m_resolvedVisualRevision == m_visualRevision)
        return true;
    if(!force && !AnimationFrameContext::instance().claimColdVisualResolution())
        return false;
    m_resolvedDatabaseGeneration = generation;
    m_resolvedVisualRevision = m_visualRevision;
    m_visualDiagnostics.clear();
    const auto database = context.current();
    static ProfileVisualProviderService providers;
    QString diagnostic;
    const auto appearanceResolver = providers.appearanceResolver(
        context.providerForRole("characterAppearance"), database, &diagnostic);
    const auto equipmentResolver = providers.equipmentResolver(
        context.providerForRole("equipmentLayout"), database, &diagnostic);
    if(appearanceResolver.isNull() || equipmentResolver.isNull())
    {
        m_visualDiagnostics.append("provider.unavailable: " + diagnostic);
        ei::log(eLogWarning, "Unit visual provider unavailable: " + diagnostic);
        applyTransientVisual("cannotdisplay", "default", {}, QVector3D(1, 1, 1));
        m_resolvedAnimationModel.clear(); m_resolvedAnimationWeapons.clear();
        return true;
    }

    SCharacterAppearanceRequest appearanceRequest;
    appearanceRequest.profileId = database.isNull() ? QString() : database->profileId;
    appearanceRequest.runtimeManifestId = database.isNull() ? QString() : database->runtimeManifestId;
    appearanceRequest.prototype = m_prototypeName;
    appearanceRequest.parentTemplate = m_parentTemplate;
    appearanceRequest.model = m_modelName;
    appearanceRequest.primaryTexture = m_primaryTexture;
    appearanceRequest.secondaryTexture = m_secondaryTexture;
    appearanceRequest.bodyParts = m_bodyParts;
    appearanceRequest.dataSource = m_bImport ? EUnitVisualDataSource::EmbeddedMob
                                             : EUnitVisualDataSource::ProfileDatabase;
    QJsonObject appearance;
    SRuntimeProviderResult appearanceResult;
    if(m_preparedAppearanceValid && m_preparedAppearanceGeneration == generation
            && m_preparedAppearanceRevision == m_visualRevision)
    {
        appearance = m_preparedAppearance;
        appearanceResult = m_preparedAppearanceResult;
        if(!m_preparedAppearanceDiagnostic.isEmpty())
            diagnostic = m_preparedAppearanceDiagnostic;
    }
    else
        appearanceResult = appearanceResolver->resolve(appearanceRequest, &appearance);
    if(!appearanceResult.success)
    {
        m_visualDiagnostics.append(appearanceResult.code + ": " + appearanceResult.message);
        ei::log(eLogWarning, QString("Unit %1 appearance placeholder [%2]: %3")
                .arg(m_prototypeName, appearanceResult.code, appearanceResult.message));
        applyTransientVisual("cannotdisplay", "default", {}, QVector3D(1, 1, 1));
        m_resolvedAnimationModel.clear(); m_resolvedAnimationWeapons.clear();
        return true;
    }

    const QString model = appearance.value("model").toString();
    const QString texture = appearance.value("primaryTexture").toString();
    const QString secondaryTexture = appearance.value("secondaryTexture").toString();
    const bool databaseEquipmentPlan = appearance.value("source").toString() == "profileDatabase";
    QJsonObject planIdentity;
    planIdentity.insert("schema", int(kResolvedVisualPlanSchema));
    planIdentity.insert("databaseCacheKey", database.isNull() ? QString() : database->cacheKey);
    planIdentity.insert("appearanceProvider", context.providerForRole("characterAppearance"));
    planIdentity.insert("equipmentProvider", context.providerForRole("equipmentLayout"));
    planIdentity.insert("resourceFingerprint",
        configuredResourceFingerprint(CObjectList::getInstance()->settings()));
    planIdentity.insert("prototype", m_prototypeName);
    planIdentity.insert("parentTemplate", m_parentTemplate);
    planIdentity.insert("model", m_modelName);
    planIdentity.insert("primaryTexture", m_primaryTexture);
    planIdentity.insert("secondaryTexture", m_secondaryTexture);
    planIdentity.insert("serializedParts", QJsonArray::fromStringList(m_bodyParts));
    planIdentity.insert("embeddedArmor", QJsonArray::fromStringList(m_aArmor));
    planIdentity.insert("embeddedWeapons", QJsonArray::fromStringList(m_aWeapon));
    planIdentity.insert("dataSource", int(appearanceRequest.dataSource));
    planIdentity.insert("appearance", appearance);
    const QString visualPlanKey = resolvedVisualPlanKey(planIdentity);
    bool cachedPlanPending = false;
    auto applyResolvedPlan = [this, &cachedPlanPending](const QJsonObject& plan,
                                                       const QVector3D& complexion) -> bool
    {
        const QString resolvedModel = plan.value("model").toString();
        const QString resolvedTexture = plan.value("primaryTexture").toString();
        if(resolvedModel.isEmpty() || resolvedTexture.isEmpty())
            return false;
        const QString resolvedSecondary = plan.value("secondaryTexture").toString();
        const QStringList resolvedParts = jsonStringList(plan, "visibleParts");
        const QStringList resolvedLayers = jsonStringList(plan, "bodyTextureLayers");
        const QStringList resolvedWeapons = jsonStringList(plan, "animationWeapons");
        const QMap<QString, QString> resolvedComponentTextures =
            jsonStringMap(plan.value("componentTextures").toObject());
        const QMap<QString, QVector3D> resolvedEmissions =
            jsonVectorMap(plan.value("partEmissions").toObject());
        if(!resolvedLayers.isEmpty())
        {
            const ei::ECachedCompositePreparation composite =
                ei::requestCachedLayerComposite(
                    configuredResourceFingerprint(CObjectList::getInstance()->settings()),
                    resolvedModel, resolvedLayers);
            if(composite == ei::ECachedCompositePreparation::Pending)
            {
                // Keep the unit unresolved while its already-cached source
                // layers are composited off the GUI thread. The idle scheduler
                // will retry and perform only the final GL upload.
                m_resolvedDatabaseGeneration = std::numeric_limits<quint64>::max();
                m_resolvedVisualRevision = std::numeric_limits<quint64>::max();
                cachedPlanPending = true;
                return false;
            }
        }
        m_visualDiagnostics = jsonStringList(plan, "diagnostics");
        m_renderedModelName = resolvedModel;
        m_renderedPrimaryTextureName = resolvedTexture;
        m_renderedSecondaryTextureName = resolvedSecondary;
        m_renderedBodyTextureLayers = resolvedLayers;
        applyTransientVisual(resolvedModel, resolvedTexture, resolvedParts, complexion,
                             resolvedComponentTextures, resolvedSecondary,
                             resolvedLayers, resolvedEmissions);
        m_resolvedAnimationModel = resolvedModel;
        m_resolvedAnimationWeapons = resolvedWeapons;
        return true;
    };
    QJsonObject cachedVisualPlan;
    if(readResolvedVisualPlan(visualPlanKey, &cachedVisualPlan))
    {
        if(applyResolvedPlan(cachedVisualPlan, m_complection))
        {
            m_preparedAppearanceValid = false;
            return true;
        }
        if(cachedPlanPending)
            return false;
    }
    const QStringList assemblyParts = CObjectList::getInstance()->figurePartNames(model);
    const QStringList resolvedBodyParts = appearance.contains("bodyParts")
        ? jsonStringList(appearance, "bodyParts") : m_bodyParts;
    QStringList visibleParts;
    // A normal unit's database-selected race can differ from its embedded MOB
    // assembly. Reusing that instance mask drops anatomical components (most
    // visibly limbs on humans, orcs, and skeletons). Build the database body
    // from the selected assembly's non-optional base members, then add exactly
    // the resolved hair/equipment components below. Imported units remain
    // strictly controlled by their embedded mask.
    if(databaseEquipmentPlan)
    {
        for(const QString& part : assemblyParts)
            if(!optionalEquipmentPart(part))
                visibleParts.append(part);
    }
    else if(!resolvedBodyParts.isEmpty())
    {
        for(const QString& part : resolvedBodyParts)
            visibleParts.append(part);
    }
    else
    {
        for(const QString& part : assemblyParts)
            visibleParts.append(part);
    }

    QVector3D complexion = m_complection;
    if(databaseEquipmentPlan)
    {
        // Constitution/complection is serialized per MOB instance and is the
        // actor's actual body shape. Monster database values are prototype
        // defaults, not a reason to overwrite an already placed unit.
        const int hair = appearance.value("hairIndex").toInt(-1);
        if(hair >= 0)
        {
            const QString hairPart = QString("hr.%1").arg(hair, 2, 10, QChar('0'));
            if(assemblyParts.contains(hairPart, Qt::CaseInsensitive))
                visibleParts.append(hairPart);
        }
    }

    SEquipmentLayoutRequest equipmentRequest;
    equipmentRequest.profileId = appearanceRequest.profileId;
    equipmentRequest.runtimeManifestId = appearanceRequest.runtimeManifestId;
    equipmentRequest.prototype = m_prototypeName;
    equipmentRequest.model = model;
    equipmentRequest.assemblyParts = assemblyParts;
    if(ei::CFigure* assemblyFigure = CObjectList::getInstance()->getFigure(model))
    {
        QMap<QString, QVector3D> ignoredRestOffsets;
        QVector3D hierarchyComplexion = m_complection;
        assemblyFigure->getAssemblyHierarchy(equipmentRequest.assemblyParents,
                                              ignoredRestOffsets, hierarchyComplexion);
    }
    equipmentRequest.dataSource = appearanceRequest.dataSource;
    equipmentRequest.armor = m_bImport ? m_aArmor
        : (databaseEquipmentPlan ? jsonStringList(appearance, "armor") : QStringList());
    equipmentRequest.weapons = m_bImport ? m_aWeapon
        : (databaseEquipmentPlan ? jsonStringList(appearance, "weapons") : QStringList());
    // Quick/quest/loot inventory is intentionally absent: inventory slots are
    // valid database references but never form part of an avatar render plan.
    QJsonObject equipment;
    const SRuntimeProviderResult equipmentResult = equipmentResolver->resolve(equipmentRequest, &equipment);
    for(const QString& replaced : jsonStringList(equipment, "replacedParts"))
        for(int index = visibleParts.size() - 1; index >= 0; --index)
            if(visibleParts.at(index).compare(replaced, Qt::CaseInsensitive) == 0)
                visibleParts.removeAt(index);
    if(equipment.value("hidesHair").toBool(false))
        for(int index = visibleParts.size() - 1; index >= 0; --index)
            if(visibleParts.at(index).startsWith("hr.", Qt::CaseInsensitive))
                visibleParts.removeAt(index);
    for(const QString& component : jsonStringList(equipment, "components"))
        if(!visibleParts.contains(component, Qt::CaseInsensitive))
            visibleParts.append(component);
    for(const QJsonValue& finding : equipment.value("diagnostics").toArray())
    {
        const QString text = finding.toString();
        m_visualDiagnostics.append("equipment: " + text);
        ei::log(eLogWarning, QString("Unit %1 equipment: %2").arg(m_prototypeName, text));
    }
    if(!equipmentResult.success && equipment.value("placeholders").toArray().isEmpty())
    {
        m_visualDiagnostics.append(equipmentResult.code + ": " + equipmentResult.message);
        ei::log(eLogWarning, QString("Unit %1 equipment [%2]: %3")
                .arg(m_prototypeName, equipmentResult.code, equipmentResult.message));
    }
    QMap<QString, QString> componentTextures;
    const QJsonObject textureObject = equipment.value("componentTextures").toObject();
    for(auto it = textureObject.constBegin(); it != textureObject.constEnd(); ++it)
        componentTextures.insert(it.key(), it.value().toString());
    if(databaseEquipmentPlan)
    {
        QStringList texturePlan;
        for(auto it = componentTextures.constBegin(); it != componentTextures.constEnd(); ++it)
            texturePlan.append(it.key() + '=' + it.value());
        const QString bodyLayers = jsonStringList(equipment, "bodyTextureLayers").join(",");
        const QString planKey = m_prototypeName.toLower() + '|' + model.toLower() + '|'
            + visibleParts.join(",").toLower() + '|' + texturePlan.join(",").toLower()
            + '|' + bodyLayers.toLower();
        if(!s_loggedVisualPlans.contains(planKey))
        {
            s_loggedVisualPlans.insert(planKey);
            ei::log(eLogInfo, QString("Unit visual plan representative id=%1 prototype=%2 model=%3 parts=%4 "
                                     "textures=%5 bodyLayers=%6 constitution=%7,%8,%9")
                    .arg(m_mapID).arg(m_prototypeName).arg(model).arg(visibleParts.join(","))
                    .arg(texturePlan.join(",")).arg(bodyLayers)
                    .arg(complexion.x()).arg(complexion.y()).arg(complexion.z()));
        }
    }
    const QStringList equipmentBodyLayers = jsonStringList(equipment, "bodyTextureLayers");
    QMap<QString, QVector3D> partEmissions;
    for(const QString& part : jsonStringList(equipment, "emissiveParts"))
        partEmissions.insert(part.toLower(), QVector3D(1.0f, 1.0f, 1.0f));
    QStringList bodyTextureLayers;
    // Imported units keep their local armor list, but the active provider may
    // interpret those explicit item names into the same read-only atlas
    // layers. Do not discard those layers merely because the list came from
    // the MOB rather than the Monster database row.
    if(!equipmentBodyLayers.isEmpty())
    {
        bodyTextureLayers.append(texture);
        bodyTextureLayers.append(equipmentBodyLayers);
    }
    m_renderedModelName = model;
    m_renderedPrimaryTextureName = texture;
    m_renderedSecondaryTextureName = secondaryTexture;
    m_renderedBodyTextureLayers = bodyTextureLayers;
    applyTransientVisual(model, texture, visibleParts, complexion, componentTextures,
                         secondaryTexture, bodyTextureLayers, partEmissions);
    m_resolvedAnimationModel = model;
    m_resolvedAnimationWeapons = equipmentRequest.weapons;
    QJsonObject cachedPlan;
    cachedPlan.insert("model", model);
    cachedPlan.insert("primaryTexture", texture);
    cachedPlan.insert("secondaryTexture", secondaryTexture);
    cachedPlan.insert("visibleParts", QJsonArray::fromStringList(visibleParts));
    cachedPlan.insert("bodyTextureLayers", QJsonArray::fromStringList(bodyTextureLayers));
    cachedPlan.insert("animationWeapons", QJsonArray::fromStringList(equipmentRequest.weapons));
    cachedPlan.insert("diagnostics", QJsonArray::fromStringList(m_visualDiagnostics));
    QJsonObject cachedTextures;
    for(auto it = componentTextures.constBegin(); it != componentTextures.constEnd(); ++it)
        cachedTextures.insert(it.key(), it.value());
    cachedPlan.insert("componentTextures", cachedTextures);
    QJsonObject cachedEmissions;
    for(auto it = partEmissions.constBegin(); it != partEmissions.constEnd(); ++it)
    {
        QJsonArray values;
        values.append(it.value().x());
        values.append(it.value().y());
        values.append(it.value().z());
        cachedEmissions.insert(it.key(), values);
    }
    cachedPlan.insert("partEmissions", cachedEmissions);
    writeResolvedVisualPlan(visualPlanKey, cachedPlan);
    m_preparedAppearanceValid = false;
    return true;
}

namespace {

QString resolvedVisualPlanKey(const QJsonObject& identity)
{
    return ei::visualCacheKey(QJsonDocument(identity).toJson(QJsonDocument::Compact));
}

bool readResolvedVisualPlan(const QString& key, QJsonObject* plan)
{
    if(!plan || key.isEmpty())
        return false;
    const auto cached = s_resolvedVisualPlanCache.constFind(key);
    if(cached != s_resolvedVisualPlanCache.constEnd())
    {
        *plan = cached.value();
        return true;
    }
    QByteArray payload;
    if(!ei::readVisualCachePayload(ei::visualCacheDirectory("character-plans"), key,
                                   kResolvedVisualPlanSchema, &payload))
        return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if(error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    *plan = document.object();
    s_resolvedVisualPlanCache.insert(key, *plan);
    return true;
}

void writeResolvedVisualPlan(const QString& key, const QJsonObject& plan)
{
    if(key.isEmpty() || plan.isEmpty())
        return;
    s_resolvedVisualPlanCache.insert(key, plan);
    ei::writeVisualCachePayload(ei::visualCacheDirectory("character-plans"), key,
                                kResolvedVisualPlanSchema,
                                QJsonDocument(plan).toJson(QJsonDocument::Compact));
}

QMap<QString, QString> jsonStringMap(const QJsonObject& object)
{
    QMap<QString, QString> result;
    for(auto it = object.constBegin(); it != object.constEnd(); ++it)
        result.insert(it.key(), it.value().toString());
    return result;
}

QMap<QString, QVector3D> jsonVectorMap(const QJsonObject& object)
{
    QMap<QString, QVector3D> result;
    for(auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        const QJsonArray values = it.value().toArray();
        if(values.size() == 3)
            result.insert(it.key(), QVector3D(float(values.at(0).toDouble()),
                                              float(values.at(1).toDouble()),
                                              float(values.at(2).toDouble())));
    }
    return result;
}

}

void CUnit::refreshAnimationPose()
{
    AnimationFrameContext& frame = AnimationFrameContext::instance();
    QMap<QString, QMatrix4x4> staticPose;
    if(!frame.enabled() || m_resolvedAnimationModel.isEmpty())
    {
        if(!m_animationPoseIsStatic)
        {
            applyTransientPartPoses(staticPose);
            applyTransientPartMorphs({});
            m_animationPoseIsStatic = true;
            m_lastAppliedAnimationFrame = -1.0;
        }
        return;
    }

    const float distanceSquared = (m_drawPosition - frame.cameraPosition()).lengthSquared();
    const float distance = distanceSquared <= 10000.0f ? 100.0f
        : (distanceSquared <= 90000.0f ? 300.0f : 301.0f);
    const EAnimationDistanceTier tier = animationDistanceTier(true,
        m_state == ENodeState::eSelect, distance);
    const auto database = ei::ProfileDatabaseContext::instance().current();
    const QString providerId = ei::ProfileDatabaseContext::instance().providerForRole("unitAnimation");
    const quint64 databaseGeneration = ei::ProfileDatabaseContext::instance().generation();
    prepareUnitGenerationCaches(databaseGeneration);
    const QString fingerprint = providerId + '|' + QString::number(databaseGeneration)
        + '|' + m_resolvedAnimationModel.toLower()
        + '|' + m_resolvedAnimationWeapons.join(';').toLower();
    if(fingerprint != m_animationFingerprint)
    {
        m_animationFingerprint = fingerprint;
        m_idleClip = SAnimationClip();
        m_lastAppliedAnimationFrame = -1.0;
        m_animationPoseIsStatic = true;
        const auto cachedClip = s_idleClipCache.constFind(fingerprint);
        if(cachedClip != s_idleClipCache.constEnd())
            m_idleClip = cachedClip.value();
        else
        {
            static ProfileVisualProviderService providers;
            QString diagnostic;
            const auto resolver = providers.animationResolver(providerId, database, &diagnostic);
            QJsonObject resolved;
            SRuntimeProviderResult result;
            if(!resolver.isNull())
            {
                SUnitAnimationRequest request;
                request.profileId = database.isNull() ? QString() : database->profileId;
                request.runtimeManifestId = database.isNull() ? QString() : database->runtimeManifestId;
                request.model = m_resolvedAnimationModel;
                request.weapons = m_resolvedAnimationWeapons;
                result = resolver->resolve(request, &resolved);
            }
            else
            { result.code = "animation.provider.unavailable"; result.message = diagnostic; }
            if(result.success)
            {
                m_idleClip = loadOrParseCachedAnimationClip(
                    CObjectList::getInstance()->modelAnimation(m_resolvedAnimationModel),
                    resolved.value("actionName").toString(), resolved.value("length").toInt());
                int morphChannels = 0;
                for(const SAnimationChannel& channel : m_idleClip.channels)
                    if(!channel.morphations.isEmpty()) ++morphChannels;
                ei::log(eLogInfo, QString("Unit animation plan prototype=%1 model=%2 action=%3 length=%4 "
                                         "morphChannels=%5")
                        .arg(m_prototypeName).arg(m_resolvedAnimationModel)
                        .arg(resolved.value("actionName").toString())
                        .arg(resolved.value("length").toInt())
                        .arg(morphChannels));
                if(m_idleClip.valid())
                    s_idleClipCache.insert(fingerprint, m_idleClip);
            }
            if(!result.success || !m_idleClip.valid())
            {
                const QString detail = !result.message.isEmpty()
                    ? result.message : m_idleClip.diagnostics.join("; ");
                ei::log(eLogWarning, QString("Unit %1 static animation fallback [%2]: %3")
                        .arg(m_prototypeName, result.code, detail));
            }
        }
    }
    if(!m_idleClip.valid())
    {
        frame.counters().staticFallbacks++;
        if(!m_animationPoseIsStatic)
        {
            applyTransientPartPoses(staticPose);
            applyTransientPartMorphs({});
            m_animationPoseIsStatic = true;
            m_lastAppliedAnimationFrame = -1.0;
        }
        return;
    }

    switch(tier)
    {
    case EAnimationDistanceTier::Full: frame.counters().fullEvaluations++; break;
    case EAnimationDistanceTier::HalfRate: frame.counters().halfRateEvaluations++; break;
    case EAnimationDistanceTier::LowRate: frame.counters().lowRateEvaluations++; break;
    case EAnimationDistanceTier::Skip: frame.counters().skippedOffscreen++; return;
    }
    const double logicalFrame = quantizedAnimationFrame(tier, frame.elapsedMilliseconds(),
                                                         m_idleClip.logicalLength, m_mapID);
    if(qAbs(logicalFrame - m_lastAppliedAnimationFrame) < 0.000001)
    {
        frame.counters().poseCacheHits++;
        return;
    }
    const SAnimationSkeleton& skeleton = transientAnimationSkeleton();
    frame.counters().poseEvaluations++;
    frame.counters().componentsEvaluated += quint64(skeleton.restOffsets.size());
    const SAnimationLocalPose local = interpolateAnimationPose(m_idleClip, logicalFrame, &skeleton);
    applyTransientPartPoses(propagateAnimationHierarchy(local, skeleton));
    applyTransientPartMorphs(local.morphDeltas);
    m_lastAppliedAnimationFrame = logicalFrame;
    m_animationPoseIsStatic = false;
}

uint CUnit::deserialize(util::CMobParser& parser)
{
    uint readByte(0);
    int logicNum(0);
    while(true)
    {
        if(parser.isNextTag("UNIT_R"))
        {
            Q_ASSERT("UNIT_R" && false);
            readByte += parser.skipHeader();
            readByte += parser.readDword(m_type);
        }
        else if(parser.isNextTag("UNIT_PROTOTYPE"))
        {
            readByte += parser.readHeader();
            readByte += parser.readString(m_prototypeName, parser.nodeLen(), &m_prototypeNullTerminated);
        }
        else if(parser.isNextTag("UNIT_ITEMS"))
        {
            Q_ASSERT("UNIT_ITEMS" && false);
        }
        else if(parser.isNextTag("UNIT_STATS"))
        {
            readByte += parser.readHeader();
            readByte += parser.readUnitStats(m_stat, parser.nodeLen());
        }
        else if(parser.isNextTag("UNIT_QUEST_ITEMS"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readStringArray(m_aQuestItem);
        }
        else if(parser.isNextTag("UNIT_QUICK_ITEMS"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readStringArray(m_aQuickItem);
        }
        else if(parser.isNextTag("UNIT_SPELLS"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readStringArray(m_aSpell);
        }
        else if(parser.isNextTag("UNIT_WEAPONS"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readStringArray(m_aWeapon);
        }
        else if(parser.isNextTag("UNIT_ARMORS"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readStringArray(m_aArmor);
        }
        else if(parser.isNextTag("UNIT_NEED_IMPORT"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readBool(m_bImport);
        }
        else if(parser.isNextTag("UNIT_LOGIC"))
        {
            readByte += parser.skipHeader();
            CLogic* pLogic = m_aLogic[logicNum];
            readByte += pLogic->deserialize(parser);
            ++logicNum;
        }
        else
        {
            uint baseByte = CWorldObj::deserialize(parser);
            if(baseByte > 0)
                readByte += baseByte;
            else
            { //debugging branch :)
                auto tag = parser.nextTag();
                Q_UNUSED(tag);
                break;
            }
        }
        //"UNIT_R", eNull};
        //"UNIT_ITEMS", eNull};
    }
    if(logicNum != 5)
        ei::log(eLogWarning, "Logic has incorrect data. Unit name:" + m_name + ", Id:" + QString::number(m_mapID) + ". Logic count: " + QString::number(logicNum));
    //Q_ASSERT(logicNum == 5);
    Q_ASSERT((m_type==50)||(m_type==51)||(m_type==52));
    return readByte;
}

void CUnit::serializeJson(QJsonObject& obj)
{
    CWorldObj::serializeJson(obj);
    obj.insert("Prototype name", m_prototypeName);
    obj.insert("Subtype", QJsonValue::fromVariant(m_type));
    //todo: m_stat
    const auto serializeStringList = [&obj](QList<QString>& aList, QString name)
    {
        QJsonArray aitem;
        for(auto& item : aList)
            aitem.append(item);
        obj.insert(name, aitem);
    };

    serializeStringList(m_aQuestItem, "Quest items");
    serializeStringList(m_aQuickItem, "Quick items");
    serializeStringList(m_aSpell, "Spells");
    serializeStringList(m_aWeapon, "Weapons");
    serializeStringList(m_aArmor, "Armors");
    obj.insert("Is import (use mob info instead database)?", m_bImport);
    QJsonArray logicArr;
    for(auto& logic: m_aLogic)
    {
        QJsonObject logicObj;
        logic->serializeJson(logicObj);
        logicArr.append(logicObj);
    }

    obj.insert("Logics", logicArr);
}

uint CUnit::serialize(util::CMobParser &parser)
{
    uint writeByte(0);
    writeByte += parser.startSection("UNIT");
//    parser.startSection("UNIT_R");
//    parser.writeDword(m_type);
//    parser.endSection(); //"UNIT_R"

    writeByte += parser.startSection("UNIT_NEED_IMPORT");
    writeByte += parser.writeBool(m_bImport);
    parser.endSection(); //"UNIT_NEED_IMPORT"

    writeByte += parser.startSection("UNIT_PROTOTYPE");
    writeByte += parser.writeString(m_prototypeName);
    if(m_prototypeNullTerminated)
        writeByte += parser.writeByte(0);
    parser.endSection();// "UNIT_PROTOTYPE"

    writeByte += parser.startSection("UNIT_ARMORS");
    writeByte += parser.writeStringArray(m_aArmor, "UNIT_ARMORS");
    parser.endSection(); //"UNIT_ARMORS"

    writeByte += parser.startSection("UNIT_WEAPONS");
    writeByte += parser.writeStringArray(m_aWeapon, "UNIT_WEAPONS");
    parser.endSection(); //"UNIT_WEAPONS"

    writeByte += parser.startSection("UNIT_SPELLS");
    writeByte += parser.writeStringArray(m_aSpell, "UNIT_SPELLS");
    parser.endSection(); //"UNIT_SPELLS"

//что-то тут не то. мапед сохраняет чуточку по-другому, сам мапед на мой файл ругается на ошибку.
    writeByte += parser.startSection("UNIT_QUICK_ITEMS");
    writeByte += parser.writeStringArray(m_aQuickItem, "UNIT_QUICK_ITEMS");
    parser.endSection(); //"UNIT_QUICK_ITEMS"

    writeByte += parser.startSection("UNIT_QUEST_ITEMS");
    writeByte += parser.writeStringArray(m_aQuestItem, "UNIT_QUEST_ITEMS");
    parser.endSection(); //"UNIT_QUEST_ITEMS"


    writeByte += parser.startSection("UNIT_STATS");
    writeByte += parser.writeUnitStats(m_stat);
    parser.endSection();// "UNIT_STATS"

    writeByte += CWorldObj::serialize(parser);

    for(const auto& logic : m_aLogic)
    {
        writeByte += logic->serialize(parser);
    }

    parser.endSection(); //UNIT
    return writeByte;
}

//CSettings* CUnit::settings()
//{
//    return m_pMob->view()->settings();
//}

void CUnit::collectParams(QList<QSharedPointer<IPropertyBase>>& aProp, ENodeType paramType)
{
    CWorldObj::collectParams(aProp, paramType);

    auto comm = paramType & eUnit;
    if (comm != eUnit)
        return;

    propBool bImport(eObjParam_UNIT_NEED_IMPORT, m_bImport);
    util::addParam(aProp, &bImport);
    propStr dbName(eObjParam_UNIT_PROTOTYPE, m_prototypeName);
    util::addParam(aProp, &dbName);
    propStrAr aArmor(eObjParam_UNIT_ARMORS, m_aArmor);
    util::addParam(aProp, &aArmor);
    propStrAr aWeapon(eObjParam_UNIT_WEAPONS, m_aWeapon);
    util::addParam(aProp, &aWeapon);
    propStrAr aSpell(eObjParam_UNIT_SPELLS, m_aSpell);
    util::addParam(aProp, &aSpell);
    propStrAr aQuickItem(eObjParam_UNIT_QUICK_ITEMS, m_aQuickItem);
    util::addParam(aProp, &aQuickItem);
    propStrAr aQuestItem(eObjParam_UNIT_QUEST_ITEMS, m_aQuestItem);
    util::addParam(aProp, &aQuestItem);
    propUnitStat stat(eObjParam_UNIT_STATS, *(m_stat.get()));
    util::addUnitStatParam(aProp, &stat);
    propUint type(eObjParam_TYPE, m_type);
    util::addParam(aProp, &type);
}

void CUnit::collectlogicParams(QList<QSharedPointer<IPropertyBase>>& aProp, ENodeType paramType)
{
    auto comm = paramType & eUnit;
    if (comm != eUnit)
        return;

    editorLogic()->collectlogicParams(aProp);
}

void CUnit::getParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType)
{
    switch (propType)
    {
    case eObjParam_UNIT_NEED_IMPORT:
    {
        prop.reset(new propBool(propType, m_bImport));
        break;
    }
    case eObjParam_TYPE:
    {
        prop.reset(new propUint(propType, m_type));
        break;
    }
    case eObjParam_UNIT_PROTOTYPE:
    {
        prop.reset(new propStr(propType, m_prototypeName));
        break;
    }
    case eObjParam_UNIT_ARMORS:
    {
        prop.reset(new propStrAr(propType, m_aArmor));
        break;
    }
    case eObjParam_UNIT_WEAPONS:
    {
        prop.reset(new propStrAr(propType, m_aWeapon));
        break;
    }
    case eObjParam_UNIT_SPELLS:
    {
        prop.reset(new propStrAr(propType, m_aSpell));
        break;
    }
    case eObjParam_UNIT_QUICK_ITEMS:
    {
        prop.reset(new propStrAr(propType, m_aQuickItem));
        break;
    }
    case eObjParam_UNIT_QUEST_ITEMS:
    {
        prop.reset(new propStrAr(propType, m_aQuestItem));
        break;
    }
    case eObjParam_UNIT_STATS:
    {
        prop.reset(new propUnitStat(propType, *m_stat.get())); //todo: check memory leaks
        break;
    }
    default:
        CWorldObj::getParam(prop, propType);
    }
}

void CUnit::applyParam(const QSharedPointer<IPropertyBase>& prop)
{
    ++m_visualRevision;
    invalidateTransientVisual();
    switch (prop->type())
    {
    case eObjParam_UNIT_NEED_IMPORT:
    {
        m_bImport = dynamic_cast<propBool*>(prop.get())->value();
        break;
    }
    case eObjParam_UNIT_PROTOTYPE:
    {
        m_prototypeName = dynamic_cast<propStr*>(prop.get())->value();
        break;
    }
    case eObjParam_UNIT_ARMORS:
    {
        m_aArmor = dynamic_cast<propStrAr*>(prop.get())->value();
        break;
    }
    case eObjParam_UNIT_WEAPONS:
    {
        m_aWeapon = dynamic_cast<propStrAr*>(prop.get())->value();
        break;
    }
    case eObjParam_UNIT_SPELLS:
    {
        m_aSpell = dynamic_cast<propStrAr*>(prop.get())->value();
        break;
    }
    case eObjParam_UNIT_QUICK_ITEMS:
    {
        m_aQuickItem = dynamic_cast<propStrAr*>(prop.get())->value();
        break;
    }
    case eObjParam_UNIT_QUEST_ITEMS:
    {
        m_aQuestItem = dynamic_cast<propStrAr*>(prop.get())->value();
        break;
    }
    case eObjParam_UNIT_STATS:
    {
        util::propListToUnitStat(*(m_stat.get()), dynamic_cast<propUnitStat*>(prop.get())->value());
        break;
    }
    default:
        CWorldObj::applyParam(prop);
    }
}

void CUnit::getLogicParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType)
{
    editorLogic()->getLogicParam(prop, propType);
    return;
}

void CUnit::applyLogicParam(const QSharedPointer<IPropertyBase>& prop)
{
    editorLogic()->applyLogicParam(prop);
}

bool CUnit::updatePos(QVector3D &pos)
{
    QVector3D offset = pos - m_position;
    CObjectBase::updatePos(pos);
    Q_ASSERT(!m_aLogic.isEmpty());
    CLogic* pLogic = editorLogic();

    if(CScene::getInstance()->getMode() == eEditModeLogic)
    {
        // logic placement can be change separately from unit position, but by default it changes sinchroniously
        QVector3D pos = m_position;
        pos.setZ(0.0f);
        CLandscape::getInstance()->projectPt(pos);
        pLogic->setGuardPlacement(pos);
        pLogic->updateLogicLines();
    }
    else
        m_aLogic.front()->updatePos(offset);

    return true;
}

QJsonObject CUnit::toJson()
{
    QJsonObject obj;
    QJsonObject world_obj = CWorldObj::toJson();
    obj.insert("World object", world_obj);
    obj.insert("Subtype", QJsonValue::fromVariant(m_type));
    obj.insert("Prototype name", m_prototypeName);
    obj.insert("Unit stats", m_stat->toJson());
    const auto serializeStringList = [&obj](QList<QString>& aList, QString name)
    {
        QJsonArray aitem;
        for(auto& item : aList)
            aitem.append(item);
        obj.insert(name, aitem);
    };

    serializeStringList(m_aQuestItem, "Quest items");
    serializeStringList(m_aQuickItem, "Quick items");
    serializeStringList(m_aSpell, "Spells");
    serializeStringList(m_aWeapon, "Weapons");
    serializeStringList(m_aArmor, "Armors");
    obj.insert("Is use mob stats?", m_bImport);
    QJsonArray logicArr;
    for(auto& logic: m_aLogic)
    {
        QJsonObject logicObj;
        logic->serializeJson(logicObj);
        logicArr.append(logicObj);
    }

    obj.insert("Logics", logicArr);
    return obj;
}

void CUnit::collectLogicNodes(QList<CNode *> &arrNode)
{
    editorLogic()->collectPatrolNodes(arrNode);
}

void CUnit::clearLogicSelect()
{
    editorLogic()->clearPatrolSelect();
}

void CUnit::clearPaths()
{
    editorLogic()->clearLogicPaths();
    addFirstPatrolPoint();
}

bool CUnit::isChild(CPatrolPoint *pPointIn)
{
    return editorLogic()->isChild(pPointIn);
}

void CUnit::addFirstPatrolPoint()
{
    QVector3D pos(m_position);
    CLandscape::getInstance()->projectPt(pos);
    editorLogic()->addFirstPoint(pos);
}

void CUnit::undo_addFirstPatrolPoint()
{
    editorLogic()->undo_addFirstPoint();
}

bool CUnit::isBehaviourPath()
{
    return editorLogic()->isBehaviourPath();
}

int CUnit::getPatrolId(CPatrolPoint *pPoint)
{
    return editorLogic()->getPatrolId(pPoint);
}

void CUnit::getViewId(int &parentPatrol, int &parentView, CLookPoint *pPoint)
{
    editorLogic()->getViewId(parentPatrol, parentView, pPoint);
}

void CUnit::createPatrolByIndex(int index)
{
    editorLogic()->createPatrolByIndex(index);
}

void CUnit::undo_createPatrolByIndex(int index)
{
    editorLogic()->undo_createPatrolByIndex(index);
}

void CUnit::createViewByIndex(int pointId, int viewId)
{
    editorLogic()->createViewByIndex(pointId, viewId);
}

void CUnit::undo_createViewByIndex(int pointId, int viewId)
{
    editorLogic()->undo_createViewByIndex(pointId, viewId);
}

CPatrolPoint *CUnit::patrolByIndex(int index)
{
    return editorLogic()->patrolByIndex(index);
}

CLogic* CUnit::editorLogic() const
{
    Q_ASSERT(!m_aLogic.isEmpty());
    return m_aLogic.at(qBound(0, m_editorLogicIndex, m_aLogic.size() - 1));
}

void CUnit::setEditorLogicIndex(int index)
{
    m_editorLogicIndex = qBound(0, index, qMax(0, m_aLogic.size() - 1));
}

void CUnit::getLogicParamAt(int index, QSharedPointer<IPropertyBase>& prop,
                            EObjParam propType)
{
    if(index >= 0 && index < m_aLogic.size())
        m_aLogic.at(index)->getLogicParam(prop, propType);
}

void CUnit::applyLogicParamAt(int index, const QSharedPointer<IPropertyBase>& prop)
{
    if(index >= 0 && index < m_aLogic.size())
        m_aLogic.at(index)->applyLogicParam(prop);
}

CPatrolPoint* CUnit::patrolByIndex(int logicIndex, int patrolIndex)
{
    if(logicIndex < 0 || logicIndex >= m_aLogic.size())
        return nullptr;
    return m_aLogic.at(logicIndex)->patrolByIndex(patrolIndex);
}

CLookPoint* CUnit::viewByIndex(int logicIndex, int patrolIndex, int viewIndex)
{
    CPatrolPoint* patrol = patrolByIndex(logicIndex, patrolIndex);
    return patrol ? patrol->viewByIndex(viewIndex) : nullptr;
}

void CUnit::createPatrolByIndex(int logicIndex, int index)
{
    if(logicIndex >= 0 && logicIndex < m_aLogic.size())
        m_aLogic.at(logicIndex)->createPatrolByIndex(index);
}

void CUnit::undo_createPatrolByIndex(int logicIndex, int index)
{
    if(logicIndex >= 0 && logicIndex < m_aLogic.size())
        m_aLogic.at(logicIndex)->undo_createPatrolByIndex(index);
}

void CUnit::createViewByIndex(int logicIndex, int pointId, int viewId)
{
    if(logicIndex >= 0 && logicIndex < m_aLogic.size())
        m_aLogic.at(logicIndex)->createViewByIndex(pointId, viewId);
}

void CUnit::undo_createViewByIndex(int logicIndex, int pointId, int viewId)
{
    if(logicIndex >= 0 && logicIndex < m_aLogic.size())
        m_aLogic.at(logicIndex)->undo_createViewByIndex(pointId, viewId);
}

void CUnit::resetLogic()
{
    //todo: realise resetting logic instead deleting
    for(auto& pLogic : m_aLogic)
        delete pLogic;

    m_aLogic.clear();
    for(int i(0); i<5; ++i)
    {
        m_aLogic.append(new CLogic(this, i==0));
    }
}

void CUnit::refreshLogicProjection()
{
    for(CLogic* logic : m_aLogic)
        logic->createLogicLines();
}

QVector<QVector3D> CUnit::movementState() const
{
    QVector<QVector3D> state;
    state.append(m_position);
    for(const CLogic* logic : m_aLogic)
        logic->appendMovementState(state);
    return state;
}

bool CUnit::restoreMovementState(const QVector<QVector3D>& state)
{
    if(state.isEmpty())
        return false;

    int index = 0;
    QVector3D unitPosition = state.at(index++);
    CObjectBase::updatePos(unitPosition); // Do not translate logic children by an inverse delta.
    for(CLogic* logic : m_aLogic)
    {
        if(!logic->restoreMovementState(state, index))
            return false;
    }
    return index == state.size();
}

CLogic::CLogic(CUnit* unit, bool bUse):
    m_indexBuf(QOpenGLBuffer::IndexBuffer)
  ,m_helpIndexBuf(QOpenGLBuffer::IndexBuffer)
    ,m_bCyclic(false)
    ,m_behaviour(eRadius)
    ,m_guardRadius(10.0f)
    ,m_numAlarm(0)
    ,m_use(bUse)
    ,m_wait(0.0f)
    ,m_alarmCondition(0)
    ,m_help(10.0f)
    ,m_alwaysActive(0)
    ,m_agressionMode(0)
    ,m_parent(unit)
{
}

CLogic::CLogic(CUnit* unit, const CLogic &logic):
    m_parent(unit)
{
    m_bCyclic = logic.m_bCyclic;
    m_behaviour = logic.m_behaviour;
    m_guardRadius = logic.m_guardRadius;
    m_guardPlacement = logic.m_guardPlacement;
    m_numAlarm = logic.m_numAlarm;
    m_use = logic.m_use;
    m_wait = logic.m_wait;
    m_alarmCondition = logic.m_alarmCondition;
    m_help = logic.m_help;
    m_alwaysActive = logic.m_alwaysActive;
    m_agressionMode = logic.m_agressionMode;
    for(auto pP: logic.m_aPatrolPt)
        m_aPatrolPt.append(new CPatrolPoint(*pP));

    m_aDrawPoint = logic.m_aDrawPoint;
    createLogicLines();
    Q_ASSERT(m_parent);
}

CLogic::~CLogic()
{
    m_vertexBuf.destroy();
    m_indexBuf.destroy();
    m_helpVertexBuf.destroy();
    m_helpIndexBuf.destroy();
    for(auto& pp: m_aPatrolPt)
        delete pp;
}


void CLogic::draw(bool isActive, QOpenGLShaderProgram* program)
{
    if(!m_use)
        return;

    drawHelp(isActive, program);
    if(m_aDrawPoint.empty())
            return;
    COptBool* pOpt = dynamic_cast<COptBool*>(CObjectList::getInstance()->settings()->opt("drawLogic"));
    if (nullptr == pOpt)
        return;

    // 1) unit draw, not selected
    // 1.1) option on? => draw logic
    // 1.2) option of? => return
    // 2) unit draw, unit selected => draw logic
    // 3) logic mode

    if(!pOpt->value())
    {
        if (CScene::getInstance()->getMode() == eEditModeLogic)
        {
            if (!isActive)
                return;
        }
        else
        {
         if (m_parent->nodeState() != ENodeState::eSelect)
             return;
        }
    }



    ei::gl::Disable(GL_DEPTH_TEST);
    //todo: draw help radius
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    program->setUniformValue("u_modelMmatrix", matrix);
    if (m_behaviour == EBehaviourType::eRadius)
        program->setUniformValue("customColor", QVector4D(0.9f, 0.4f, 0.1f, 1.0f));
    else
        program->setUniformValue("customColor", QVector4D(0.8f, 0.8f, 0.2f, 1.0f));

    int offset(0);
    // Tell OpenGL which VBOs to use
    m_vertexBuf.bind();
    // Tell OpenGL programmable pipeline how to locate vertex position data
    int vertexLocation = program->attributeLocation("a_position");
    program->enableAttributeArray(vertexLocation);
    program->setAttributeBuffer(vertexLocation, GL_FLOAT, offset, 3, int(sizeof(QVector3D)));

//    offset+=int(sizeof(QVector3D)); // size of vertex position
//    int normLocation = program->attributeLocation("a_normal");
//    program->enableAttributeArray(normLocation);
//    program->setAttributeBuffer(normLocation, GL_FLOAT, offset, 3, int(sizeof(SVertexData)));

//    offset+=int(sizeof(QVector3D)); // size of normal
//    int textureLocation = program->attributeLocation("a_texture");
//    program->enableAttributeArray(textureLocation);
//    program->setAttributeBuffer(textureLocation, GL_FLOAT, offset, 2, int(sizeof(SVertexData)));

    // Draw cube geometry using indices from VBO 1
    m_indexBuf.bind();
    ei::gl::LineWidth(2);
    bool bDottedLines = false;
    if(bDottedLines)
    {
        ei::gl::Enable(GL_LINE_STIPPLE);
        ei::gl::LineStipple(3,555);
    }

    ei::gl::DrawElements(GL_LINE_STRIP, m_aDrawPoint.count(), GL_UNSIGNED_SHORT, nullptr);
    if(bDottedLines)
        ei::gl::Disable(GL_LINE_STIPPLE);

    program->setUniformValue("customColor", QVector4D(0.0, 0.0, 0.0, 0.0));

    if(m_behaviour == EBehaviourType::ePath)
    {
        // Logic markers are drawn without depth testing. Draw selected patrol
        // groups last so a nearby later point cannot paint over the highlight.
        for(auto& pp : m_aPatrolPt)
            if(pp->nodeState() != ENodeState::eSelect && !pp->hasSelectedLookPoint())
                pp->draw(isActive, program);
        for(auto& pp : m_aPatrolPt)
            if(pp->nodeState() == ENodeState::eSelect || pp->hasSelectedLookPoint())
                pp->draw(isActive, program);
    }

    ei::gl::Enable(GL_DEPTH_TEST);
}

void CLogic::drawSelect(QOpenGLShaderProgram* program)
{
    if(!m_use)
        return;

    COptBool* pOpt = dynamic_cast<COptBool*>(CObjectList::getInstance()->settings()->opt("drawLogic"));
    if (pOpt && !pOpt->value())
        return;

    ei::gl::Disable(GL_DEPTH_TEST);
    //todo: choose guard behavior
    for(auto& pp : m_aPatrolPt)
        pp->drawSelect(program);

    ei::gl::Enable(GL_DEPTH_TEST);
}

void CLogic::createLogicLines()
{
    m_aDrawPoint.clear();
    m_aHelpPoint.clear();
    switch (m_behaviour) {
    case EBehaviourType::eRadius:
    {
        QVector3D centr(m_guardPlacement);
        centr.setZ(.0f);
        util::getCirclePoint(m_aDrawPoint, centr, double(m_guardRadius), 40);
        CLandscape::getInstance()->projectPt(m_aDrawPoint);
        generateVisibleLogicVBO();
        break;
    }
    case EBehaviourType::ePath:
    {
        Q_ASSERT(m_parent);
        QVector3D pos(m_parent->position());
        pos.setZ(0.0f);
        CLandscape::getInstance()->projectPt(pos);
        m_aDrawPoint.append(pos);
        for (auto& pt: m_aPatrolPt)
        {
            if(pt->isMarkDeleted())
                continue;

            pos = pt->position();
            pos.setZ(0.0f);
            CLandscape::getInstance()->projectPt(pos);
            pt->setDrawPosition(pos);
            m_aDrawPoint.append(pos);
            for (auto& look : pt->lookPoint())
            {
                pos = look->position();
                pos.setZ(0.0f);
                CLandscape::getInstance()->projectPt(pos);
                look->setDrawPosition(pos);
            }
            pt->update();
        }
        util::splitByLen(m_aDrawPoint, 2.0f);
        generateVisibleLogicVBO();
        break;
    }
    case EBehaviourType::ePlace:
    {
        break;
    }
    case EBehaviourType::eBriffing:
    {
        break;
    }
    case EBehaviourType::eGuardAlaram:
    {
        //Q_ASSERT("We found it, master" && false);
        ei::log(eLogDebug, "We found it, master");
        return;
    }
    default:
        //Q_ASSERT("unknown behaviour type" && false);
        break;
    }

    //get circle for help radius
    QVector3D centr(m_guardPlacement);
    centr.setZ(.0f);
    util::getCirclePoint(m_aHelpPoint, centr, double(m_help), 40);
    CLandscape::getInstance()->projectPt(m_aHelpPoint);
    // Generate VBOs for help radius
    m_helpVertexBuf.create();
    m_helpVertexBuf.bind();
    m_helpVertexBuf.allocate(m_aHelpPoint.data(), m_aHelpPoint.count() * int(sizeof(QVector3D)));
    m_helpVertexBuf.release();

    QVector<ushort> aInd;
    for (ushort i(0); i<m_aHelpPoint.size(); ++i)
        aInd.append(i);

    m_helpIndexBuf.create();
    m_helpIndexBuf.bind();
    m_helpIndexBuf.allocate(aInd.data(), aInd.count() * int(sizeof(ushort)));
    m_helpIndexBuf.release();
}

void CLogic::updateLogicLines()
{
    m_aDrawPoint.clear();
    m_aHelpPoint.clear();
    switch (m_behaviour) {
    case EBehaviourType::eRadius:
    {
        QVector3D centr(m_guardPlacement);
        centr.setZ(.0f);
        util::getCirclePoint(m_aDrawPoint, centr, double(m_guardRadius), 40);
        CLandscape::getInstance()->projectPt(m_aDrawPoint);
        generateVisibleLogicVBO();
        break;
    }
    case EBehaviourType::ePath:
    {
        Q_ASSERT(m_parent);
        QVector3D pos(m_parent->position());
        pos.setZ(0.0f);
        CLandscape::getInstance()->projectPt(pos);
        m_aDrawPoint.append(pos);
        for (auto& pt: m_aPatrolPt)
        {
            if(pt->isMarkDeleted())
                continue;

            pos = pt->position();
            m_aDrawPoint.append(pos);
        }
        util::splitByLen(m_aDrawPoint, 2.0f);
        generateVisibleLogicVBO();
        break;
    }
    case EBehaviourType::ePlace:
    {
        //todo: draw custom symbol? star may be
        break; // no need to update draw elements. just exit func
    }
    case EBehaviourType::eBriffing:
    {
        //todo: draw custom symbol for briffing? face may be
        break;
    }
    case EBehaviourType::eIdle:
    case EBehaviourType::eGuardAlaram:
    {
        //idk what does is mean
        //Q_ASSERT("We found it, master" && false);
        ei::log(eLogDebug, "We found it, master");
        break;
    }
    default:
        Q_ASSERT("unknown behaviour type" && false);
        break;
    }

    //get circle for help radius
    QVector3D centr(m_guardPlacement);
    centr.setZ(.0f);
    util::getCirclePoint(m_aHelpPoint, centr, double(m_help), 40);
    CLandscape::getInstance()->projectPt(m_aHelpPoint);
    // Generate VBOs for help radius
    m_helpVertexBuf.create();
    m_helpVertexBuf.bind();
    m_helpVertexBuf.allocate(m_aHelpPoint.data(), m_aHelpPoint.count() * int(sizeof(QVector3D)));
    m_helpVertexBuf.release();

    QVector<ushort> aInd;
    for (ushort i(0); i<m_aHelpPoint.size(); ++i)
        aInd.append(i);

    m_helpIndexBuf.create();
    m_helpIndexBuf.bind();
    m_helpIndexBuf.allocate(aInd.data(), aInd.count() * int(sizeof(ushort)));
    m_helpIndexBuf.release();
}

void CLogic::recalcPatrolPath()
{
    Q_ASSERT(m_behaviour == EBehaviourType::ePath);
    Q_ASSERT(m_parent);
    m_aDrawPoint.clear();

    QVector3D pos(m_parent->position());
    pos.setZ(0.0f);
    CLandscape::getInstance()->projectPt(pos);
    m_aDrawPoint.append(pos);
    for (auto& pt: m_aPatrolPt)
    {
        if(pt->isMarkDeleted()) continue;
        pos = pt->position();
        pos.setZ(0.0f);
        CLandscape::getInstance()->projectPt(pos);
        pt->setPos(pos);
        pt->setDrawPosition(pos);
        m_aDrawPoint.append(pos);
        pt->update();
    }
    util::splitByLen(m_aDrawPoint, 2.0f);

    // Generate VBOs and transfer data
    m_vertexBuf.create();
    m_vertexBuf.bind();
    m_vertexBuf.allocate(m_aDrawPoint.data(), m_aDrawPoint.count() * int(sizeof(QVector3D)));
    m_vertexBuf.release();

    QVector<ushort> aInd;
    for (ushort i(0); i<m_aDrawPoint.size(); ++i)
        aInd.append(i);

    m_indexBuf.create();
    m_indexBuf.bind();
    m_indexBuf.allocate(aInd.data(), aInd.count() * int(sizeof(ushort)));
    m_indexBuf.release();
}

void CLogic::addNewPatrolPoint(CPatrolPoint *base, CPatrolPoint *created)
{
    int i = m_aPatrolPt.indexOf(base);
    m_aPatrolPt.insert(i+1, created);
    QObject::connect(created, SIGNAL(patrolChanges()), this, SLOT(createLogicLines()));
    QObject::connect(created, SIGNAL(addNewPatrolPoint(CPatrolPoint*,CPatrolPoint*)), this, SLOT(addNewPatrolPoint(CPatrolPoint*,CPatrolPoint*)));
    QObject::connect(created, SIGNAL(undo_addNewPatrolPoint(CPatrolPoint*)), this, SLOT(undo_addNewPatrolPoint(CPatrolPoint*)));
    createLogicLines();
}

void CLogic::undo_addNewPatrolPoint(CPatrolPoint *pCreated)
{
    int index = m_aPatrolPt.indexOf(pCreated);
    m_aPatrolPt.removeAt(index);
    delete pCreated;
    createLogicLines();
}

void CLogic::generateVisibleLogicVBO()
{
    // Generate VBOs and transfer data
    m_vertexBuf.create();
    m_vertexBuf.bind();
    m_vertexBuf.allocate(m_aDrawPoint.data(), m_aDrawPoint.count() * int(sizeof(QVector3D)));
    m_vertexBuf.release();

    QVector<ushort> aInd;
    for (ushort i(0); i<m_aDrawPoint.size(); ++i)
        aInd.append(i);

    m_indexBuf.create();
    m_indexBuf.bind();
    m_indexBuf.allocate(aInd.data(), aInd.count() * int(sizeof(ushort)));
    m_indexBuf.release();
}

void CLogic::drawHelp(bool isActive, QOpenGLShaderProgram *program)
{
    if(!m_use || m_aHelpPoint.empty())
        return;

    if(!isActive)
        return;
    //todo: for selected objects ALWAYS draw logic
    COptBool* pOpt = dynamic_cast<COptBool*>(CObjectList::getInstance()->settings()->opt("drawHelp"));
    if(nullptr == pOpt)
        return;

    if(CScene::getInstance()->getMode() == eEditModeObjects)
        return; //dont draw in object mode

    if (!pOpt->value() && m_parent->nodeState() != ENodeState::eSelect)
        return;

    ei::gl::Disable(GL_DEPTH_TEST);
    //todo: draw help radius
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    program->setUniformValue("u_modelMmatrix", matrix);
    program->setUniformValue("customColor", QVector4D(0.8f, 0.0f, 1.0f, 1.0f));

    int offset(0);
    // Tell OpenGL which VBOs to use
    m_helpVertexBuf.bind();
    // Tell OpenGL programmable pipeline how to locate vertex position data
    int vertexLocation = program->attributeLocation("a_position");
    program->enableAttributeArray(vertexLocation);
    program->setAttributeBuffer(vertexLocation, GL_FLOAT, offset, 3, int(sizeof(QVector3D)));

    m_helpIndexBuf.bind();
    ei::gl::LineWidth(2);
    ei::gl::DrawElements(GL_LINE_STRIP, m_aHelpPoint.count(), GL_UNSIGNED_SHORT, nullptr);
    program->setUniformValue("customColor", QVector4D(0.0, 0.0, 0.0, 0.0));

    ei::gl::Enable(GL_DEPTH_TEST);
}

void CLogic::updatePos(QVector3D& offset)
{
    m_guardPlacement += offset;
    for(auto& patrol : m_aPatrolPt)
    {
        for(auto& look : patrol->lookPoint())
            look->position() += offset;

        patrol->position() += offset;
    }
    createLogicLines();
}

void CLogic::appendMovementState(QVector<QVector3D>& state) const
{
    state.append(m_guardPlacement);
    for(CPatrolPoint* patrol : m_aPatrolPt)
    {
        state.append(patrol->position());
        for(CLookPoint* look : patrol->lookPoint())
            state.append(look->position());
    }
}

bool CLogic::restoreMovementState(const QVector<QVector3D>& state, int& index)
{
    if(index >= state.size())
        return false;
    m_guardPlacement = state.at(index++);
    for(CPatrolPoint* patrol : m_aPatrolPt)
    {
        if(index >= state.size())
            return false;
        QVector3D patrolPosition = state.at(index++);
        patrol->setPos(patrolPosition);
        for(CLookPoint* look : patrol->lookPoint())
        {
            if(index >= state.size())
                return false;
            QVector3D lookPosition = state.at(index++);
            look->setPos(lookPosition);
        }
    }
    createLogicLines();
    return true;
}

void CLogic::collectPatrolNodes(QList<CNode *> &arrNode)
{
    CPatrolPoint* pPoint = nullptr;
    foreach(pPoint, m_aPatrolPt)
    {
        arrNode.append(pPoint);
        pPoint->collectLookNodes(arrNode);
    }
}

void CLogic::clearPatrolSelect()
{
    for(auto& pPoint : m_aPatrolPt)
    {
        pPoint->setState(ENodeState::eDraw);
        pPoint->clearLookSelect();
    }
}

void CLogic::clearLogicPaths()
{
    m_aPatrolPt.clear();
}

void CLogic::collectlogicParams(QList<QSharedPointer<IPropertyBase>>& aProp)
{
    propBool enabled(eObjParam_LOGIC_USE, m_use != 0);
    util::addParam(aProp, &enabled);
    propUint beh(eObjParam_LOGIC_BEHAVIOUR, m_behaviour);
    util::addParam(aProp, &beh);
    propFloat help(eObjParam_GUARD_ALARM, m_help);
    util::addParam(aProp, &help);
    //if behaviour is radius
    propFloat radius(eObjParam_GUARD_RADIUS, m_guardRadius);
    util::addParam(aProp, &radius);
    propFloat placeX(eObjParam_GUARD_PLACE_X, m_guardPlacement.x());
    util::addParam(aProp, &placeX);
    propFloat placeY(eObjParam_GUARD_PLACE_Y, m_guardPlacement.y());
    util::addParam(aProp, &placeY);
    propFloat placeZ(eObjParam_GUARD_PLACE_Z, m_guardPlacement.z());
    util::addParam(aProp, &placeZ);
    propBool bAlwaysActive(eObjParam_ALWAYS_ACTIVE, m_alwaysActive);
    util::addParam(aProp, &bAlwaysActive);
    propChar agrMode(eObjParam_AGRESSION_MODE, m_agressionMode);
    util::addParam(aProp, &agrMode);
    propChar alarmCondition(eObjParam_LOGIC_ALARM_CONDITION, m_alarmCondition);
    util::addParam(aProp, &alarmCondition);
    propBool cyclic(eObjParam_LOGIC_CYCLIC, m_bCyclic);
    util::addParam(aProp, &cyclic);
    propFloat wait(eObjParam_LOGIC_WAIT, m_wait);
    util::addParam(aProp, &wait);
    propChar alarmNumber(eObjParam_LOGIC_ALARM_NUMBER, m_numAlarm);
    util::addParam(aProp, &alarmNumber);
}

void CLogic::getLogicParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType)
{
    switch (propType){
    case eObjParam_LOGIC_USE:
        prop.reset(new propBool(propType, m_use != 0));
        break;
    case eObjParam_LOGIC_BEHAVIOUR:
    {
        prop.reset(new propUint(propType, m_behaviour));
        break;
    }
    case eObjParam_GUARD_ALARM:
    {
        prop.reset(new propFloat(propType, m_help));
        break;
    }
    case eObjParam_GUARD_RADIUS:
    {
        prop.reset(new propFloat(propType, m_guardRadius));
        break;
    }
    case eObjParam_GUARD_PLACE:
    {
        prop.reset(new prop3D(propType, m_guardPlacement));
        break;
    }
    case eObjParam_ALWAYS_ACTIVE:
    {
        prop.reset(new propBool(propType, m_alwaysActive));
        break;
    }
    case eObjParam_AGRESSION_MODE:
    {
        prop.reset(new propChar(propType, m_agressionMode));
        break;
    }
    case eObjParam_LOGIC_ALARM_CONDITION:
        prop.reset(new propChar(propType, m_alarmCondition));
        break;
    case eObjParam_LOGIC_CYCLIC:
        prop.reset(new propBool(propType, m_bCyclic));
        break;
    case eObjParam_LOGIC_WAIT:
        prop.reset(new propFloat(propType, m_wait));
        break;
    case eObjParam_LOGIC_ALARM_NUMBER:
        prop.reset(new propChar(propType, m_numAlarm));
        break;
    default:
        Q_ASSERT(false);
    }

    return;
}

void CLogic::applyLogicParam(const QSharedPointer<IPropertyBase>& prop)
{
    switch (prop->type()){
    case eObjParam_LOGIC_USE:
        m_use = dynamic_cast<const propBool*>(prop.get())->value() ? 1 : 0;
        break;
    case eObjParam_LOGIC_BEHAVIOUR:
    {
        m_behaviour = (EBehaviourType)dynamic_cast<const propUint*>(prop.get())->value();
        createLogicLines();
        break;
    }
    case eObjParam_GUARD_ALARM:
    {
        m_help = dynamic_cast<const propFloat*>(prop.get())->value();
        createLogicLines();
        break;
    }
    case eObjParam_GUARD_RADIUS:
    {
        m_guardRadius = dynamic_cast<const propFloat*>(prop.get())->value();
        createLogicLines();
        break;
    }
    case eObjParam_GUARD_PLACE:
    {
        m_guardPlacement = dynamic_cast<const prop3D*>(prop.get())->value();
        break;
    }
    case eObjParam_ALWAYS_ACTIVE:
    {
        m_alwaysActive = dynamic_cast<const propBool*>(prop.get())->value();
        break;
    }
    case eObjParam_AGRESSION_MODE:
    {
        m_agressionMode = dynamic_cast<const propChar*>(prop.get())->value();
        break;
    }
    case eObjParam_LOGIC_ALARM_CONDITION:
        m_alarmCondition = dynamic_cast<const propChar*>(prop.get())->value();
        break;
    case eObjParam_LOGIC_CYCLIC:
        m_bCyclic = dynamic_cast<const propBool*>(prop.get())->value();
        createLogicLines();
        break;
    case eObjParam_LOGIC_WAIT:
        m_wait = dynamic_cast<const propFloat*>(prop.get())->value();
        break;
    case eObjParam_LOGIC_ALARM_NUMBER:
        m_numAlarm = dynamic_cast<const propChar*>(prop.get())->value();
        break;
    default:
        Q_ASSERT(false);
    }
}

bool CLogic::isChild(CPatrolPoint *pPointIn)
{
    CPatrolPoint* pPoint = nullptr;
    foreach(pPoint, m_aPatrolPt)
    {
        if(pPoint == pPointIn)
            return true;
    }
    return false;
}

bool CLogic::owns(const CPatrolPoint* point) const
{
    return m_aPatrolPt.contains(const_cast<CPatrolPoint*>(point));
}

bool CLogic::owns(const CLookPoint* point, int* patrolIndex, int* lookIndex) const
{
    for(int patrol = 0; patrol < m_aPatrolPt.size(); ++patrol)
    {
        const int look = m_aPatrolPt.at(patrol)->lookPoint().indexOf(
            const_cast<CLookPoint*>(point));
        if(look < 0)
            continue;
        if(patrolIndex) *patrolIndex = patrol;
        if(lookIndex) *lookIndex = look;
        return true;
    }
    return false;
}

void CLogic::addFirstPoint(QVector3D& pos)
{
    CPatrolPoint* pPoint = new CPatrolPoint();
    pPoint->updatePos(pos);
    m_aPatrolPt.push_front(pPoint);
    pPoint->setState(ENodeState::eSelect);
    QObject::connect(pPoint, SIGNAL(patrolChanges()), this, SLOT(createLogicLines()));
    QObject::connect(pPoint, SIGNAL(addNewPatrolPoint(CPatrolPoint*,CPatrolPoint*)), this, SLOT(addNewPatrolPoint(CPatrolPoint*,CPatrolPoint*)));
    QObject::connect(pPoint, SIGNAL(undo_addNewPatrolPoint(CPatrolPoint*)), this, SLOT(undo_addNewPatrolPoint(CPatrolPoint*)));
    createLogicLines();
}

void CLogic::undo_addFirstPoint()
{
    CPatrolPoint* pPoint = m_aPatrolPt.front();
    m_aPatrolPt.pop_front();
    delete pPoint;
}

int CLogic::getPatrolId(CPatrolPoint *pPoint)
{
    return m_aPatrolPt.indexOf(pPoint);
}

void CLogic::getViewId(int &parentPatrol, int &parentView, CLookPoint *pPoint)
{
   parentPatrol = -1;
   parentView = -1;
   CPatrolPoint* point = nullptr;
   foreach(point, m_aPatrolPt)
   {
       int index = point->getViewId(pPoint);
       if(index >= 0)
       {
           parentView = index;
           parentPatrol = m_aPatrolPt.indexOf(point);
           break;
       }
   }
}

void CLogic::createPatrolByIndex(int index)
{
    CPatrolPoint* pPoint = new CPatrolPoint();
    if(index >= 0)
    {
        QVector3D pos = m_aPatrolPt[index]->position();
        pPoint->updatePos(pos);
    }
    else
    {
        pPoint->updatePos(m_guardPlacement);
    }
    m_aPatrolPt.insert(index+1, pPoint); //check first and final points
    pPoint->setState(ENodeState::eSelect);
    QObject::connect(pPoint, SIGNAL(patrolChanges()), this, SLOT(recalcPatrolPath()));
    //QObject::connect(pPoint, SIGNAL(addNewPatrolPoint(CPatrolPoint*,CPatrolPoint*)), this, SLOT(addNewPatrolPoint(CPatrolPoint*,CPatrolPoint*)));
    //QObject::connect(pPoint, SIGNAL(undo_addNewPatrolPoint(CPatrolPoint*)), this, SLOT(undo_addNewPatrolPoint(CPatrolPoint*)));
    createLogicLines();
}

void CLogic::undo_createPatrolByIndex(int index)
{
    CPatrolPoint* pPoint = m_aPatrolPt.at(index+1);
    m_aPatrolPt.remove(index+1);
    delete pPoint;
    createLogicLines();
}

void CLogic::createViewByIndex(int pointId, int viewId)
{
    auto pPoint = m_aPatrolPt.at(pointId);
    pPoint->createViewByIndex(viewId);
}

void CLogic::undo_createViewByIndex(int pointId, int viewId)
{
    auto pPoint = m_aPatrolPt.at(pointId);
    pPoint->undo_createViewByIndex(viewId);
}

CPatrolPoint *CLogic::patrolByIndex(int index)
{
    Q_ASSERT(index < m_aPatrolPt.size());
    return m_aPatrolPt[index];
}

uint CLogic::deserialize(util::CMobParser& parser)
{
    uint readByte(0);
    while(true)
    {
        if(parser.isNextTag("UNIT_LOGIC_CYCLIC"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readBool(m_bCyclic);
        }
        else if(parser.isNextTag("UNIT_LOGIC_MODEL"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readDword(m_behaviour);
        }
        else if(parser.isNextTag("UNIT_LOGIC_GUARD_R"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readFloat(m_guardRadius);
        }
        else if(parser.isNextTag("UNIT_LOGIC_GUARD_PT"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readPlot(m_guardPlacement);
        }
        else if(parser.isNextTag("UNIT_LOGIC_NALARM"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readByte(m_numAlarm);
        }
        else if(parser.isNextTag("UNIT_LOGIC_USE"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readByte(m_use);
        }
        else if(parser.isNextTag("UNIT_LOGIC_WAIT"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readFloat(m_wait);
        }
        else if(parser.isNextTag("UNIT_LOGIC_ALARM_CONDITION"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readByte(m_alarmCondition);
        }
        else if(parser.isNextTag("UNIT_LOGIC_HELP"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readFloat(m_help);
        }
        else if(parser.isNextTag("UNIT_LOGIC_ALWAYS_ACTIVE"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readByte(m_alwaysActive);
        }
        else if(parser.isNextTag("UNIT_LOGIC_AGRESSION_MODE"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readByte(m_agressionMode);
        }
        else if(parser.isNextTag("GUARD_PT"))
        {
            readByte += parser.skipHeader();
            CPatrolPoint* place = new CPatrolPoint(); //need unit parent?
            //place->attachMob(m_parent->mob());
            QObject::connect(place, SIGNAL(patrolChanges()), this, SLOT(recalcPatrolPath()));
            QObject::connect(place, SIGNAL(addNewPatrolPoint(CPatrolPoint*,CPatrolPoint*)), this, SLOT(addNewPatrolPoint(CPatrolPoint*,CPatrolPoint*)));
            QObject::connect(place, SIGNAL(undo_addNewPatrolPoint(CPatrolPoint*)), this, SLOT(undo_addNewPatrolPoint(CPatrolPoint*)));
            readByte += place->deserialize(parser);

            m_aPatrolPt.append(place);
        }
        else
        {
            auto a = parser.nextTag();
            break;
        }
    }
    createLogicLines();
    return readByte;
}

void CLogic::serializeJson(QJsonObject& obj)
{
    obj.insert("Is cyclyc?", m_bCyclic);
    obj.insert("Model", QJsonValue::fromVariant(m_behaviour));
    obj.insert("Guard raidus", QJsonValue::fromVariant(m_guardRadius));

    QJsonArray aPlacement;
    aPlacement.append(QJsonValue::fromVariant(m_guardPlacement.x()));
    aPlacement.append(QJsonValue::fromVariant(m_guardPlacement.y()));
    aPlacement.append(QJsonValue::fromVariant(m_guardPlacement.z()));

    obj.insert("Guard placement", aPlacement);
    obj.insert("Alarm number", m_numAlarm);
    obj.insert("Is logic use?", m_use);
    obj.insert("Wait", QJsonValue::fromVariant(m_wait));
    obj.insert("Alarm condition", m_alarmCondition);
    obj.insert("Help radius", QJsonValue::fromVariant(m_help));
    obj.insert("Always active", m_alwaysActive);
    obj.insert("Aggression mode", m_agressionMode);

    QJsonArray ppArr;
    for(auto& pp: m_aPatrolPt)
    {
        QJsonObject ppObj;
        pp->serializeJson(ppObj);
        ppArr.append(ppObj);
    }
    obj.insert("Patrol Points", ppArr);
}

void CLogic::deSerializeJson(QJsonObject data)
{
    m_bCyclic = data["Is cyclyc?"].toBool();
    m_behaviour = (EBehaviourType)data["Model"].toVariant().toUInt();
    m_guardRadius = data["Guard raidus"].toVariant().toFloat();

    QJsonArray aPlacement = data["Guard placement"].toArray();
    if(aPlacement.size()==3)
    m_guardPlacement = QVector3D(aPlacement[0].toVariant().toFloat(), aPlacement[1].toVariant().toFloat(), aPlacement[2].toVariant().toFloat());

    m_numAlarm = (char)data["Alarm number"].toInt();
    m_use = (char)data["Is logic use?"].toInt();
    m_wait = data["Wait"].toVariant().toFloat();
    m_alarmCondition = (char)data["Alarm condition"].toInt();
    m_help = data["Help radius"].toVariant().toFloat();
    m_alwaysActive = (char)data["Always active"].toInt();
    m_agressionMode = (char)data["Aggression mode"].toInt();

    QJsonArray arrPP = data["Patrol Points"].toArray();
    for(auto it=arrPP.begin(); it<arrPP.end(); ++it)
    {
        CPatrolPoint* place = new CPatrolPoint(); //need unit parent?
        place->deSerializeJson(it->toObject());
        m_aPatrolPt.append(place);
        QObject::connect(place, SIGNAL(patrolChanges()), this, SLOT(recalcPatrolPath()));
        QObject::connect(place, SIGNAL(addNewPatrolPoint(CPatrolPoint*,CPatrolPoint*)), this, SLOT(addNewPatrolPoint(CPatrolPoint*,CPatrolPoint*)));
        QObject::connect(place, SIGNAL(undo_addNewPatrolPoint(CPatrolPoint*)), this, SLOT(undo_addNewPatrolPoint(CPatrolPoint*)));
    }
    createLogicLines();
}

uint CLogic::serialize(util::CMobParser& parser)
{
    uint writeByte(0);
    writeByte += parser.startSection("UNIT_LOGIC");

    writeByte += parser.startSection("UNIT_LOGIC_ALARM_CONDITION");
    writeByte += parser.writeByte(m_alarmCondition);
    parser.endSection(); //"UNIT_LOGIC_ALARM_CONDITION"

    writeByte += parser.startSection("UNIT_LOGIC_HELP");
    writeByte += parser.writeFloat(m_help);
    parser.endSection(); //"UNIT_LOGIC_HELP"

    writeByte += parser.startSection("UNIT_LOGIC_CYCLIC");
    writeByte += parser.writeBool(m_bCyclic);
    parser.endSection(); //"UNIT_LOGIC_CYCLIC"

    writeByte += parser.startSection("UNIT_LOGIC_AGRESSION_MODE");
    writeByte += parser.writeByte(m_agressionMode);
    parser.endSection();

    writeByte += parser.startSection("UNIT_LOGIC_ALWAYS_ACTIVE");
    writeByte += parser.writeByte(m_alwaysActive);
    parser.endSection(); //"UNIT_LOGIC_ALWAYS_ACTIVE"

    writeByte += parser.startSection("UNIT_LOGIC_MODEL");
    writeByte += parser.writeDword(m_behaviour);
    parser.endSection(); //parser.skipHeader();

    writeByte += parser.startSection("UNIT_LOGIC_GUARD_R");
    writeByte += parser.writeFloat(m_guardRadius);
    parser.endSection(); //"UNIT_LOGIC_GUARD_R"

    writeByte += parser.startSection("UNIT_LOGIC_WAIT");
    writeByte += parser.writeFloat(m_wait);
    parser.endSection(); //"UNIT_LOGIC_WAIT"

    writeByte += parser.startSection("UNIT_LOGIC_GUARD_PT");
    writeByte += parser.writePlot(m_guardPlacement);
    parser.endSection(); //"UNIT_LOGIC_GUARD_PT"

    writeByte += parser.startSection("UNIT_LOGIC_USE");
    writeByte += parser.writeByte(m_use);
    parser.endSection(); //"UNIT_LOGIC_USE"

    writeByte += parser.startSection("UNIT_LOGIC_NALARM");
    writeByte += parser.writeByte(m_numAlarm);
    parser.endSection(); //"UNIT_LOGIC_NALARM"

    CPatrolPoint* pPoint = nullptr;
    foreach(pPoint, m_aPatrolPt)
    {
        if(pPoint->isMarkDeleted())
            continue;

        writeByte += pPoint->serialize(parser);
    }
    parser.endSection(); //"UNIT_LOGIC"
    return writeByte;
}

CPatrolPoint::CPatrolPoint():
    m_indexBuf(QOpenGLBuffer::IndexBuffer)
{
    CObjectBase::updateFigure(CObjectList::getInstance()->getFigure("point"));
    CObjectBase::setTexture(CTextureList::getInstance()->texture("point"));
}

CPatrolPoint::CPatrolPoint(const CPatrolPoint &patrol):
    CObjectBase(patrol)
    ,m_indexBuf(QOpenGLBuffer::IndexBuffer)
{
    //m_vertexBuf = patrol.m_vertexBuf;
    //m_indexBuf = patrol.m_indexBuf;
    CLookPoint* pPoint = nullptr;
    foreach(pPoint, patrol.m_aLookPt)
        m_aLookPt.append(new CLookPoint(*pPoint));

    m_aDrawingLine = patrol.m_aDrawingLine;
    update();
}


CPatrolPoint::~CPatrolPoint()
{
    m_vertexBuf.destroy();
    m_indexBuf.destroy();
    for(auto& lp : m_aLookPt)
        delete lp;
}

void CPatrolPoint::draw(bool isActive, QOpenGLShaderProgram* program)
{
    if(isMarkDeleted())
        return;

    CObjectBase::draw(isActive, program);

    if(m_aDrawingLine.size() == 0) // do not draw looking point if absent
        return;

    QMatrix4x4 matrix;
    matrix.setToIdentity();
    program->setUniformValue("u_modelMmatrix", matrix);

    program->setUniformValue("customColor", QVector4D(0.12f, 0.4f, 0.8f, 1.0));
    int offset(0);
    // Tell OpenGL which VBOs to use

    m_vertexBuf.bind();
    // Tell OpenGL programmable pipeline how to locate vertex position data
    int vertexLocation = program->attributeLocation("a_position");
    program->enableAttributeArray(vertexLocation);
    program->setAttributeBuffer(vertexLocation, GL_FLOAT, offset, 3, int(sizeof(QVector3D)));

//    offset+=int(sizeof(QVector3D)); // size of vertex position
//    int normLocation = program->attributeLocation("a_normal");
//    program->enableAttributeArray(normLocation);
//    program->setAttributeBuffer(normLocation, GL_FLOAT, offset, 3, int(sizeof(SVertexData)));

//    offset+=int(sizeof(QVector3D)); // size of normal
//    int textureLocation = program->attributeLocation("a_texture");
//    program->enableAttributeArray(textureLocation);
//    program->setAttributeBuffer(textureLocation, GL_FLOAT, offset, 2, int(sizeof(SVertexData)));

    // Draw cube geometry using indices from VBO 1
    m_indexBuf.bind();
    ei::gl::DrawElements(GL_LINES, (m_aDrawingLine.count()-1)*2, GL_UNSIGNED_SHORT, nullptr);
    program->setUniformValue("customColor", QVector4D(0.0, 0.0, 0.0, 0.0));
    for(auto& look: m_aLookPt)
        if(look->nodeState() != ENodeState::eSelect)
            look->draw(isActive, program);
    for(auto& look: m_aLookPt)
        if(look->nodeState() == ENodeState::eSelect)
            look->draw(isActive, program);

    // The point itself may overlap a look point or another large point marker.
    // Repaint a selected point last so its highlight is always visible.
    if(nodeState() == ENodeState::eSelect)
        CObjectBase::draw(isActive, program);
}

bool CPatrolPoint::hasSelectedLookPoint() const
{
    for(const CLookPoint* look : m_aLookPt)
        if(look && look->nodeState() == ENodeState::eSelect)
            return true;
    return false;
}

void CPatrolPoint::drawSelect(QOpenGLShaderProgram* program)
{
    if(isMarkDeleted())
        return;

    CObjectBase::drawSelect(program);
    for(auto& look: m_aLookPt)
    {
        look->drawSelect(program);
    }
}


void CPatrolPoint::update()
{
    m_aDrawingLine.clear();
    if(m_aLookPt.empty())
        return;

//    QVector3D pos(position());
//    pos.setZ(0.0f);
    m_aDrawingLine.append(drawPosition()); // terrain-projected display position
    for(auto& lp: m_aLookPt)
    {
        if(lp->isMarkDeleted()) continue;
        m_aDrawingLine.append(lp->drawPosition());
//        pos=lp->position();
//        pos.setZ(0.0f);
//        m_aDrawingLine.append(pos);   //drawing position?!
    }
//    m_pMob->view()->land()->projectPt(m_aDrawingLine);

    // Generate VBOs and transfer data
    m_vertexBuf.create();
    m_vertexBuf.bind();
    m_vertexBuf.allocate(m_aDrawingLine.data(), m_aDrawingLine.count() * int(sizeof(QVector3D)));
    m_vertexBuf.release();

    QVector<ushort> aInd;
    for(int i(0); i<m_aLookPt.size(); ++i)
    {
        aInd.append(0);
        aInd.append(ushort(i+1));
    }

    m_indexBuf.create();
    m_indexBuf.bind();
    m_indexBuf.allocate(aInd.data(), aInd.count() * int(sizeof(ushort)));
    m_indexBuf.release();
}

void CPatrolPoint::addNewLookPoint(CLookPoint *pBase, CLookPoint *pCreated)
{
    int i = m_aLookPt.indexOf(pBase);
    m_aLookPt.insert(i+1, pCreated);
    QObject::connect(pCreated, SIGNAL(lookPointChanges()), this, SLOT(update()));
    QObject::connect(pCreated, SIGNAL(addNewLookPoint(CLookPoint*,CLookPoint*)), this, SLOT(addNewLookPoint(CLookPoint*,CLookPoint*)));
    QObject::connect(pCreated, SIGNAL(undo_addNewLookPoint(CLookPoint*)), this, SLOT(undo_addNewLookPoint(CLookPoint*)));
    update();
}

void CPatrolPoint::undo_addNewLookPoint(CLookPoint *pCreated)
{
    int index = m_aLookPt.indexOf(pCreated);
    m_aLookPt.removeAt(index);
    delete pCreated;
    update();
}

uint CPatrolPoint::deserialize(util::CMobParser& parser)
{
    uint readByte(0);
    while(true)
    {
        if(parser.isNextTag("GUARD_PT_POSITION"))
        {
            QVector3D pos;
            readByte += parser.skipHeader();
            readByte += parser.readPlot(pos);
            setPos(pos);
            setDrawPosition(pos);
        }
        else if(parser.isNextTag("GUARD_PT_ACTION"))
        {
            Q_ASSERT("GUARD_PT_ACTION" && false);
        }
        else if(parser.isNextTag("ACTION_PT"))
        {
            readByte += parser.skipHeader();
            CLookPoint* pLook = new CLookPoint();
            readByte += pLook->deserialize(parser);
            QObject::connect(pLook, SIGNAL(lookPointChanges()), this, SLOT(update()));
            QObject::connect(pLook, SIGNAL(addNewLookPoint(CLookPoint*,CLookPoint*)), this, SLOT(addNewLookPoint(CLookPoint*,CLookPoint*)));
            QObject::connect(pLook, SIGNAL(undo_addNewLookPoint(CLookPoint*)), this, SLOT(undo_addNewLookPoint(CLookPoint*)));

            m_aLookPt.append(pLook);
        }
        else
        {
            auto a = parser.nextTag();
            break;
        }
    }
    return readByte;
}

void CPatrolPoint::getLogicParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType)
{
    switch (propType){
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
    default:
        Q_ASSERT(false);
    }
    return;
}

void CPatrolPoint::applyLogicParam(const QSharedPointer<IPropertyBase>& prop)
{
    switch (prop->type()){
    case eObjParam_POSITION:
    {
        //m_position = util::vec3FromString(value);
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
        Q_ASSERT(false);
    }
    emit patrolChanges();
}

void CPatrolPoint::collectlogicParams(QList<QSharedPointer<IPropertyBase>>& aProp, ENodeType paramType)
{
    //CObjectBase::collectParams(aParam, paramType);
    auto comm = paramType & ePatrolPoint;
    if (comm != ePatrolPoint)
        return;

    propFloat posX(eObjParam_POSITION_X, m_position.x());
    util::addParam(aProp, &posX);
    propFloat posY(eObjParam_POSITION_Y, m_position.y());
    util::addParam(aProp, &posY);
    propFloat posZ(eObjParam_POSITION_Z, m_position.z());
    util::addParam(aProp, &posZ);
}

void CPatrolPoint::serializeJson(QJsonObject &obj)
{
    QJsonArray lpArr;
    for(auto& lp: m_aLookPt)
    {
        if(lp->isMarkDeleted())
            continue;

        QJsonObject lpObj;
        lp->serializeJson(lpObj);
        lpArr.append(lpObj);
    }
    QJsonArray posArr;
    posArr.append(QJsonValue::fromVariant(position().x()));
    posArr.append(QJsonValue::fromVariant(position().y()));
    posArr.append(QJsonValue::fromVariant(position().z()));
    obj.insert("Position", posArr);
    obj.insert("Looking points", lpArr);
}

void CPatrolPoint::deSerializeJson(QJsonObject data)
{
    QJsonArray arrPos = data["Position"].toArray();
    if(arrPos.size()==3)
    {
        QVector3D pos(arrPos[0].toVariant().toFloat(), arrPos[1].toVariant().toFloat(), arrPos[2].toVariant().toFloat());
        setPos(pos);
        setDrawPosition(pos);
    }
    QJsonArray arrLP = data["Looking points"].toArray();
    for(auto it=arrLP.begin(); it<arrLP.end(); ++it)
    {
        CLookPoint* pLook = new CLookPoint();
        pLook->deSerializeJson(it->toObject());
        QObject::connect(pLook, SIGNAL(lookPointChanges()), this, SLOT(update()));
        QObject::connect(pLook, SIGNAL(addNewLookPoint(CLookPoint*,CLookPoint*)), this, SLOT(addNewLookPoint(CLookPoint*,CLookPoint*)));
        QObject::connect(pLook, SIGNAL(undo_addNewLookPoint(CLookPoint*)), this, SLOT(undo_addNewLookPoint(CLookPoint*)));
        m_aLookPt.append(pLook);
    }
}

uint CPatrolPoint::serialize(util::CMobParser &parser)
{
    uint writeByte(0);
    writeByte += parser.startSection("GUARD_PT");

    writeByte += parser.startSection("GUARD_PT_POSITION");
    writeByte += parser.writePlot(position());
    parser.endSection(); //"GUARD_PT_POSITION
    for(const auto& lookP : m_aLookPt)
    {
        if(lookP->isMarkDeleted())
            continue;

        writeByte += lookP->serialize(parser);
    }

    parser.endSection(); //"GUARD_PT"
    return writeByte;
}

void CPatrolPoint::collectLookNodes(QList<CNode *> &arrNode)
{
    CLookPoint* pLook = nullptr;
    foreach(pLook, m_aLookPt)
    {
        arrNode.append(pLook);
    }
}

void CPatrolPoint::clearLookSelect()
{
    for(auto pLook : m_aLookPt)
        pLook->setState(ENodeState::eDraw);
}

bool CPatrolPoint::updatePos(QVector3D &pos)
{
    bool res = CObjectBase::updatePos(pos);
    emit patrolChanges();
    return res;
}

void CPatrolPoint::markAsDeleted(bool bDeleted)
{
    CObjectBase::markAsDeleted(bDeleted);
    setState(ENodeState::eDraw); // clear select for undo-redo
    emit patrolChanges();
}

bool CPatrolPoint::isOperationAxisAllow(EOperationAxisType type)
{
    return type == EOperationAxisType::eMove;
}

CPatrolPoint* CPatrolPoint::createNewPoint()
{
    CPatrolPoint* pPoint = new CPatrolPoint;
    pPoint->updatePos(m_position);
    emit addNewPatrolPoint(this, pPoint);
    return pPoint;
}

void CPatrolPoint::undo_createNewPoint(CPatrolPoint *pCreatedPoint)
{
    emit undo_addNewPatrolPoint(pCreatedPoint);
}

void CPatrolPoint::addFirstViewPoint()
{
    CLookPoint* pPoint = new CLookPoint();
    pPoint->updatePos(m_position);
    m_aLookPt.push_front(pPoint);
    pPoint->setState(ENodeState::eSelect);
    QObject::connect(pPoint, SIGNAL(lookPointChanges()), this, SLOT(update()));
    QObject::connect(pPoint, SIGNAL(addNewLookPoint(CLookPoint*,CLookPoint*)), this, SLOT(addNewLookPoint(CLookPoint*,CLookPoint*)));
    QObject::connect(pPoint, SIGNAL(undo_addNewLookPoint(CLookPoint*)), this, SLOT(undo_addNewLookPoint(CLookPoint*)));
    update();
}

void CPatrolPoint::undo_addFirstViewPoint()
{
    CLookPoint* pPoint = m_aLookPt.front();
    m_aLookPt.pop_front();
    delete pPoint;
}

int CPatrolPoint::getViewId(CLookPoint *pPoint)
{
    return m_aLookPt.indexOf(pPoint);
}

void CPatrolPoint::createViewByIndex(int index)
{
    CLookPoint* pPoint = index >= 0
            ? new CLookPoint(*m_aLookPt.at(index))
            : new CLookPoint();
    if(index >= 0)
    {
        QVector3D pos = m_aLookPt[index]->position();
        pPoint->updatePos(pos);
    }
    else
    {
        pPoint->updatePos(m_position);
    }
    m_aLookPt.insert(index+1, pPoint); //check first and final points
    pPoint->setState(ENodeState::eSelect);
    QObject::connect(pPoint, SIGNAL(lookPointChanges()), this, SLOT(update()));
    update();
}

void CPatrolPoint::undo_createViewByIndex(int index)
{
    auto pLook = m_aLookPt.at(index+1);
    m_aLookPt.remove(index+1);
    delete pLook;
    update();
}

CLookPoint *CPatrolPoint::viewByIndex(int index)
{
    Q_ASSERT(index < m_aLookPt.size());
    return m_aLookPt[index];
}

CLookPoint::CLookPoint():
    m_wait(0)
  ,m_turnSpeed(0)
  ,m_flag(0)
{
    CObjectBase::updateFigure(CObjectList::getInstance()->getFigure("view"));
    CObjectBase::setTexture(CTextureList::getInstance()->texture("view"));
}

CLookPoint::CLookPoint(const CLookPoint &look):
    CObjectBase(look)
{
    m_wait = look.m_wait;
    m_turnSpeed = look.m_turnSpeed;
    m_flag = look.m_flag;
}

void CLookPoint::getLogicParam(QSharedPointer<IPropertyBase>& prop, EObjParam propType)
{
    switch (propType){
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
    case eObjParam_VIEW_WAIT:
    {
        prop.reset(new propInt(propType, m_wait));
        break;
    }
    case eObjParam_VIEW_TURN_SPEED:
    {
        prop.reset(new propUint(propType, m_turnSpeed));
        break;
    }
    case eObjParam_VIEW_FLAGS:
        prop.reset(new propChar(propType, m_flag));
        break;
    default:
        Q_ASSERT(false);
    }
    return;
}

void CLookPoint::applyLogicParam(const QSharedPointer<IPropertyBase>& prop)
{
    switch (prop->type()){
    case eObjParam_POSITION:
    {
        QVector3D pos = dynamic_cast<const prop3D*>(prop.get())->value();
        updatePos(pos);
        break;
    }
    case eObjParam_POSITION_X:
    {
        auto pos = m_position;
        pos.setX(dynamic_cast<const propFloat*>(prop.get())->value());
        updatePos(pos);
        break;
    }
    case eObjParam_POSITION_Y:
    {
        auto pos = m_position;
        pos.setY(dynamic_cast<const propFloat*>(prop.get())->value());
        updatePos(pos);
        break;
    }
    case eObjParam_POSITION_Z:
    {
        auto pos = m_position;
        pos.setZ(dynamic_cast<const propFloat*>(prop.get())->value());
        updatePos(pos);
        break;
    }
    case eObjParam_VIEW_WAIT:
    {
        m_wait = dynamic_cast<const propInt*>(prop.get())->value();
        break;
    }
    case eObjParam_VIEW_TURN_SPEED:
    {
        m_turnSpeed = dynamic_cast<const propUint*>(prop.get())->value();
        break;
    }
    case eObjParam_VIEW_FLAGS:
        m_flag = dynamic_cast<const propChar*>(prop.get())->value();
        break;
    default:
        Q_ASSERT(false);
    }
    emit lookPointChanges();
}

void CLookPoint::collectlogicParams(QList<QSharedPointer<IPropertyBase>>& aProp, ENodeType paramType)
{
    //CObjectBase::collectParams(aParam, paramType);
    auto comm = paramType & eLookPoint;
    if (comm != eLookPoint)
        return;

    propFloat posX(eObjParam_POSITION_X, m_position.x());
    util::addParam(aProp, &posX);
    propFloat posY(eObjParam_POSITION_Y, m_position.y());
    util::addParam(aProp, &posY);
    propFloat posZ(eObjParam_POSITION_Z, m_position.z());
    util::addParam(aProp, &posZ);
    propInt wait(eObjParam_VIEW_WAIT, m_wait);
    util::addParam(aProp, &wait); //TODO: calc to seconds
    propUint speed(eObjParam_VIEW_TURN_SPEED, m_turnSpeed);
    util::addParam(aProp, &speed);
    propChar flags(eObjParam_VIEW_FLAGS, m_flag);
    util::addParam(aProp, &flags);
}

uint CLookPoint::deserialize(util::CMobParser& parser)
{
    uint readByte(0);
    while(true)
    {
        if(parser.isNextTag("ACTION_PT_LOOK_PT"))
        {
            QVector3D pos;
            readByte += parser.skipHeader();
            readByte += parser.readPlot(pos);
            setPos(pos);
            setDrawPosition(pos);
        }
        else if(parser.isNextTag("ACTION_PT_WAIT_SEG"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readDword(m_wait);
        }
        else if(parser.isNextTag("ACTION_PT_TURN_SPEED"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readDword(m_turnSpeed);
        }
        else if(parser.isNextTag("ACTION_PT_FLAGS"))
        {
            readByte += parser.skipHeader();
            readByte += parser.readByte(m_flag);
        }
        else
        {
            auto a = parser.nextTag();
            break;
        }
    }
    return readByte;

}

void CLookPoint::serializeJson(QJsonObject &obj)
{
    obj.insert("Wait", QJsonValue::fromVariant(m_wait));
    obj.insert("Turn speed", QJsonValue::fromVariant(m_turnSpeed));
    obj.insert("Point flag", m_flag);
    QJsonArray posArr;
    posArr.append(QJsonValue::fromVariant(position().x()));
    posArr.append(QJsonValue::fromVariant(position().y()));
    posArr.append(QJsonValue::fromVariant(position().z()));
    obj.insert("Position", posArr);
}

void CLookPoint::deSerializeJson(QJsonObject data)
{
    m_wait = data["Wait"].toVariant().toInt();
    m_turnSpeed = data["Turn speed"].toVariant().toUInt();
    m_flag = (char)data["Point flag"].toInt();
    QJsonArray arrPos = data["Position"].toArray();
    if(arrPos.size()==3)
    {
        QVector3D pos(arrPos[0].toVariant().toFloat(), arrPos[1].toVariant().toFloat(), arrPos[2].toVariant().toFloat());
        setPos(pos);
        setDrawPosition(pos);
    }
}

uint CLookPoint::serialize(util::CMobParser &parser)
{
    uint writeByte(0);
    writeByte += parser.startSection("ACTION_PT");
    writeByte += parser.startSection("ACTION_PT_LOOK_PT");
    writeByte += parser.writePlot(position());
    parser.endSection(); //"ACTION_PT_LOOK_PT"

    writeByte += parser.startSection("ACTION_PT_WAIT_SEG");
    writeByte += parser.writeDword(m_wait);
    parser.endSection(); //"ACTION_PT_WAIT_SEG"

    writeByte += parser.startSection("ACTION_PT_TURN_SPEED");
    writeByte += parser.writeDword(m_turnSpeed);
    parser.endSection(); //"ACTION_PT_TURN_SPEED"

    writeByte += parser.startSection("ACTION_PT_FLAGS");
    writeByte += parser.writeByte(m_flag);
    parser.endSection(); //"ACTION_PT_FLAGS"

    parser.endSection(); //"ACTION_PT"
    return writeByte;
}

bool CLookPoint::updatePos(QVector3D &pos)
{
    bool res = CObjectBase::updatePos(pos);
    emit lookPointChanges();
    return res;
}

void CLookPoint::markAsDeleted(bool bDeleted)
{
    CObjectBase::markAsDeleted(bDeleted);
    setState(ENodeState::eDraw); // clear select for undo-redo
    emit lookPointChanges();
}

bool CLookPoint::isOperationAxisAllow(EOperationAxisType type)
{
    return type == EOperationAxisType::eMove;
}

CLookPoint *CLookPoint::createLookPoint()
{
    CLookPoint* pPoint = new CLookPoint(*this);
    pPoint->updatePos(m_position);
    emit addNewLookPoint(this, pPoint);
    return pPoint;
}

void CLookPoint::undo_createLookPoint(CLookPoint *pCreatedPoint)
{
    emit undo_addNewLookPoint(pCreatedPoint);
}
