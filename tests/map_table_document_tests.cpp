#include <QtTest>

#include "map_table_document.h"

class MapTableDocumentTests : public QObject
{
    Q_OBJECT

private slots:
    void noOpIsByteIdentical();
    void parsesMapsAndEntranceMetadata();
    void preservesUnknownZoneText();
    void assignsSeparatorHeadingToFollowingZone();
    void editsRawZoneSpanWithoutTouchingNeighbours();
    void validatesUnsafeMetadata();
    void rejectsUnknownZoneTypeAcrossDocument();
    void rejectsGameRegistrationWithoutResources();
    void editsOnlyRequestedKnownLines();
    void editsEntranceWithoutRewritingUnknownText();
    void appendsExplicitRegistrationBeforeGlobalQuests();
};

void MapTableDocumentTests::noOpIsByteIdentical()
{
    const QByteArray bytes =
        "## preface\r\n"
        "#zone gz1g gipat game\r\n"
        "#res\r\nzone1 zone1\r\n"
        "#maps\r\n11 9\r\nzone1map zone1quest\r\n"
        "#unknown\r\nauthored bytes\r\n";
    CMapTableDocument document;
    QStringList errors;
    QVERIFY(document.loadBytes(bytes, &errors));
    QVERIFY(errors.isEmpty());
    QCOMPARE(document.serialize(), bytes);
    QVERIFY(!document.isDirty());
}

void MapTableDocumentTests::parsesMapsAndEntranceMetadata()
{
    const QByteArray bytes =
        "#zone gz1g gipat game\n"
        "#res\nzone1 zone1\n"
        "#maps\n11 9\nzone1map zone1quest\n"
        "#exit 2\nbz1g 1\n"
        "#deploy\n184 101 190 108\n"
        "#remove\n181 108 193 120\n"
        "#passtime\n35\n"
        "#view\n135.2\n"
        "#deployangle\n90\n";
    CMapTableDocument document;
    QVERIFY(document.loadBytes(bytes));
    QCOMPARE(document.zones().size(), 1);
    const SMapTableZone& zone = document.zones().first();
    QCOMPARE(zone.zoneId, QString("gz1g"));
    QCOMPARE(zone.mprName, QString("zone1"));
    QVERIFY(zone.maps.present);
    QCOMPARE(zone.maps.sectorWidth, 11);
    QCOMPARE(zone.maps.sectorHeight, 9);
    QCOMPARE(zone.maps.minimapTexture, QString("zone1map"));
    QCOMPARE(zone.maps.objectivesTexture, QString("zone1quest"));
    QCOMPARE(zone.exits.size(), 1);
    const SMapTableExit& exit = zone.exits.first();
    QCOMPARE(exit.exitId, 2);
    QCOMPARE(exit.destinationZoneId, QString("bz1g"));
    QCOMPARE(exit.destinationExitId, 1);
    QVERIFY(exit.hasDeployRectangle);
    QCOMPARE(exit.deployRectangle, QRectF(QPointF(184, 101), QPointF(190, 108)));
    QVERIFY(exit.hasRemoveRectangle);
    QVERIFY(exit.hasPassTime);
    QCOMPARE(exit.passTime, 35.0);
    QVERIFY(exit.hasViewAngle);
    QCOMPARE(exit.viewAngle, 135.2);
    QVERIFY(exit.hasDeployAngle);
    QCOMPARE(exit.deployAngle, 90.0);
}

void MapTableDocumentTests::preservesUnknownZoneText()
{
    const QByteArray bytes =
        "#zone custom island game\r\n"
        "#weather\r\nsnow\r\n"
        "#allod\r\nprofile-specific authored bytes\r\n"
        "## authored comment\r\n"
        "#mystery\r\n01 02 03\r\n"
        "#quest quest_after_zone\r\n"
        "1 2 3\r\n";
    CMapTableDocument document;
    QVERIFY(document.loadBytes(bytes));
    QCOMPARE(document.zones().size(), 1);
    QVERIFY(document.zones().first().rawBlock.contains("#mystery"));
    QVERIFY(document.zones().first().rawBlock.contains("#allod"));
    QVERIFY(!document.zones().first().rawBlock.contains("#quest"));
    QCOMPARE(document.serialize(), bytes);
}

void MapTableDocumentTests::assignsSeparatorHeadingToFollowingZone()
{
    const QByteArray bytes =
        "################################################\r\n"
        "\r\n"
        "## Base on Gipat\r\n"
        "#zone bz1g gipat brief\r\n"
        "#res\r\nbz1g bz1g\r\n"
        "#exit 1\r\ngz1g 1\r\n"
        "## #remove\r\n"
        "## 322 0 469 139\r\n"
        "\r\n"
        "################################################\r\n"
        "\r\n"
        "## WAY FROM BASE TO Ruins\r\n"
        "#zone gz1g_bz1g gipat edge\r\n"
        "#position\r\n-2.749 -0.277 0.235\r\n"
        "#exit 1\r\ngz1g 2\r\n";
    CMapTableDocument document;
    QVERIFY(document.loadBytes(bytes));
    QCOMPARE(document.zones().size(), 2);
    QVERIFY(document.zones().at(0).rawBlock.startsWith("################################################\r\n\r\n## Base on Gipat"));
    QVERIFY(document.zones().at(0).rawBlock.contains("## #remove\r\n## 322 0 469 139"));
    QVERIFY(!document.zones().at(0).rawBlock.contains("WAY FROM BASE"));
    QVERIFY(document.zones().at(1).rawBlock.startsWith(
        "################################################\r\n\r\n## WAY FROM BASE TO Ruins"));
    QVERIFY(!document.zones().at(1).rawBlock.contains("## #remove"));
    QCOMPARE(document.serialize(), bytes);
    QVERIFY(!document.isDirty());
}

void MapTableDocumentTests::editsRawZoneSpanWithoutTouchingNeighbours()
{
    const QByteArray bytes =
        "################################################\r\n"
        "## First heading\r\n"
        "#zone first island brief\r\n"
        "#res\r\nfirst first\r\n"
        "################################################\r\n"
        "## Second heading\r\n"
        "#zone second island edge\r\n"
        "#position\r\n1 2 3\r\n";
    CMapTableDocument document;
    QVERIFY(document.loadBytes(bytes));
    const QByteArray secondBefore = document.zones().at(1).rawBlock.toLocal8Bit();
    QString edited = document.zones().at(0).rawBlock;
    edited.replace("## First heading", "## First heading\n## Authored note");
    const SMapTableEditResult result = document.replaceRawZoneSpan("first", edited);
    QVERIFY2(result.success, qPrintable(result.toText()));
    QVERIFY(document.zones().at(0).rawBlock.contains("## Authored note\r\n#zone first"));
    QCOMPARE(document.zones().at(1).rawBlock.toLocal8Bit(), secondBefore);
    QVERIFY(document.serialize().contains(
        "## First heading\r\n## Authored note\r\n#zone first"));

    const QByteArray afterEdit = document.serialize();
    const SMapTableEditResult noOp = document.replaceRawZoneSpan(
        "first", document.zones().at(0).rawBlock);
    QVERIFY(noOp.success);
    QVERIFY(noOp.changedFields.isEmpty());
    QCOMPARE(document.serialize(), afterEdit);
}

void MapTableDocumentTests::rejectsUnknownZoneTypeAcrossDocument()
{
    const QByteArray bytes =
        "#zone valid island game\r\n"
        "#res\r\nvalid valid\r\n"
        "#zone invalid island arbitrary\r\n";
    CMapTableDocument document;
    QVERIFY(document.loadBytes(bytes));
    const SMapTableEditResult result = document.validateAllZones();
    QVERIFY(!result.success);
    QVERIFY(result.toText().contains("invalid: Zone type must be game, brief, or edge."));
    QCOMPARE(document.serialize(), bytes);
}

void MapTableDocumentTests::validatesUnsafeMetadata()
{
    SMapTableZone zone;
    zone.zoneId = "bad zone";
    zone.allodId = "island";
    zone.zoneType = "game";
    zone.maps.present = true;
    zone.maps.sectorWidth = 0;
    zone.maps.sectorHeight = 9;
    zone.maps.minimapTexture = "";
    zone.maps.objectivesTexture = "quest";
    SMapTableExit exit;
    exit.exitId = 1;
    exit.destinationZoneId = "none";
    zone.exits.append(exit);
    zone.exits.append(exit);

    const SMapTableEditResult result = CMapTableDocument::validateZone(zone);
    QVERIFY(!result.success);
    QVERIFY(result.errors.size() >= 3);
}

void MapTableDocumentTests::rejectsGameRegistrationWithoutResources()
{
    SMapTableZone game;
    game.zoneId = "custom";
    game.allodId = "gipat";
    game.zoneType = "game";
    const SMapTableEditResult missing = CMapTableDocument::validateZone(game);
    QVERIFY(!missing.success);
    QVERIFY(missing.errors.contains("Game zones require both MPR and MOB resource names."));

    game.mprName = "custom_land";
    game.mobName = "custom_objects";
    QVERIFY(CMapTableDocument::validateZone(game).success);

    SMapTableZone edge;
    edge.zoneId = "custom_edge";
    edge.allodId = "gipat";
    edge.zoneType = "edge";
    QVERIFY(CMapTableDocument::validateZone(edge).success);
}

void MapTableDocumentTests::editsOnlyRequestedKnownLines()
{
    const QByteArray bytes =
        "## authored preface\r\n"
        "#zone gz1g gipat game\r\n"
        "#res\r\nzone1 zone1\r\n"
        "#maps\r\n11 9\r\nzone1map zone1quest\r\n"
        "#mystery\r\nkeep these exact bytes\r\n";
    CMapTableDocument document;
    QVERIFY(document.loadBytes(bytes));
    SMapTableZone edited = document.zones().first();
    edited.maps.minimapTexture = "zone1testmap";
    const SMapTableEditResult result = document.replaceZone("gz1g", edited);
    QVERIFY2(result.success, qPrintable(result.toText()));
    const QByteArray expected =
        "## authored preface\r\n"
        "#zone gz1g gipat game\r\n"
        "#res\r\nzone1 zone1\r\n"
        "#maps\r\n11 9\r\nzone1testmap zone1quest\r\n"
        "#mystery\r\nkeep these exact bytes\r\n";
    QCOMPARE(document.serialize(), expected);
    QVERIFY(document.isDirty());

    const QByteArray changed = document.serialize();
    const SMapTableEditResult noOp = document.replaceZone("gz1g", document.zones().first());
    QVERIFY(noOp.success);
    QCOMPARE(document.serialize(), changed);
}

void MapTableDocumentTests::editsEntranceWithoutRewritingUnknownText()
{
    const QByteArray bytes =
        "#zone gz1g gipat game\n"
        "#res\nzone1 zone1\n"
        "#exit 2\n"
        "bz1g 1\n"
        "## authored entrance note\n"
        "#deploy\n184 101 190 108\n"
        "#extension\nopaque entrance value\n"
        "#view\n135.2\n";
    CMapTableDocument document;
    QVERIFY(document.loadBytes(bytes));
    SMapTableZone edited = document.zones().first();
    edited.exits[0].viewAngle = 90.0;
    const SMapTableEditResult result = document.replaceZone("gz1g", edited);
    QVERIFY2(result.success, qPrintable(result.toText()));
    const QByteArray output = document.serialize();
    QVERIFY(output.contains("## authored entrance note\n"));
    QVERIFY(output.contains("#extension\nopaque entrance value\n"));
    QVERIFY(output.contains("#view\n90\n"));
    QVERIFY(output.contains("#deploy\n184 101 190 108\n"));
}

void MapTableDocumentTests::appendsExplicitRegistrationBeforeGlobalQuests()
{
    const QByteArray bytes =
        "#zone existing island game\r\n"
        "#res\r\nold old\r\n"
        "#quest global_quest\r\n"
        "opaque global bytes\r\n";
    CMapTableDocument document;
    QVERIFY(document.loadBytes(bytes));
    SMapTableZone zone;
    zone.zoneId = "custom";
    zone.allodId = "island";
    zone.zoneType = "game";
    zone.mprName = "custom";
    zone.mobName = "custom";
    zone.maps.present = true;
    zone.maps.sectorWidth = 8;
    zone.maps.sectorHeight = 6;
    zone.maps.minimapTexture = "custommap";
    zone.maps.objectivesTexture = "customquest";
    const SMapTableEditResult result = document.appendZone(zone);
    QVERIFY2(result.success, qPrintable(result.toText()));
    const QByteArray output = document.serialize();
    const int zoneOffset = output.indexOf("#zone custom island game");
    const int questOffset = output.indexOf("#quest global_quest");
    QVERIFY(zoneOffset > 0);
    QVERIFY(questOffset > zoneOffset);
    QVERIFY(output.endsWith("#quest global_quest\r\nopaque global bytes\r\n"));
    QCOMPARE(document.zones().size(), 2);
}

QTEST_APPLESS_MAIN(MapTableDocumentTests)
#include "map_table_document_tests.moc"
