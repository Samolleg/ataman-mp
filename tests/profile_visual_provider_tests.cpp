#include <QtTest>
#include <QJsonArray>

#include "profile_visual_provider.h"

namespace {

ei::SItemDatabaseRecord item(const QString& name, const QString& category,
                             int typeId, int tti)
{
    ei::SItemDatabaseRecord value;
    value.name = name; value.category = category; value.typeId = typeId;
    value.textureType1 = tti;
    return value;
}

QSharedPointer<ei::SProfileDatabaseSnapshot> database()
{
    QSharedPointer<ei::SProfileDatabaseSnapshot> db(new ei::SProfileDatabaseSnapshot);
    db->providerId = "test";
    const struct Race { const char* name; const char* model; const char* texture; } races[] = {
        {"Human Male", "unhuma", "Skin_00,Skin_01"},
        {"Orc Male", "unorma", "Skin_00"},
        {"Skeleton", "unmosk", "Skeleton00"},
        {"Undead", "unmoud", "Undead00"}
    };
    for(const Race& source : races)
    {
        ei::SRaceModelDatabaseRecord race;
        race.name = source.name; race.model = source.model;
        race.primaryTextures << source.texture;
        db->raceModels.insert(race.name.toLower(), race);
        ei::SMonsterDatabaseRecord monster;
        monster.name = QString(source.name) + " Test";
        monster.baseRace = race.name; monster.skinIndex = 0;
        db->monsters.insert(monster.name.toLower(), monster);
    }
    db->items.insert("shirt", item("shirt", "armor", 3, 12));
    db->items.insert("sword", item("sword", "weapons", 0, 1));
    db->items.insert("axe", item("axe", "weapons", 1, 2));
    db->items.insert("bow", item("bow", "weapons", 5, 3));
    db->items.insert("shield", item("shield", "weapons", 9, 4));
    db->items.insert("great sword", item("great sword", "weapons", 12, 5));
    db->items.insert("great axe", item("great axe", "weapons", 13, 6));
    return db;
}

QStringList jsonStrings(const QJsonObject& value, const char* key)
{
    QStringList result;
    for(const QJsonValue& item : value.value(key).toArray()) result.append(item.toString());
    return result;
}

}

class ProfileVisualProviderTests : public QObject
{
    Q_OBJECT
private slots:
    void resolvesDatabaseHumanoidsAcrossFamilies();
    void importedAppearanceNeverFallsBack();
    void basePreservesSingleWeaponBehavior();
    void jmvLayoutsAreIndependentlyGated();
    void validatesAssemblyPartsWithPlaceholders();
    void unknownVisualProviderFailsClosed();
};

void ProfileVisualProviderTests::resolvesDatabaseHumanoidsAcrossFamilies()
{
    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("base.standard.v1", database());
    const QMap<QString, QString> expected = {
        {"Human Male Test", "unhumaskin_00.mmp"}, {"Orc Male Test", "unormaskin_00.mmp"},
        {"Skeleton Test", "skeleton00.mmp"}, {"Undead Test", "undead00.mmp"}
    };
    for(auto it = expected.constBegin(); it != expected.constEnd(); ++it)
    {
        SCharacterAppearanceRequest request;
        request.prototype = it.key();
        request.model = "embedded-wrong";
        request.primaryTexture = "embedded-wrong";
        QJsonObject output;
        QVERIFY2(resolver->resolve(request, &output).success, qPrintable(it.key()));
        QCOMPARE(output.value("primaryTexture").toString(), it.value());
        QVERIFY(output.value("model").toString() != request.model);
    }
}

void ProfileVisualProviderTests::importedAppearanceNeverFallsBack()
{
    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("jmv.v1", database());
    SCharacterAppearanceRequest request;
    request.prototype = "Human Male Test";
    request.dataSource = EUnitVisualDataSource::EmbeddedMob;
    QJsonObject output;
    QVERIFY(!resolver->resolve(request, &output).success);
    QVERIFY(output.value("placeholder").toBool());
    request.model = "local_model"; request.primaryTexture = "local_texture";
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("model").toString(), QString("local_model"));
}

void ProfileVisualProviderTests::basePreservesSingleWeaponBehavior()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("base.standard.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    request.weapons << "sword.iron" << "axe.iron";
    QJsonObject output;
    resolver->resolve(request, &output);
    QCOMPARE(jsonStrings(output, "components"), QStringList({"unhuma.sword01"}));
    QVERIFY(output.value("diagnostics").toArray().first().toString().contains("Base"));
}

void ProfileVisualProviderTests::jmvLayoutsAreIndependentlyGated()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    struct Case { QStringList weapons; QString layout; bool gated; };
    const QVector<Case> cases = {
        {{"sword.iron", "shield.iron"}, "oneHandShield", false},
        {{"sword.iron", "axe.iron"}, "dualOneHand", false},
        {{"great sword.iron"}, "singleTwoHand", false},
        {{"great sword.iron", "great axe.iron"}, "dualTwoHand", true},
        {{"great sword.iron", "shield.iron"}, "twoHandShield", true}
    };
    for(const Case& test : cases)
    {
        SEquipmentLayoutRequest request; request.model = "unhuma"; request.weapons = test.weapons;
        QJsonObject output; resolver->resolve(request, &output);
        QCOMPARE(output.value("layout").toString(), test.layout);
        QCOMPARE(!output.value("placeholders").toArray().isEmpty(), test.gated);
    }
}

void ProfileVisualProviderTests::validatesAssemblyPartsWithPlaceholders()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("base.standard.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhuma"; request.armor << "shirt.leather";
    request.assemblyParts << "unhuma.armor99";
    QJsonObject output; resolver->resolve(request, &output);
    QCOMPARE(jsonStrings(output, "placeholders"), QStringList({"unhuma.armor12"}));
}

void ProfileVisualProviderTests::unknownVisualProviderFailsClosed()
{
    ProfileVisualProviderService service;
    QString diagnostic;
    QVERIFY(service.appearanceResolver("unknown.v1", database(), &diagnostic).isNull());
    QVERIFY(diagnostic.contains("no fallback"));
}

QTEST_APPLESS_MAIN(ProfileVisualProviderTests)
#include "profile_visual_provider_tests.moc"
