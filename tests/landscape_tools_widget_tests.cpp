#include <QtTest>

#include <QLineEdit>
#include <QLabel>
#include <QListWidget>

#include "landscape_tools.h"

class LandscapeToolsWidgetTests : public QObject
{
    Q_OBJECT

private slots:
    void manualPaletteDoesNotAllocateRowsForEmptyAtlasHoles();
};

void LandscapeToolsWidgetTests::manualPaletteDoesNotAllocateRowsForEmptyAtlasHoles()
{
    CLandscapeTools tools;
    tools.resize(730, 1500);
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Manual);
    tools.show();
    QCoreApplication::processEvents();

    QVector<QImage> tiles(619);
    QVector<qint32> runtimeTypes(619, 1);
    QStringList descriptions;
    for(int slot = 0; slot < tiles.size(); ++slot)
        descriptions.append(QString());
    descriptions[64] = QStringLiteral("unique-after-hole");
    for(int slot = 0; slot < tiles.size(); ++slot)
        if(slot < 12 || slot >= 64)
        {
            tiles[slot] = QImage(16, 16, QImage::Format_RGBA8888);
            tiles[slot].fill(QColor::fromHsv(slot % 360, 255, 180));
        }

    tools.setManualAtlasTiles(tiles, runtimeTypes, descriptions);
    QCoreApplication::processEvents();

    QListWidget* list = tools.findChild<QListWidget*>(
        QStringLiteral("manualAtlasTileList"));
    QVERIFY(list);
    QCOMPARE(list->count(), 567);
    QCOMPARE(list->item(11)->data(Qt::UserRole).toInt(), 11);
    QCOMPARE(list->item(12)->data(Qt::UserRole).toInt(), 64);

    QLabel* status = tools.findChild<QLabel*>(
        QStringLiteral("terrainPaintTilesetStatus"));
    QVERIFY(status);
    QVERIFY2(status->text().contains(QStringLiteral("619 physical")),
             qPrintable(status->text()));

    const QRect lastBeforeHole = list->visualItemRect(list->item(11));
    const QRect firstAfterHole = list->visualItemRect(list->item(12));
    QVERIFY(lastBeforeHole.isValid());
    QVERIFY(firstAfterHole.isValid());
    QVERIFY2(firstAfterHole.top() - lastBeforeHole.top()
                 <= list->gridSize().height(),
             "An empty atlas-page hole allocated blank palette rows");

    QVERIFY(tools.selectManualAtlasTile(64));
    QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(), 64);

    QLineEdit* filter = tools.findChild<QLineEdit*>(
        QStringLiteral("terrainPaintFilter"));
    QVERIFY(filter);
    filter->setText(QStringLiteral("unique after hole"));
    QCoreApplication::processEvents();
    int visibleItems = 0;
    for(int row = 0; row < list->count(); ++row)
        visibleItems += !list->item(row)->isHidden();
    QCOMPARE(visibleItems, 1);
    QCOMPARE(list->item(12)->data(Qt::UserRole).toInt(), 64);
}

QTEST_MAIN(LandscapeToolsWidgetTests)
#include "landscape_tools_widget_tests.moc"
