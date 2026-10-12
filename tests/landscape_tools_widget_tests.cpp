#include <QtTest>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QApplication>
#include <QStyle>
#include <QDir>
#include <QTranslator>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QGridLayout>
#include <QSlider>
#include <QVBoxLayout>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QOpenGLTextureBlitter>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include "../source/render/terrain_preview_context.h"

#include "loaded_map_panel_policy.h"
#include "landscape_tools.h"
#include "types.h"

class LandscapeToolsWidgetTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settingsDirectory;

private slots:
    void initTestCase()
    {
        QVERIFY(settingsDirectory.isValid());
        QCoreApplication::setOrganizationName("AtamanTests");
        QCoreApplication::setApplicationName("LandscapeTools");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDirectory.path());
    }
    void mobHeightOptionPersistsAndOnlyAppliesToGroundSculpt()
    {
        QSettings().remove("ui/landscape/moveMobObjects");
        CLandscapeTools tools;
        auto* box=tools.findChild<QCheckBox*>("landscapeMoveMobObjects");
        QVERIFY(box);QVERIFY(tools.moveMobObjectsWithTerrain());
        tools.activateMode(ELandscapeEditMode::Sculpt);
        QVERIFY(!box->isHidden());box->setChecked(false);
        QVERIFY(!tools.moveMobObjectsWithTerrain());
        CLandscapeTools restored;QVERIFY(!restored.moveMobObjectsWithTerrain());
        tools.activateMode(ELandscapeEditMode::OverlaySculpt);QVERIFY(box->isHidden());
        tools.activateMode(ELandscapeEditMode::TilePaint);QVERIFY(box->isHidden());
        tools.activateMode(ELandscapeEditMode::Sculpt);QVERIFY(!box->isHidden());
        QSettings().remove("ui/landscape/moveMobObjects");
    }
    void boundaryFillHasNoFixedHeight()
    {
        CLandscapeTools tools;
        const auto original=tools.sculptTool();
        tools.setSculptTool(ELandscapeSculptTool::BoundaryFill);
        QCOMPARE(tools.sculptTool(),ELandscapeSculptTool::BoundaryFill);
        auto* combo=tools.findChild<QComboBox*>(QStringLiteral("landscapeSculptTool"));
        auto* target=tools.findChild<QSpinBox*>(QStringLiteral("landscapeFlattenTarget"));
        QVERIFY(combo);QVERIFY(target);QVERIFY(target->isHidden());
        QCOMPARE(combo->count(),6);
        CLandscapeTools restored;
        QCOMPARE(restored.sculptTool(),ELandscapeSculptTool::BoundaryFill);
        tools.setSculptTool(ELandscapeSculptTool::Flatten);
        QVERIFY(!target->isHidden());
        tools.setSculptTool(original);
    }
    void nestedTerrainPreviewKeepsBothGpuBuffersCurrent();
    void neighborTerrainShaderCompiles();
    void paintPaletteUsesVisibleWidthOnFirstActivation_data();
    void paintPaletteUsesVisibleWidthOnFirstActivation();
    void usedMaterialFilterTracksMapAndSelection();
    void worldTerrainControlsStayProtectedAndSignal();
    void manualPaletteDoesNotAllocateRowsForEmptyAtlasHoles();
    void manualPaletteFiltersNonNullPaddingAndRestoresPhysicalIdentity();
    void terrainPaletteBatchDefersIntermediateWidgetRebuilds();
    void deferredTerrainThumbnailRefreshesWithoutInitialsFallback();
    void manualPaletteIsRequestedOnlyWhenItsTabIsOpened();
    void paintHelpLivesOnRelevantControls();
    void modernAutoResolveBlendsIsExplicitAndModeScoped();
    void sculptModeNeverAdmitsTilePreview();
    void overlaySurfaceModeLocksTileTargetAndOverlayLayer();
    void overlayModesExposeMprMaterialsShortcut();
    void overlaySurfaceModesExposeMaterialSlotLabels();
    void overlayMaterialDropdownRefreshesImmediately();
    void landscapeModeSegmentsKeepEverySurfaceActionAvailable();
    void landscapePanelPolishControlsAndOrder();
    void classicSettingsMigrateAndCUsesManualOnlyWhileHeld();
    void sharedBrushSizePersistsAcrossModes();
    void paintOptionsRemainVisibleAsRussianChips();
    void cornerOnlyMaterialsAreSeparatedFromMainPalette();
    void capturesApprovedLandscapePanelScreenshots();
    void compressedTerrainAtlasFramebufferCapability();
    void loadedMapRefreshWaitsForTreeItemMutationToReturn();
};

void LandscapeToolsWidgetTests::usedMaterialFilterTracksMapAndSelection()
{
    const QString key = QStringLiteral("ui/landscape/usedMaterialsOnly");
    QSettings().remove(key);
    CLandscapeTools tools;
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    STerrainPaintMaterialChoice used, unused;
    used.id = 101; used.name = "Grass A"; used.familyName = "Grass"; used.used = true;
    unused = used; unused.id = 102; unused.name = "Grass B"; unused.used = false;
    tools.setPaintMaterials({used, unused}, {}, {used, unused}, 2, 2);
    tools.setClassicAutoMaterials({used, unused}, {}, {used, unused});
    auto* filter = tools.findChild<QCheckBox*>("terrainUsedMaterialsOnly");
    QVERIFY(filter);
    QVERIFY(!filter->isChecked());
    auto* scroll = tools.findChild<QScrollArea*>("groundMaterialScroll");
    QVERIFY(scroll);
    QCOMPARE(scroll->findChildren<QToolButton*>().size(), 2);
    int headings = 0;
    for(auto* label : scroll->findChildren<QLabel*>())
        headings += label->text() == "Grass 2";
    QCOMPARE(headings, 1); // Used and unused variants share the same family.
    QVERIFY(tools.selectPaintTerrainType(102));
    QSignalSpy changed(&tools, &CLandscapeTools::paintTerrainTypeChanged);
    filter->setChecked(true);
    QCOMPARE(scroll->findChildren<QToolButton*>().size(), 1);
    QCOMPARE(tools.paintTerrainType(), qint32(101));
    QVERIFY(!changed.isEmpty());
    QCOMPARE(changed.last().first().toInt(), 101);
    for(const QString& name : {QStringLiteral("overlayMaterialScroll"),
            QStringLiteral("classicGroundMaterialScroll"),
            QStringLiteral("classicOverlayMaterialScroll")})
        QCOMPARE(tools.findChild<QScrollArea*>(name)->findChildren<QToolButton*>().size(), 1);
    tools.setUsedTerrainFamilies({102}, {});
    QCOMPARE(tools.paintTerrainType(), qint32(102));
    tools.setUsedTerrainFamilies({}, {});
    bool selected = true;
    tools.paintTerrainType(&selected);
    QVERIFY(!selected);
    QCOMPARE(scroll->findChildren<QToolButton*>().size(), 0);
    CLandscapeTools restored;
    QVERIFY(restored.findChild<QCheckBox*>("terrainUsedMaterialsOnly")->isChecked());
    filter->setChecked(false);
    QCOMPARE(scroll->findChildren<QToolButton*>().size(), 2);
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Manual);
    QVERIFY(filter->isHidden());
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
    QVERIFY(!filter->isHidden());
    QSettings().remove(key);
}

void LandscapeToolsWidgetTests::paintPaletteUsesVisibleWidthOnFirstActivation_data()
{
    QTest::addColumn<bool>("overlay");
    QTest::newRow("ground") << false;
    QTest::newRow("overlay") << true;
}

void LandscapeToolsWidgetTests::paintPaletteUsesVisibleWidthOnFirstActivation()
{
    QFETCH(bool, overlay);
    CLandscapeTools tools;
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
    tools.activateMode(ELandscapeEditMode::Sculpt, false);
    QVector<STerrainPaintMaterialChoice> choices;
    for(int i = 0; i < 8; ++i)
    {
        STerrainPaintMaterialChoice choice;
        choice.id = 100 + i;
        choice.name = QStringLiteral("Grass %1").arg(i);
        choice.familyName = QStringLiteral("Grass");
        choice.used = true;
        choices.append(choice);
    }
    tools.setPaintMaterials(choices, {}, choices, 8, 8);
    tools.setClassicAutoMaterials(choices, {}, choices);
    tools.resize(420, 1100);
    tools.show();
    QTest::qWait(100);
    const QString scrollName = overlay ? "overlayMaterialScroll" : "groundMaterialScroll";
    auto* scroll = tools.findChild<QScrollArea*>(scrollName);
    QVERIFY(scroll);
    const auto buttonFor = [scroll](int id) {
        for(auto* button : scroll->findChildren<QToolButton*>())
            if(button->property("terrainMaterialId").toInt() == id)
                return button;
        return static_cast<QToolButton*>(nullptr);
    };
    QVERIFY(buttonFor(102));
    buttonFor(102)->setChecked(true);
    tools.activateMode(overlay ? ELandscapeEditMode::OverlayPaint
                               : ELandscapeEditMode::TilePaint, false);
    QTest::qWait(100);
    QVERIFY(scroll->isVisible());
    QVERIFY(scroll->viewport()->width() > 280);
    QVERIFY2(buttonFor(100)->y() == buttonFor(102)->y(),
             "First paint activation left material buttons in a single column");
    QVERIFY(buttonFor(102)->isChecked());
    const QPoint firstPosition = buttonFor(102)->pos();
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
    QTest::qWait(100);
    QCOMPARE(buttonFor(102)->pos(), firstPosition);
    QVERIFY(buttonFor(102)->isChecked());
    tools.resize(280, 1100);
    QTest::qWait(100);
    QVERIFY(buttonFor(102)->isChecked());
    QVERIFY(buttonFor(100)->width() >= 64);
    QVERIFY2(buttonFor(102)->geometry().right() <= scroll->viewport()->width(),
             qPrintable(QStringLiteral("button right=%1 viewport width=%2 scroll area width=%3")
                 .arg(buttonFor(102)->geometry().right())
                 .arg(scroll->viewport()->width()).arg(scroll->width())));
    tools.resize(420, 1100);
    QTest::qWait(100);
    QCOMPARE(buttonFor(102)->pos(), firstPosition);
    auto* filter = tools.findChild<QLineEdit*>(QStringLiteral("terrainPaintFilter"));
    QVERIFY(filter);
    filter->setText(QStringLiteral("Grass 2"));
    tools.activateMode(ELandscapeEditMode::Sculpt, false);
    QTest::qWait(50);
    tools.activateMode(overlay ? ELandscapeEditMode::OverlayPaint
                               : ELandscapeEditMode::TilePaint, false);
    QTest::qWait(100);
    QVERIFY(buttonFor(102)->isVisible());
    QVERIFY(buttonFor(100)->isHidden());
    QVERIFY(buttonFor(102)->isChecked());
}

void LandscapeToolsWidgetTests::neighborTerrainShaderCompiles()
{
    QGLWidget viewport;
    if(!viewport.isValid()) QSKIP("No legacy viewport OpenGL context");
    viewport.makeCurrent();
    QOpenGLShaderProgram program;
    QVERIFY2(program.addShaderFromSourceFile(QOpenGLShader::Vertex,
        QFINDTESTDATA("../assets/shaders/landshader.vsh")),qPrintable(program.log()));
    QVERIFY2(program.addShaderFromSourceFile(QOpenGLShader::Fragment,
        QFINDTESTDATA("../assets/shaders/landshader.fsh")),qPrintable(program.log()));
    QVERIFY2(program.link(),qPrintable(program.log()));
    QVERIFY(program.bind());
    QVERIFY(program.uniformLocation("u_terrainShadowOrigin")>=0);
    program.setUniformValue("u_terrainShadowOrigin",QVector2D(128,-64));
    program.release();
}

void LandscapeToolsWidgetTests::nestedTerrainPreviewKeepsBothGpuBuffersCurrent()
{
    QGLWidget viewport;
    if(!viewport.isValid()) QSKIP("No legacy viewport OpenGL context");
    viewport.makeCurrent();
    QOpenGLBuffer active, neighbor;
    QVERIFY(active.create()); QVERIFY(neighbor.create());
    const float initial=20, smoothed=12;
    QVERIFY(active.bind()); active.allocate(&initial,sizeof(initial));active.release();
    QVERIFY(neighbor.bind());neighbor.allocate(&initial,sizeof(initial));neighbor.release();
    {
        CTerrainPreviewContext batch(&viewport);
        {
            CTerrainPreviewContext refreshNeighbor(&viewport);
            QVERIFY(neighbor.bind());neighbor.allocate(&smoothed,sizeof(smoothed));neighbor.release();
        }
        // Regression: neighbor doneCurrent() used to detach the context before
        // the deferred active-map upload, leaving old heights on the GPU.
        QCOMPARE(QGLContext::currentContext(),viewport.context());
        QVERIFY(active.bind());active.allocate(&smoothed,sizeof(smoothed));active.release();
    }
    QCOMPARE(QGLContext::currentContext(),viewport.context());
    float a=0,b=0;
    QVERIFY(active.bind());QVERIFY(active.read(0,&a,sizeof(a)));active.release();
    QVERIFY(neighbor.bind());QVERIFY(neighbor.read(0,&b,sizeof(b)));neighbor.release();
    QCOMPARE(a,smoothed);QCOMPARE(b,smoothed);
    active.destroy();neighbor.destroy();viewport.doneCurrent();
    { CTerrainPreviewContext fromTimer(&viewport);
      QCOMPARE(QGLContext::currentContext(),viewport.context()); }
    QVERIFY(!QGLContext::currentContext());
}

void LandscapeToolsWidgetTests::worldTerrainControlsStayProtectedAndSignal()
{
    CLandscapeTools tools;
    tools.activateMode(ELandscapeEditMode::Sculpt,false);
    tools.setEditingEnabled(true,false);
    auto maps=tools.findChild<QPushButton*>("worldTerrainMaps");
    auto undo=tools.findChild<QPushButton*>("worldTerrainUndo");
    auto redo=tools.findChild<QPushButton*>("worldTerrainRedo");
    auto save=tools.findChild<QPushButton*>("worldTerrainSave");
    QVERIFY(maps && undo && redo && save);
    QVERIFY(!undo->isEnabled());QVERIFY(!redo->isEnabled());QVERIFY(!save->isEnabled());
    QSignalSpy choose(&tools,&CLandscapeTools::worldSculptSelectionRequested);
    maps->click();QCOMPARE(choose.count(),1);
    tools.setWorldTerrainStatus("World terrain: on. Modified: a.mpr, b.mpr",true,false,true,true,2);
    QVERIFY(undo->isEnabled());QVERIFY(!redo->isEnabled());QVERIFY(save->isEnabled());
    QSignalSpy undoStroke(&tools,&CLandscapeTools::worldSculptUndoRequested);
    undo->click();QCOMPARE(undoStroke.count(),1);
    tools.resize(380,850);tools.show();QApplication::processEvents();
    QVERIFY(tools.grab().save("world-terrain-controls.png"));
}

void LandscapeToolsWidgetTests::compressedTerrainAtlasFramebufferCapability()
{
    QOffscreenSurface surface;
    surface.create();
    QOpenGLContext context;
    if(!context.create() || !context.makeCurrent(&surface))
        QSKIP("The selected headless platform plugin exposes no OpenGL context");
    QOpenGLTexture compressed(QOpenGLTexture::Target2D);
    QVERIFY(compressed.create());
    compressed.setSize(256, 256);
    compressed.setFormat(QOpenGLTexture::RGBA_DXT1);
    compressed.setMipLevels(1);
    compressed.allocateStorage(QOpenGLTexture::RGBA,
                               QOpenGLTexture::UInt8);
    QVERIFY2(compressed.isStorageAllocated(),
             "Retained context cannot allocate the shipped DXT1 atlas format");
    QOpenGLFunctions* gl = context.functions();
    GLuint framebuffer = 0;
    gl->glGenFramebuffers(1, &framebuffer);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, compressed.textureId(), 0);
    const GLenum status = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    qInfo("retained DXT1 framebuffer status=0x%x complete=%d",
          uint(status), status == GL_FRAMEBUFFER_COMPLETE ? 1 : 0);
    QByteArray redDxt(256 * 256 / 2, char(0));
    for(int offset = 0; offset < redDxt.size(); offset += 8)
    {
        redDxt[offset + 0] = char(0x00);
        redDxt[offset + 1] = char(0xf8);
    }
    compressed.setCompressedData(redDxt.size(), redDxt.constData());
    QOpenGLTexture target(QOpenGLTexture::Target2D);
    QVERIFY(target.create());
    target.setSize(256, 256);
    target.setFormat(QOpenGLTexture::RGBA8_UNorm);
    target.setMipLevels(1);
    target.allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, target.textureId(), 0);
    QCOMPARE(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER),
             GLenum(GL_FRAMEBUFFER_COMPLETE));
    gl->glViewport(0, 0, 256, 256);
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDisable(GL_CULL_FACE);
    QOpenGLTextureBlitter blitter;
    QVERIFY(blitter.create());
    blitter.bind();
    blitter.blit(compressed.textureId(),
        QOpenGLTextureBlitter::targetTransform(
            QRectF(0, 0, 256, 256), QRect(0, 0, 256, 256)),
        QOpenGLTextureBlitter::OriginBottomLeft);
    blitter.release();
    uchar pixel[4] = {0, 0, 0, 0};
    gl->glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    QVERIFY2(pixel[0] > 200 && pixel[1] < 40 && pixel[2] < 40,
             "Retained context could not sample DXT1 into an RGBA regrid target");
    blitter.destroy();
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &framebuffer);
    target.destroy();
    compressed.destroy();
    context.doneCurrent();
}

void LandscapeToolsWidgetTests::modernAutoResolveBlendsIsExplicitAndModeScoped()
{
    QSettings settings;
    settings.remove(QStringLiteral("ui/landscape/modernAutoResolveBlends"));
    CLandscapeTools tools;
    tools.resize(730, 1500);
    tools.setEditingEnabled(true, false);
    QCheckBox* autoResolve = tools.findChild<QCheckBox*>(
        QStringLiteral("modernAutoResolveBlends"));
    QVERIFY2(autoResolve,
             "Modern painting has no explicit Auto-resolve blends control");
    QVERIFY(autoResolve->isChecked());

    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    QVERIFY(autoResolve->isVisibleTo(&tools));
    QVERIFY(tools.modernAutoResolveBlends());
    autoResolve->setChecked(false);
    QVERIFY(!tools.modernAutoResolveBlends());

    tools.activateMode(ELandscapeEditMode::OverlaySurface, false);
    QVERIFY2(autoResolve->isVisibleTo(&tools),
             "Add Overlay Surface hides the Modern blend policy");
    QVERIFY(!tools.modernAutoResolveBlends());

    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::ClassicAuto);
    QVERIFY(autoResolve->isVisibleTo(&tools));
    QCOMPARE(tools.paintPaletteMode(), ETerrainPaintPaletteMode::Modern);
}

void LandscapeToolsWidgetTests::sculptModeNeverAdmitsTilePreview()
{
    CLandscapeTools tools;
    tools.setEditingEnabled(true, false);
    tools.activateMode(ELandscapeEditMode::Sculpt, false);
    QVERIFY(!tools.tilePaintPreviewEnabled());
    tools.activateMode(ELandscapeEditMode::OverlaySculpt, false);
    QVERIFY(!tools.tilePaintPreviewEnabled());
    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    QVERIFY(tools.tilePaintPreviewEnabled());
    tools.activateMode(ELandscapeEditMode::OverlaySurface, false);
    QVERIFY(tools.tilePaintPreviewEnabled());
}

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
    QCOMPARE(tabs->count(), 2);
    QVERIFY(!tabs->tabBar()->isVisible());
    QVERIFY(!tools.findChild<QComboBox*>(QStringLiteral("landscapePaintAlgorithm"))->isVisible());
    for(int index = 0; index < tabs->count(); ++index)
        QVERIFY2(!tabs->tabToolTip(index).trimmed().isEmpty(),
                 "A terrain paint mode has no contextual tooltip");

    QPushButton* resolve = tools.findChild<QPushButton*>(
        QStringLiteral("resolveMissingBlendsButton"));
    QVERIFY(resolve);
    QCOMPARE(resolve->text(), QStringLiteral("Исправить переходы…"));
    QPushButton* saveDraft = tools.findChild<QPushButton*>(
        QStringLiteral("saveModernDraftButton"));
    QVERIFY(saveDraft);
    QCOMPARE(saveDraft->text(), QStringLiteral("Сохранить черновик…"));

    for(QLabel* label : tools.findChildren<QLabel*>())
    {
        QVERIFY2(!label->text().startsWith(QStringLiteral(
                     "Z / X / C switches")),
                 "The removed terrain-paint prose block is still visible");
    }
    QSlider* brushSize = tools.findChild<QSlider*>(
        QStringLiteral("terrainPaintRadius"));
    QVERIFY(brushSize && brushSize->parentWidget());
    QVERIFY2(brushSize->parentWidget()->toolTip().contains(
                 QStringLiteral("Alt+колесо")),
             "The brush-size shortcut is missing from the size control");
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
    QToolButton* advanced = tools.findChild<QToolButton*>(
        QStringLiteral("landscapeMapToggle"));
    QVERIFY(advanced);
    advanced->click();
    QVERIFY(advanced->isChecked());
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

void LandscapeToolsWidgetTests::overlaySurfaceModesExposeMaterialSlotLabels()
{
    const QString diagnosticsKey = QStringLiteral(
        "ui/landscape/folds/landscapeDiagnostics");
    QSettings().remove(diagnosticsKey);
    CLandscapeTools tools;
    tools.resize(730, 1500);
    tools.setEditingEnabled(true, false);
    QCheckBox* labels = tools.findChild<QCheckBox*>(
        QStringLiteral("showOverlayMaterialSlotLabels"));
    QVERIFY(labels);
    QComboBox* operation = tools.findChild<QComboBox*>(
        QStringLiteral("overlaySurfaceOperation"));
    QVERIFY(operation);

    tools.activateMode(ELandscapeEditMode::OverlaySurface, false);
    auto* diagnostics = tools.findChild<QToolButton*>(
        QStringLiteral("landscapeDiagnosticsToggle"));
    QVERIFY(diagnostics);
    if(!diagnostics->isChecked()) diagnostics->click();
    QVERIFY(operation->findText(QStringLiteral("Удалить поверхность")) >= 0);
    operation->setCurrentIndex(0);
    QVERIFY2(labels->isVisibleTo(&tools),
             "Add Overlay Surface hides material-slot inspection");
    operation->setCurrentIndex(1);
    QVERIFY2(labels->isVisibleTo(&tools),
             "Remove Overlay Surface hides material-slot inspection");
    tools.activateMode(ELandscapeEditMode::OverlayPaint, false);
    QVERIFY(labels->isVisibleTo(&tools));
    QSettings().remove(diagnosticsKey);
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

void LandscapeToolsWidgetTests::landscapeModeSegmentsKeepEverySurfaceActionAvailable()
{
    const QString mapFoldKey = QStringLiteral(
        "ui/landscape/folds/landscapeMap");
    const QString diagnosticsFoldKey = QStringLiteral(
        "ui/landscape/folds/landscapeDiagnostics");
    QSettings().remove(mapFoldKey);
    QSettings().remove(diagnosticsFoldKey);
    CLandscapeTools tools;
    tools.setEditingEnabled(true, false);
    auto* paint = tools.findChild<QToolButton*>("landscapePaintSegment");
    auto* sculpt = tools.findChild<QToolButton*>("landscapeSculptSegment");
    auto* surfaces = tools.findChild<QToolButton*>("landscapeSurfaceSegment");
    auto* actions = tools.findChild<QComboBox*>("landscapeSurfaceAction");
    QVERIFY(paint && sculpt && surfaces && actions);
    QCOMPARE(actions->count(), 3);
    QVERIFY(!tools.findChild<QComboBox*>("landscapeMode")->isVisible());

    // This is also the migration map recorded in the 147 validation report.
    // Keep every former command discoverable through its new panel control.
    const QVector<QPair<QString, QString>> migratedActions = {
        {QStringLiteral("Рисовать тайлы суши"), QStringLiteral("landscapePaintSegment")},
        {QStringLiteral("Sculpt terrain"), QStringLiteral("landscapeSculptSegment")},
        {QStringLiteral("Sculpt overlay surfaces"), QStringLiteral("landscapeSurfaceSegment")},
        {QStringLiteral("Paint existing overlays"), QStringLiteral("landscapeSurfaceAction")},
        {QStringLiteral("Classic Auto (X)"), QStringLiteral("modernAutoResolveBlends")},
        {QStringLiteral("Manual physical atlas painting (C)"), QStringLiteral("materialPaletteTabs")},
        {QStringLiteral("Brush radius and shape"), QStringLiteral("landscapeSharedBrush")},
        {QStringLiteral("Tile / vertex target"), QStringLiteral("terrainPaintTargetWidget")},
        {QStringLiteral("Sculpt strength"), QStringLiteral("landscapeSculptStrength")},
        {QStringLiteral("Auto transitions"), QStringLiteral("modernAutoResolveBlends")},
        {QStringLiteral("Mixed edge"), QStringLiteral("modernMixedEdge")},
        {QStringLiteral("Random variants"), QStringLiteral("randomPaintVariants")},
        {QStringLiteral("HD variants"), QStringLiteral("generatedHdVariants")},
        {QStringLiteral("Tile rotation"), QStringLiteral("randomizeUniformRotation")},
        {QStringLiteral("Material layers"), QStringLiteral("modernMaterialLayerTabs")},
        {QStringLiteral("Material search"), QStringLiteral("terrainPaintFilter")},
        {QStringLiteral("Only on map"), QStringLiteral("terrainUsedMaterialsOnly")},
        {QStringLiteral("Palette columns"), QStringLiteral("terrainPaletteColumns")},
        {QStringLiteral("Selected material variants"), QStringLiteral("terrainVariantStripScroll")},
        {QStringLiteral("Raise / lower / smooth / flatten / noise / boundary fill"), QStringLiteral("landscapeSculptToolGrid")},
        {QStringLiteral("Sculpt falloff"), QStringLiteral("landscapeSculptFalloff")},
        {QStringLiteral("Target height"), QStringLiteral("landscapeFlattenTarget")},
        {QStringLiteral("Move objects with terrain"), QStringLiteral("landscapeMoveMobObjects")},
        {QStringLiteral("Map actions fold"), QStringLiteral("landscapeMapToggle")},
        {QStringLiteral("Shuffle tiles"), QStringLiteral("shuffleTerrainTiles")},
        {QStringLiteral("Repair transitions"), QStringLiteral("resolveMissingBlendsButton")},
        {QStringLiteral("Convert to HD"), QStringLiteral("terrainHdRepublish")},
        {QStringLiteral("Generate HD variants"), QStringLiteral("terrainHdVariantsGenerate")},
        {QStringLiteral("Height map"), QStringLiteral("terrainHeightmapImport")},
        {QStringLiteral("Material atlas"), QStringLiteral("materialAtlasButton")},
        {QStringLiteral("Surface materials"), QStringLiteral("overlayMprMaterialsButton")},
        {QStringLiteral("Map settings"), QStringLiteral("openMprSettingsFromLandscape")},
        {QStringLiteral("Save draft / world maps"), QStringLiteral("worldTerrainSave")},
        {QStringLiteral("Diagnostics and atlas fold"), QStringLiteral("landscapeDiagnosticsToggle")},
        {QStringLiteral("Bad-tile category"), QStringLiteral("paintIssueCategory")},
        {QStringLiteral("Mark bad tiles"), QStringLiteral("markPaintIssueButton")},
        {QStringLiteral("Undo shared stroke"), QStringLiteral("worldTerrainUndo")},
        {QStringLiteral("Redo shared stroke"), QStringLiteral("worldTerrainRedo")},
        {QStringLiteral("Export diagnostics"), QStringLiteral("exportPaintDiagnostic")},
        {QStringLiteral("Ground / surfaces visibility (M / O)"), QStringLiteral("showTerrainSurfaces")}
    };
    for(const auto& action : migratedActions)
        QVERIFY2(tools.findChild<QWidget*>(action.second),
                 qPrintable(action.first + QStringLiteral(" -> ") + action.second));

    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    surfaces->click();
    QCOMPARE(tools.editMode(), ELandscapeEditMode::OverlayPaint);
    actions->setCurrentIndex(0);
    QCOMPARE(tools.editMode(), ELandscapeEditMode::OverlayPaint);
    QVERIFY(surfaces->isChecked());
    actions->setCurrentIndex(1);
    QCOMPARE(tools.editMode(), ELandscapeEditMode::OverlaySurface);
    QVERIFY(tools.overlaySurfaceEnabled());
    actions->setCurrentIndex(2);
    QCOMPARE(tools.editMode(), ELandscapeEditMode::OverlaySculpt);
    QVERIFY(tools.overlaySculptEnabled());

    paint->click();
    QCOMPARE(tools.editMode(), ELandscapeEditMode::TilePaint);
    sculpt->click();
    QCOMPARE(tools.editMode(), ELandscapeEditMode::Sculpt);
    for(const QString& name : {QStringLiteral("shuffleTerrainTiles"),
            QStringLiteral("resolveMissingBlendsButton"),
            QStringLiteral("terrainHdRepublish"),
            QStringLiteral("terrainHdVariantsGenerate"),
            QStringLiteral("terrainHeightmapImport"),
            QStringLiteral("openMprSettingsFromLandscape"),
            QStringLiteral("saveModernDraftButton"),
            QStringLiteral("worldTerrainMaps"),
            QStringLiteral("worldTerrainSave"),
            QStringLiteral("randomPaintScope"),
            QStringLiteral("worldTerrainUndo"),
            QStringLiteral("worldTerrainRedo"),
            QStringLiteral("overlayMprMaterialsButton"),
            QStringLiteral("materialAtlasButton"),
            QStringLiteral("exportPaintDiagnostic"),
            QStringLiteral("showOverlayMaterialSlotLabels"),
            QStringLiteral("paintIssueCategory"),
            QStringLiteral("markPaintIssueButton"),
            QStringLiteral("landscapeDiagnosticsToggle"),
            QStringLiteral("terrainPaletteColumns")})
        QVERIFY2(tools.findChild<QWidget*>(name), qPrintable(name));
    auto* mapBody = tools.findChild<QWidget*>("landscapeMap");
    auto* diagnosticBody = tools.findChild<QWidget*>("landscapeDiagnostics");
    auto* brushBody = tools.findChild<QWidget*>("landscapeSharedBrush");
    QVERIFY(mapBody && diagnosticBody && brushBody);
    for(const QString& name : {QStringLiteral("shuffleTerrainTiles"),
            QStringLiteral("terrainHdRepublish"),
            QStringLiteral("terrainHdVariantsGenerate"),
            QStringLiteral("resolveMissingBlendsButton"),
            QStringLiteral("terrainHeightmapImport"),
            QStringLiteral("overlayMprMaterialsButton"),
            QStringLiteral("openMprSettingsFromLandscape"),
            QStringLiteral("saveModernDraftButton"),
            QStringLiteral("worldTerrainMaps"),
            QStringLiteral("worldTerrainSave")})
        QVERIFY2(mapBody->isAncestorOf(tools.findChild<QWidget*>(name)),
                 qPrintable(name));
    for(const QString& name : {QStringLiteral("materialAtlasButton"),
            QStringLiteral("showOverlayMaterialSlotLabels"),
            QStringLiteral("exportPaintDiagnostic"),
            QStringLiteral("markPaintIssueButton")})
        QVERIFY2(diagnosticBody->isAncestorOf(tools.findChild<QWidget*>(name)),
                 qPrintable(name));
    QVERIFY(diagnosticBody->isAncestorOf(
        tools.findChild<QWidget*>("showMainTerrain")));
    QVERIFY(diagnosticBody->isAncestorOf(
        tools.findChild<QWidget*>("showTerrainSurfaces")));
    QVERIFY(brushBody->isAncestorOf(
        tools.findChild<QWidget*>("worldTerrainUndo")));
    QVERIFY(brushBody->isAncestorOf(
        tools.findChild<QWidget*>("worldTerrainRedo")));
    auto* mapToggle = tools.findChild<QToolButton*>("landscapeMapToggle");
    auto* diagnosticsToggle = tools.findChild<QToolButton*>(
        "landscapeDiagnosticsToggle");
    QVERIFY(mapToggle);
    QVERIFY(diagnosticsToggle);
    QVERIFY(mapToggle->isChecked());
    QVERIFY(!diagnosticsToggle->isChecked());
    mapToggle->click();
    QVERIFY(!mapToggle->isChecked());
    mapToggle->click();
    QVERIFY(mapToggle->isChecked());
    diagnosticsToggle->click();
    QVERIFY(diagnosticsToggle->isChecked());
    CLandscapeTools restored;
    QVERIFY(restored.findChild<QToolButton*>("landscapeMapToggle")->isChecked());
    QVERIFY(restored.findChild<QToolButton*>("landscapeDiagnosticsToggle")->isChecked());
    QSettings().remove(mapFoldKey);
    QSettings().remove(diagnosticsFoldKey);
}

void LandscapeToolsWidgetTests::landscapePanelPolishControlsAndOrder()
{
    QSettings().remove(QStringLiteral("ui/landscape/folds/landscapeMap"));
    QSettings().remove(QStringLiteral("ui/landscape/folds/landscapeDiagnostics"));
    CLandscapeTools tools;
    tools.setEditingEnabled(true, false);
    tools.resize(320, 760);
    tools.show();
    QApplication::processEvents();

    auto* paint = tools.findChild<QToolButton*>("landscapePaintSegment");
    auto* sculpt = tools.findChild<QToolButton*>("landscapeSculptSegment");
    auto* surfaces = tools.findChild<QToolButton*>("landscapeSurfaceSegment");
    QVERIFY(paint && sculpt && surfaces);
    QVERIFY(!paint->text().isEmpty() && !sculpt->text().isEmpty()
        && !surfaces->text().isEmpty());
    QVERIFY(paint->isChecked());
    QVERIFY(paint->styleSheet().contains(QStringLiteral(":checked")));
    sculpt->click();
    QVERIFY(sculpt->isChecked());
    QVERIFY(!paint->isChecked());
    surfaces->click();
    QVERIFY(surfaces->isChecked());
    QVERIFY(!sculpt->isChecked());
    tools.activateMode(ELandscapeEditMode::Sculpt, false);
    QVERIFY(sculpt->isChecked());
    QVERIFY(!surfaces->isChecked());

    for(int tool = int(ELandscapeSculptTool::Raise);
        tool <= int(ELandscapeSculptTool::BoundaryFill); ++tool)
    {
        auto* button = tools.findChild<QToolButton*>(
            QStringLiteral("sculptTool_%1").arg(tool));
        QVERIFY(button);
        button->click();
        QVERIFY(button->isChecked());
        QVERIFY(button->styleSheet().contains(QStringLiteral(":checked")));
    }

    QVERIFY(!tools.findChild<QWidget*>("landscapeSculptAdvanced"));
    QVERIFY(!tools.findChild<QWidget*>("landscapeHelp"));
    QVERIFY(tools.findChild<QLabel*>("landscapeSculptShortcuts"));
    auto* strengthSlider = tools.findChild<QSlider*>("landscapeSculptStrengthSlider");
    auto* strength = tools.findChild<QDoubleSpinBox*>("landscapeSculptStrength");
    auto* stepSlider = tools.findChild<QSlider*>("landscapeRaiseLowerStepSlider");
    auto* step = tools.findChild<QDoubleSpinBox*>("landscapeRaiseLowerStep");
    QVERIFY(strengthSlider && strength && stepSlider && step);
    QCOMPARE(step->singleStep(), 100.0);
    strengthSlider->setValue(62);
    QCOMPARE(strength->value(), 0.62);
    stepSlider->setValue(27);
    QCOMPARE(step->value(), 270.0);
    tools.setSculptTool(ELandscapeSculptTool::Raise);
    QVERIFY(!stepSlider->isHidden());

    QVERIFY(!tools.findChild<QWidget*>("randomPaintSeed"));
    QVERIFY(!tools.findChild<QWidget*>("newRandomPaintSeed"));
    auto* shuffle = tools.findChild<QPushButton*>("shuffleTerrainTiles");
    auto* scope = tools.findChild<QComboBox*>("randomPaintScope");
    QVERIFY(shuffle && scope && scope->count() == 2);
    QSignalSpy shuffleSpy(&tools, &CLandscapeTools::shuffleTerrainTilesRequested);
    shuffle->click();
    QCOMPARE(shuffleSpy.count(), 1);
    QCOMPARE(shuffleSpy.takeFirst().at(0).toBool(), false);

    auto* shortcuts = tools.findChild<QLabel*>("landscapeBrushShortcuts");
    QVERIFY(shortcuts);
    QVERIFY(shortcuts->font().pointSizeF() <= 9.0);
    auto* undo = tools.findChild<QPushButton*>("worldTerrainUndo");
    auto* redo = tools.findChild<QPushButton*>("worldTerrainRedo");
    QVERIFY(undo && redo);
    QVERIFY(!undo->text().isEmpty() && !redo->text().isEmpty());
    QVERIFY(undo->toolTip().contains(QStringLiteral("Ctrl+Z")));
    QVERIFY(redo->toolTip().contains(QStringLiteral("Ctrl+Y")));

    auto* chips = tools.findChild<QWidget*>("terrainPaintOptionChips");
    QVERIFY(chips && qobject_cast<QGridLayout*>(chips->layout()->itemAt(0)->layout()));
    for(QCheckBox* chip : chips->findChildren<QCheckBox*>())
    {
        QVERIFY(chip->minimumHeight() >= 28);
        QVERIFY(chip->styleSheet().contains(QStringLiteral(":checked")));
    }
    auto* variantTitle = tools.findChild<QLabel*>("terrainVariantStripTitle");
    auto* variantStrip = tools.findChild<QScrollArea*>("terrainVariantStripScroll");
    QVERIFY(variantTitle && variantStrip);
    QVERIFY(variantTitle->isHidden());
    QVERIFY(variantStrip->isHidden());
    STerrainPaintMaterialChoice choice;
    choice.id = 701;
    choice.name = QStringLiteral("Тестовый материал");
    choice.thumbnail = QImage(24, 24, QImage::Format_ARGB32_Premultiplied);
    choice.thumbnail.fill(QColor(90, 130, 70));
    tools.setPaintMaterials({choice}, {}, {}, 1, 1);
    QVERIFY(!variantTitle->isHidden());
    QVERIFY(!variantStrip->isHidden());
    QCOMPARE(variantTitle->text(), QStringLiteral("Выбрано: Тестовый материал — 0 исходных + 0 сгенерированных"));
    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    QApplication::processEvents();
    const auto variantButtons = variantStrip->findChildren<QToolButton*>();
    QVERIFY(variantButtons.size() >= 1);
    QVERIFY(!variantButtons.front()->icon().isNull());
    QVERIFY(variantButtons.front()->iconSize().width() >= 32);
    QVERIFY(variantButtons.front()->width() >= 32);
    tools.setPaintMaterials({}, {}, {}, 0, 0);
    QVERIFY(variantTitle->isHidden());
    QVERIFY(variantStrip->isHidden());

    auto* panelScroll = tools.findChild<QScrollArea*>("landscapeToolScroll");
    QVERIFY(panelScroll && panelScroll->widget());
    auto* contentsLayout = qobject_cast<QVBoxLayout*>(panelScroll->widget()->layout());
    auto* palette = tools.findChild<QWidget*>("landscapePaintControls");
    auto* map = tools.findChild<QWidget*>("landscapeMapSection");
    auto* diagnostics = tools.findChild<QWidget*>("landscapeDiagnosticsSection");
    QVERIFY(contentsLayout && palette && map && diagnostics);
    QVERIFY(contentsLayout->indexOf(palette) >= 0);
    QVERIFY(contentsLayout->indexOf(palette) < contentsLayout->indexOf(map));
    QVERIFY(contentsLayout->indexOf(map) < contentsLayout->indexOf(diagnostics));
    QVERIFY(contentsLayout->stretch(contentsLayout->indexOf(palette)) > 0);
    QVERIFY(palette->height() > 250);
    tools.close();
}

void LandscapeToolsWidgetTests::classicSettingsMigrateAndCUsesManualOnlyWhileHeld()
{
    const QString modeKey = QStringLiteral("ui/landscape/paintPaletteMode");
    QSettings().setValue(modeKey, int(ETerrainPaintPaletteMode::ClassicAuto));
    CLandscapeTools tools;
    QVERIFY(!tools.classicAutoPaintingEnabled());
    QCOMPARE(tools.paintPaletteMode(), ETerrainPaintPaletteMode::Modern);
    QCOMPARE(QSettings().value(modeKey).toInt(),
             int(ETerrainPaintPaletteMode::Modern));
    auto* paletteTabs = tools.findChild<QTabWidget*>("materialPaletteTabs");
    QVERIFY(paletteTabs);
    QCOMPARE(paletteTabs->count(), 2);
    QVERIFY(!paletteTabs->tabText(0).contains(QStringLiteral("Класс")));
    QVERIFY(!paletteTabs->tabText(1).contains(QStringLiteral("Класс")));
    QVERIFY(!paletteTabs->tabToolTip(0).contains(
        QStringLiteral("Classic"), Qt::CaseInsensitive));
    QVERIFY(!paletteTabs->tabToolTip(1).contains(
        QStringLiteral("Classic"), Qt::CaseInsensitive));
    QVERIFY(paletteTabs->tabToolTip(1).contains(QStringLiteral("удерживается C")));
    tools.setEditingEnabled(true, false);
    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Manual); // C press
    QVERIFY(tools.manualTileSelectionEnabled());
    QCOMPARE(tools.paintPaletteMode(), ETerrainPaintPaletteMode::Manual);
    QCOMPARE(paletteTabs->currentIndex(), 1);
    auto* manualHint = tools.findChild<QLabel*>("manualPaintHint");
    QVERIFY(manualHint);
    QVERIFY(manualHint->isVisibleTo(&tools));
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern); // C release
    QVERIFY(!tools.manualTileSelectionEnabled());
    QCOMPARE(tools.paintPaletteMode(), ETerrainPaintPaletteMode::Modern);
    QCOMPARE(QSettings().value(modeKey).toInt(),
             int(ETerrainPaintPaletteMode::Modern));
    QSettings().remove(modeKey);
}

void LandscapeToolsWidgetTests::sharedBrushSizePersistsAcrossModes()
{
    const QString sizeKey = QStringLiteral("ui/landscape/brushSize");
    const QString shapeKey = QStringLiteral("ui/landscape/brushShape");
    const QString columnsKey = QStringLiteral("ui/landscape/paletteColumns");
    QSettings().remove(sizeKey);
    QSettings().remove(shapeKey);
    QSettings().remove(columnsKey);
    CLandscapeTools tools;
    auto* size = tools.findChild<QSlider*>("terrainPaintRadius");
    QVERIFY(size);
    auto* square = tools.findChild<QToolButton*>("squarePaintShape");
    QVERIFY(square);
    auto* columns = tools.findChild<QSlider*>("terrainPaletteColumns");
    QVERIFY(columns);
    QCOMPARE(columns->minimum(), 3);
    QCOMPARE(columns->maximum(), 8);
    square->click();
    QCOMPARE(tools.paintShape(), ETerrainTileBrushShape::Square);
    size->setValue(7);
    columns->setValue(6);
    QCOMPARE(QSettings().value(columnsKey).toInt(), 6);
    QCOMPARE(tools.radius(), 7.0f);
    QCOMPARE(tools.paintShape(), ETerrainTileBrushShape::Square);
    tools.activateMode(ELandscapeEditMode::Sculpt, false);
    QCOMPARE(tools.radius(), 7.0f);
    auto* targetWidget = tools.findChild<QWidget*>("terrainPaintTargetWidget");
    auto* tileTarget = tools.findChild<QToolButton*>("tilePaintTarget");
    auto* vertexTarget = tools.findChild<QToolButton*>("vertexPaintTarget");
    auto* strength = tools.findChild<QDoubleSpinBox*>("landscapeSculptStrength");
    QVERIFY(targetWidget && tileTarget && vertexTarget && strength);
    QVERIFY(targetWidget->isHidden());
    QVERIFY(tileTarget->isHidden() && vertexTarget->isHidden());
    QVERIFY(!strength->isHidden());
    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    QVERIFY(!targetWidget->isHidden());
    QVERIFY(!tileTarget->isHidden() && !vertexTarget->isHidden());
    QVERIFY(strength->isHidden());
    tools.adjustSculptRadius(2);
    QCOMPARE(size->value(), 9);
    CLandscapeTools restored;
    QCOMPARE(restored.findChild<QSlider*>("terrainPaletteColumns")->value(), 6);
    QCOMPARE(restored.radius(), 9.0f);
    QCOMPARE(restored.paintRadius(), 8.0f); // UI size 9 maps to zero-based brush radius 8
    QCOMPARE(restored.paintShape(), ETerrainTileBrushShape::Square);
    QSettings().remove(sizeKey);
    QSettings().remove(shapeKey);
    QSettings().remove(columnsKey);
}

void LandscapeToolsWidgetTests::paintOptionsRemainVisibleAsRussianChips()
{
    CLandscapeTools tools;
    tools.setEditingEnabled(true, false);
    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    tools.resize(320, 880);
    tools.show();
    QApplication::processEvents();
    const QMap<QString, QString> expected = {
        {"modernAutoResolveBlends", "Переходы"},
        {"modernMixedEdge", "Кромки"},
        {"randomPaintVariants", "Варианты"},
        {"generatedHdVariants", "HD"},
        {"randomizeUniformRotation", "Повороты"},
    };
    for(auto it = expected.cbegin(); it != expected.cend(); ++it)
    {
        auto* chip = tools.findChild<QCheckBox*>(it.key());
        QVERIFY2(chip, qPrintable(it.key()));
        QCOMPARE(chip->text(), it.value());
        QVERIFY(chip->isVisibleTo(&tools));
        QVERIFY(chip->parentWidget()->objectName()
            == QStringLiteral("terrainPaintOptionChips"));
        QVERIFY2(QFontMetrics(chip->font()).width(chip->text()) + 18
                     <= chip->width(),
                 qPrintable(QStringLiteral("Chip text is clipped at 320 px: %1 (%2/%3)")
                     .arg(chip->text())
                     .arg(QFontMetrics(chip->font()).width(chip->text()) + 18)
                     .arg(chip->width())));
    }
}

void LandscapeToolsWidgetTests::cornerOnlyMaterialsAreSeparatedFromMainPalette()
{
    CLandscapeTools tools;
    tools.setEditingEnabled(true, false);
    tools.activateMode(ELandscapeEditMode::TilePaint, false);
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
    STerrainPaintMaterialChoice regular, junction;
    regular.id = 1; regular.name = QStringLiteral("Grass");
    regular.familyName = QStringLiteral("Grass");
    regular.generatedIds.append(QStringLiteral("hd-grass-01"));
    regular.generatedThumbnails = {QImage(16, 16, QImage::Format_ARGB32)};
    junction.id = 2; junction.supportId = 1;
    junction.name = QStringLiteral("Grass / Water corner");
    junction.familyName = QStringLiteral("Grass");
    junction.cornerOnly = true;
    junction.contextualStamp = true;
    tools.setPaintMaterials({regular, junction}, {}, {}, 2, 2);
    tools.resize(320, 880);
    tools.show();
    QApplication::processEvents();
    auto* scroll = tools.findChild<QScrollArea*>("groundMaterialScroll");
    QVERIFY(scroll);
    QToolButton* mainMaterial = nullptr;
    QToolButton* cornerMaterial = nullptr;
    for(auto* button : scroll->findChildren<QToolButton*>())
    {
        if(button->property("terrainMaterialId").toInt() == 1)
            mainMaterial = button;
        if(button->property("terrainMaterialId").toInt() == 2)
            cornerMaterial = button;
    }
    QVERIFY(mainMaterial);
    QVERIFY(cornerMaterial);
    mainMaterial->click();
    auto* variantTitle = tools.findChild<QLabel*>("terrainVariantStripTitle");
    QVERIFY(variantTitle);
    QCOMPARE(variantTitle->text(), QStringLiteral("Варианты: Grass"));
    bool generatedVariantVisible = false;
    auto* variantScroll = tools.findChild<QScrollArea*>("terrainVariantStripScroll");
    QVERIFY(variantScroll);
    for(auto* button : variantScroll->findChildren<QToolButton*>())
        generatedVariantVisible |= button->text() == QStringLiteral("HD · hd-grass-01");
    QVERIFY(generatedVariantVisible);
    QVERIFY(!mainMaterial->property("terrainCornerOnly").toBool());
    QVERIFY(cornerMaterial->property("terrainCornerOnly").toBool());
    auto* grid = qobject_cast<QGridLayout*>(cornerMaterial->parentWidget()->layout());
    QVERIFY(grid);
    int headingRow = -1, cornerRow = -1, mainRow = -1;
    QLabel* junctionHeading = nullptr;
    for(int i = 0; i < grid->count(); ++i)
    {
        QLayoutItem* item = grid->itemAt(i);
        int row = -1, column = -1, rowSpan = 0, columnSpan = 0;
        grid->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
        if(item->widget() == cornerMaterial) cornerRow = row;
        if(item->widget() == mainMaterial) mainRow = row;
        auto* label = qobject_cast<QLabel*>(item->widget());
        if(label && label->text() == QStringLiteral("Только у стыков"))
        {
            headingRow = row;
            junctionHeading = label;
        }
    }
    QVERIFY(headingRow >= 0);
    QVERIFY(mainRow >= 0 && mainRow < headingRow);
    QVERIFY(cornerRow > headingRow);
    QVERIFY(junctionHeading);
    QVERIFY(junctionHeading->isVisible());
    QVERIFY(junctionHeading->height() > 0);
    QVERIFY(mainMaterial->geometry().bottom() < junctionHeading->geometry().top());
    QVERIFY(cornerMaterial->geometry().top() > junctionHeading->geometry().bottom());
}

void LandscapeToolsWidgetTests::capturesApprovedLandscapePanelScreenshots()
{
    const QString output = qEnvironmentVariable(
        "EI_LANDSCAPE_PANEL_SCREENSHOT_DIR");
    if(output.isEmpty()) return;
    QVERIFY(QDir().mkpath(output));
    QTranslator russian;
    const QString translationPath = QDir::cleanPath(
        QDir::currentPath() + QStringLiteral("/../translations/ei_mapper_ru.qm"));
    QVERIFY2(russian.load(translationPath), qPrintable(translationPath));
    QCoreApplication::installTranslator(&russian);
    const QPalette original = QApplication::palette();
    const QString originalStyle = QApplication::style()->objectName();
    QApplication::setStyle(QStringLiteral("Fusion"));
    const QList<QPair<QString, bool>> themes = {
        {QStringLiteral("light"), false}, {QStringLiteral("dark"), true}};
    const QList<QPair<QString, ELandscapeEditMode>> modes = {
        {QStringLiteral("paint"), ELandscapeEditMode::TilePaint},
        {QStringLiteral("sculpt"), ELandscapeEditMode::Sculpt},
        {QStringLiteral("surfaces"), ELandscapeEditMode::OverlayPaint}};
    for(const auto& theme : themes)
    {
        QPalette palette = original;
        if(theme.second)
        {
            palette.setColor(QPalette::Window, QColor(37, 39, 43));
            palette.setColor(QPalette::WindowText, QColor(235, 237, 240));
            palette.setColor(QPalette::Base, QColor(27, 29, 33));
            palette.setColor(QPalette::AlternateBase, QColor(43, 46, 51));
            palette.setColor(QPalette::Text, QColor(235, 237, 240));
            palette.setColor(QPalette::Button, QColor(48, 51, 57));
            palette.setColor(QPalette::ButtonText, QColor(235, 237, 240));
            palette.setColor(QPalette::Mid, QColor(103, 109, 119));
            palette.setColor(QPalette::Highlight, QColor(49, 105, 75));
            palette.setColor(QPalette::HighlightedText, Qt::white);
            palette.setColor(QPalette::ToolTipBase, QColor(48, 51, 57));
            palette.setColor(QPalette::ToolTipText, QColor(235, 237, 240));
            for(QPalette::ColorGroup group : {QPalette::Active,
                                               QPalette::Inactive,
                                               QPalette::Disabled})
            {
                palette.setColor(group, QPalette::Window, QColor(37, 39, 43));
                palette.setColor(group, QPalette::WindowText, QColor(235, 237, 240));
                palette.setColor(group, QPalette::Base, QColor(27, 29, 33));
                palette.setColor(group, QPalette::AlternateBase, QColor(43, 46, 51));
                palette.setColor(group, QPalette::Text, QColor(235, 237, 240));
                palette.setColor(group, QPalette::Button, QColor(48, 51, 57));
                palette.setColor(group, QPalette::ButtonText, QColor(235, 237, 240));
                palette.setColor(group, QPalette::Mid, QColor(103, 109, 119));
                palette.setColor(group, QPalette::Highlight, QColor(49, 105, 75));
                palette.setColor(group, QPalette::HighlightedText, Qt::white);
                palette.setColor(group, QPalette::ToolTipBase, QColor(48, 51, 57));
                palette.setColor(group, QPalette::ToolTipText, QColor(235, 237, 240));
            }
        }
        QApplication::setPalette(palette);
        for(const auto& mode : modes)
            for(int width : {420, 320})
            {
                CLandscapeTools tools;
                tools.setEditingEnabled(true, false);
                tools.activateMode(mode.second, false);
                STerrainPaintMaterialChoice base, junction;
                base.id = 1; base.name = QStringLiteral("Земля");
                base.familyName = QStringLiteral("Трава");
                base.thumbnail = QImage(24, 24, QImage::Format_ARGB32);
                base.thumbnail.fill(QColor(82, 139, 78));
                junction = base; junction.id = 2;
                junction.name = QStringLiteral("Стык воды");
                junction.cornerOnly = true;
                junction.contextualStamp = true;
                tools.setPaintMaterials({base, junction}, {}, {base, junction}, 2, 2);
                tools.resize(width, 880);
                tools.show();
                QApplication::processEvents();
                if(mode.first == QStringLiteral("paint"))
                {
                    auto* strip = tools.findChild<QScrollArea*>(
                        QStringLiteral("terrainVariantStripScroll"));
                    if(strip)
                    {
                        const auto cards = strip->findChildren<QToolButton*>();
                        if(!cards.isEmpty())
                        {
                            QVERIFY(cards.front()->isVisible());
                            QVERIFY(cards.front()->geometry().height() >= 48);
                            QVERIFY(!cards.front()->icon().isNull());
                        }
                    }
                }
                const QString file = QStringLiteral("%1-%2-%3.png")
                    .arg(theme.first, mode.first).arg(width);
                QVERIFY2(tools.grab().save(QDir(output).filePath(file), "PNG"),
                         qPrintable(file));
                tools.hide();
                QApplication::processEvents();
            }
    }
    QApplication::setPalette(original);
    QApplication::setStyle(originalStyle);
    QCoreApplication::removeTranslator(&russian);
}

QTEST_MAIN(LandscapeToolsWidgetTests)
#include "landscape_tools_widget_tests.moc"
