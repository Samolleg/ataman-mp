#include <QtTest>

#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QSignalSpy>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>

#include "loaded_map_panel_policy.h"
#include "landscape_tools.h"
#include "types.h"

class LandscapeToolsWidgetTests : public QObject
{
    Q_OBJECT

private slots:
    void manualPaletteDoesNotAllocateRowsForEmptyAtlasHoles();
    void manualPaletteFiltersNonNullPaddingAndRestoresPhysicalIdentity();
    void terrainPaletteBatchDefersIntermediateWidgetRebuilds();
    void deferredTerrainThumbnailRefreshesWithoutInitialsFallback();
    void manualPaletteIsRequestedOnlyWhenItsTabIsOpened();
    void paintHelpLivesOnRelevantControls();
    void overlaySurfaceModeLocksTileTargetAndOverlayLayer();
    void overlayModesExposeMprMaterialsShortcut();
    void overlayMaterialDropdownRefreshesImmediately();
    void loadedMapRefreshWaitsForTreeItemMutationToReturn();
};

void LandscapeToolsWidgetTests::loadedMapRefreshWaitsForTreeItemMutationToReturn()
{
    QTreeWidget tree;
    tree.setColumnCount(2);
    QTreeWidgetItem* original = new QTreeWidgetItem(&tree);
    original->setFlags(original->flags() | Qt::ItemIsUserCheckable);
    original->setCheckState(1, Qt::Checked);

    bool refreshPending = false;
    int rebuildCount = 0;
    QObject::connect(&tree, &QTreeWidget::itemChanged, &tree,
        [&](QTreeWidgetItem* item, int column) {
        if(item != original || column != 1)
            return;
        QVERIFY(scheduleLoadedMapPanelRefresh(&tree, &refreshPending, [&]() {
            ++rebuildCount;
            tree.clear();
            QTreeWidgetItem* replacement = new QTreeWidgetItem(&tree);
            replacement->setText(0, QStringLiteral("rebuilt"));
        }));
        QVERIFY(!scheduleLoadedMapPanelRefresh(&tree, &refreshPending, [&]() {
            ++rebuildCount;
        }));
        // The originating QTreeWidgetItem::setData call is still on the stack.
        // A synchronous clear here was the observed use-after-free.
        QCOMPARE(tree.topLevelItem(0), original);
    });

    original->setCheckState(1, Qt::Unchecked);
    QCOMPARE(rebuildCount, 0);
    QCOMPARE(tree.topLevelItem(0), original);
    QVERIFY(refreshPending);

    QCoreApplication::processEvents();
    QCOMPARE(rebuildCount, 1);
    QCOMPARE(tree.topLevelItemCount(), 1);
    QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("rebuilt"));
    QVERIFY(!refreshPending);
}

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
    QVERIFY2(status->text().contains(QStringLiteral("567 physical")),
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

void LandscapeToolsWidgetTests::manualPaletteFiltersNonNullPaddingAndRestoresPhysicalIdentity()
{
    CLandscapeTools tools;
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Manual);

    QVector<QImage> tiles;
    QVector<qint32> runtimeTypes;
    QVector<QByteArray> identities;
    for(int slot = 0; slot < 64; ++slot)
    {
        QImage image(16, 16, QImage::Format_RGBA8888);
        image.fill(QColor::fromHsv(slot * 5, 255, 180));
        tiles.append(image);
        runtimeTypes.append(1);
        if(slot < 61)
            identities.append(QByteArray("physical-pixels-")
                              + QByteArray::number(slot) + QByteArray("/type-1"));
        else
            identities.append(QByteArray());
    }

    tools.setManualAtlasTiles(tiles, runtimeTypes, {}, identities);
    QListWidget* list = tools.findChild<QListWidget*>(
        QStringLiteral("manualAtlasTileList"));
    QVERIFY(list);
    QCOMPARE(list->count(), 61);
    QVERIFY(!tools.selectManualAtlasTile(61));
    QVERIFY(tools.selectManualAtlasTile(13));

    QVector<QByteArray> duplicateIdentities = identities;
    duplicateIdentities[5] = identities.at(13);
    tools.setManualAtlasTiles(tiles, runtimeTypes, {}, duplicateIdentities);
    QVERIFY(list->currentItem());
    QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(), 13);

    QVector<QByteArray> refreshedIdentities = identities;
    refreshedIdentities[13] = QByteArray("replacement-pixels/type-1");
    refreshedIdentities[21] = identities.at(13);
    tools.setManualAtlasTiles(tiles, runtimeTypes, {}, refreshedIdentities);

    QVERIFY(list->currentItem());
    QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(), 21);
    QLabel* status = tools.findChild<QLabel*>(
        QStringLiteral("terrainPaintTilesetStatus"));
    QVERIFY(status);
    QVERIFY2(status->text().contains(QStringLiteral("61 physical")),
             qPrintable(status->text()));
}

void LandscapeToolsWidgetTests::terrainPaletteBatchDefersIntermediateWidgetRebuilds()
{
    CLandscapeTools tools;
    const auto choice = [](qint32 id, const QString& name) {
        STerrainPaintMaterialChoice result;
        result.id = id;
        result.name = name;
        result.familyName = QStringLiteral("Ground");
        result.thumbnail = QImage(8, 8, QImage::Format_RGBA8888);
        result.thumbnail.fill(QColor::fromHsv((id * 23) % 360, 255, 180));
        return result;
    };
    const auto buttonFor = [&tools](qint32 id) {
        for(QToolButton* button : tools.findChildren<QToolButton*>())
            if(button->property("terrainMaterialId").toInt() == id)
                return button;
        return static_cast<QToolButton*>(nullptr);
    };

    tools.setPaintMaterials({choice(101, QStringLiteral("Ground / Initial"))},
                            {}, {}, 1, 1);
    QPointer<QToolButton> initialButton = buttonFor(101);
    QVERIFY(initialButton);

    tools.beginTerrainPaletteUpdate();
    tools.setPaintMaterials({choice(102, QStringLiteral("Ground / Modern"))},
                            {}, {}, 1, 1);
    tools.setClassicAutoMaterials(
        {choice(103, QStringLiteral("Ground / Classic"))}, {}, {});
    tools.setUsedTerrainFamilies({102, 103}, {});

    QVERIFY2(initialButton,
             "A batched catalog update rebuilt the palette before commit");
    QVERIFY(!buttonFor(102));
    QVERIFY(!buttonFor(103));

    tools.endTerrainPaletteUpdate();
    QVERIFY(initialButton.isNull());
    QVERIFY(buttonFor(102));
    QVERIFY(buttonFor(103));
}

void LandscapeToolsWidgetTests::deferredTerrainThumbnailRefreshesWithoutInitialsFallback()
{
    CLandscapeTools tools;
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);

    STerrainPaintMaterialChoice pending;
    pending.id = 211;
    pending.name = QStringLiteral("Stone / Dark gray");
    pending.familyName = QStringLiteral("Stone");
    pending.thumbnailPending = true;
    tools.setPaintMaterials({pending}, {}, {}, 1, 1);

    const auto buttonFor = [&tools](qint32 id) {
        for(QToolButton* button : tools.findChildren<QToolButton*>())
            if(button->property("terrainMaterialId").toInt() == id)
                return button;
        return static_cast<QToolButton*>(nullptr);
    };
    QToolButton* loading = buttonFor(pending.id);
    QVERIFY(loading);
    QVERIFY(loading->icon().isNull());
    QCOMPARE(loading->text(), QString::fromUtf8("\xE2\x80\xA6"));
    QVERIFY2(!loading->text().contains(QStringLiteral("SD")),
             "A missing terrain preview fell back to material initials");
    loading->setChecked(true);

    STerrainPaintMaterialChoice hydrated = pending;
    hydrated.thumbnailPending = false;
    hydrated.thumbnail = QImage(16, 16, QImage::Format_RGBA8888);
    hydrated.thumbnail.fill(QColor(37, 51, 68));
    tools.setPaintMaterials({hydrated}, {}, {}, 1, 1);

    QToolButton* ready = buttonFor(hydrated.id);
    QVERIFY(ready);
    QVERIFY(!ready->icon().isNull());
    QVERIFY(ready->text().isEmpty());
    QVERIFY2(ready->isChecked(),
             "Publishing a deferred terrain thumbnail lost brush selection");
}

void LandscapeToolsWidgetTests::manualPaletteIsRequestedOnlyWhenItsTabIsOpened()
{
    CLandscapeTools tools;
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
    QSignalSpy requested(&tools,
                         &CLandscapeTools::manualAtlasPaletteRequested);

    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::ClassicAuto);
    QCOMPARE(requested.count(), 0);
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Manual);
    QCOMPARE(requested.count(), 1);
}

void LandscapeToolsWidgetTests::paintHelpLivesOnRelevantControls()
{
    CLandscapeTools tools;
    QTabWidget* tabs = tools.findChild<QTabWidget*>(
        QStringLiteral("materialPaletteTabs"));
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Modern (Z)"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Classic Auto (X)"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("Manual (C)"));
    for(int index = 0; index < tabs->count(); ++index)
        QVERIFY2(!tabs->tabToolTip(index).trimmed().isEmpty(),
                 "A terrain paint mode has no contextual tooltip");

    QPushButton* resolve = tools.findChild<QPushButton*>(
        QStringLiteral("resolveMissingBlendsButton"));
    QVERIFY(resolve);
    QCOMPARE(resolve->text(),
             QStringLiteral("Resolve missing blends... (Shift+R)"));
    QPushButton* saveDraft = tools.findChild<QPushButton*>(
        QStringLiteral("saveModernDraftButton"));
    QVERIFY(saveDraft);
    QCOMPARE(saveDraft->text(),
             QStringLiteral("Save MPR as modern draft..."));

    bool brushShortcutShown = false;
    for(QLabel* label : tools.findChildren<QLabel*>())
    {
        QVERIFY2(!label->text().startsWith(QStringLiteral(
                     "Z / X / C switches")),
                 "The removed terrain-paint prose block is still visible");
        if(label->text() == QStringLiteral("Brush size (Alt+Wheel)"))
            brushShortcutShown = true;
    }
    QVERIFY2(brushShortcutShown,
             "The brush-size shortcut is not shown beside its slider");
}

void LandscapeToolsWidgetTests::overlaySurfaceModeLocksTileTargetAndOverlayLayer()
{
    CLandscapeTools tools;
    tools.resize(730, 1500);
    tools.setEditingEnabled(true, false);

    QToolButton* tile = tools.findChild<QToolButton*>(
        QStringLiteral("tilePaintTarget"));
    QToolButton* vertex = tools.findChild<QToolButton*>(
        QStringLiteral("vertexPaintTarget"));
    QTabWidget* modernLayers = tools.findChild<QTabWidget*>(
        QStringLiteral("modernMaterialLayerTabs"));
    QTabWidget* classicLayers = tools.findChild<QTabWidget*>(
        QStringLiteral("classicMaterialLayerTabs"));
    QVERIFY(tile);
    QVERIFY(vertex);
    QVERIFY(modernLayers);
    QVERIFY(classicLayers);

    vertex->setChecked(true);
    QCOMPARE(tools.paintTargetMode(), ETerrainPaintTargetMode::Vertex);
    tools.activateMode(ELandscapeEditMode::OverlaySurface, false);

    QCOMPARE(tools.paintTargetMode(), ETerrainPaintTargetMode::Tile);
    QVERIFY(tile->isChecked());
    QVERIFY(!vertex->isEnabled());
    QCOMPARE(modernLayers->currentIndex(), 1);
    QCOMPARE(classicLayers->currentIndex(), 1);
    QVERIFY(!modernLayers->tabBar()->isEnabled());
    QVERIFY(!classicLayers->tabBar()->isEnabled());

    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    QCOMPARE(tools.paintTargetMode(), ETerrainPaintTargetMode::Vertex);
    QVERIFY(vertex->isEnabled());
    QVERIFY(modernLayers->tabBar()->isEnabled());
    QVERIFY(classicLayers->tabBar()->isEnabled());
}

void LandscapeToolsWidgetTests::overlayModesExposeMprMaterialsShortcut()
{
    CLandscapeTools tools;
    tools.resize(730, 1500);
    tools.setEditingEnabled(true, false);
    QPushButton* shortcut = tools.findChild<QPushButton*>(
        QStringLiteral("overlayMprMaterialsButton"));
    QVERIFY2(shortcut, "Overlay modes have no immediate MPR Materials action");
    QSignalSpy requested(&tools, SIGNAL(openMprMaterialsRequested()));

    tools.activateMode(ELandscapeEditMode::OverlaySurface, false);
    QVERIFY(shortcut->isVisibleTo(&tools));
    shortcut->click();
    QCOMPARE(requested.count(), 1);

    tools.activateMode(ELandscapeEditMode::OverlayPaint, false);
    QVERIFY(shortcut->isVisibleTo(&tools));
    shortcut->click();
    QCOMPARE(requested.count(), 2);

    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    QVERIFY(!shortcut->isVisibleTo(&tools));
}

void LandscapeToolsWidgetTests::overlayMaterialDropdownRefreshesImmediately()
{
    CLandscapeTools tools;
    QComboBox* dropdown = tools.findChild<QComboBox*>(
        QStringLiteral("overlaySurfaceMaterial"));
    QVERIFY(dropdown);
    SMaterial first{};
    first.type = eTerrainWater;
    tools.setOverlayMaterials({first});
    QCOMPARE(dropdown->count(), 1);
    QCOMPARE(dropdown->itemData(0).toInt(), 0);

    SMaterial appended = first;
    appended.R = 0.8f;
    tools.setOverlayMaterials({first, appended});
    QCOMPARE(dropdown->count(), 2);
    QCOMPARE(dropdown->itemData(1).toInt(), 1);
    QCOMPARE(tools.overlayMaterialIndex(), qint16(0));
}

QTEST_MAIN(LandscapeToolsWidgetTests)
#include "landscape_tools_widget_tests.moc"
