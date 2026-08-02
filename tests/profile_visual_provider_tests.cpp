#include <QtTest>
#include <QJsonArray>

#include "profile_visual_provider.h"
#include "character_bodypart_runtime.h"

namespace {

ei::SItemDatabaseRecord item(const QString& name, const QString& category,
                             int typeId, int tti)
{
    ei::SItemDatabaseRecord value;
    value.name = name; value.category = category; value.typeId = typeId;
    value.textureType1 = tti;
    value.textureType2 = 0;
    value.type = category == "armor" ? "shirt" : "weapon";
    value.materialType = category == "armor" ? "Leather" : "Metal";
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
    ei::SItemDatabaseRecord helmet = item("helmet", "armor", 0, 8);
    helmet.type = "helm";
    helmet.materialType = "Metal";
    db->items.insert("helmet", helmet);
    db->items.insert("sword", item("sword", "weapons", 0, 1));
    db->items.insert("axe", item("axe", "weapons", 1, 2));
    db->items.insert("bow", item("bow", "weapons", 5, 3));
    db->items.insert("shield", item("shield", "weapons", 9, 4));
    db->items.insert("great sword", item("great sword", "weapons", 12, 1));
    db->items.insert("great axe", item("great axe", "weapons", 13, 3));
    db->items.insert("great pike", item("great pike", "weapons", 10, 2));
    db->items.insert("great hammer", item("great hammer", "weapons", 14, 3));
    db->items.insert("great staff", item("great staff", "weapons", 15, 1));
    db->items.insert("agility staff", item("agility staff", "weapons", 11, 2));
    ei::SMaterialDatabaseRecord iron;
    iron.name = "iron"; iron.type = "Metal"; iron.code = "ir"; iron.id = 7;
    db->materialRecords.insert("iron", iron);
    ei::SMaterialDatabaseRecord leather;
    leather.name = "leather"; leather.type = "Leather"; leather.code = "le"; leather.id = 8;
    db->materialRecords.insert("leather", leather);
    ei::SMaterialDatabaseRecord silver;
    silver.name = "silver"; silver.type = "Metal"; silver.code = "sv"; silver.id = 9;
    db->materialRecords.insert("silver", silver);
    ei::SMaterialDatabaseRecord dragonRed;
    dragonRed.name = "dragon red"; dragonRed.type = "Hide"; dragonRed.code = "dr";
    dragonRed.id = 10;
    db->materialRecords.insert("dragon red", dragonRed);
    ei::SMaterialDatabaseRecord magic;
    magic.name = "magic"; magic.type = "Magic"; magic.code = "mg"; magic.id = 11;
    db->materialRecords.insert("magic", magic);
    ei::SMonsterDatabaseRecord extendedSkin;
    extendedSkin.name = "Human Extended Skin";
    extendedSkin.baseRace = "Human Male";
    extendedSkin.skinIndex = 39;
    db->monsters.insert(extendedSkin.name.toLower(), extendedSkin);
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
    void resolvesHumanoidSkinNumberBeyondRaceModelExamples();
    void jmvKeepsEmbeddedWearableAssemblyForDatabaseAppearance();
    void jmvDatabaseStandardAssemblyOverridesConflictingEmbeddedAssembly();
    void importedDefaultTextureResolvesOnlyThroughEmbeddedParentTemplate();
    void jmvPreservesEmbeddedNonHumanoidAppearance();
    void jmvResolvesIntegralOgreWeaponAtlasFromPrototype();
    void jmvHidesGoblinSlingWhenExplicitWeaponIsEnabled();
    void jmvResolvesZombieX4Textures();
    void jmvResolvesSkeletonX4Textures();
    void importedAppearanceNeverFallsBack();
    void basePreservesSingleWeaponBehavior();
    void jmvLayoutsAreIndependentlyGated();
    void validatesAssemblyPartsWithPlaceholders();
    void armorUsesSlotsAndMaterialTextureResources();
    void jmvExplicitMaterialSuffixCanCrossGameplayMaterialFamilies();
    void jmvWearOrderControlsUpperArmorComposition();
    void jmvTopArmorWinsAvailableLowerLayerOverlaps();
    void jmvLeggingsIncludeLinkedShellsWithoutClaimingArmsOrHead();
    void jmvMissingTopOverlapRetainsLowerArmor();
    void jmvMissingExactTopPartsAreNotSynthesized();
    void baseArmorKeepsDisjointSlots();
    void zeroTtiArmorTexturesBaseParts();
    void missingArmorGeometryPreservesBaseParts();
    void zombieEquipmentUsesSharedHumanoidAtlas();
    void helmetAddsDatabaseGeometryAndAtlas();
    void helmetHairFallbackMatchesSerializedSelection();
    void serializedHairVariantsRemainLoadedThroughFallback();
    void jmvRecoversUnambiguousConcatenatedHelmetAndPlate();
    void weaponAtlasesUseLiteralTtiAndEstablishedFamilies();
    void weaponGeometryDoesNotReplaceHandsOrGloves();
    void equippedMaterialsRemainProfileExtensible();
    void magicEquipmentCarriesWhiteEmission();
    void jmvTwoHandWeaponsUseDedicatedAtlasFamilies();
    void resolvesCompoundBowGeometry();
    void jmvIgnoresWeaponsBeyondFirstPair();
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

void ProfileVisualProviderTests::resolvesHumanoidSkinNumberBeyondRaceModelExamples()
{
    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("jmv.v1", database());
    SCharacterAppearanceRequest request;
    request.prototype = "Human Extended Skin";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("primaryTexture").toString(), QString("unhumaskin_39_x4.mmp"));
}

void ProfileVisualProviderTests::jmvKeepsEmbeddedWearableAssemblyForDatabaseAppearance()
{
    auto db = database();
    ei::SRaceModelDatabaseRecord race;
    race.name = "UndeadMale"; race.model = "unmozo0";
    race.primaryTextures << "vurdalak_00.x4,vurdalak_01.x4,vurdalak_02.x4,vurdalak_03.x4";
    db->raceModels.insert(race.name.toLower(), race);
    ei::SMonsterDatabaseRecord monster;
    monster.name = "UndeadF16"; monster.baseRace = race.name; monster.skinIndex = 29;
    db->monsters.insert(monster.name.toLower(), monster);
    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("jmv.v1", db);
    SCharacterAppearanceRequest request;
    request.prototype = monster.name;
    request.model = "unhuma";
    request.primaryTexture = "unhumaskin_00";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("model").toString(), QString("unhuma"));
    QCOMPARE(output.value("modelSource").toString(), QString("embeddedMobAssembly"));
    QCOMPARE(output.value("primaryTexture").toString(), QString("unhumaskin_29_x4.mmp"));
}

void ProfileVisualProviderTests::jmvDatabaseStandardAssemblyOverridesConflictingEmbeddedAssembly()
{
    auto db = database();
    ei::SMonsterDatabaseRecord monster;
    monster.name = "Male With Stale Female MOB";
    monster.baseRace = "Human Male";
    monster.skinIndex = 39;
    db->monsters.insert(monster.name.toLower(), monster);
    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("jmv.v1", db);
    SCharacterAppearanceRequest request;
    request.prototype = monster.name;
    request.model = "unhufe";
    request.primaryTexture = "unhufeskin_00";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("model").toString(), QString("unhuma"));
    QCOMPARE(output.value("modelSource").toString(), QString("raceModelsMask"));
    QCOMPARE(output.value("primaryTexture").toString(), QString("unhumaskin_39_x4.mmp"));
}

void ProfileVisualProviderTests::importedDefaultTextureResolvesOnlyThroughEmbeddedParentTemplate()
{
    auto db = database();
    ei::SMonsterDatabaseRecord parent;
    parent.name = "Embedded Parent";
    parent.baseRace = "Human Male";
    parent.skinIndex = 1;
    db->monsters.insert(parent.name.toLower(), parent);
    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("jmv.v1", db);
    SCharacterAppearanceRequest request;
    request.dataSource = EUnitVisualDataSource::EmbeddedMob;
    request.prototype = "Unrelated Database Prototype";
    request.parentTemplate = parent.name;
    request.model = "unhuma";
    request.primaryTexture = "default0";
    request.secondaryTexture = "default0";
    request.bodyParts << "bd" << "hd";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("source").toString(), QString("embeddedMob"));
    QCOMPARE(output.value("textureSource").toString(), QString("embeddedMobParentTemplate"));
    QCOMPARE(output.value("primaryTexture").toString(), QString("unhumaskin_01_x4.mmp"));

    request.parentTemplate = "Missing Parent";
    output = QJsonObject();
    const SRuntimeProviderResult missing = resolver->resolve(request, &output);
    QVERIFY(!missing.success);
    QCOMPARE(missing.code, QString("appearance.import.texture.unresolved"));
}

void ProfileVisualProviderTests::jmvPreservesEmbeddedNonHumanoidAppearance()
{
    auto db = database();
    ei::SRaceModelDatabaseRecord creature;
    creature.name = "Ogre"; creature.model = "unmoog2";
    creature.primaryTextures << "Ogre03";
    db->raceModels.insert(creature.name.toLower(), creature);
    ei::SMonsterDatabaseRecord ogre;
    ogre.name = "Ogre Test"; ogre.baseRace = creature.name; ogre.skinIndex = 0;
    db->monsters.insert(ogre.name.toLower(), ogre);
    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("jmv.v1", db);
    SCharacterAppearanceRequest request;
    request.prototype = ogre.name;
    request.model = "unmoog1";
    request.primaryTexture = "ogreblack";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("source").toString(), QString("embeddedMobCreature"));
    QCOMPARE(output.value("model").toString(), QString("unmoog1"));
    QCOMPARE(output.value("primaryTexture").toString(), QString("ogreblack"));
    QVERIFY(output.value("weapons").toArray().isEmpty());
}

void ProfileVisualProviderTests::jmvHidesGoblinSlingWhenExplicitWeaponIsEnabled()
{
    auto db = database();
    ei::SRaceModelDatabaseRecord race;
    race.name = "Goblin"; race.model = "unmogo"; race.primaryTextures << "goblin00";
    db->raceModels.insert(race.name.toLower(), race);
    ei::SMonsterDatabaseRecord monster;
    monster.name = "Goblin Spear"; monster.baseRace = race.name; monster.skinIndex = 0;
    db->monsters.insert(monster.name.toLower(), monster);
    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("jmv.v1", db);
    SCharacterAppearanceRequest request;
    request.prototype = monster.name; request.model = "unmogo";
    request.primaryTexture = "goblin03"; request.secondaryTexture = "pike01";
    request.bodyParts << "bd" << "sling" << "rh3.pike00";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "bodyParts"), QStringList({"bd", "rh3.pike00"}));
    QCOMPARE(output.value("secondaryTexture").toString(), QString("pike01"));
}

void ProfileVisualProviderTests::jmvResolvesZombieX4Textures()
{
    auto db = database();
    ei::SRaceModelDatabaseRecord race;
    race.name = "Zombie Female"; race.model = "unhuzf";
    race.primaryTextures << "ZombieFemale00.x4";
    db->raceModels.insert(race.name.toLower(), race);
    ei::SMonsterDatabaseRecord monster;
    monster.name = "Zombie Female Test"; monster.baseRace = race.name; monster.skinIndex = 1;
    db->monsters.insert(monster.name.toLower(), monster);
    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("jmv.v1", db);
    SCharacterAppearanceRequest request; request.prototype = monster.name;
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("primaryTexture").toString(), QString("unhuzfskin_01_x4.mmp"));
}

void ProfileVisualProviderTests::jmvResolvesSkeletonX4Textures()
{
    auto db = database();
    ei::SRaceModelDatabaseRecord race;
    race.name = "Skeleton JMV"; race.model = "unhusk"; race.primaryTextures << "unhuskskin_00.x4";
    db->raceModels.insert(race.name.toLower(), race);
    ei::SMonsterDatabaseRecord monster;
    monster.name = "Skeleton JMV Test"; monster.baseRace = race.name; monster.skinIndex = 2;
    db->monsters.insert(monster.name.toLower(), monster);
    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("jmv.v1", db);
    SCharacterAppearanceRequest request; request.prototype = monster.name;
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("primaryTexture").toString(), QString("unhuskskin_02_x4.mmp"));
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
    request.assemblyParts << "rh3.sword01" << "rh3.axe02";
    QJsonObject output;
    resolver->resolve(request, &output);
    QCOMPARE(jsonStrings(output, "components"), QStringList({"rh3.sword01"}));
    QVERIFY(output.value("diagnostics").toArray().first().toString().contains("Base"));
}

void ProfileVisualProviderTests::jmvLayoutsAreIndependentlyGated()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    struct Case { QStringList weapons; QString layout; bool gated; };
    const QVector<Case> cases = {
        {{"sword.iron", "shield.iron"}, "oneHandShield", false},
        {{"agility staff.iron", "shield.iron"}, "oneHandShield", false},
        {{"sword.iron", "axe.iron"}, "dualOneHand", false},
        {{"great sword.iron"}, "singleTwoHand", false},
        {{"great sword.iron", "great axe.iron"}, "dualTwoHand", true},
        {{"great sword.iron", "shield.iron"}, "twoHandShield", true}
    };
    for(const Case& test : cases)
    {
        SEquipmentLayoutRequest request; request.model = "unhuma"; request.weapons = test.weapons;
        request.assemblyParts << "rh3.sword01" << "rh3.staff02"
                              << "lh3.axe02" << "lh2.shield04"
                              << "rh3.dsword01" << "rh3.daxe03" << "lh3.daxe03";
        QJsonObject output; resolver->resolve(request, &output);
        QCOMPARE(output.value("layout").toString(), test.layout);
        QCOMPARE(!output.value("placeholders").toArray().isEmpty(), test.gated);
        if(test.layout == "dualTwoHand")
        {
            QCOMPARE(jsonStrings(output, "components"), QStringList({"rh3.dsword01"}));
            QCOMPARE(jsonStrings(output, "placeholders"), QStringList({"great axe.iron"}));
            QVERIFY(output.value("diagnostics").toArray().first().toString()
                    .contains("rendering the first two-handed weapon"));
        }
    }
}

void ProfileVisualProviderTests::jmvResolvesIntegralOgreWeaponAtlasFromPrototype()
{
    auto db = database();
    ei::SRaceModelDatabaseRecord creature;
    creature.name = "Ogre Boss"; creature.model = "unmoog2";
    creature.primaryTextures << "Ogre03";
    db->raceModels.insert(creature.name.toLower(), creature);
    ei::SMonsterDatabaseRecord ogre;
    ogre.name = "Ogre Hadagan F21"; ogre.baseRace = creature.name;
    ogre.items << "2h slander.meteoric" << "material.ogre black leather [18]";
    db->monsters.insert(ogre.name.toLower(), ogre);
    ei::SItemDatabaseRecord weapon = item("2h slander", "weapons", 14, 2);
    weapon.type = "hammer";
    db->items.insert(weapon.name.toLower(), weapon);
    ei::SMaterialDatabaseRecord meteoric;
    meteoric.name = "meteoric"; meteoric.type = "Metal"; meteoric.code = "mr";
    db->materialRecords.insert(meteoric.name, meteoric);

    ProfileVisualProviderService service;
    const auto resolver = service.appearanceResolver("jmv.v1", db);
    SCharacterAppearanceRequest request;
    request.prototype = ogre.name; request.model = "unmoog2";
    request.primaryTexture = "ogrezombie01"; request.secondaryTexture = "club7";
    request.bodyParts << "bd" << "rh3" << "rh3.club02";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("secondaryTexture").toString(),
             QString("unhumath_02.mr.0.x2.mmp"));
    QCOMPARE(output.value("secondaryTextureSource").toString(),
             QString("profileDatabaseIntegralWeapon"));

    request.bodyParts = QStringList() << "bd" << "rh3" << "rh3.club00";
    request.secondaryTexture = "club6";
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("secondaryTexture").toString(), QString("club6"));
    QCOMPARE(output.value("secondaryTextureSource").toString(), QString("embeddedMob"));
}

void ProfileVisualProviderTests::validatesAssemblyPartsWithPlaceholders()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("base.standard.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhuma"; request.armor << "shirt.leather";
    request.assemblyParts << "bd.armor99";
    QJsonObject output; resolver->resolve(request, &output);
    QCOMPARE(jsonStrings(output, "placeholders"), QStringList({"bd", "lh1", "rh1"}));
}


void ProfileVisualProviderTests::armorUsesSlotsAndMaterialTextureResources()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhufe"; request.armor << "shirt.leather";
    request.assemblyParts << "bd" << "lh1" << "rh1"
                          << "bd.armor12" << "lh1.armor12" << "rh1.armor12";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "components"), QStringList({"bd", "lh1", "rh1"}));
    QVERIFY(jsonStrings(output, "replacedParts").isEmpty());
    QCOMPARE(jsonStrings(output, "bodyTextureLayers"),
             QStringList({"unhumash_12.le.0.x4.mmp"}));
    QVERIFY(output.value("componentTextures").toObject().isEmpty());
}

void ProfileVisualProviderTests::jmvExplicitMaterialSuffixCanCrossGameplayMaterialFamilies()
{
    auto db = database();
    ei::SItemDatabaseRecord shirt = item("metal shirt", "armor", 3, 14);
    shirt.type = "shirt";
    shirt.materialType = "Metal";
    db->items.insert("metal shirt", shirt);

    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", db);
    SEquipmentLayoutRequest request;
    request.model = "unhufe";
    request.armor << "metal shirt.dragon red";
    request.assemblyParts << "bd" << "lh1" << "rh1";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "bodyTextureLayers"),
             QStringList({"unhumash_14.dr.0.x4.mmp"}));
    QVERIFY(jsonStrings(output, "placeholders").isEmpty());
    QVERIFY(jsonStrings(output, "diagnostics").first().contains("explicit database"));
}

void ProfileVisualProviderTests::jmvWearOrderControlsUpperArmorComposition()
{
    auto db = database();
    ei::SItemDatabaseRecord shirt = item("shirt order", "armor", 3, 4);
    shirt.type = "shirt";
    ei::SItemDatabaseRecord pants = item("pants order", "armor", 4, 7);
    pants.type = "pants";
    ei::SItemDatabaseRecord plate = item("plate order", "armor", 1, 8);
    plate.type = "plate";
    plate.wearOrder = 1;
    ei::SItemDatabaseRecord leggings = item("leggings order", "armor", 2, 9);
    leggings.type = "leggings";
    leggings.wearOrder = 0;
    db->items.insert("shirt order", shirt);
    db->items.insert("pants order", pants);
    db->items.insert("plate order", plate);
    db->items.insert("leggings order", leggings);

    ProfileVisualProviderService service;
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    // Reverse every relevant list ordering: resolution must still be lower
    // armor first, followed by Wear Order and the canonical upper-slot tie.
    request.armor << "leggings order.leather" << "plate order.leather"
                  << "pants order.leather" << "shirt order.leather";
    request.assemblyParts << "bd" << "lh1" << "rh1"
                          << "hp" << "ll1" << "rl1"
                          << "bd.armor08" << "hp.armor09";

    auto resolver = service.equipmentResolver("jmv.v1", db);
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "bodyTextureLayers"), QStringList({
        "unhumash_04.le.0.x4.mmp", "unhumapt_07.le.0.x4.mmp",
        "unhumapl_08.le.0.x4.mmp", "unhumalg_09.le.0.x4.mmp"}));

    // The corpus also uses 0/0: the canonical tie is plate then leggings.
    plate.wearOrder = 0;
    db->items.insert("plate order", plate);
    resolver = service.equipmentResolver("jmv.v1", db);
    output = QJsonObject();
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "bodyTextureLayers"), QStringList({
        "unhumash_04.le.0.x4.mmp", "unhumapt_07.le.0.x4.mmp",
        "unhumapl_08.le.0.x4.mmp", "unhumalg_09.le.0.x4.mmp"}));
}

void ProfileVisualProviderTests::jmvTopArmorWinsAvailableLowerLayerOverlaps()
{
    auto db = database();
    ei::SItemDatabaseRecord shirt = item("shirt", "armor", 3, 4);
    shirt.type = "shirt";
    ei::SItemDatabaseRecord gloves = item("gloves", "armor", 6, 5);
    gloves.type = "gloves";
    ei::SItemDatabaseRecord plate = item("plate", "armor", 1, 8);
    plate.type = "plate";
    ei::SItemDatabaseRecord pants = item("pants", "armor", 4, 7);
    pants.type = "pants";
    ei::SItemDatabaseRecord boots = item("boots", "armor", 5, 6);
    boots.type = "boots";
    ei::SItemDatabaseRecord leggings = item("leggings", "armor", 2, 9);
    leggings.type = "leggings";
    db->items.insert("shirt", shirt);
    db->items.insert("gloves", gloves);
    db->items.insert("plate", plate);
    db->items.insert("pants", pants);
    db->items.insert("boots", boots);
    db->items.insert("leggings", leggings);

    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", db);
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    // Put top armor first to prove precedence is independent of MOB/database order.
    request.armor << "plate.leather" << "leggings.leather"
                  << "shirt.leather" << "gloves.leather"
                  << "pants.leather" << "boots.leather";
    request.assemblyParts << "bd" << "lh1" << "rh1"
                          << "lh1.armor08" << "rh1.armor08"
                          << "lh2" << "lh3" << "rh2" << "rh3"
                          << "bd.armor08"
                          << "lh2.armor08" << "lh3.armor08" << "rh2.armor08" << "rh3.armor08"
                          << "hp" << "ll1" << "rl1"
                          << "hp.armor09"
                          << "ll1.armor09" << "rl1.armor09"
                          << "ll2" << "ll3" << "rl2" << "rl3"
                          << "ll2.armor09" << "ll3.armor09" << "rl2.armor09" << "rl3.armor09";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    const QStringList components = jsonStrings(output, "components");
    QVERIFY(!components.contains("bd"));
    QVERIFY(!components.contains("lh1"));
    QVERIFY(!components.contains("rh1"));
    QVERIFY(!components.contains("lh2"));
    QVERIFY(!components.contains("rh2"));
    QVERIFY(!components.contains("ll2"));
    QVERIFY(!components.contains("rl2"));
    QVERIFY(!components.contains("ll1"));
    QVERIFY(!components.contains("rl1"));
    QVERIFY(components.contains("lh1.armor08"));
    QVERIFY(components.contains("rh1.armor08"));
    QVERIFY(components.contains("lh2.armor08"));
    QVERIFY(components.contains("rh2.armor08"));
    QVERIFY(components.contains("ll1.armor09"));
    QVERIFY(components.contains("rl1.armor09"));
    QVERIFY(components.contains("ll2.armor09"));
    QVERIFY(components.contains("rl2.armor09"));
    QCOMPARE(jsonStrings(output, "bodyTextureLayers"), QStringList({
        "unhumash_04.le.0.x4.mmp", "unhumapt_07.le.0.x4.mmp",
        "unhumabt_06.le.0.x4.mmp", "unhumagl_05.le.0.x4.mmp",
        "unhumapl_08.le.0.x4.mmp", "unhumalg_09.le.0.x4.mmp"}));
}

void ProfileVisualProviderTests::jmvLeggingsIncludeLinkedShellsWithoutClaimingArmsOrHead()
{
    auto db = database();
    ei::SItemDatabaseRecord leggings = item("female suite leggings", "armor", 2, 24);
    leggings.type = "leggings";
    db->items.insert(leggings.name, leggings);
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", db);
    SEquipmentLayoutRequest request;
    request.model = "unhufe";
    request.armor << "female suite leggings.leather";
    request.assemblyParts << "bd" << "hp" << "hd" << "lh1" << "rh1"
                          << "ll1" << "rl1" << "hd.armor24"
                          << "lh1.armor24" << "rh1.armor24"
                          << "ll1.armor24" << "rl1.armor24"
                          << "l_shell.armor24" << "r_shell.armor24";
    request.assemblyParents.insert("hd.armor24", "hd");
    request.assemblyParents.insert("lh1.armor24", "lh1");
    request.assemblyParents.insert("rh1.armor24", "rh1");
    request.assemblyParents.insert("ll1.armor24", "ll1");
    request.assemblyParents.insert("rl1.armor24", "rl1");
    request.assemblyParents.insert("l_shell.armor24", "ll1.armor24");
    request.assemblyParents.insert("r_shell.armor24", "rl1.armor24");
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    const QStringList components = jsonStrings(output, "components");
    QVERIFY(components.contains("ll1.armor24"));
    QVERIFY(components.contains("rl1.armor24"));
    QVERIFY(components.contains("l_shell.armor24"));
    QVERIFY(components.contains("r_shell.armor24"));
    QVERIFY(!components.contains("hd.armor24"));
    QVERIFY(!components.contains("lh1.armor24"));
    QVERIFY(!components.contains("rh1.armor24"));
    const QStringList replaced = jsonStrings(output, "replacedParts");
    QVERIFY(replaced.contains("ll1"));
    QVERIFY(replaced.contains("rl1"));
}

void ProfileVisualProviderTests::jmvMissingTopOverlapRetainsLowerArmor()
{
    auto db = database();
    ei::SItemDatabaseRecord shirt = item("shirt", "armor", 3, 4);
    shirt.type = "shirt";
    ei::SItemDatabaseRecord gloves = item("gloves", "armor", 6, 5);
    gloves.type = "gloves";
    ei::SItemDatabaseRecord plate = item("plate", "armor", 1, 8);
    plate.type = "plate";
    ei::SItemDatabaseRecord pants = item("pants", "armor", 4, 7);
    pants.type = "pants";
    ei::SItemDatabaseRecord leggings = item("leggings", "armor", 2, 9);
    leggings.type = "leggings";
    db->items.insert("shirt", shirt);
    db->items.insert("gloves", gloves);
    db->items.insert("plate", plate);
    db->items.insert("pants", pants);
    db->items.insert("leggings", leggings);

    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", db);
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    request.armor << "plate.leather" << "leggings.leather"
                  << "shirt.leather" << "gloves.leather" << "pants.leather";
    request.assemblyParts << "bd" << "lh1" << "rh1"
                          << "lh2" << "lh3" << "rh2" << "rh3"
                          << "hp" << "ll1" << "rl1"
                          << "bd.armor08" << "hp.armor09";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    const QStringList components = jsonStrings(output, "components");
    QVERIFY(components.contains("bd.armor08"));
    QVERIFY(components.contains("lh1"));
    QVERIFY(components.contains("rh1"));
    QVERIFY(components.contains("lh2"));
    QVERIFY(components.contains("lh3"));
    QVERIFY(components.contains("rh2"));
    QVERIFY(components.contains("rh3"));
    QVERIFY(components.contains("ll1"));
    QVERIFY(components.contains("rl1"));
    QVERIFY(jsonStrings(output, "placeholders").isEmpty());
}

void ProfileVisualProviderTests::jmvMissingExactTopPartsAreNotSynthesized()
{
    auto db = database();
    ei::SItemDatabaseRecord plate = item("plate", "armor", 1, 8);
    plate.type = "plate";
    ei::SItemDatabaseRecord leggings = item("leggings", "armor", 2, 9);
    leggings.type = "leggings";
    db->items.insert("plate", plate);
    db->items.insert("leggings", leggings);

    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", db);
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    request.armor << "plate.leather" << "leggings.leather";
    request.assemblyParts << "bd" << "hp" << "lh1" << "rh1" << "ll1" << "rl1";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QVERIFY(jsonStrings(output, "components").isEmpty());
    QVERIFY(jsonStrings(output, "placeholders").isEmpty());
    QCOMPARE(jsonStrings(output, "bodyTextureLayers"), QStringList({
        "unhumapl_08.le.0.x4.mmp", "unhumalg_09.le.0.x4.mmp"}));
}

void ProfileVisualProviderTests::baseArmorKeepsDisjointSlots()
{
    auto db = database();
    ei::SItemDatabaseRecord gloves = item("gloves", "armor", 6, 5);
    gloves.type = "gloves";
    ei::SItemDatabaseRecord plate = item("plate", "armor", 1, 8);
    plate.type = "plate";
    db->items.insert("gloves", gloves);
    db->items.insert("plate", plate);

    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("base.standard.v1", db);
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    request.armor << "plate.leather" << "gloves.leather";
    request.assemblyParts << "bd.armor08" << "lh2" << "rh2"
                          << "lh2.armor08" << "rh2.armor08";
    QJsonObject output;
    QVERIFY(!resolver->resolve(request, &output).success); // lh3/rh3 glove members are absent.
    const QStringList components = jsonStrings(output, "components");
    QVERIFY(components.contains("bd.armor08"));
    QVERIFY(components.contains("lh2"));
    QVERIFY(components.contains("rh2"));
    QVERIFY(!components.contains("lh2.armor08"));
    QVERIFY(!components.contains("rh2.armor08"));
}

void ProfileVisualProviderTests::zeroTtiArmorTexturesBaseParts()
{
    auto db = database();
    ei::SItemDatabaseRecord thin = item("thin shirt", "armor", 3, 0);
    thin.textureType2 = 0;
    db->items.insert("thin shirt", thin);
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", db);
    SEquipmentLayoutRequest request;
    request.model = "unhufe";
    request.armor << "thin shirt.leather";
    request.assemblyParts << "bd" << "lh1" << "rh1"
                          << "bd.armor01" << "lh1.armor01" << "rh1.armor01";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "components"), QStringList({"bd", "lh1", "rh1"}));
    QVERIFY(jsonStrings(output, "replacedParts").isEmpty());
    QCOMPARE(jsonStrings(output, "bodyTextureLayers"),
             QStringList({"unhumash_00.le.0.x4.mmp"}));
}

void ProfileVisualProviderTests::missingArmorGeometryPreservesBaseParts()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhufe"; request.armor << "shirt.leather";
    request.assemblyParts << "bd" << "lh1";
    QJsonObject output;
    QVERIFY(!resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "components"), QStringList({"bd", "lh1"}));
    QVERIFY(jsonStrings(output, "replacedParts").isEmpty());
    QVERIFY(jsonStrings(output, "placeholders").contains("rh1"));
}

void ProfileVisualProviderTests::zombieEquipmentUsesSharedHumanoidAtlas()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhuzf"; request.armor << "shirt.leather";
    request.assemblyParts << "bd" << "lh1" << "rh1";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "bodyTextureLayers"),
             QStringList({"unhumash_12.le.0.x4.mmp"}));
}

void ProfileVisualProviderTests::helmetAddsDatabaseGeometryAndAtlas()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unorfe";
    request.armor << "helmet.iron";
    request.assemblyParts << "hd" << "hd.armor08";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "components"), QStringList({"hd.armor08"}));
    QVERIFY(jsonStrings(output, "replacedParts").isEmpty());
    QVERIFY(!output.contains("hidesHair"));
    QCOMPARE(output.value("componentTextures").toObject().value("hd.armor08").toString(),
             QString("unhumahl_08.ir.0.x2.mmp"));
}

void ProfileVisualProviderTests::helmetHairFallbackMatchesSerializedSelection()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());

    SEquipmentLayoutRequest rootOnly;
    rootOnly.model = "unorfe";
    rootOnly.armor << "helmet.iron";
    rootOnly.assemblyParts << "hd" << "hr.01" << "hd.armor08";
    rootOnly.bodyParts << "hd" << "hr";
    rootOnly.hairIndex = 1;
    QJsonObject rootOnlyOutput;
    QVERIFY(resolver->resolve(rootOnly, &rootOnlyOutput).success);
    QCOMPARE(jsonStrings(rootOnlyOutput, "components"),
             QStringList({"hd.armor08", "hr.01"}));
    QVERIFY(jsonStrings(rootOnlyOutput, "replacedParts").isEmpty());

    SEquipmentLayoutRequest exactHair = rootOnly;
    exactHair.bodyParts = QStringList() << "hd" << "hr.01";
    QJsonObject exactHairOutput;
    QVERIFY(resolver->resolve(exactHair, &exactHairOutput).success);
    QCOMPARE(jsonStrings(exactHairOutput, "components"),
             QStringList({"hd.armor08"}));
    QCOMPARE(jsonStrings(exactHairOutput, "replacedParts"),
             QStringList({"hr.01"}));

    SEquipmentLayoutRequest allPartsEnabled = rootOnly;
    allPartsEnabled.bodyParts.clear();
    QJsonObject allPartsEnabledOutput;
    QVERIFY(resolver->resolve(allPartsEnabled, &allPartsEnabledOutput).success);
    QCOMPARE(jsonStrings(allPartsEnabledOutput, "components"),
             QStringList({"hd.armor08"}));
    QCOMPARE(jsonStrings(allPartsEnabledOutput, "replacedParts"),
             QStringList({"hr.01"}));

    SEquipmentLayoutRequest legacyHuman = rootOnly;
    legacyHuman.model = "unhuma";
    QJsonObject legacyHumanOutput;
    QVERIFY(resolver->resolve(legacyHuman, &legacyHumanOutput).success);
    QCOMPARE(jsonStrings(legacyHumanOutput, "components"),
             QStringList({"hd.armor08"}));
    QCOMPARE(jsonStrings(legacyHumanOutput, "replacedParts"),
             QStringList({"hr.01"}));

    SEquipmentLayoutRequest equippedUndead = rootOnly;
    equippedUndead.model = "unmoud";
    equippedUndead.bodyParts = QStringList() << "hd";
    QJsonObject equippedUndeadOutput;
    QVERIFY(resolver->resolve(equippedUndead, &equippedUndeadOutput).success);
    QCOMPARE(jsonStrings(equippedUndeadOutput, "components"),
             QStringList({"hd.armor08", "hr.01"}));
    QVERIFY(jsonStrings(equippedUndeadOutput, "replacedParts").isEmpty());

    SEquipmentLayoutRequest authoredReplacement = rootOnly;
    authoredReplacement.assemblyParts << "hr.01.armor08";
    QJsonObject replacementOutput;
    QVERIFY(resolver->resolve(authoredReplacement, &replacementOutput).success);
    QCOMPARE(jsonStrings(replacementOutput, "components"),
             QStringList({"hd.armor08", "hr.01.armor08"}));
    QCOMPARE(jsonStrings(replacementOutput, "replacedParts"),
             QStringList({"hr.01"}));
}

void ProfileVisualProviderTests::serializedHairVariantsRemainLoadedThroughFallback()
{
    const QStringList assembly = {"hd", "hr.01", "hr.02", "hd.armor05"};
    QCOMPARE(ei::serializedAssemblyHairParts({"hd", "hr.01"}, assembly),
             QStringList({"hr.01"}));
    const ei::SHelmetHairRuntimeDecision fallback = ei::helmetHairRuntimeDecision(
        "unhuma", {"hd", "hr.01"}, assembly, 2, 5);
    QVERIFY(fallback.fallbackLoadsNativeHair);
    QCOMPARE(fallback.nativeHairPart, QString("hr.02"));
    QCOMPARE(ei::serializedAssemblyHairParts({}, assembly),
             QStringList({"hr.01", "hr.02"}));
}

void ProfileVisualProviderTests::jmvRecoversUnambiguousConcatenatedHelmetAndPlate()
{
    auto db = database();
    ei::SItemDatabaseRecord helmet = item("mlh", "armor", 0, 26);
    helmet.type = "helm";
    helmet.materialType = "Leather";
    ei::SItemDatabaseRecord plate = item("mlp", "armor", 1, 26);
    plate.type = "plate";
    db->items.insert(helmet.name, helmet);
    db->items.insert(plate.name, plate);
    ei::SMaterialDatabaseRecord thin;
    thin.name = "tanned thin";
    thin.type = "Leather";
    thin.code = "bl";
    db->materialRecords.insert(thin.name, thin);

    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", db);
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    request.armor << "mlh.tanned thin.mlp.tanned thin";
    request.assemblyParts << "hd" << "bd" << "hd.armor26" << "bd.armor26";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    const QStringList components = jsonStrings(output, "components");
    QVERIFY(components.contains("hd.armor26"));
    QVERIFY(components.contains("bd.armor26"));
    QCOMPARE(output.value("componentTextures").toObject().value("hd.armor26").toString(),
             QString("unhumahl_26.bl.0.x2.mmp"));
    QVERIFY(jsonStrings(output, "diagnostics").join(' ').contains("concatenated"));
}

void ProfileVisualProviderTests::weaponAtlasesUseLiteralTtiAndEstablishedFamilies()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    request.weapons << "sword.iron" << "shield.iron";
    request.assemblyParts << "rh3.sword01" << "lh2.shield04";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    const QJsonObject textures = output.value("componentTextures").toObject();
    QCOMPARE(textures.value("rh3.sword01").toString(), QString("unhumasw_01.ir.0.x2.mmp"));
    QCOMPARE(textures.value("lh2.shield04").toString(), QString("unhumasd_04.ir.0.x2.mmp"));
}

void ProfileVisualProviderTests::weaponGeometryDoesNotReplaceHandsOrGloves()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    request.weapons << "sword.iron";
    request.assemblyParts << "rh3" << "rh3.armor05" << "baserh3"
                          << "basesword01" << "l.basesword01" << "rh3.sword01";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "components"), QStringList({"rh3.sword01"}));
    QVERIFY(jsonStrings(output, "replacedParts").isEmpty());
}

void ProfileVisualProviderTests::equippedMaterialsRemainProfileExtensible()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unorfe";
    request.armor << "helmet.silver";
    request.assemblyParts << "hd" << "hd.armor08";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("componentTextures").toObject().value("hd.armor08").toString(),
             QString("unhumahl_08.sv.0.x2.mmp"));
}

void ProfileVisualProviderTests::magicEquipmentCarriesWhiteEmission()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    request.weapons << "sword.magic";
    request.assemblyParts << "rh3.sword01";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "emissiveParts"), QStringList({"rh3.sword01"}));
}

void ProfileVisualProviderTests::jmvTwoHandWeaponsUseDedicatedAtlasFamilies()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    const struct Test { const char* reference; const char* component; const char* texture; } tests[] = {
        {"great sword.iron", "rh3.dsword01", "unhumats_01.ir.0.x2.mmp"},
        {"great pike.iron", "rh3.dpike02", "unhumatp_02.ir.0.x2.mmp"},
        {"great hammer.iron", "rh3.dclub03", "unhumath_03.ir.0.x2.mmp"},
        {"great staff.iron", "rh3.dstaff01", "unhumatf_01.ir.0.x2.mmp"}
    };
    for(const Test& test : tests)
    {
        SEquipmentLayoutRequest request;
        request.model = "unhuma";
        request.weapons << test.reference;
        request.assemblyParts << test.component;
        QJsonObject output;
        QVERIFY(resolver->resolve(request, &output).success);
        QCOMPARE(output.value("componentTextures").toObject().value(test.component).toString(),
                 QString(test.texture));
    }
}

void ProfileVisualProviderTests::resolvesCompoundBowGeometry()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    request.weapons << "bow.iron";
    request.assemblyParts << "bwparta03" << "lh3.bwpartb03"
                          << "bwtetivaa03" << "bwtetivab03" << "quiver" << "arrows";
    QJsonObject output;
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(jsonStrings(output, "components"), request.assemblyParts);
    const QJsonObject textures = output.value("componentTextures").toObject();
    for(const QString& component : request.assemblyParts)
        QCOMPARE(textures.value(component).toString(), QString("unhumabw_03.ir.0.x2.mmp"));
}

void ProfileVisualProviderTests::jmvIgnoresWeaponsBeyondFirstPair()
{
    ProfileVisualProviderService service;
    const auto resolver = service.equipmentResolver("jmv.v1", database());
    SEquipmentLayoutRequest request;
    request.model = "unhuma";
    request.weapons << "sword.iron" << "axe.iron" << "missing.invalid";
    request.assemblyParts << "rh3.sword01" << "lh3.axe02";
    QJsonObject output;
    resolver->resolve(request, &output);
    QCOMPARE(jsonStrings(output, "components"), QStringList({"rh3.sword01", "lh3.axe02"}));
    for(const QString& placeholder : jsonStrings(output, "placeholders"))
        QVERIFY(!placeholder.contains("missing"));
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
