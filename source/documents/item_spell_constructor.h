#ifndef ITEM_SPELL_CONSTRUCTOR_H
#define ITEM_SPELL_CONSTRUCTOR_H

#include <QMap>
#include <QImage>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ei {
class DatabaseDocument;

struct SConstructorMaterial
{
    QString name, displayName, type, code;
    double mana{0.0};
    int slotCapacity{0};
};

struct SConstructorItem
{
    QString name, displayName, category, type, materialType, builtInSpell;
    int typeId{-1}, textureType1{-1}, textureType2{-1}, slotCapacity{0}, components{0};
    double mana{0.0};
};

struct SConstructorSpell
{
    QString name, displayName, code, subtype, school;
    int recordType{-1}, textureType{-1}, slotCapacity{0}, complexity{0};
    int durationSegments{0}, targets{0}, actions{0};
    double mana{0.0}, speed{0.0}, range{0.0}, area{0.0}, effect{0.0};
};

struct SConstructorRune
{
    QString name, displayName, code, tag;
    int type{-1}, complexity{0}, textureIndex{-1};
    double mana{0.0}, value{0.0};
};

struct SParsedSpellCode
{
    bool valid{false};
    QString prototypeCode;
    QStringList runeCodes;
    QStringList diagnostics;
};

struct SParsedItemCode
{
    bool valid{false};
    QString prototypeName, materialName;
    SParsedSpellCode spell;
    QString bracketPayload;
    QStringList diagnostics;
};

struct SConstructorLimitReport
{
    double spellMana{0.0}, itemMana{0.0};
    int spellComplexity{0}, spellSlots{0}, itemSlots{0};
    double range{0.0}, area{0.0}, areaRadius{0.0}, effect{0.0};
    int durationSegments{0}, targets{0};
    QStringList warnings;
};

class ItemSpellConstructorCatalog
{
public:
    static ItemSpellConstructorCatalog fromDocument(const DatabaseDocument& document);
    static SParsedSpellCode parseSpellCode(const QString& code);
    static QString formatSpellCode(const QString& prototypeCode, const QStringList& runeCodes);
    static SParsedItemCode parseItemCode(const QString& code);
    static QString formatItemCode(const QString& prototypeName, const QString& materialName,
                                  const QString& spellCode = QString());
    static QString itemFigureName(const SConstructorItem& item);
    static QString itemTextureName(const SConstructorItem& item,
                                   const SConstructorMaterial* material);
    QStringList spellTextureCandidates(const SConstructorSpell& spell) const;
    QString spellTextureName(const SConstructorSpell& spell) const;
    QStringList runeTextureCandidates(const SConstructorRune& rune) const;
    static QString spellSchoolName(const QString& subtype);
    static QImage orientPreviewImage(const QImage& image);

    void applyGameTexts(const QMap<QString, QString>& entries);
    QString itemDisplayName(const SConstructorItem& item,
                            const SConstructorMaterial* material = nullptr) const;
    QVector<SConstructorMaterial> compatibleMaterials(const SConstructorItem& item) const;
    bool isAuthoredMaterialCompatibility(const QString& itemName,
                                         const QString& materialName) const;
    SConstructorLimitReport spellReport(const SConstructorSpell& spell,
                                        const QStringList& runeCodes) const;
    SConstructorLimitReport limits(const SConstructorItem& item,
                                    const SConstructorMaterial* material,
                                    const SConstructorSpell* spell,
                                    const QStringList& runeCodes) const;
    const SConstructorItem* findItem(const QString& name) const;
    const SConstructorMaterial* findMaterial(const QString& name) const;
    const SConstructorSpell* findSpell(const QString& code) const;
    const SConstructorRune* findRune(const QString& code) const;
    QString previewModelName() const { return m_previewModelName; }
    QString previewTextureName() const { return m_previewTextureName; }
    bool isJmvProfile() const { return m_isJmvProfile; }

    QVector<SConstructorMaterial> materials;
    QVector<SConstructorItem> items;
    QVector<SConstructorSpell> spells;
    QVector<SConstructorRune> runes;
private:
    QMap<QString, QString> m_gameTexts;
    QMap<QString, QSet<QString>> m_authoredMaterialsByItem;
    QString m_previewModelName{QStringLiteral("unhuma")};
    QString m_previewTextureName{QStringLiteral("unhumaskin_00")};
    bool m_isJmvProfile{false};
};

}
#endif
