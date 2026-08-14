#include "item_spell_constructor.h"

#include "database_document.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QRegExp>
#include <QtMath>

namespace {
QString folded(const QString& value) { return value.trimmed().toCaseFolded(); }
QVariant field(const ei::SDatabaseRow& row, int id)
{
    return row.values.value(QStringLiteral("fld.%1.0").arg(id));
}
QString firstLine(QString value)
{
    value.replace('\r', '\n');
    return value.split('\n', QString::SkipEmptyParts).value(0).trimmed();
}
QString textEntry(const QMap<QString, QString>& texts, const QStringList& candidates)
{
    for(const QString& candidate : candidates)
        for(auto it = texts.constBegin(); it != texts.constEnd(); ++it)
            if(it.key().compare(candidate, Qt::CaseInsensitive) == 0)
                return firstLine(it.value());
    return QString();
}
void appendItems(const ei::DatabaseDocument& document, const QString& tableKey,
                 const QString& category, QVector<ei::SConstructorItem>* output)
{
    const ei::SDatabaseTable* table = document.table(tableKey);
    if(!table) return;
    for(const ei::SDatabaseRow& row : table->rows)
    {
        if(row.deleted) continue;
        ei::SConstructorItem item;
        item.name = field(row, 0).toString();
        if(item.name.isEmpty()) continue;
        item.displayName = item.name;
        item.category = category;
        item.type = field(row, 1).toString();
        item.typeId = field(row, 2).toInt();
        item.materialType = field(row, 3).toString();
        item.textureType1 = field(row, 5).toInt();
        item.textureType2 = field(row, 6).toInt();
        item.mana = field(row, 10).toDouble();
        item.slotCapacity = field(row, 11).toInt();
        item.components = field(row, 13).toInt();
        if(category == QStringLiteral("Quick Item")) item.builtInSpell = field(row, 25).toString();
        output->append(item);
    }
}

QStringList itemReferences(const QVariant& value)
{
    if(value.type() == QVariant::StringList) return value.toStringList();
    const QString text = value.toString().trimmed();
    const QJsonDocument json = QJsonDocument::fromJson(text.toUtf8());
    if(json.isArray())
    {
        QStringList result;
        for(const QJsonValue& entry : json.array())
            if(entry.isString()) result << entry.toString();
        return result;
    }
    return text.isEmpty() ? QStringList() : QStringList{text};
}

QPair<QString, QString> itemAndMaterial(const QString& reference)
{
    QString base = reference.trimmed();
    const int payload = base.indexOf('[');
    if(payload >= 0) base = base.left(payload).trimmed();
    const int dot = base.lastIndexOf('.');
    if(dot <= 0 || dot == base.size() - 1) return {};
    return {folded(base.left(dot)), folded(base.mid(dot + 1))};
}
}

namespace ei {
ItemSpellConstructorCatalog ItemSpellConstructorCatalog::fromDocument(const DatabaseDocument& document)
{
    ItemSpellConstructorCatalog result;
    if(const SDatabaseTable* table = document.table(QStringLiteral("items.materials")))
        for(const SDatabaseRow& row : table->rows)
        {
            if(row.deleted) continue;
            SConstructorMaterial material;
            material.name = field(row, 0).toString();
            if(material.name.isEmpty()) continue;
            material.displayName = material.name;
            material.type = field(row, 1).toString();
            material.code = field(row, 2).toString();
            material.mana = field(row, 6).toDouble();
            material.slotCapacity = field(row, 7).toInt();
            result.materials.append(material);
        }
    appendItems(document, QStringLiteral("items.weapons"), QStringLiteral("Weapon"), &result.items);
    appendItems(document, QStringLiteral("items.armors"), QStringLiteral("Armor"), &result.items);
    appendItems(document, QStringLiteral("items.quickitems"), QStringLiteral("Quick Item"), &result.items);

    if(const SDatabaseTable* table = document.table(QStringLiteral("spells.spellprototypes")))
        for(const SDatabaseRow& row : table->rows)
        {
            if(row.deleted) continue;
            SConstructorSpell spell;
            spell.name = field(row, 0).toString();
            spell.displayName = spell.name;
            spell.code = field(row, 1).toString();
            spell.subtype = field(row, 2).toString();
            spell.school = field(row, 4).toInt();
            spell.mana = field(row, 5).toDouble();
            spell.slotCapacity = field(row, 6).toInt();
            spell.speed = field(row, 7).toDouble();
            spell.range = field(row, 8).toDouble();
            spell.area = field(row, 9).toDouble();
            spell.effect = field(row, 10).toDouble();
            spell.targets = field(row, 12).toInt();
            spell.durationSegments = field(row, 13).toInt();
            spell.actions = field(row, 14).toInt();
            spell.textureType = field(row, 18).toInt();
            spell.complexity = field(row, 21).toInt();
            if(!spell.code.isEmpty()) result.spells.append(spell);
        }
    if(const SDatabaseTable* table = document.table(QStringLiteral("spells.spellmodifiers")))
        for(const SDatabaseRow& row : table->rows)
        {
            if(row.deleted) continue;
            SConstructorRune rune;
            rune.name = field(row, 0).toString();
            rune.displayName = rune.name;
            rune.code = field(row, 1).toString();
            rune.type = field(row, 3).toInt();
            rune.mana = field(row, 4).toDouble();
            rune.value = field(row, 5).toDouble();
            rune.complexity = field(row, 6).toInt();
            if(!rune.code.isEmpty()) result.runes.append(rune);
        }
    if(const SDatabaseTable* table = document.table(QStringLiteral("units.monsters")))
        for(const SDatabaseRow& row : table->rows)
        {
            if(row.deleted) continue;
            for(int id : {29, 32, 33, 42})
                for(const QString& reference : itemReferences(field(row, id)))
                {
                    const auto pair = itemAndMaterial(reference);
                    if(!pair.first.isEmpty() && !pair.second.isEmpty())
                        result.m_authoredMaterialsByItem[pair.first].insert(pair.second);
                }
        }
    return result;
}

SParsedSpellCode ItemSpellConstructorCatalog::parseSpellCode(const QString& input)
{
    SParsedSpellCode result;
    const QString value = input.trimmed();
    const int open = value.indexOf('{'), close = value.lastIndexOf('}');
    if(open <= 0 || close != value.size() - 1 || close < open)
    {
        result.diagnostics << QStringLiteral("Expected spell code in the form prototype{rune;rune}.");
        return result;
    }
    result.prototypeCode = value.left(open).trimmed();
    const QString body = value.mid(open + 1, close - open - 1);
    for(const QString& rune : body.split(QRegExp("[;,]"), QString::SkipEmptyParts))
        result.runeCodes << rune.trimmed();
    if(result.prototypeCode.isEmpty()) result.diagnostics << QStringLiteral("Spell prototype code is empty.");
    if(result.runeCodes.size() > 8) result.diagnostics << QStringLiteral("The game spell format supports at most eight runes.");
    result.valid = !result.prototypeCode.isEmpty() && result.runeCodes.size() <= 8;
    return result;
}

QString ItemSpellConstructorCatalog::formatSpellCode(const QString& prototypeCode,
                                                     const QStringList& runeCodes)
{
    QStringList clean;
    for(const QString& rune : runeCodes) if(!rune.trimmed().isEmpty()) clean << rune.trimmed();
    return prototypeCode.trimmed() + QLatin1Char('{') + clean.join(QLatin1Char(';')) + QLatin1Char('}');
}

SParsedItemCode ItemSpellConstructorCatalog::parseItemCode(const QString& input)
{
    SParsedItemCode result;
    QString base = input.trimmed();
    const int open = base.indexOf('[');
    if(open >= 0)
    {
        if(!base.endsWith(']'))
        {
            result.diagnostics << QStringLiteral("Item bracket payload is not closed.");
            return result;
        }
        result.bracketPayload = base.mid(open + 1, base.size() - open - 2).trimmed();
        base = base.left(open).trimmed();
        if(result.bracketPayload.contains('{'))
        {
            result.spell = parseSpellCode(result.bracketPayload);
            result.diagnostics += result.spell.diagnostics;
        }
    }
    const int dot = base.lastIndexOf('.');
    if(dot > 0)
    {
        result.prototypeName = base.left(dot).trimmed();
        result.materialName = base.mid(dot + 1).trimmed();
    }
    else result.prototypeName = base;
    if(result.prototypeName.isEmpty()) result.diagnostics << QStringLiteral("Item prototype is empty.");
    result.valid = !result.prototypeName.isEmpty()
        && (result.bracketPayload.isEmpty() || !result.bracketPayload.contains('{') || result.spell.valid);
    return result;
}

QString ItemSpellConstructorCatalog::formatItemCode(const QString& prototypeName,
                                                    const QString& materialName,
                                                    const QString& spellCode)
{
    QString result = prototypeName.trimmed();
    if(!materialName.trimmed().isEmpty()) result += QLatin1Char('.') + materialName.trimmed();
    if(!spellCode.trimmed().isEmpty()) result += QLatin1Char('[') + spellCode.trimmed() + QLatin1Char(']');
    return result;
}

QString ItemSpellConstructorCatalog::itemFigureName(const SConstructorItem& item)
{
    static const QMap<int, QString> weapons = {
        {0,"sw"},{1,"ax"},{2,"dg"},{3,"sp"},{4,"hm"},{5,"bw"},{6,"cb"},
        {9,"sd"},{10,"tp"},{11,"sf"},{12,"ts"},{13,"ta"},{14,"th"},{15,"tf"}
    };
    static const QMap<QString, QString> armors = {
        {"helm","hl"},{"plate","pl"},{"leggings","lg"},{"shirt","sh"},
        {"pants","pt"},{"boots","bt"},{"gloves","gl"}
    };
    if(item.category == QStringLiteral("Weapon"))
    {
        const QString code = weapons.value(item.typeId);
        return code.isEmpty() ? QString() : QStringLiteral("initwe%1%2weapon").arg(code).arg(item.textureType1);
    }
    if(item.category == QStringLiteral("Armor"))
    {
        const QString code = armors.value(folded(item.type));
        return code.isEmpty() ? QString() : QStringLiteral("initar%1%2armor").arg(code).arg(item.textureType1);
    }
    if(item.category == QStringLiteral("Quick Item"))
        return QStringLiteral("initqi%1item").arg(item.textureType1);
    return QString();
}

QString ItemSpellConstructorCatalog::itemTextureName(const SConstructorItem& item,
                                                       const SConstructorMaterial* material)
{
    static const QMap<int, QString> weapons = {
        {0,"sw"},{1,"ax"},{2,"dg"},{3,"sp"},{4,"hm"},{5,"bw"},{6,"cb"},
        {9,"sd"},{10,"tp"},{11,"sf"},{12,"ts"},{13,"ta"},{14,"th"},{15,"tf"}
    };
    static const QMap<QString, QString> armors = {
        {"helm","hl"},{"plate","pl"},{"leggings","lg"},{"shirt","sh"},
        {"pants","pt"},{"boots","bt"},{"gloves","gl"}
    };
    if(item.category == QStringLiteral("Quick Item"))
    {
        QString result = QStringLiteral("qitem%1").arg(item.textureType1, 4, 10, QChar('0'));
        if(material && !material->code.trimmed().isEmpty())
            result += QLatin1Char('.') + material->code.trimmed().toLower();
        return result;
    }
    const QString code = item.category == QStringLiteral("Weapon")
        ? weapons.value(item.typeId) : armors.value(folded(item.type));
    if(code.isEmpty() || !material || material->code.trimmed().isEmpty())
        return QString();
    return QStringLiteral("unhuma%1_%2.%3.%4").arg(code)
        .arg(item.textureType1, 2, 10, QChar('0'))
        .arg(material->code.trimmed().toLower()).arg(item.textureType2);
}

QString ItemSpellConstructorCatalog::spellTextureName(const SConstructorSpell& spell)
{
    return spell.textureType < 0 ? QString()
        : QStringLiteral("spell%1").arg(spell.textureType, 4, 10, QChar('0'));
}

QImage ItemSpellConstructorCatalog::orientPreviewImage(const QImage& image)
{
    // MMP scanlines are uploaded to OpenGL after a vertical mirror. Match the
    // rendered game orientation when the decoded image is shown in a Qt label.
    return image.mirrored(false, true);
}

void ItemSpellConstructorCatalog::applyGameTexts(const QMap<QString, QString>& entries)
{
    m_gameTexts = entries;
    for(SConstructorMaterial& material : materials)
    {
        const QString name = textEntry(entries, {QStringLiteral("MATERIAL ") + material.name});
        if(!name.isEmpty()) material.displayName = name;
    }
    for(SConstructorSpell& spell : spells)
    {
        const QString name = textEntry(entries, {QStringLiteral("SPELL ") + spell.code,
                                                  QStringLiteral("SPELL ") + spell.name});
        if(!name.isEmpty()) spell.displayName = name;
    }
    for(SConstructorRune& rune : runes)
    {
        const QString name = textEntry(entries, {QStringLiteral("MODIFIER ") + rune.code,
                                                  QStringLiteral("MODIFIER ") + rune.name});
        if(!name.isEmpty()) rune.displayName = name;
    }
    for(SConstructorItem& item : items)
    {
        const QString prefix = item.category == QStringLiteral("Weapon") ? QStringLiteral("WEAPON ")
            : item.category == QStringLiteral("Armor") ? QStringLiteral("ARMOR ") : QStringLiteral("QITEM ");
        QString name = textEntry(entries, {prefix + item.name, QStringLiteral("INSTR ") + item.name});
        if(!name.isEmpty()) item.displayName = name;
    }
}

QString ItemSpellConstructorCatalog::itemDisplayName(const SConstructorItem& item,
                                                      const SConstructorMaterial* material) const
{
    const QString prefix = item.category == QStringLiteral("Weapon") ? QStringLiteral("WEAPON ")
        : item.category == QStringLiteral("Armor") ? QStringLiteral("ARMOR ") : QStringLiteral("QITEM ");
    QStringList candidates;
    if(material) candidates << prefix + item.name + QLatin1Char(' ') + material->name;
    candidates << prefix + item.name << QStringLiteral("INSTR ") + item.name;
    const QString resolved = textEntry(m_gameTexts, candidates);
    return resolved.isEmpty() ? item.displayName : resolved;
}

QVector<SConstructorMaterial> ItemSpellConstructorCatalog::compatibleMaterials(const SConstructorItem& item) const
{
    QVector<SConstructorMaterial> result;
    if(item.materialType.trimmed().isEmpty() || item.materialType.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0)
        return result;
    for(const SConstructorMaterial& material : materials)
        if(material.type.compare(item.materialType, Qt::CaseInsensitive) == 0
           || isAuthoredMaterialCompatibility(item.name, material.name))
            result.append(material);
    return result;
}

bool ItemSpellConstructorCatalog::isAuthoredMaterialCompatibility(
    const QString& itemName, const QString& materialName) const
{
    return m_authoredMaterialsByItem.value(folded(itemName)).contains(folded(materialName));
}

SConstructorLimitReport ItemSpellConstructorCatalog::limits(const SConstructorItem& item,
                                                             const SConstructorMaterial* material,
                                                             const SConstructorSpell* spell,
                                                             const QStringList& runeCodes) const
{
    SConstructorLimitReport result = spell ? spellReport(*spell, runeCodes)
                                           : SConstructorLimitReport();
    // Components controls crafting material consumption and price only. The
    // runtime adds the selected material's magic capacities exactly once.
    result.itemMana = item.mana + (material ? material->mana : 0.0);
    result.itemSlots = item.slotCapacity + (material ? material->slotCapacity : 0);
    if(!spell) return result;
    if(result.spellMana > result.itemMana)
        result.warnings << QCoreApplication::translate(
                           "ItemSpellConstructorCatalog",
                           "Spell energy %1 exceeds the constructed item's energy %2.")
                           .arg(result.spellMana, 0, 'g', 8).arg(result.itemMana, 0, 'g', 8);
    if(result.spellSlots > result.itemSlots)
        result.warnings << QCoreApplication::translate(
                           "ItemSpellConstructorCatalog",
                           "Spell uses %1 slots but the constructed item has %2.")
                           .arg(result.spellSlots).arg(result.itemSlots);
    return result;
}

SConstructorLimitReport ItemSpellConstructorCatalog::spellReport(
    const SConstructorSpell& spell, const QStringList& runeCodes) const
{
    SConstructorLimitReport result;
    result.spellMana = spell.mana;
    result.spellSlots = spell.slotCapacity;
    result.spellComplexity = spell.complexity;
    result.range = spell.range;
    result.area = spell.area;
    result.effect = spell.effect;
    result.durationSegments = spell.durationSegments;
    result.targets = spell.targets;
    for(const QString& code : runeCodes)
    {
        const SConstructorRune* rune = findRune(code);
        if(!rune)
        {
            result.warnings << QCoreApplication::translate(
                               "ItemSpellConstructorCatalog",
                               "Unknown rune code '%1'.").arg(code);
            continue;
        }
        const QChar kind = rune->code.isEmpty() ? QChar() : rune->code.at(0).toLower();
        if(kind == QLatin1Char('r')) result.range = qMax(0.0, result.range + spell.range * rune->value);
        else if(kind == QLatin1Char('t')) result.targets = qMax(0, result.targets + int(spell.targets * rune->value));
        else if(kind == QLatin1Char('a')) result.area = qMax(0.0, result.area + spell.area * rune->value);
        else if(kind == QLatin1Char('e')) result.effect = qMax(0.0, result.effect + spell.effect * rune->value);
        else if(kind == QLatin1Char('d'))
            result.durationSegments = qMax(0, result.durationSegments
                                               + int(spell.durationSegments * rune->value));
        else if(kind == QLatin1Char('m'))
            result.spellMana = qMax(0.0, result.spellMana + spell.mana * rune->value);
        ++result.spellSlots;
        result.spellComplexity += rune->complexity;
        result.spellMana += rune->mana;
    }
    result.spellMana = qMax(spell.mana, result.spellMana);
    result.areaRadius = result.area > 0.0 ? qSqrt(result.area / M_PI) : 0.0;
    return result;
}

const SConstructorItem* ItemSpellConstructorCatalog::findItem(const QString& name) const
{ for(const auto& value : items) if(folded(value.name) == folded(name)) return &value; return nullptr; }
const SConstructorMaterial* ItemSpellConstructorCatalog::findMaterial(const QString& name) const
{ for(const auto& value : materials) if(folded(value.name) == folded(name)) return &value; return nullptr; }
const SConstructorSpell* ItemSpellConstructorCatalog::findSpell(const QString& code) const
{ for(const auto& value : spells) if(folded(value.code) == folded(code) || folded(value.name) == folded(code)) return &value; return nullptr; }
const SConstructorRune* ItemSpellConstructorCatalog::findRune(const QString& code) const
{ for(const auto& value : runes) if(folded(value.code) == folded(code)) return &value; return nullptr; }
}
