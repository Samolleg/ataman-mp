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
    , m_groundMaterialScroll(new QScrollArea(this))
    , m_overlayMaterialScroll(new QScrollArea(this))
    , m_groundMaterialPage(new QWidget(this))
    , m_overlayMaterialPage(new QWidget(this))
    , m_groundMaterialGrid(new QGridLayout)
    , m_underwaterMaterialGrid(new QGridLayout)
    , m_overlayMaterialGrid(new QGridLayout)
    , m_groundMaterialButtons(new QButtonGroup(this))
    , m_overlayMaterialButtons(new QButtonGroup(this))
    , m_paintRadius(new QSlider(Qt::Horizontal, this))
    , m_paintRadiusValue(new QLabel(this))
    , m_circlePaintShape(new QToolButton(this))
    , m_squarePaintShape(new QToolButton(this))
    , m_manualTileSelection(new QCheckBox(tr("Manual atlas tile mode (no blending)"), this))
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

    QVBoxLayout* paintLayout = new QVBoxLayout(m_paintControls);
    paintLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* paintHelp = new QLabel(tr(
        "Automatic material brushes paint terrain vertices and resolve the surrounding atlas tiles together. Size 1 paints the nearest vertex (up to four tiles); larger circles and squares select more vertices. "
        "Each palette card is one uniform vertex-material identity. The editor uses exact authored junctions when available and automatically generates, caches, and reuses any missing corner combination on stroke release. "
        "Manual mode paints the selected existing atlas tile and rotation without blending. Right-click samples from the map. "
        "Alt+Wheel changes brush size. Shift+Wheel selects the automatic material or rotates the manual tile; Wheel keeps camera zoom. "
        "Overlay surface mode adds/removes coverage. Its Terrain material selects the atlas texture; its Surface material independently controls tint, opacity, illumination, waves, and texture warp. "
        "Overlay painting only changes the atlas texture on cells that already exist."), this);
    paintHelp->setWordWrap(true);
    paintHelp->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    paintLayout->addWidget(paintHelp);
    m_tilesetStatus->setWordWrap(true);
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
    m_materialTabs->setObjectName(QStringLiteral("materialPaletteTabs"));
    paintLayout->addWidget(m_materialTabs, 1);

    m_paintRadius->setRange(1, 33);
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
    QButtonGroup* shapeButtons = new QButtonGroup(this);
    shapeButtons->setExclusive(true);
    shapeButtons->addButton(m_circlePaintShape);
    shapeButtons->addButton(m_squarePaintShape);
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
    paintForm->addRow(tr("Brush size (vertices)"), sizeWidget);
    paintForm->addRow(tr("Brush shape"), shapeWidget);
    paintForm->addRow(m_manualTileSelection);
    paintForm->addRow(m_overlaySurfaceOperationLabel, m_overlaySurfaceOperation);
    paintForm->addRow(m_overlayMaterialLabel, m_overlayMaterial);
    paintLayout->addLayout(paintForm);
    QPushButton* tilePicker = new QPushButton(tr("Inspect atlas / choose tile..."), this);
    paintLayout->addWidget(tilePicker);
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
    QPushButton* finalizeAtlas = new QPushButton(
        tr("Finalize map-specific atlas package..."), this);
    finalizeAtlas->setToolTip(tr(
        "Generate exact missing transitions, compact map-wide tile usage, and write "
        "a portable isolated MPR + RES package. Unused source slots are evicted. "
        "The packed format allows 16,384 slots; sizes above 4,096 remain potentially "
        "unstable and are clearly warned, but staging is not blocked."));
    paintLayout->addWidget(exportDiagnostic);
    paintLayout->addWidget(finalizeAtlas);
    // The material palette is the primary authoring surface. Give it all
    // remaining vertical space instead of leaving an empty stretch below the
    // fixed heightmap controls.
    layout->addWidget(m_paintControls, 1);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(tr("Heightmap import"), this));
    layout->addWidget(m_flipVertical);
    QPushButton* importButton = new QPushButton(tr("Import grayscale heightmap..."), this);
    layout->addWidget(importButton);
    connect(importButton, &QPushButton::clicked, this, &CLandscapeTools::importHeightmapRequested);
    connect(tilePicker, &QPushButton::clicked, this, &CLandscapeTools::tilePickerRequested);
    connect(reviewAtlas, &QPushButton::clicked, this, &CLandscapeTools::atlasProfileReviewRequested);
    connect(exportDiagnostic, &QPushButton::clicked, this, &CLandscapeTools::exportPaintDiagnosticRequested);
    connect(finalizeAtlas, &QPushButton::clicked, this, &CLandscapeTools::finalizeVirtualAtlasRequested);
    m_atlasCoverage->setObjectName(QStringLiteral("materialAtlasSourceStatus"));
    m_generatedAtlasTiles->setObjectName(QStringLiteral("generatedAtlasTileCount"));
    refreshAtlasCapacity(0);
    connect(m_groundMaterialButtons, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, [this](int) {
                bool selected = false;
                const qint32 material = paintTerrainType(&selected);
                emit paintTerrainTypeChanged(selected ? material : -1);
            });
    connect(m_overlayMaterialButtons, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, [this](int) {
                bool selected = false;
                const qint32 material = paintTerrainType(&selected);
                emit paintTerrainTypeChanged(selected ? material : -1);
            });
    connect(m_materialTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if(!m_editingActive)
            return;
        if(index == 0 && (editMode() == ELandscapeEditMode::OverlayPaint
                          || editMode() == ELandscapeEditMode::OverlaySurface))
            activateMode(ELandscapeEditMode::TilePaint);
        else if(index == 1 && editMode() == ELandscapeEditMode::TilePaint)
            activateMode(ELandscapeEditMode::OverlayPaint);
    });
    connect(m_paintRadius, &QSlider::valueChanged, this, [this](int value) {
        m_paintRadiusValue->setText(QString::number(value));
        QSettings().setValue("ui/landscape/paintSize", value);
    });
    connect(m_circlePaintShape, &QToolButton::toggled, this,
            [this](bool) { emit editingStateChanged(); });
    connect(m_squarePaintShape, &QToolButton::toggled, this,
            [this](bool) { emit editingStateChanged(); });
    connect(m_overlaySurfaceOperation, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int)
    {
        updateModeVisibility();
        emit editingStateChanged();
    });
    connect(m_manualTileSelection, &QCheckBox::toggled, this, [this](bool manual)
    {
        m_materialTabs->setEnabled(!manual);
        emit paintTerrainTypeChanged(-1);
        emit editingStateChanged();
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
    m_overlaySurfaceOperation->setVisible(overlaySurface);
    m_overlaySurfaceOperationLabel->setVisible(overlaySurface);
    const bool activating = overlaySurface
        && overlaySurfaceOperation() == EOverlaySurfaceOperation::Activate;
    m_overlayMaterial->setVisible(activating);
    m_overlayMaterialLabel->setVisible(activating);
    m_manualTileSelection->setVisible(!overlaySurface);
    m_materialTabs->setEnabled((overlaySurface || !m_manualTileSelection->isChecked())
        && (!overlaySurface || activating));
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
        && m_manualTileSelection->isChecked();
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
    QButtonGroup* group = (editMode() == ELandscapeEditMode::OverlayPaint
                           || editMode() == ELandscapeEditMode::OverlaySurface)
        ? m_overlayMaterialButtons : m_groundMaterialButtons;
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
    const QButtonGroup* group = (editMode() == ELandscapeEditMode::OverlayPaint
                                 || editMode() == ELandscapeEditMode::OverlaySurface)
        ? m_overlayMaterialButtons : m_groundMaterialButtons;
    const QAbstractButton* button = group->checkedButton();
    const int id = button ? button->property("terrainMaterialId").toInt() : -1;
    if(selected)
        *selected = id >= 0;
    return id >= 0 ? id : 0;
}
qint32 CLandscapeTools::paintTerrainSupport() const
{
    const QButtonGroup* group = (editMode() == ELandscapeEditMode::OverlayPaint
                                 || editMode() == ELandscapeEditMode::OverlaySurface)
        ? m_overlayMaterialButtons : m_groundMaterialButtons;
    const QAbstractButton* button = group->checkedButton();
    return button && button->property("terrainContextualStamp").toBool()
        ? button->property("terrainSupportMaterialId").toInt() : 0;
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

void CLandscapeTools::rebuildMaterialPalette()
{
    const auto selectedMaterial = [](const QButtonGroup* group) {
        const QAbstractButton* button = group->checkedButton();
        return qMakePair(button ? button->property("terrainMaterialId").toInt() : -1,
                         button ? button->property("terrainSupportMaterialId").toInt() : 0);
    };
    const QPair<int, int> previousGround = selectedMaterial(m_groundMaterialButtons);
    const QPair<int, int> previousOverlay = selectedMaterial(m_overlayMaterialButtons);
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

    const int columns = qMax(1, m_paletteColumns);
    const auto populate = [this, columns](QGridLayout* grid, QButtonGroup* group,
            const QVector<STerrainPaintMaterialChoice>& choices) {
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
              button->setCheckable(true);
              button->setAutoRaise(false);
              button->setToolTip(tr("%1 - %2 authored uniform source(s). %3")
                  .arg(choice.name).arg(choice.assignedSlots)
                  .arg(used ? tr("Currently used on this map.")
                            : tr("Unused on this map; painting adds this material and generates exact blends as needed.")));
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
    populate(m_groundMaterialGrid, m_groundMaterialButtons, m_landPaintTypes);
    populate(m_underwaterMaterialGrid, m_groundMaterialButtons, m_underwaterPaintTypes);
    populate(m_overlayMaterialGrid, m_overlayMaterialButtons, m_overlayPaintTypes);
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
    if(!restore(m_groundMaterialButtons, previousGround)
       && !m_groundMaterialButtons->buttons().isEmpty())
        m_groundMaterialButtons->buttons().front()->setChecked(true);
    if(!restore(m_overlayMaterialButtons, previousOverlay)
       && !m_overlayMaterialButtons->buttons().isEmpty())
        m_overlayMaterialButtons->buttons().front()->setChecked(true);
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
        const int viewportWidth = m_groundMaterialScroll
            ? m_groundMaterialScroll->viewport()->width() : width();
        const int columns = qMax(1, (viewportWidth - 12) / 70);
        if(columns != m_paletteColumns)
        {
            m_paletteColumns = columns;
            rebuildMaterialPalette();
        }
    });
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
    if(changed)
        rebuildMaterialPalette();
}

void CLandscapeTools::refreshPaintTerrainTypes()
{
    const bool overlay = editMode() == ELandscapeEditMode::OverlayPaint
        || editMode() == ELandscapeEditMode::OverlaySurface;
    const int materialCount = overlay
        ? m_overlayPaintTypes.size()
        : m_landPaintTypes.size() + m_underwaterPaintTypes.size();
    {
        const QSignalBlocker blocker(m_materialTabs);
        m_materialTabs->setCurrentIndex(overlay ? 1 : 0);
    }
    if(!m_paintCatalogStatus.isEmpty())
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
    QButtonGroup* group = (editMode() == ELandscapeEditMode::OverlayPaint
                           || editMode() == ELandscapeEditMode::OverlaySurface)
        ? m_overlayMaterialButtons : m_groundMaterialButtons;
    for(QAbstractButton* button : group->buttons())
        if(button->property("terrainMaterialId").toInt() == terrainType)
        {
            button->setChecked(true);
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
