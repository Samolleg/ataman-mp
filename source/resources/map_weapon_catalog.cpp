#include "map_weapon_catalog.h"
#include "res_archive_index.h"
#include "resourcemanager.h"
#include "worldobj.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace ei {
QString MapWeaponCatalog::pathForDatabase(const QString& databasePath)
{
    return databasePath.isEmpty() ? QString() : QFileInfo(databasePath).dir().filePath("mapweapons-catalog.json");
}

bool MapWeaponCatalog::load(const QString& path, const ItemSpellConstructorCatalog& database)
{
    m_models.clear(); m_archive.clear(); m_diagnostic.clear();
    m_present = !path.isEmpty() && QFileInfo::exists(path);
    if(!m_present) return false;
    const auto fail = [&](const QString& message) { m_diagnostic = message; m_models.clear(); return false; };
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        return fail(QString::fromUtf8("Не удалось прочитать каталог предметов: ") + path);
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    const QJsonObject root = document.object();
    const QJsonObject db = root.value("database").toObject();
    if(parse.error != QJsonParseError::NoError || root.value("schema_version").toInt() != 2
       || root.value("path_base").toString() != "catalog_directory"
       || db.value("table").toString() != "items.weapons"
       || db.value("identity_field").toInt(-1) != 0
       || db.value("weapon_class_field").toInt(-1) != 2
       || db.value("model_index_field").toInt(-1) != 5)
        return fail(QString::fromUtf8("Некорректный формат mapweapons-catalog.json (ожидается версия 2)."));
    // Armor is an optional extension of schema 2. Retain weapon-only catalogs,
    // but never interpret armor bindings without their declared DB contract.
    const QJsonObject armorDb = root.value("armor_database").toObject();
    const bool hasArmor = root.contains("armor_database");
    if(hasArmor && (armorDb.value("table").toString() != "items.armors"
       || armorDb.value("identity_field").toInt(-1) != 0
       || armorDb.value("armor_type_field").toInt(-1) != 2
       || armorDb.value("model_index_field").toInt(-1) != 5))
        return fail(QString::fromUtf8("Некорректное описание items.armors в каталоге предметов."));
    const QString archive = root.value("model_archive").toString();
    const QRegularExpression leaf("^[A-Za-z0-9_.-]+$");
    if(!leaf.match(archive).hasMatch() || !archive.endsWith(".res", Qt::CaseInsensitive))
        return fail(QString::fromUtf8("Некорректное имя архива моделей предметов."));
    m_archive = QFileInfo(path).dir().absoluteFilePath(archive);
    CResArchiveIndex index;
    QString error;
    if(!index.open(m_archive, &error))
        return fail(QString::fromUtf8("Архив моделей предметов недоступен: ") + error);
    int rejected = 0;
    for(const QJsonValue& value : root.value("models").toArray())
    {
        const QJsonObject row = value.toObject();
        SMapWeaponModel model{row.value("model").toString(), row.value("mod").toString(), row.value("bon").toString()};
        if(!leaf.match(model.model).hasMatch() || model.mod != model.model + ".mod"
           || model.bon != model.model + ".bon" || index.memberIndex(model.mod) < 0
           || index.memberIndex(model.bon) < 0) { ++rejected; continue; }
        const auto bind = [&](const QJsonArray& bindings, bool armor) {
            for(const QJsonValue& bindingValue : bindings)
            {
                const QJsonObject binding = bindingValue.toObject();
                const QString name = binding.value("name").toString();
                const auto* item = database.findItem(name);
                // Record positions and archive hashes can change after unrelated DB edits.
                // Bind by stable identity and verify both geometry selectors against live DB.
                if(!item || item->category != (armor ? "Armor" : "Weapon")
                   || binding.value(armor ? "armor_type" : "weapon_class").toInt(-1) != item->typeId
                   || binding.value("model_index").toInt(-1) != item->textureType1
                   || (armor && (!hasArmor
                       || binding.value("texture_variant").toInt(-1) != item->textureType2
                       || binding.value("material_type").toString().compare(item->materialType, Qt::CaseInsensitive) != 0)))
                    { ++rejected; continue; }
                auto& models = m_models[name.toCaseFolded()];
                bool duplicate = false;
                for(const auto& existing : models) if(existing.model == model.model) duplicate = true;
                if(!duplicate) models.append(model);
            }
        };
        bind(row.value("db_weapons").toArray(), false);
        bind(row.value("db_armors").toArray(), true);
    }
    for(auto it = m_models.begin(); it != m_models.end(); ++it)
        std::sort(it->begin(), it->end(), [](const SMapWeaponModel& a, const SMapWeaponModel& b) {
            // Prefer the right-hand extraction, while exposing every catalog variant.
            if(a.model.endsWith('r') != b.model.endsWith('r')) return a.model.endsWith('r');
            return a.model < b.model;
        });
    if(m_models.isEmpty()) return fail(QString::fromUtf8("В каталоге нет моделей, связанных с предметами активной БД."));
    if(rejected) m_diagnostic = QString::fromUtf8("Пропущено неподтверждённых связей каталога: %1.").arg(rejected);
    return true;
}

QVector<SMapWeaponModel> MapWeaponCatalog::models(const QString& weapon) const
{
    return m_models.value(weapon.toCaseFolded());
}

QString MapWeaponCatalog::textureName(const SConstructorItem& item, const SConstructorMaterial* material,
                                     bool highResolution)
{
    const QString base = ItemSpellConstructorCatalog::itemTextureName(item, material);
    if(base.isEmpty() || !highResolution) return base;
    // Same authored atlas naming as JMV equipment: weapons/helmets use x2,
    // while the six remaining armor types use x4. Keep full-atlas UVs.
    return base + (item.category == "Armor" && item.type.compare("helm", Qt::CaseInsensitive) != 0
        ? ".x4" : ".x2");
}

CWorldObj* MapWeaponCatalog::create(const SConstructorItem& item, const SConstructorMaterial* material,
                                   const QString& modelName, bool highResolution, QString* diagnostic) const
{
    if(diagnostic) diagnostic->clear();
    const auto fail = [&](const QString& message) -> CWorldObj* { if(diagnostic) *diagnostic = message; return nullptr; };
    SMapWeaponModel selected;
    for(const auto& model : models(item.name)) if(model.model == modelName) selected = model;
    if(selected.model.isEmpty()) return fail(QString::fromUtf8("Модель не связана с выбранным предметом в каталоге."));
    if(!material || material->type.compare(item.materialType, Qt::CaseInsensitive) != 0)
        return fail(QString::fromUtf8("Выберите совместимый материал предмета."));
    for(const QString& resource : {selected.mod, selected.bon})
    {
        QString source;
        if(CObjectList::getInstance()->resourceData(resource, &source).isEmpty()
           || QFileInfo(source).canonicalFilePath().compare(QFileInfo(m_archive).canonicalFilePath(), Qt::CaseInsensitive) != 0)
            return fail(QString::fromUtf8("Ресурс %1 из %2 недоступен или перекрыт другим архивом профиля.").arg(resource, m_archive));
    }
    if(!CObjectList::getInstance()->getFigureResource(selected.mod))
        return fail(QString::fromUtf8("Не удалось загрузить модель предмета: ") + selected.mod);
    const QString exactTexture = textureName(item, material, highResolution);
    QStringList candidates;
    if(!exactTexture.isEmpty()) {
        candidates.append(exactTexture);
    }
    QString texture;
    const auto available = CTextureList::getInstance()->textureList();
    for(const QString& candidate : candidates) {
        bool found = false;
        for(const QString& name : available) if(name.compare(candidate, Qt::CaseInsensitive) == 0) found = true;
        if(found && CTextureList::getInstance()->hasTexture(candidate, selected.model)) { texture = candidate; break; }
    }
    if(texture.isEmpty()) return fail(QString::fromUtf8("Нет текстуры для этого материала: %1. Выберите другой материал или добавьте текстуру в ресурсы профиля.").arg(candidates.join(", ")));
    CWorldObj defaults;
    QJsonObject data = defaults.toJson();
    QJsonObject baseObject = data.value("Base object").toObject();
    baseObject.insert("Map name", ItemSpellConstructorCatalog::formatItemCode(item.name, material->name));
    data.insert("Base object", baseObject);
    data.insert("Model name", selected.model);
    // Extracted models have authored full-atlas UVs: use the original MMP directly.
    data.insert("Primary texture", texture);
    data.insert("Secondary texture", texture);
    data.insert("Object parts", QJsonArray());
    auto* object = new CWorldObj(data);
    object->loadFigure(); object->loadTexture();
    return object;
}
}
