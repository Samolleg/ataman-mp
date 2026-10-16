#include "map_weapon_catalog.h"
#include "res_archive_index.h"
#include "resourcemanager.h"
#include "worldobj.h"
#include "database_document.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace ei {
QString MapInventoryCatalog::pathForDatabase(const QString& databasePath)
{
    return databasePath.isEmpty() ? QString() : QFileInfo(databasePath).dir().filePath("mapitems-catalog.json");
}

bool MapInventoryCatalog::load(const QString& path, const DatabaseDocument* database)
{
    m_items.clear(); m_diagnostic.clear();
    m_present = !path.isEmpty() && QFileInfo::exists(path);
    if(!m_present) return false;
    const auto fail = [&](const QString& message) { m_items.clear(); m_diagnostic = message; return false; };
    QFile file(path);
    if(!database || !file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        return fail(QString::fromUtf8("Не удалось прочитать каталог квестовых и быстрых предметов."));
    QJsonParseError parse;
    const auto root = QJsonDocument::fromJson(file.readAll(), &parse).object();
    const auto db = root.value("database").toObject();
    bool quick = false, quest = false;
    for(const auto& value : db.value("tables").toArray()) {
        const auto table = value.toObject();
        if(table.value("table") == "items.quick_items" && table.value("section_id").toInt(-1) == 4
           && table.value("type_field").toInt(-1) == 21) quick = true;
        if(table.value("table") == "items.quest_items" && table.value("section_id").toInt(-1) == 5
           && table.value("script_id_field").toInt(-1) == 21) quest = true;
    }
    if(parse.error != QJsonParseError::NoError || root.value("schema_version").toInt() != 2
       || root.value("path_base") != "catalog_directory" || db.value("resource") != "items.idb"
       || db.value("identity_field").toInt(-1) != 0 || db.value("model_index_field").toInt(-1) != 5
       || db.value("material_type_field").toInt(-1) != 3 || !quick || !quest
       || root.value("model_kind") != "existing_inventory_geometry"
       || root.value("texture_layout").toObject().value("mode") != "inventory_atlas"
       || root.value("texture_layout").toObject().value("logical_layer_size").toArray() != QJsonArray({64,64})
       || root.value("texture_layout").toObject().value("logical_atlas_size").toArray() != QJsonArray({256,256})
       || root.value("texture_layout").toObject().value("logical_layer_origin").toArray() != QJsonArray({0,192})
       || !root.value("texture_layout").toObject().value("preserve_source_uv").toBool())
        return fail(QString::fromUtf8("Некорректный формат mapitems-catalog.json (ожидается версия 2)."));
    const QDir directory = QFileInfo(path).dir();
    const auto constructor = ItemSpellConstructorCatalog::fromDocument(*database);
    QMap<QString, CResArchiveIndex> archives;
    const QRegularExpression leaf("^[A-Za-z0-9_.-]+$");
    const auto resourceExists = [&](const QJsonObject& resource, const QString& expected, QString* archivePath) {
        const QString relative = resource.value("archive").toString();
        if(relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(':')
           || !relative.endsWith(".res", Qt::CaseInsensitive) || resource.value("resource").toString() != expected)
            return false;
        // Schema paths intentionally include ../../../Base/Res. Resolve from the
        // catalog directory; these archives are read-only and never copied.
        *archivePath = QDir::cleanPath(directory.absoluteFilePath(relative));
        auto it = archives.find(*archivePath);
        if(it == archives.end()) {
            CResArchiveIndex index;
            if(!index.open(*archivePath)) return false;
            it = archives.insert(*archivePath, index);
        }
        return it->memberIndex(expected) >= 0;
    };
    int rejected = 0;
    for(const auto& value : root.value("models").toArray()) {
        const auto row = value.toObject();
        const QString model = row.value("model").toString();
        const QString component = row.value("component").toString();
        if(!leaf.match(model).hasMatch() || component != model + "item") { ++rejected; continue; }
        for(bool isQuest : {false, true}) {
            const auto* table = database->table(isQuest ? "items.questitems" : "items.quickitems");
            if(!table) continue;
            for(const auto& bindingValue : row.value(isQuest ? "db_quest_items" : "db_quick_items").toArray()) {
                const auto binding = bindingValue.toObject();
                const QString name = binding.value("name").toString();
                const SDatabaseRow* live = nullptr;
                for(const auto& candidate : table->rows)
                    if(!candidate.deleted && !name.isEmpty()
                       && candidate.values.value("fld.0.0").toString().compare(name, Qt::CaseInsensitive) == 0) { live = &candidate; break; }
                const auto field = [&](int id) { return live ? live->values.value(QString("fld.%1.0").arg(id)) : QVariant(); };
                const int modelIndex = field(5).toInt();
                if(!live || binding.value("model_index").toInt(-1) != modelIndex
                   || row.value("model_index").toInt(-1) != modelIndex
                   || model != QString(isQuest ? "initqu%1" : "initqi%1").arg(modelIndex)
                   || binding.value("material_type").toString().compare(field(3).toString(), Qt::CaseInsensitive) != 0
                   || binding.value("type").toString().compare(field(1).toString(), Qt::CaseInsensitive) != 0
                   || binding.value(isQuest ? "script_id" : "quick_item_type").toInt(-1) != field(21).toInt())
                    { ++rejected; continue; }
                SMapInventoryItem item;
                item.name = name; item.label = binding.value("name_ru").toString();
                item.category = isQuest ? "questItems" : "quickItems"; item.model = component;
                const auto resources = row.value("resources").toObject();
                bool geometry = true;
                for(const auto& key : {QString("lnk"), QString("fig"), QString("figure_bon")}) {
                    const QString member = key == "lnk" ? model + ".lnk" : component + (key == "fig" ? ".fig" : ".bon");
                    QString archivePath;
                    if(!resourceExists(resources.value(key).toObject(), member, &archivePath)) geometry = false;
                    else item.modelResources.insert(member, archivePath);
                }
                item.texture = QString(isQuest ? "quitem%1" : "qitem%1").arg(modelIndex, 4, 10, QChar('0'));
                const auto addVariant = [&](const QJsonObject& source, const QString& material, const QString& code) {
                    SMapInventoryVariant choice;
                    choice.material = material; choice.descriptor = material.isEmpty() ? name : ItemSpellConstructorCatalog::formatItemCode(name, material);
                    choice.label = source.value("name_ru").toString();
                    choice.texture = item.texture + (code.isEmpty() ? QString() : "." + code);
                    const auto texture = source.value("texture").toObject();
                    if(texture.value("name").toString() != choice.texture) { ++rejected; return; }
                    for(const auto& variantValue : texture.value("variants").toArray()) {
                        const auto variant = variantValue.toObject();
                        const QString member = variant.value("resource").toString();
                        if(member != choice.texture + ".mmp" && member != choice.texture + ".x2.mmp") continue;
                        QString archivePath;
                        if(resourceExists(variant, member, &archivePath)) choice.textures.append(member.left(member.size() - 4));
                    }
                    std::sort(choice.textures.begin(), choice.textures.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
                    item.textures.append(choice.textures); item.variants.append(choice);
                };
                if(binding.value("texture_selection").toObject().value("mode") == "spell_contents") {
                    item.unavailableReason = QString::fromUtf8("Текстура этого предмета зависит от вложенного заклинания. Каталог не задаёт готовый вариант для размещения.");
                } else if(binding.contains("materials")) {
                    for(const auto& materialValue : binding.value("materials").toArray()) {
                        const auto materialBinding = materialValue.toObject();
                        const QString name = materialBinding.value("material").toString();
                        const auto* material = constructor.findMaterial(name);
                        if(!material || material->type.compare(field(3).toString(), Qt::CaseInsensitive) != 0
                           || material->code != materialBinding.value("material_code").toString()) { ++rejected; continue; }
                        addVariant(materialBinding, name, material->code);
                    }
                } else addVariant(binding, QString(), QString());
                if(item.label.isEmpty() && !item.variants.isEmpty()) item.label = item.variants.first().label;
                if(!geometry) item.unavailableReason = QString::fromUtf8("Модель предмета отсутствует в архивах каталога: ") + model;
                else if(item.unavailableReason.isEmpty() && item.textures.isEmpty()) item.unavailableReason = QString::fromUtf8("Текстура предмета отсутствует в архивах каталога: ") + item.texture;
                const QString key = name.toCaseFolded();
                if(m_items.contains(key)) { ++rejected; m_items[key].unavailableReason = QString::fromUtf8("Неоднозначная связь предмета в каталоге."); }
                else m_items.insert(key, item);
            }
        }
    }
    if(m_items.isEmpty()) return fail(QString::fromUtf8("В каталоге нет подтверждённых предметов активной БД."));
    if(rejected) m_diagnostic = QString::fromUtf8("Пропущено неподтверждённых связей каталога предметов: %1.").arg(rejected);
    return true;
}

const SMapInventoryItem* MapInventoryCatalog::find(const QString& name) const
{
    const auto it = m_items.constFind(name.toCaseFolded());
    return it == m_items.constEnd() ? nullptr : &it.value();
}

CWorldObj* MapInventoryCatalog::create(const QString& name, QString* diagnostic, const QString& material) const
{
    if(diagnostic) diagnostic->clear();
    const auto fail = [&](const QString& message) -> CWorldObj* { if(diagnostic) *diagnostic = message; return nullptr; };
    const auto* item = find(name);
    if(!item) return fail(QString::fromUtf8("Предмет не связан с активной БД в mapitems-catalog.json."));
    if(!item->unavailableReason.isEmpty()) return fail(item->unavailableReason);
    const SMapInventoryVariant* selected = nullptr;
    for(const auto& variant : item->variants) if(variant.material == material) { selected = &variant; break; }
    if(!selected) return fail(QString::fromUtf8("Выберите подтверждённый материал предмета."));
    for(auto it = item->modelResources.constBegin(); it != item->modelResources.constEnd(); ++it) {
        QString source;
        if(CObjectList::getInstance()->resourceData(it.key(), &source).isEmpty()
           || QFileInfo(source).canonicalFilePath().compare(QFileInfo(it.value()).canonicalFilePath(), Qt::CaseInsensitive) != 0)
            return fail(QString::fromUtf8("Ресурс модели недоступен или перекрыт в профиле: ") + it.key());
    }
    if(!CObjectList::getInstance()->getFigureResource(item->model + ".fig"))
        return fail(QString::fromUtf8("Не удалось загрузить модель: ") + item->model);
    QString texture;
    for(const auto& candidate : selected->textures)
        if(CTextureList::getInstance()->textureList().contains(candidate)
           && CTextureList::getInstance()->hasTexture(candidate, item->model)) { texture = candidate; break; }
    if(texture.isEmpty()) return fail(QString::fromUtf8("Текстура недоступна в активном профиле: ") + selected->texture);
    CWorldObj defaults;
    auto data = defaults.toJson();
    auto base = data.value("Base object").toObject(); base.insert("Map name", selected->descriptor);
    data.insert("Base object", base); data.insert("Model name", item->model);
    data.insert("Primary texture", texture); data.insert("Secondary texture", texture);
    data.insert("Object parts", QJsonArray());
    auto* object = new CWorldObj(data); object->loadFigure(); object->loadTexture();
    return object;
}

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
