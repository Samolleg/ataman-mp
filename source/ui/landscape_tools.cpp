#include "landscape_tools.h"
#include "types.h"

#include <QCheckBox>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QIcon brushShapeIcon(bool circle)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(245, 245, 245), 2));
    painter.setBrush(QColor(90, 150, 220));
    const QRectF shape(2.5, 2.5, 27.0, 27.0);
    if(circle) painter.drawEllipse(shape);
    else painter.drawRect(shape);
    return QIcon(pixmap);
}

QString normalizedTerrainFilterText(const QString& value)
{
    QString normalized;
    normalized.reserve(value.size());
    for(const QChar character : value)
        normalized.append(character.isLetterOrNumber()
            ? character.toLower() : QLatin1Char(' '));
    return normalized.simplified();
}

bool terrainFilterMatches(const QString& searchable, const QString& query)
{
    const QString normalizedQuery = normalizedTerrainFilterText(query);
    if(normalizedQuery.isEmpty())
        return true;
    const QString normalizedSearchable =
        normalizedTerrainFilterText(searchable);
    for(const QString& token : normalizedQuery.split(
            QLatin1Char(' '), QString::SkipEmptyParts))
        if(!normalizedSearchable.contains(token))
            return false;
    return true;
}
}

CLandscapeTools::CLandscapeTools(QWidget* parent)
    : QWidget(parent)
    , m_mode(new QComboBox(this))
    , m_tool(new QComboBox(this))
    , m_radius(new QDoubleSpinBox(this))
    , m_strength(new QDoubleSpinBox(this))
    , m_delta(new QDoubleSpinBox(this))
    , m_flatten(new QSpinBox(this))
    , m_flipVertical(new QCheckBox(tr("Flip image vertically"), this))
    , m_materialTabs(new QTabWidget(this))
    , m_paintModeTabs(new QTabWidget(this))
    , m_classicMaterialTabs(new QTabWidget(this))
    , m_paintFilter(new QLineEdit(this))
    , m_modernModePage(new QWidget(this))
    , m_classicModePage(new QWidget(this))
    , m_manualModePage(new QWidget(this))
    , m_groundMaterialScroll(new QScrollArea(this))
    , m_overlayMaterialScroll(new QScrollArea(this))
    , m_groundMaterialPage(new QWidget(this))
    , m_overlayMaterialPage(new QWidget(this))
    , m_groundMaterialGrid(new QGridLayout)
    , m_underwaterMaterialGrid(new QGridLayout)
    , m_overlayMaterialGrid(new QGridLayout)
    , m_groundMaterialButtons(new QButtonGroup(this))
    , m_overlayMaterialButtons(new QButtonGroup(this))
    , m_classicGroundMaterialScroll(new QScrollArea(this))
    , m_classicOverlayMaterialScroll(new QScrollArea(this))
    , m_classicGroundMaterialPage(new QWidget(this))
    , m_classicOverlayMaterialPage(new QWidget(this))
    , m_classicGroundMaterialGrid(new QGridLayout)
    , m_classicUnderwaterMaterialGrid(new QGridLayout)
    , m_classicOverlayMaterialGrid(new QGridLayout)
    , m_classicGroundMaterialButtons(new QButtonGroup(this))
    , m_classicOverlayMaterialButtons(new QButtonGroup(this))
    , m_manualTileList(new QListWidget(this))
    , m_paintRadius(new QSlider(Qt::Horizontal, this))
    , m_paintRadiusValue(new QLabel(this))
    , m_circlePaintShape(new QToolButton(this))
    , m_squarePaintShape(new QToolButton(this))
    , m_tilePaintTarget(new QToolButton(this))
    , m_vertexPaintTarget(new QToolButton(this))
    , m_classicAutoSafe(new QCheckBox(
          tr("Safe: exact authored blends only"), this))
    , m_randomizeUniformRotation(new QCheckBox(
          tr("Vary uniform texture rotation across strokes"), this))
    , m_paintIssueCategory(new QComboBox(this))
    , m_markPaintIssue(new QToolButton(this))
    , m_overlaySurfaceOperation(new QComboBox(this))
    , m_overlayMaterial(new QComboBox(this))
    , m_overlaySurfaceOperationLabel(new QLabel(tr("Overlay operation"), this))
    , m_overlayMaterialLabel(new QLabel(tr("Surface material (tint / animation)"), this))
    , m_showMainTerrain(new QCheckBox(tr("Show main terrain (M)"), this))
    , m_showOverlays(new QCheckBox(tr("Show overlays (J)"), this))
    , m_tilesetStatus(new QLabel(tr("Terrain materials are loaded from the MPR namespace template."), this))
    , m_atlasCoverage(new QLabel(tr("0 / 0 assigned"), this))
    , m_generatedAtlasTiles(new QLabel(tr("Added exact tiles: 0"), this))
    , m_atlasCapacity(new QProgressBar(this))
    , m_atlasCapacityWarning(new QLabel(this))
    , m_sculptControls(new QWidget(this))
    , m_paintControls(new QWidget(this))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    QLabel* warning = new QLabel(tr(
        "Terrain edits are undoable. Changed MPR saves use staged validation, backup, and atomic replacement."), this);
    warning->setWordWrap(true);
    warning->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    warning->setStyleSheet("QLabel { color: #b06000; font-weight: bold; }");
    layout->addWidget(warning);
    m_showMainTerrain->setChecked(true);
    m_showOverlays->setChecked(true);
    layout->addWidget(m_showMainTerrain);
    layout->addWidget(m_showOverlays);

    m_mode->addItem(tr("Sculpt terrain"));
    m_mode->addItem(tr("Paint land tiles"));
    m_mode->addItem(tr("Add / remove overlay surfaces"));
    m_mode->addItem(tr("Paint existing overlays"));
    m_mode->addItem(tr("Sculpt overlay surfaces"));
    QFormLayout* modeForm = new QFormLayout();
    modeForm->addRow(tr("Landscape tool"), m_mode);
    layout->addLayout(modeForm);

    m_tool->addItem(tr("Raise"));
    m_tool->addItem(tr("Lower"));
    m_tool->addItem(tr("Flatten"));
    m_tool->addItem(tr("Smooth"));
    m_radius->setRange(0.5, 64.0);
    m_radius->setValue(4.0);
    m_radius->setSingleStep(0.5);
    m_strength->setRange(0.01, 1.0);
    m_strength->setValue(0.35);
    m_strength->setSingleStep(0.05);
    m_strength->setDecimals(2);
    m_delta->setRange(1.0, 10000.0);
    m_delta->setValue(180.0);
    m_flatten->setRange(0, 65535);
    m_flatten->setValue(20000);

    QSettings settings;
    m_randomizeUniformRotation->setChecked(settings.value(
        "ui/landscape/randomizeUniformRotation", true).toBool());
    m_randomizeUniformRotation->setToolTip(tr(
        "Distributes proven uniform physical variants and deterministic rotations across each stroke interior. "
        "Exact blend rotations remain fixed by their corner identities."));
    m_tool->setCurrentIndex(qBound(0,
        settings.value("ui/landscape/sculptTool", 0).toInt(), m_tool->count() - 1));
    m_radius->setValue(settings.value("ui/landscape/sculptRadius", 4.0).toDouble());
    m_strength->setValue(settings.value("ui/landscape/sculptStrength", 0.35).toDouble());
    m_delta->setValue(settings.value("ui/landscape/sculptDelta", 180.0).toDouble());
    m_flatten->setValue(settings.value("ui/landscape/flattenHeight", 20000).toInt());

    QFormLayout* form = new QFormLayout(m_sculptControls);
    form->addRow(tr("Tool"), m_tool);
    form->addRow(tr("Radius (vertices)"), m_radius);
    form->addRow(tr("Strength"), m_strength);
    form->addRow(tr("Raise/lower step"), m_delta);
    form->addRow(tr("Flatten target"), m_flatten);
    layout->addWidget(m_sculptControls);

    QLabel* help = new QLabel(tr(
        "Left-drag sculpts. Alt+Wheel changes radius; Shift+Wheel changes strength; Wheel keeps camera zoom. Each drag is one undo step."), this);
    help->setWordWrap(true);
    form->addRow(help);
    form->addRow(tr("Heightmap import"), m_flipVertical);
    QPushButton* importButton = new QPushButton(
        tr("Import grayscale heightmap..."), m_sculptControls);
    form->addRow(importButton);

    QVBoxLayout* paintLayout = new QVBoxLayout(m_paintControls);
    paintLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* paintHelp = new QLabel(tr(
        "Z / X / C switches Modern / Classic Auto / Manual. Tile targeting paints complete cells; Vertex targeting edits shared corners. "
        "Modern reuses exact authored junctions and adds a generated exact blend only when one is missing. Classic Auto never changes the atlas: Safe mode applies every available exact connection and leaves only unsupported boundaries untouched. "
        "Manual paints one selected physical atlas tile and rotation directly, without semantic blending. Hold right-click and move to sample the nearest material vertex, or the physical tile in Manual. "
        "Alt+Wheel changes brush size. Shift+Wheel selects the automatic material or rotates the manual tile; Wheel keeps camera zoom. "
        "Overlay surface mode adds/removes coverage. Its Terrain material selects the atlas texture; its Surface material independently controls tint, opacity, illumination, waves, and texture warp. "
        "Overlay painting only changes the atlas texture on cells that already exist."), this);
    paintHelp->setWordWrap(true);
    paintHelp->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    paintLayout->addWidget(paintHelp);
    m_tilesetStatus->setWordWrap(true);
    m_tilesetStatus->setObjectName(
        QStringLiteral("terrainPaintTilesetStatus"));
    m_tilesetStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    paintLayout->addWidget(m_tilesetStatus);

    QVBoxLayout* groundPalette = new QVBoxLayout(m_groundMaterialPage);
    groundPalette->setContentsMargins(6, 6, 6, 6);
    groundPalette->addWidget(new QLabel(tr("Ground"), m_groundMaterialPage));
    QWidget* groundGrid = new QWidget(m_groundMaterialPage);
    groundGrid->setLayout(m_groundMaterialGrid);
    groundPalette->addWidget(groundGrid);
    groundPalette->addSpacing(8);
    groundPalette->addWidget(new QLabel(tr("Underwater"), m_groundMaterialPage));
    QWidget* underwaterGrid = new QWidget(m_groundMaterialPage);
    underwaterGrid->setLayout(m_underwaterMaterialGrid);
    groundPalette->addWidget(underwaterGrid);
    groundPalette->addStretch(1);
    QVBoxLayout* overlayPalette = new QVBoxLayout(m_overlayMaterialPage);
    overlayPalette->setContentsMargins(6, 6, 6, 6);
    overlayPalette->addWidget(new QLabel(tr("Overlay textures"), m_overlayMaterialPage));
    QWidget* overlayGrid = new QWidget(m_overlayMaterialPage);
    overlayGrid->setLayout(m_overlayMaterialGrid);
    overlayPalette->addWidget(overlayGrid);
    overlayPalette->addStretch(1);
    const auto configurePaletteScroll = [](QScrollArea* scroll, QWidget* page) {
        scroll->setWidgetResizable(true);
        scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        scroll->setMinimumSize(0, 0);
        scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setWidget(page);
    };
    configurePaletteScroll(m_groundMaterialScroll, m_groundMaterialPage);
    configurePaletteScroll(m_overlayMaterialScroll, m_overlayMaterialPage);
    m_groundMaterialScroll->setObjectName(QStringLiteral("groundMaterialScroll"));
    m_overlayMaterialScroll->setObjectName(QStringLiteral("overlayMaterialScroll"));
    m_materialTabs->addTab(m_groundMaterialScroll, tr("Ground"));
    m_materialTabs->addTab(m_overlayMaterialScroll, tr("Overlay"));
    m_materialTabs->setObjectName(QStringLiteral("modernMaterialLayerTabs"));

    QVBoxLayout* classicGroundPalette = new QVBoxLayout(
        m_classicGroundMaterialPage);
    classicGroundPalette->setContentsMargins(6, 6, 6, 6);
    classicGroundPalette->addWidget(new QLabel(
        tr("Existing ground designations"), m_classicGroundMaterialPage));
    QWidget* classicGroundGrid = new QWidget(m_classicGroundMaterialPage);
    classicGroundGrid->setLayout(m_classicGroundMaterialGrid);
    classicGroundPalette->addWidget(classicGroundGrid);
    classicGroundPalette->addSpacing(8);
    classicGroundPalette->addWidget(new QLabel(
        tr("Existing underwater designations"),
        m_classicGroundMaterialPage));
    QWidget* classicUnderwaterGrid = new QWidget(
        m_classicGroundMaterialPage);
    classicUnderwaterGrid->setLayout(m_classicUnderwaterMaterialGrid);
    classicGroundPalette->addWidget(classicUnderwaterGrid);
    classicGroundPalette->addStretch(1);
    QVBoxLayout* classicOverlayPalette = new QVBoxLayout(
        m_classicOverlayMaterialPage);
    classicOverlayPalette->setContentsMargins(6, 6, 6, 6);
    classicOverlayPalette->addWidget(new QLabel(
        tr("Existing overlay designations"),
        m_classicOverlayMaterialPage));
    QWidget* classicOverlayGrid = new QWidget(
        m_classicOverlayMaterialPage);
    classicOverlayGrid->setLayout(m_classicOverlayMaterialGrid);
    classicOverlayPalette->addWidget(classicOverlayGrid);
    classicOverlayPalette->addStretch(1);
    configurePaletteScroll(m_classicGroundMaterialScroll,
                           m_classicGroundMaterialPage);
    configurePaletteScroll(m_classicOverlayMaterialScroll,
                           m_classicOverlayMaterialPage);
    m_classicGroundMaterialScroll->setObjectName(
        QStringLiteral("classicGroundMaterialScroll"));
    m_classicOverlayMaterialScroll->setObjectName(
        QStringLiteral("classicOverlayMaterialScroll"));
    m_classicMaterialTabs->addTab(m_classicGroundMaterialScroll,
                                  tr("Ground"));
    m_classicMaterialTabs->addTab(m_classicOverlayMaterialScroll,
                                  tr("Overlay"));
    m_classicMaterialTabs->setObjectName(
        QStringLiteral("classicMaterialLayerTabs"));

    QVBoxLayout* modernModeLayout = new QVBoxLayout(m_modernModePage);
    modernModeLayout->setContentsMargins(0, 0, 0, 0);
    modernModeLayout->addWidget(m_materialTabs);
    QVBoxLayout* classicModeLayout = new QVBoxLayout(m_classicModePage);
    classicModeLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* classicHelp = new QLabel(tr(
        "Uses only tiles already present in this atlas. Exact authored blends "
        "win. Safe mode keeps every valid connection and leaves unsupported "
        "boundary cells untouched instead of forcing a uniform tile."),
        m_classicModePage);
    classicHelp->setWordWrap(true);
    classicModeLayout->addWidget(classicHelp);
    m_classicAutoSafe->setObjectName(QStringLiteral("classicAutoSafeMode"));
    m_classicAutoSafe->setChecked(settings.value(
        QStringLiteral("ui/landscape/classicAutoSafe"), true).toBool());
    m_classicAutoSafe->setToolTip(tr(
        "Apply every exact authored connection that is available and leave "
        "unsupported boundary cells untouched. Disable this only when a local "
        "uniform hard edge is preferable to an unchanged gap."));
    classicModeLayout->addWidget(m_classicAutoSafe);
    classicModeLayout->addWidget(m_classicMaterialTabs);
    QVBoxLayout* manualModeLayout = new QVBoxLayout(m_manualModePage);
    manualModeLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* manualHelp = new QLabel(tr(
        "Paints one selected physical atlas slot and rotation directly, without blending."),
        m_manualModePage);
    manualHelp->setWordWrap(true);
    manualModeLayout->addWidget(manualHelp);
    m_manualTileList->setObjectName(QStringLiteral("manualAtlasTileList"));
    m_manualTileList->setIconSize(QSize(52, 52));
    m_manualTileList->setUniformItemSizes(true);
    m_manualTileList->setViewMode(QListView::IconMode);
    m_manualTileList->setFlow(QListView::LeftToRight);
    m_manualTileList->setWrapping(true);
    m_manualTileList->setResizeMode(QListView::Adjust);
    m_manualTileList->setMovement(QListView::Static);
    m_manualTileList->setGridSize(QSize(62, 62));
    m_manualTileList->setSpacing(2);
    m_manualTileList->setWordWrap(false);
    m_manualTileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    manualModeLayout->addWidget(m_manualTileList);

    m_paintFilter->setObjectName(QStringLiteral("terrainPaintFilter"));
    m_paintFilter->setPlaceholderText(tr("Filter materials or atlas slots..."));
    m_paintFilter->setClearButtonEnabled(true);
    paintLayout->addWidget(m_paintFilter);
    m_paintModeTabs->addTab(m_modernModePage, tr("Modern"));
    m_paintModeTabs->addTab(m_classicModePage, tr("Classic Auto"));
    m_paintModeTabs->addTab(m_manualModePage, tr("Manual"));
    m_paintModeTabs->setObjectName(QStringLiteral("materialPaletteTabs"));
    // The palette consumes the remaining height, but it must be allowed to
    // shrink and scroll when the dock is short. A fixed minimum here makes Qt
    // place the following brush controls inside the tab geometry on the first
    // Classic Auto layout.
    m_paintModeTabs->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Expanding);
    m_paintModeTabs->setMinimumHeight(100);
    paintLayout->addWidget(m_paintModeTabs, 1);

    m_paintRadius->setRange(1, 33);
    m_paintRadius->setObjectName(QStringLiteral("terrainPaintRadius"));
    m_paintRadius->setValue(qBound(1,
        settings.value("ui/landscape/paintSize", 1).toInt(), 33));
    m_paintRadiusValue->setText(QString::number(m_paintRadius->value()));
    m_circlePaintShape->setIcon(brushShapeIcon(true));
    m_circlePaintShape->setIconSize(QSize(32, 32));
    m_circlePaintShape->setFixedSize(38, 38);
    m_circlePaintShape->setToolTip(tr("Circular brush"));
    m_circlePaintShape->setCheckable(true);
    m_circlePaintShape->setChecked(true);
    m_squarePaintShape->setIcon(brushShapeIcon(false));
    m_squarePaintShape->setIconSize(QSize(32, 32));
    m_squarePaintShape->setFixedSize(38, 38);
    m_squarePaintShape->setToolTip(tr("Square brush"));
    m_squarePaintShape->setCheckable(true);
    const QString strongChoiceStyle = QStringLiteral(
        "QToolButton { border: 2px solid palette(mid); padding: 2px; }"
        "QToolButton:hover { border-color: #ffd34e; }"
        "QToolButton:checked { border: 4px solid #ffd22e; "
        "background: #6b4c00; padding: 0px; }");
    m_circlePaintShape->setObjectName(QStringLiteral("circlePaintShape"));
    m_squarePaintShape->setObjectName(QStringLiteral("squarePaintShape"));
    m_circlePaintShape->setStyleSheet(strongChoiceStyle);
    m_squarePaintShape->setStyleSheet(strongChoiceStyle);
    QButtonGroup* shapeButtons = new QButtonGroup(this);
    shapeButtons->setExclusive(true);
    shapeButtons->addButton(m_circlePaintShape);
    shapeButtons->addButton(m_squarePaintShape);
    m_tilePaintTarget->setText(tr("Tile"));
    m_vertexPaintTarget->setText(tr("Vertex"));
    m_tilePaintTarget->setObjectName(QStringLiteral("tilePaintTarget"));
    m_vertexPaintTarget->setObjectName(QStringLiteral("vertexPaintTarget"));
    m_tilePaintTarget->setCheckable(true);
    m_vertexPaintTarget->setCheckable(true);
    m_tilePaintTarget->setStyleSheet(strongChoiceStyle);
    m_vertexPaintTarget->setStyleSheet(strongChoiceStyle);
    QButtonGroup* targetButtons = new QButtonGroup(this);
    targetButtons->setExclusive(true);
    targetButtons->addButton(m_tilePaintTarget);
    targetButtons->addButton(m_vertexPaintTarget);
    const int savedTarget = qBound(0, settings.value(
        QStringLiteral("ui/landscape/paintTargetMode"), 0).toInt(), 1);
    m_tilePaintTarget->setChecked(savedTarget == 0);
    m_vertexPaintTarget->setChecked(savedTarget == 1);
    m_overlaySurfaceOperation->addItem(tr("Add surface"));
    m_overlaySurfaceOperation->addItem(tr("Remove surface"));
    m_overlayMaterial->setToolTip(tr(
        "Selects the existing MPR material record stored on newly added overlay cells. "
        "It controls liquid/surface rendering properties, not the atlas texture selected above."));
    QFormLayout* paintForm = new QFormLayout();
    QHBoxLayout* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(m_paintRadius, 1);
    sizeRow->addWidget(m_paintRadiusValue);
    QWidget* sizeWidget = new QWidget(this);
    sizeWidget->setLayout(sizeRow);
    QHBoxLayout* shapeRow = new QHBoxLayout;
    shapeRow->addWidget(m_circlePaintShape);
    shapeRow->addWidget(m_squarePaintShape);
    shapeRow->addStretch(1);
    QWidget* shapeWidget = new QWidget(this);
    shapeWidget->setLayout(shapeRow);
    QHBoxLayout* targetRow = new QHBoxLayout;
    targetRow->addWidget(m_tilePaintTarget);
    targetRow->addWidget(m_vertexPaintTarget);
    targetRow->addStretch(1);
    QWidget* targetWidget = new QWidget(this);
    targetWidget->setLayout(targetRow);
    paintForm->addRow(tr("Brush size (tiles / vertices)"), sizeWidget);
    paintForm->addRow(tr("Brush shape"), shapeWidget);
    paintForm->addRow(tr("Paint target"), targetWidget);
    paintForm->addRow(m_randomizeUniformRotation);
    paintForm->addRow(m_overlaySurfaceOperationLabel, m_overlaySurfaceOperation);
    paintForm->addRow(m_overlayMaterialLabel, m_overlayMaterial);
    paintLayout->addLayout(paintForm);
    QPushButton* resolveMissing = new QPushButton(
        tr("Resolve missing blends..."), this);
    resolveMissing->setObjectName(QStringLiteral("resolveMissingBlendsButton"));
    resolveMissing->setToolTip(tr(
        "Scan the complete map and rebuild missing or inconsistent physical "
        "blend tiles from its shared-corner material field. The map repair is "
        "committed as one undo step. Shortcut: Shift+R."));
    paintLayout->addWidget(resolveMissing);
    QPushButton* reviewAtlas = new QPushButton(tr("Material atlas..."), this);
    reviewAtlas->setObjectName("materialAtlasButton");
    reviewAtlas->setToolTip(tr(
        "Inspect material thumbnails and visually assign unusual atlas tiles. "
        "This changes only the editor profile, not atlas images or MPR resources."));
    m_atlasCoverage->setWordWrap(true);
    m_generatedAtlasTiles->setWordWrap(true);
    m_atlasCapacity->setObjectName(QStringLiteral("terrainAtlasCapacity"));
    m_atlasCapacity->setRange(0, 16384);
    m_atlasCapacity->setTextVisible(true);
    m_atlasCapacityWarning->setObjectName(
        QStringLiteral("terrainAtlasCapacityWarning"));
    m_atlasCapacityWarning->setWordWrap(true);
    m_atlasCoverage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_generatedAtlasTiles->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QVBoxLayout* atlasRow = new QVBoxLayout;
    atlasRow->addWidget(reviewAtlas);
    atlasRow->addWidget(m_atlasCoverage);
    atlasRow->addWidget(m_generatedAtlasTiles);
    atlasRow->addWidget(m_atlasCapacity);
    atlasRow->addWidget(m_atlasCapacityWarning);
    paintLayout->addLayout(atlasRow);
    QPushButton* exportDiagnostic = new QPushButton(tr("Export paint diagnostic..."), this);
    m_paintIssueCategory->addItem(tr("No / wrong blend"), QStringLiteral("no-blend"));
    m_paintIssueCategory->addItem(tr("Wrong material / family"), QStringLiteral("tile-mismatch"));
    m_paintIssueCategory->addItem(tr("Wrong rotation"), QStringLiteral("wrong-rotation"));
    m_paintIssueCategory->setObjectName(QStringLiteral("paintIssueCategory"));
    m_markPaintIssue->setText(tr("Mark bad tiles on map"));
    m_markPaintIssue->setObjectName(QStringLiteral("markPaintIssueButton"));
    m_markPaintIssue->setCheckable(true);
    m_markPaintIssue->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_markPaintIssue->setStyleSheet(QStringLiteral(
        "QToolButton:checked { background: #9f1f1f; color: white; "
        "font-weight: bold; border: 2px solid #ff6b6b; }"));
    m_markPaintIssue->setToolTip(tr(
        "Choose an issue type, enable marking, then drag across every visibly bad tile. "
        "Painting is disabled while marking. Each drag appends exact tile, corner, rotation, "
        "atlas-binding, and stroke information to a portable diagnostic JSON."));
    QHBoxLayout* issueRow = new QHBoxLayout;
    issueRow->addWidget(m_paintIssueCategory, 1);
    issueRow->addWidget(m_markPaintIssue);
    QPushButton* saveDraft = new QPushButton(
        tr("Save editable modern draft as..."), this);
    saveDraft->setObjectName(QStringLiteral("saveModernDraftButton"));
    saveDraft->setToolTip(tr(
        "Keep unfinished modern painting in a separate MPR plus map-local atlas. "
        "Normal Save MPR and Save MPR As publish game-ready atlas pages into "
        "textures.res instead."));
    paintLayout->addWidget(exportDiagnostic);
    paintLayout->addLayout(issueRow);
    paintLayout->addWidget(saveDraft);
    // The material palette is the primary authoring surface.
    layout->addWidget(m_paintControls, 1);
    connect(importButton, &QPushButton::clicked, this, &CLandscapeTools::importHeightmapRequested);
    connect(resolveMissing, &QPushButton::clicked,
            this, &CLandscapeTools::resolveMissingBlendsRequested);
    connect(reviewAtlas, &QPushButton::clicked, this, &CLandscapeTools::atlasProfileReviewRequested);
    connect(exportDiagnostic, &QPushButton::clicked, this, &CLandscapeTools::exportPaintDiagnosticRequested);
    connect(m_markPaintIssue, &QToolButton::toggled, this, [this](bool enabled) {
        if(enabled)
        {
            clearPaintMaterialSelection();
            emit paintTerrainTypeChanged(-1);
        }
        updatePaintIssueUi();
        emit editingStateChanged();
    });
    connect(m_paintIssueCategory,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { updatePaintIssueUi(); emit editingStateChanged(); });
    connect(saveDraft, &QPushButton::clicked,
            this, &CLandscapeTools::saveModernDraftRequested);
    m_atlasCoverage->setObjectName(QStringLiteral("materialAtlasSourceStatus"));
    m_generatedAtlasTiles->setObjectName(QStringLiteral("generatedAtlasTileCount"));
    refreshAtlasCapacity(0);
    connect(m_groundMaterialButtons, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, [this](int) {
                setPaintIssueMarkingEnabled(false);
                bool selected = false;
                const qint32 material = paintTerrainType(&selected);
                emit paintTerrainTypeChanged(selected ? material : -1);
                // QButtonGroup is still processing the click when the marker
                // is disabled. Notify once more after the material is fully
                // selected so the view restores its tile-brush operation.
                emit editingStateChanged();
            });
    connect(m_overlayMaterialButtons, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, [this](int) {
                setPaintIssueMarkingEnabled(false);
                bool selected = false;
                const qint32 material = paintTerrainType(&selected);
                emit paintTerrainTypeChanged(selected ? material : -1);
                emit editingStateChanged();
            });
    const auto connectClassicGroup = [this](QButtonGroup* group) {
        connect(group, QOverload<int>::of(&QButtonGroup::buttonClicked),
                this, [this](int) {
            setPaintIssueMarkingEnabled(false);
            bool selected = false;
            const qint32 material = paintTerrainType(&selected);
            emit paintTerrainTypeChanged(selected ? material : -1);
            emit editingStateChanged();
        });
    };
    connectClassicGroup(m_classicGroundMaterialButtons);
    connectClassicGroup(m_classicOverlayMaterialButtons);
    connect(m_materialTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if(!m_editingActive)
            return;
        if(index == 0 && (editMode() == ELandscapeEditMode::OverlayPaint
                          || editMode() == ELandscapeEditMode::OverlaySurface))
            activateMode(ELandscapeEditMode::TilePaint);
        else if(index == 1 && editMode() == ELandscapeEditMode::TilePaint)
            activateMode(ELandscapeEditMode::OverlayPaint);
    });
    connect(m_classicMaterialTabs, &QTabWidget::currentChanged, this,
            [this](int index) {
        if(!m_editingActive)
            return;
        if(index == 0 && (editMode() == ELandscapeEditMode::OverlayPaint
                          || editMode() == ELandscapeEditMode::OverlaySurface))
            activateMode(ELandscapeEditMode::TilePaint);
        else if(index == 1 && editMode() == ELandscapeEditMode::TilePaint)
            activateMode(ELandscapeEditMode::OverlayPaint);
    });
    connect(m_paintModeTabs, &QTabWidget::currentChanged, this,
            [this](int index) {
        QSettings().setValue(QStringLiteral("ui/landscape/paintPaletteMode"),
                             index);
        setPaintIssueMarkingEnabled(false);
        if(paintPaletteMode() == ETerrainPaintPaletteMode::Manual
           && m_tilePaintTarget && !m_tilePaintTarget->isChecked())
        {
            const QSignalBlocker tileBlocker(m_tilePaintTarget);
            const QSignalBlocker vertexBlocker(m_vertexPaintTarget);
            m_tilePaintTarget->setChecked(true);
            if(m_vertexPaintTarget)
                m_vertexPaintTarget->setChecked(false);
        }
        updatePaintIssueUi();
        bool selected = false;
        const qint32 material = paintTerrainType(&selected);
        emit paintTerrainTypeChanged(selected ? material : -1);
        emit editingStateChanged();
        applyPaintFilter();
        QTimer::singleShot(0, this, [this]() { refreshPaletteColumns(); });
    });
    m_paintModeTabs->setCurrentIndex(qBound(0,
        settings.value(QStringLiteral("ui/landscape/paintPaletteMode"), 0)
            .toInt(), 2));
    connect(m_paintFilter, &QLineEdit::textChanged, this,
            [this](const QString&) { applyPaintFilter(); });
    connect(m_manualTileList, &QListWidget::itemSelectionChanged, this,
            [this]() {
        const QListWidgetItem* item = m_manualTileList->currentItem();
        if(!item)
            return;
        emit manualAtlasTileRequested(item->data(Qt::UserRole).toInt());
        emit paintTerrainTypeChanged(-1);
        emit editingStateChanged();
    });
    connect(m_paintRadius, &QSlider::valueChanged, this, [this](int value) {
        m_paintRadiusValue->setText(QString::number(value));
        QSettings().setValue("ui/landscape/paintSize", value);
        emit editingStateChanged();
    });
    connect(m_circlePaintShape, &QToolButton::toggled, this,
            [this](bool) { emit editingStateChanged(); });
    connect(m_squarePaintShape, &QToolButton::toggled, this,
            [this](bool) { emit editingStateChanged(); });
    const auto targetChanged = [this](bool checked) {
        if(!checked)
            return;
        updatePaintShapeUi();
        QSettings().setValue(QStringLiteral("ui/landscape/paintTargetMode"),
                             int(paintTargetMode()));
        applyPaintFilter();
        emit editingStateChanged();
    };
    connect(m_tilePaintTarget, &QToolButton::toggled, this, targetChanged);
    connect(m_vertexPaintTarget, &QToolButton::toggled, this, targetChanged);
    connect(m_classicAutoSafe, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("ui/landscape/classicAutoSafe"),
                             enabled);
        refreshPaintTerrainTypes();
        emit editingStateChanged();
    });
    connect(m_overlaySurfaceOperation, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int)
    {
        updateModeVisibility();
        emit editingStateChanged();
    });
    connect(m_randomizeUniformRotation, &QCheckBox::toggled, this,
            [](bool enabled) {
                QSettings().setValue(
                    "ui/landscape/randomizeUniformRotation", enabled);
            });
    const auto visibilityChanged = [this](bool)
    {
        emit terrainVisibilityChanged(m_showMainTerrain->isChecked(),
                                      m_showOverlays->isChecked());
    };
    connect(m_showMainTerrain, &QCheckBox::toggled, this, visibilityChanged);
    connect(m_showOverlays, &QCheckBox::toggled, this, visibilityChanged);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        updateModeVisibility();
        refreshPaintTerrainTypes();
        emit editingStateChanged();
    });
    connect(m_tool, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [](int value) { QSettings().setValue("ui/landscape/sculptTool", value); });
    connect(m_radius, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [](double value) { QSettings().setValue("ui/landscape/sculptRadius", value); });
    connect(m_strength, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [](double value) { QSettings().setValue("ui/landscape/sculptStrength", value); });
    connect(m_delta, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [](double value) { QSettings().setValue("ui/landscape/sculptDelta", value); });
    connect(m_flatten, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int value) { QSettings().setValue("ui/landscape/flattenHeight", value); });
    updateModeVisibility();
    updatePaintIssueUi();
    updatePaintShapeUi();
}

QSize CLandscapeTools::minimumSizeHint() const
{
    QSize hint = QWidget::minimumSizeHint();
    // Wrapped help/status text and long action captions must not force the
    // dock to occupy half of a wide display. They reflow correctly at compact
    // widths, while the material palette retains the remaining height.
    hint.setWidth(qMin(hint.width(), 320));
    return hint;
}

bool CLandscapeTools::editingEnabled() const { return m_editingActive; }
ELandscapeEditMode CLandscapeTools::editMode() const
{
    return static_cast<ELandscapeEditMode>(m_mode->currentIndex());
}
bool CLandscapeTools::sculptEnabled() const
{
    return editingEnabled()
        && (editMode() == ELandscapeEditMode::Sculpt
            || editMode() == ELandscapeEditMode::OverlaySculpt);
}
bool CLandscapeTools::overlaySculptEnabled() const
{
    return editingEnabled() && editMode() == ELandscapeEditMode::OverlaySculpt;
}
bool CLandscapeTools::tilePaintEnabled() const
{
    return editingEnabled() && editMode() == ELandscapeEditMode::TilePaint;
}
bool CLandscapeTools::overlayPaintEnabled() const
{
    return editingEnabled()
        && (editMode() == ELandscapeEditMode::OverlayPaint
            || editMode() == ELandscapeEditMode::OverlaySurface);
}
bool CLandscapeTools::overlaySurfaceEnabled() const
{
    return editingEnabled() && editMode() == ELandscapeEditMode::OverlaySurface;
}
void CLandscapeTools::activateMode(ELandscapeEditMode mode, bool notify)
{
    {
        const QSignalBlocker modeBlocker(m_mode);
        m_mode->setCurrentIndex(int(mode));
        m_editingActive = true;
    }
    updateModeVisibility();
    // Programmatic mode changes block the combo-box signal, so refresh the
    // layer-specific list explicitly. This prevents an overlay family from
    // remaining selected while the view is already painting land.
    refreshPaintTerrainTypes();
    if(notify)
        emit editingStateChanged();
}
void CLandscapeTools::setEditingEnabled(bool enabled, bool notify)
{
    if(m_editingActive == enabled)
        return;
    m_editingActive = enabled;
    if(notify)
        emit editingStateChanged();
}
void CLandscapeTools::updateModeVisibility()
{
    const bool sculpt = editMode() == ELandscapeEditMode::Sculpt
        || editMode() == ELandscapeEditMode::OverlaySculpt;
    m_sculptControls->setVisible(sculpt);
    m_paintControls->setVisible(!sculpt);
    const bool overlaySurface = editMode() == ELandscapeEditMode::OverlaySurface;
    const bool overlayMode = overlaySurface
        || editMode() == ELandscapeEditMode::OverlayPaint;
    {
        const QSignalBlocker blocker(m_materialTabs);
        m_materialTabs->setCurrentIndex(overlayMode ? 1 : 0);
    }
    {
        const QSignalBlocker blocker(m_classicMaterialTabs);
        m_classicMaterialTabs->setCurrentIndex(overlayMode ? 1 : 0);
    }
    m_overlaySurfaceOperation->setVisible(overlaySurface);
    m_overlaySurfaceOperationLabel->setVisible(overlaySurface);
    const bool activating = overlaySurface
        && overlaySurfaceOperation() == EOverlaySurfaceOperation::Activate;
    m_overlayMaterial->setVisible(activating);
    m_overlayMaterialLabel->setVisible(activating);
    m_paintModeTabs->setEnabled((!overlaySurface || activating)
                                && !paintIssueMarkingEnabled());
}
ELandscapeSculptTool CLandscapeTools::sculptTool() const
{
    return static_cast<ELandscapeSculptTool>(m_tool->currentIndex());
}
float CLandscapeTools::radius() const { return float(m_radius->value()); }
float CLandscapeTools::strength() const { return float(m_strength->value()); }
float CLandscapeTools::rawDelta() const { return float(m_delta->value()); }
quint16 CLandscapeTools::flattenHeight() const { return quint16(m_flatten->value()); }
bool CLandscapeTools::flipHeightmapVertically() const { return m_flipVertical->isChecked(); }
float CLandscapeTools::paintRadius() const
{
    // UI size 1 is one terrain vertex. The terrain model uses a zero-based
    // radius so subsequent integer sizes grow symmetrically around it.
    return float(m_paintRadius->value() - 1.0);
}

void CLandscapeTools::setGeneratedAtlasTileCount(int count)
{
    const int added = qMax(0, count);
    const int active = m_authoredAtlasSlots + added;
    m_generatedAtlasTiles->setText(tr(
        "%1 original + %2 added exact = %3 active slots")
        .arg(m_authoredAtlasSlots).arg(added).arg(active));
    m_generatedAtlasTiles->setToolTip(tr(
        "Original slots remain byte-identical. Added exact tiles include reused stock JMV blends and newly generated combinations cached for this map session."));
    refreshAtlasCapacity(active);
}

void CLandscapeTools::refreshAtlasCapacity(int activeSlots)
{
    const int boundedSlots = qBound(0, activeSlots, 16384);
    QColor color;
    QString tier;
    if(activeSlots <= 512)
    {
        color = QColor(53, 168, 83);
        tier = tr("shipped-size range");
    }
    else if(activeSlots <= 2048)
    {
        color = QColor(235, 190, 35);
        tier = tr("expanded range");
    }
    else if(activeSlots <= 4096)
    {
        color = QColor(232, 126, 32);
        tier = tr("high-memory range");
    }
    else
    {
        color = QColor(205, 55, 55);
        tier = tr("unvalidated / potentially unstable range");
    }
    m_atlasCapacity->setValue(boundedSlots);
    m_atlasCapacity->setFormat(tr("%1 / 16384 slots - %2")
        .arg(activeSlots).arg(tier));
    m_atlasCapacity->setStyleSheet(QStringLiteral(
        "QProgressBar { text-align: center; } "
        "QProgressBar::chunk { background-color: %1; }")
        .arg(color.name()));
    const bool unstable = activeSlots > 4096;
    m_atlasCapacityWarning->setVisible(unstable);
    m_atlasCapacityWarning->setText(unstable ? tr(
        "Potential instability: this atlas is above 4096 slots. Staging remains available, but runtime memory limits depend on the rest of the map.")
        : QString());
    m_atlasCapacityWarning->setStyleSheet(
        QStringLiteral("QLabel { color: #c73737; font-weight: bold; }"));
}
ETerrainTileBrushShape CLandscapeTools::paintShape() const
{
    return m_squarePaintShape->isChecked()
        ? ETerrainTileBrushShape::Square : ETerrainTileBrushShape::Circle;
}
bool CLandscapeTools::manualTileSelectionEnabled() const
{
    return editMode() != ELandscapeEditMode::OverlaySurface
        && paintPaletteMode() == ETerrainPaintPaletteMode::Manual;
}
bool CLandscapeTools::classicAutoPaintingEnabled() const
{
    return editMode() != ELandscapeEditMode::OverlaySurface
        && paintPaletteMode() == ETerrainPaintPaletteMode::ClassicAuto;
}
ETerrainPaintPaletteMode CLandscapeTools::paintPaletteMode() const
{
    return static_cast<ETerrainPaintPaletteMode>(
        qBound(0, m_paintModeTabs ? m_paintModeTabs->currentIndex() : 0, 2));
}
void CLandscapeTools::setPaintPaletteMode(ETerrainPaintPaletteMode mode)
{
    if(!m_paintModeTabs)
        return;
    m_paintModeTabs->setCurrentIndex(qBound(0, int(mode), 2));
}
ETerrainPaintTargetMode CLandscapeTools::paintTargetMode() const
{
    if(manualTileSelectionEnabled())
        return ETerrainPaintTargetMode::Tile;
    return m_vertexPaintTarget && m_vertexPaintTarget->isChecked()
        ? ETerrainPaintTargetMode::Vertex : ETerrainPaintTargetMode::Tile;
}
bool CLandscapeTools::classicAutoSafeMode() const
{
    return m_classicAutoSafe && m_classicAutoSafe->isChecked();
}
bool CLandscapeTools::randomizeUniformRotation() const
{
    return m_randomizeUniformRotation->isChecked();
}
bool CLandscapeTools::landFamiliesAllowedInOverlay() const
{
    return false;
}
EOverlaySurfaceOperation CLandscapeTools::overlaySurfaceOperation() const
{
    if(editMode() == ELandscapeEditMode::OverlayPaint)
        return EOverlaySurfaceOperation::PaintExisting;
    return m_overlaySurfaceOperation->currentIndex() == 0
        ? EOverlaySurfaceOperation::Activate
        : EOverlaySurfaceOperation::Deactivate;
}
qint16 CLandscapeTools::overlayMaterialIndex(bool* selected) const
{
    const QVariant value = m_overlayMaterial->currentData();
    if(selected)
        *selected = value.isValid();
    return value.isValid() ? qint16(value.toInt()) : qint16(-1);
}
void CLandscapeTools::setOverlayMaterials(const QVector<SMaterial>& materials)
{
    const QVariant previous = m_overlayMaterial->currentData();
    const QSignalBlocker blocker(m_overlayMaterial);
    m_overlayMaterial->clear();
    for(int index = 0; index < materials.size(); ++index)
    {
        const SMaterial& material = materials.at(index);
        const QString kind = material.type == eTerrainWater ? tr("water") : tr("surface");
        m_overlayMaterial->addItem(
            tr("%1 — %2; RGB %3/%4/%5, opacity %6%, light %7, wave %8, warp %9")
                .arg(index).arg(kind)
                .arg(qRound(qBound(0.0f, material.R, 1.0f) * 255.0f))
                .arg(qRound(qBound(0.0f, material.G, 1.0f) * 255.0f))
                .arg(qRound(qBound(0.0f, material.B, 1.0f) * 255.0f))
                .arg(qRound(qBound(0.0f, material.A, 1.0f) * 100.0f))
                .arg(material.selfIllumination, 0, 'f', 2)
                .arg(material.waveMultiplier, 0, 'f', 2)
                .arg(material.warpSpeed, 0, 'f', 2), index);
    }
    const int previousIndex = previous.isValid()
        ? m_overlayMaterial->findData(previous) : -1;
    m_overlayMaterial->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
}
void CLandscapeTools::adjustSculptRadius(int steps)
{
    m_radius->setValue(m_radius->value() + steps * m_radius->singleStep());
}
void CLandscapeTools::adjustSculptStrength(int steps)
{
    m_strength->setValue(m_strength->value() + steps * m_strength->singleStep());
}
void CLandscapeTools::adjustPaintSize(int steps)
{
    m_paintRadius->setValue(m_paintRadius->value() + steps);
}
void CLandscapeTools::adjustPaintTerrainType(int steps)
{
    QButtonGroup* group = activeMaterialButtons();
    if(!group)
        return;
    const QList<QAbstractButton*> buttons = group->buttons();
    if(buttons.isEmpty())
        return;
    int current = buttons.indexOf(group->checkedButton());
    current = current < 0 ? 0 : current;
    const int next = (current + steps % buttons.size() + buttons.size()) % buttons.size();
    buttons.at(next)->click();
}
qint32 CLandscapeTools::paintTerrainType(bool* selected) const
{
    const QButtonGroup* group = activeMaterialButtons();
    const QAbstractButton* button = group ? group->checkedButton() : nullptr;
    const bool hiddenStamp = button
        && paintPaletteMode() == ETerrainPaintPaletteMode::ClassicAuto
        && paintTargetMode() == ETerrainPaintTargetMode::Tile
        && button->property("terrainCornerOnly").toBool();
    const int id = button && !hiddenStamp
        ? button->property("terrainMaterialId").toInt() : -1;
    if(selected)
        *selected = id >= 0;
    return id >= 0 ? id : 0;
}
qint32 CLandscapeTools::paintTerrainSupport() const
{
    const QButtonGroup* group = activeMaterialButtons();
    const QAbstractButton* button = group ? group->checkedButton() : nullptr;
    return button && button->property("terrainContextualStamp").toBool()
        ? button->property("terrainSupportMaterialId").toInt() : 0;
}
bool CLandscapeTools::paintIssueMarkingEnabled() const
{
    return m_markPaintIssue && m_markPaintIssue->isChecked();
}
QString CLandscapeTools::paintIssueCategory() const
{
    return m_paintIssueCategory
        ? m_paintIssueCategory->currentData().toString()
        : QStringLiteral("no-blend");
}
void CLandscapeTools::setPaintIssueMarkingEnabled(bool enabled)
{
    if(m_markPaintIssue && m_markPaintIssue->isChecked() != enabled)
        m_markPaintIssue->setChecked(enabled);
}
void CLandscapeTools::clearPaintMaterialSelection()
{
    const auto clear = [](QButtonGroup* group) {
        if(!group) return;
        const bool exclusive = group->exclusive();
        group->setExclusive(false);
        for(QAbstractButton* button : group->buttons())
            button->setChecked(false);
        group->setExclusive(exclusive);
    };
    clear(m_groundMaterialButtons);
    clear(m_overlayMaterialButtons);
    clear(m_classicGroundMaterialButtons);
    clear(m_classicOverlayMaterialButtons);
}

QButtonGroup* CLandscapeTools::activeMaterialButtons() const
{
    if(paintPaletteMode() == ETerrainPaintPaletteMode::Manual)
        return nullptr;
    const bool overlay = editMode() == ELandscapeEditMode::OverlayPaint
        || editMode() == ELandscapeEditMode::OverlaySurface;
    if(paintPaletteMode() == ETerrainPaintPaletteMode::ClassicAuto)
        return overlay ? m_classicOverlayMaterialButtons
                       : m_classicGroundMaterialButtons;
    return overlay ? m_overlayMaterialButtons : m_groundMaterialButtons;
}
void CLandscapeTools::updatePaintIssueUi()
{
    const bool marking = paintIssueMarkingEnabled();
    const bool manual = manualTileSelectionEnabled();
    const QString category = m_paintIssueCategory
        ? m_paintIssueCategory->currentText() : tr("No / wrong blend");
    if(m_markPaintIssue)
        m_markPaintIssue->setText(marking
            ? tr("MARKING: %1").arg(category.toUpper())
            : tr("Mark bad tiles on map"));
    if(m_paintRadius) m_paintRadius->setEnabled(!marking);
    if(m_tilePaintTarget) m_tilePaintTarget->setEnabled(!marking && !manual);
    if(m_vertexPaintTarget) m_vertexPaintTarget->setEnabled(!marking && !manual);
    if(m_paintModeTabs) m_paintModeTabs->setEnabled(!marking);
    updatePaintShapeUi();
}

void CLandscapeTools::updatePaintShapeUi()
{
    const bool tileTarget = paintTargetMode() == ETerrainPaintTargetMode::Tile;
    const bool enabled = !paintIssueMarkingEnabled();
    if(m_circlePaintShape)
    {
        m_circlePaintShape->setEnabled(enabled);
        m_circlePaintShape->setToolTip(tileTarget
            ? tr("Circular tile brush; the viewport outlines affected tiles")
            : tr("Circular brush"));
    }
    if(m_squarePaintShape)
    {
        m_squarePaintShape->setEnabled(enabled);
        m_squarePaintShape->setToolTip(tileTarget
            ? tr("Square tile brush; the viewport outlines affected tiles")
            : tr("Square brush"));
    }
}
void CLandscapeTools::setPaintTerrainTypes(
    const QVector<QPair<qint32, QString>>& landTypes,
    const QVector<QPair<qint32, QString>>& underwaterTypes,
    const QVector<QPair<qint32, QString>>& overlayTypes,
    const QString& status)
{
    const auto convert = [](const QVector<QPair<qint32, QString>>& source) {
        QVector<STerrainPaintMaterialChoice> result;
        for(const auto& item : source)
        {
            STerrainPaintMaterialChoice choice;
            choice.id = item.first;
            choice.name = item.second;
            result.append(choice);
        }
        return result;
    };
    setPaintMaterials(convert(landTypes), convert(underwaterTypes),
                      convert(overlayTypes), 0, 0, status);
}

void CLandscapeTools::setPaintMaterials(
    const QVector<STerrainPaintMaterialChoice>& landTypes,
    const QVector<STerrainPaintMaterialChoice>& underwaterTypes,
    const QVector<STerrainPaintMaterialChoice>& overlayTypes,
    int assignedSlots, int totalSlots, const QString& status,
    const QString& atlasSource, const QString& atlasSourcePath)
{
    const auto uniqueIdentities = [](const QVector<STerrainPaintMaterialChoice>& source) {
        QVector<STerrainPaintMaterialChoice> result;
        QSet<qint32> seen;
        for(STerrainPaintMaterialChoice choice : source)
        {
            if(seen.contains(choice.id)) continue;
            seen.insert(choice.id);
            choice.supportId = 0;
            choice.contextualStamp = false;
            result.append(choice);
        }
        return result;
    };
    m_landPaintTypes = uniqueIdentities(landTypes);
    m_underwaterPaintTypes = uniqueIdentities(underwaterTypes);
    m_overlayPaintTypes = uniqueIdentities(overlayTypes);
    m_paintCatalogStatus = status;
    m_authoredAtlasSlots = qMax(0, totalSlots);
    QString coverage = totalSlots > 0
        ? tr("%1 / %2 assigned%3").arg(assignedSlots).arg(totalSlots)
              .arg(assignedSlots < totalSlots
                   ? tr(" (%1 unassigned)").arg(totalSlots - assignedSlots)
                   : QString())
        : tr("No atlas loaded");
    if(!atlasSource.isEmpty())
        coverage += QStringLiteral("  ·  ") + atlasSource;
    m_atlasCoverage->setText(coverage);
    m_atlasCoverage->setToolTip(atlasSourcePath.isEmpty()
        ? atlasSource : tr("Local override:\n%1").arg(
              QDir::toNativeSeparators(atlasSourcePath)));
    rebuildMaterialPalette();
    refreshPaintTerrainTypes();
}

void CLandscapeTools::setClassicAutoMaterials(
    const QVector<STerrainPaintMaterialChoice>& landTypes,
    const QVector<STerrainPaintMaterialChoice>& underwaterTypes,
    const QVector<STerrainPaintMaterialChoice>& overlayTypes)
{
    const auto uniqueIdentities = [](
        const QVector<STerrainPaintMaterialChoice>& source) {
        QVector<STerrainPaintMaterialChoice> result;
        QSet<QPair<qint32, qint32>> seen;
        for(STerrainPaintMaterialChoice choice : source)
        {
            const QPair<qint32, qint32> identity = qMakePair(
                choice.id, choice.contextualStamp ? choice.supportId : 0);
            if(seen.contains(identity))
                continue;
            seen.insert(identity);
            result.append(choice);
        }
        return result;
    };
    m_classicLandPaintTypes = uniqueIdentities(landTypes);
    m_classicUnderwaterPaintTypes = uniqueIdentities(underwaterTypes);
    m_classicOverlayPaintTypes = uniqueIdentities(overlayTypes);
    refreshPaletteColumns(false);
    rebuildMaterialPalette();
    refreshPaintTerrainTypes();
}

void CLandscapeTools::setManualAtlasTiles(
    const QVector<QImage>& tiles,
    const QVector<qint32>& runtimeTypes,
    const QStringList& descriptions)
{
    const int previousSlot = m_manualTileList->currentItem()
        ? m_manualTileList->currentItem()->data(Qt::UserRole).toInt() : -1;
    const QSignalBlocker blocker(m_manualTileList);
    m_manualTileList->setProperty("physicalAtlasSlotCount", tiles.size());
    m_manualTileList->clear();
    for(int slot = 0; slot < tiles.size(); ++slot)
    {
        // Working atlases preserve page-aligned physical indices. A partial
        // authored page can therefore leave null, unaddressable holes before
        // later generated tiles. Adding those holes to an icon-mode view gives
        // each one a 62 px grid cell even though it paints no thumbnail; a run
        // of page padding then looks like a large blank region in the palette.
        // Keep the real slot in UserRole, but do not allocate a selectable
        // palette cell for an image that cannot be painted.
        if(tiles.at(slot).isNull())
            continue;
        const QString description = slot < descriptions.size()
            ? descriptions.at(slot).trimmed() : QString();
        const QString label = description.isEmpty()
            ? tr("Slot %1 - runtime type %2")
                  .arg(slot).arg(slot < runtimeTypes.size()
                      ? runtimeTypes.at(slot) : -1)
            : tr("Slot %1 - %2 - runtime type %3")
                  .arg(slot).arg(description)
                  .arg(slot < runtimeTypes.size()
                      ? runtimeTypes.at(slot) : -1);
        QListWidgetItem* item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(tiles.at(slot))), QString(),
            m_manualTileList);
        item->setData(Qt::UserRole, slot);
        item->setData(Qt::UserRole + 1,
                      QStringLiteral("%1 %2").arg(slot).arg(description));
        item->setToolTip(label);
        item->setData(Qt::AccessibleTextRole, label);
        if(slot == previousSlot)
            m_manualTileList->setCurrentItem(item);
    }
    applyPaintFilter();
    refreshPaintTerrainTypes();
}

void CLandscapeTools::applyPaintFilter()
{
    const QString filter = m_paintFilter
        ? m_paintFilter->text().trimmed() : QString();
    const bool hideClassicStamps = paintPaletteMode()
        == ETerrainPaintPaletteMode::ClassicAuto
        && paintTargetMode() == ETerrainPaintTargetMode::Tile;
    const auto filterGroup = [&filter, hideClassicStamps](
        QButtonGroup* group, bool classic) {
        if(!group)
            return;
        for(QAbstractButton* button : group->buttons())
        {
            const QString searchable = button->property(
                "terrainFilterText").toString();
            const bool stampOnly = button->property(
                "terrainCornerOnly").toBool();
            button->setVisible(terrainFilterMatches(searchable, filter)
                && !(classic && hideClassicStamps && stampOnly));
        }
    };
    filterGroup(m_groundMaterialButtons, false);
    filterGroup(m_overlayMaterialButtons, false);
    filterGroup(m_classicGroundMaterialButtons, true);
    filterGroup(m_classicOverlayMaterialButtons, true);
    if(m_manualTileList)
        for(int row = 0; row < m_manualTileList->count(); ++row)
        {
            QListWidgetItem* item = m_manualTileList->item(row);
            item->setHidden(!terrainFilterMatches(
                item->data(Qt::UserRole + 1).toString(), filter));
        }
}

void CLandscapeTools::rebuildMaterialPalette()
{
    const auto selectedMaterial = [](const QButtonGroup* group) {
        const QAbstractButton* button = group->checkedButton();
        return qMakePair(button ? button->property("terrainMaterialId").toInt() : -1,
                         button ? button->property("terrainSupportMaterialId").toInt() : 0);
    };
    const QPair<int, int> previousGround = selectedMaterial(m_groundMaterialButtons);
    const QPair<int, int> previousOverlay = selectedMaterial(m_overlayMaterialButtons);
    const QPair<int, int> previousClassicGround =
        selectedMaterial(m_classicGroundMaterialButtons);
    const QPair<int, int> previousClassicOverlay =
        selectedMaterial(m_classicOverlayMaterialButtons);
    const auto clear = [](QGridLayout* layout) {
        while(QLayoutItem* item = layout->takeAt(0))
        {
            delete item->widget();
            delete item;
        }
    };
    clear(m_groundMaterialGrid);
    clear(m_underwaterMaterialGrid);
    clear(m_overlayMaterialGrid);
    clear(m_classicGroundMaterialGrid);
    clear(m_classicUnderwaterMaterialGrid);
    clear(m_classicOverlayMaterialGrid);

    const int columns = qMax(1, m_paletteColumns);
    const auto populate = [this, columns](QGridLayout* grid,
            QButtonGroup* group,
            const QVector<STerrainPaintMaterialChoice>& choices,
            bool classicAuto) {
        int row = 0;
        const auto addSection = [&](bool used, const QString& title) {
          QVector<STerrainPaintMaterialChoice> section;
          for(const STerrainPaintMaterialChoice& choice : choices)
              if(choice.used == used) section.append(choice);
          if(section.isEmpty()) return;
          QLabel* heading = new QLabel(title, this);
          heading->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; margin-top: 5px; }"));
          grid->addWidget(heading, row++, 0, 1, columns);
          QMap<QString, QVector<STerrainPaintMaterialChoice>> byFamily;
          for(const STerrainPaintMaterialChoice& choice : section)
              byFamily[choice.familyName.isEmpty() ? choice.name
                                                   : choice.familyName].append(choice);
          for(auto family = byFamily.cbegin(); family != byFamily.cend(); ++family)
          {
            QLabel* familyHeading = new QLabel(family.key(), this);
            familyHeading->setStyleSheet(QStringLiteral("QLabel { margin-top: 3px; color: palette(mid); }"));
            grid->addWidget(familyHeading, row++, 0, 1, columns);
            int index = 0;
            for(const STerrainPaintMaterialChoice& choice : family.value())
            {
              QToolButton* button = new QToolButton(this);
              button->setProperty("terrainMaterialId", choice.id);
              button->setProperty("terrainSupportMaterialId", choice.supportId);
              button->setProperty("terrainContextualStamp", choice.contextualStamp);
              button->setProperty("terrainCornerOnly", choice.cornerOnly);
              button->setProperty("terrainFilterText",
                  QStringLiteral("%1 %2").arg(choice.name,
                                               choice.familyName));
              button->setCheckable(true);
              button->setAutoRaise(false);
              button->setStyleSheet(QStringLiteral(
                  "QToolButton { border: 2px solid palette(mid); padding: 2px; }"
                  "QToolButton:hover { border-color: #ffd34e; }"
                  "QToolButton:checked { border: 4px solid #d8ff35; "
                  "background: #315b16; padding: 0px; }"));
              button->setToolTip((choice.contextualStamp
                  ? tr("%1 - exact authored one-corner stamp. Vertex target only. %2")
                        .arg(choice.name)
                        .arg(classicAutoSafeMode()
                            ? tr("Safe mode applies it only when every incident authored tile is exact.")
                            : tr("It is restricted to its reviewed support material."))
                  : tr("%1 - %2 authored uniform source(s). %3")
                  .arg(choice.name).arg(choice.assignedSlots)
                  .arg(used ? tr("Currently used on this map.")
                            : (classicAuto
                                ? tr("Unused on this map; Classic Auto preserves available authored blends. Safe mode leaves a boundary unchanged when its exact blend is absent.")
                                : tr("Unused on this map; Modern painting adds this material and generates exact blends as needed.")))));
              if(!choice.thumbnail.isNull())
              {
                  button->setIcon(QPixmap::fromImage(choice.thumbnail));
                  button->setIconSize(QSize(52, 52));
              }
              else
                  button->setText(choice.name.left(2).toUpper());
              button->setMinimumSize(64, 64);
              button->setMaximumHeight(64);
              button->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Fixed);
              group->addButton(button);
              grid->addWidget(button, row + index / columns, index % columns);
              ++index;
            }
            row += (family.value().size() + columns - 1) / columns;
          }
        };
        addSection(true, tr("Used on this map"));
        addSection(false, tr("Unused materials"));
        for(int column = 0; column < columns; ++column)
            grid->setColumnStretch(column, 1);
        grid->setAlignment(Qt::AlignTop);
    };
    populate(m_groundMaterialGrid, m_groundMaterialButtons,
             m_landPaintTypes, false);
    populate(m_underwaterMaterialGrid, m_groundMaterialButtons,
             m_underwaterPaintTypes, false);
    populate(m_overlayMaterialGrid, m_overlayMaterialButtons,
             m_overlayPaintTypes, false);
    populate(m_classicGroundMaterialGrid,
             m_classicGroundMaterialButtons, m_classicLandPaintTypes, true);
    populate(m_classicUnderwaterMaterialGrid,
             m_classicGroundMaterialButtons,
             m_classicUnderwaterPaintTypes, true);
    populate(m_classicOverlayMaterialGrid,
             m_classicOverlayMaterialButtons,
             m_classicOverlayPaintTypes, true);
    const auto restore = [](QButtonGroup* group, const QPair<int, int>& selection) {
        for(QAbstractButton* button : group->buttons())
            if(button->property("terrainMaterialId").toInt() == selection.first
               && button->property("terrainSupportMaterialId").toInt() == selection.second)
            {
                button->setChecked(true);
                return true;
            }
        return false;
    };
    if(!paintIssueMarkingEnabled())
    {
        if(!restore(m_groundMaterialButtons, previousGround)
           && !m_groundMaterialButtons->buttons().isEmpty())
            m_groundMaterialButtons->buttons().front()->setChecked(true);
        if(!restore(m_overlayMaterialButtons, previousOverlay)
           && !m_overlayMaterialButtons->buttons().isEmpty())
            m_overlayMaterialButtons->buttons().front()->setChecked(true);
        if(!restore(m_classicGroundMaterialButtons, previousClassicGround)
           && !m_classicGroundMaterialButtons->buttons().isEmpty())
            m_classicGroundMaterialButtons->buttons().front()->setChecked(true);
        if(!restore(m_classicOverlayMaterialButtons, previousClassicOverlay)
           && !m_classicOverlayMaterialButtons->buttons().isEmpty())
            m_classicOverlayMaterialButtons->buttons().front()->setChecked(true);
    }
    applyPaintFilter();
}

void CLandscapeTools::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if(m_paletteRelayoutPending)
        return;
    m_paletteRelayoutPending = true;
    // Rebuilding a layout synchronously from its parent's resize event can
    // recurse through QScrollArea geometry negotiation. Coalesce it onto the
    // event queue once the viewport has its final width.
    QTimer::singleShot(0, this, [this]() {
        m_paletteRelayoutPending = false;
        refreshPaletteColumns();
    });
}

void CLandscapeTools::refreshPaletteColumns(bool rebuild)
{
    QScrollArea* activeScroll = nullptr;
    const bool overlay = editMode() == ELandscapeEditMode::OverlayPaint
        || editMode() == ELandscapeEditMode::OverlaySurface;
    if(paintPaletteMode() == ETerrainPaintPaletteMode::ClassicAuto)
        activeScroll = overlay ? m_classicOverlayMaterialScroll
                               : m_classicGroundMaterialScroll;
    else if(paintPaletteMode() == ETerrainPaintPaletteMode::Modern)
        activeScroll = overlay ? m_overlayMaterialScroll
                               : m_groundMaterialScroll;
    int viewportWidth = activeScroll && activeScroll->viewport()
        ? activeScroll->viewport()->width() : 0;
    if(viewportWidth < 70 && m_paintModeTabs)
        viewportWidth = m_paintModeTabs->width() - 16;
    if(viewportWidth < 70)
        viewportWidth = width() - 24;
    const int columns = qMax(1, (qMax(70, viewportWidth) - 12) / 70);
    if(columns == m_paletteColumns)
        return;
    m_paletteColumns = columns;
    if(rebuild)
        rebuildMaterialPalette();
}

void CLandscapeTools::setUsedTerrainFamilies(
    const QSet<qint32>& landFamilies, const QSet<qint32>& overlayFamilies)
{
    bool changed = false;
    const auto update = [&changed](QVector<STerrainPaintMaterialChoice>* choices,
                                   const QSet<qint32>& used) {
        for(STerrainPaintMaterialChoice& choice : *choices)
            if(choice.used != used.contains(choice.id))
            {
                choice.used = used.contains(choice.id);
                changed = true;
            }
    };
    update(&m_landPaintTypes, landFamilies);
    update(&m_underwaterPaintTypes, landFamilies);
    update(&m_overlayPaintTypes, overlayFamilies);
    update(&m_classicLandPaintTypes, landFamilies);
    update(&m_classicUnderwaterPaintTypes, landFamilies);
    update(&m_classicOverlayPaintTypes, overlayFamilies);
    if(changed)
        rebuildMaterialPalette();
}

void CLandscapeTools::refreshPaintTerrainTypes()
{
    const bool overlay = editMode() == ELandscapeEditMode::OverlayPaint
        || editMode() == ELandscapeEditMode::OverlaySurface;
    int materialCount = 0;
    if(paintPaletteMode() == ETerrainPaintPaletteMode::Manual)
        materialCount = m_manualTileList
            ? m_manualTileList->property("physicalAtlasSlotCount").toInt()
            : 0;
    else if(paintPaletteMode() == ETerrainPaintPaletteMode::ClassicAuto)
        materialCount = overlay ? m_classicOverlayPaintTypes.size()
            : m_classicLandPaintTypes.size()
                + m_classicUnderwaterPaintTypes.size();
    else
        materialCount = overlay ? m_overlayPaintTypes.size()
            : m_landPaintTypes.size() + m_underwaterPaintTypes.size();
    {
        const QSignalBlocker blocker(m_materialTabs);
        m_materialTabs->setCurrentIndex(overlay ? 1 : 0);
    }
    {
        const QSignalBlocker blocker(m_classicMaterialTabs);
        m_classicMaterialTabs->setCurrentIndex(overlay ? 1 : 0);
    }
    if(paintPaletteMode() == ETerrainPaintPaletteMode::ClassicAuto)
        m_tilesetStatus->setText(materialCount == 0
            ? tr("No existing automatic material designation is available for this layer.")
            : (classicAutoSafeMode()
                ? tr("%1 existing material designation(s). Safe Classic Auto applies only complete exact authored placements; tiles or vertices with unsupported boundaries stay unchanged.")
                      .arg(materialCount)
                : tr("%1 existing material designation(s). Classic Auto never changes the atlas; unresolved boundaries may use a local uniform hard edge.")
                      .arg(materialCount)));
    else if(paintPaletteMode() == ETerrainPaintPaletteMode::Manual)
        m_tilesetStatus->setText(materialCount == 0
            ? tr("No physical atlas tiles are available.")
            : tr("%1 physical atlas tile(s). Manual painting writes the selected slot and rotation directly.")
                  .arg(materialCount));
    else if(!m_paintCatalogStatus.isEmpty())
        m_tilesetStatus->setText(m_paintCatalogStatus);
    else
    {
        m_tilesetStatus->setText(materialCount == 0
            ? tr("No materials are available for this layer.")
            : tr("%1 material(s) ready for this layer.").arg(materialCount));
    }
    bool selected = false;
    const qint32 terrainType = paintTerrainType(&selected);
    emit paintTerrainTypeChanged(selected ? terrainType : -1);
}
bool CLandscapeTools::selectPaintTerrainType(qint32 terrainType)
{
    QButtonGroup* group = activeMaterialButtons();
    if(!group)
        return false;
    for(QAbstractButton* button : group->buttons())
        if(button->property("terrainMaterialId").toInt() == terrainType)
        {
            setPaintIssueMarkingEnabled(false);
            button->setChecked(true);
            emit paintTerrainTypeChanged(terrainType);
            emit editingStateChanged();
            return true;
        }
    return false;
}

bool CLandscapeTools::selectManualAtlasTile(int slot)
{
    if(!m_manualTileList)
        return false;
    for(int row = 0; row < m_manualTileList->count(); ++row)
    {
        QListWidgetItem* item = m_manualTileList->item(row);
        if(item->data(Qt::UserRole).toInt() != slot)
            continue;
        m_manualTileList->setCurrentItem(item);
        m_manualTileList->scrollToItem(item);
        return true;
    }
    return false;
}
bool CLandscapeTools::underwaterFamiliesVisible() const
{
    return true;
}
void CLandscapeTools::setUnderwaterFamiliesVisible(bool visible)
{
    Q_UNUSED(visible);
}
bool CLandscapeTools::mainTerrainVisible() const
{
    return m_showMainTerrain->isChecked();
}
bool CLandscapeTools::overlaysVisible() const
{
    return m_showOverlays->isChecked();
}
void CLandscapeTools::setTerrainVisibility(bool mainTerrainVisible,
                                           bool overlaysVisible, bool notify)
{
    {
        const QSignalBlocker landBlocker(m_showMainTerrain);
        const QSignalBlocker overlayBlocker(m_showOverlays);
        m_showMainTerrain->setChecked(mainTerrainVisible);
        m_showOverlays->setChecked(overlaysVisible);
    }
    if(notify)
        emit terrainVisibilityChanged(mainTerrainVisible, overlaysVisible);
}
