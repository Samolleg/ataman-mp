#include "landscape_tools.h"
#include "types.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

CLandscapeTools::CLandscapeTools(QWidget* parent)
    : QWidget(parent)
    , m_mode(new QComboBox(this))
    , m_tool(new QComboBox(this))
    , m_radius(new QDoubleSpinBox(this))
    , m_strength(new QDoubleSpinBox(this))
    , m_delta(new QDoubleSpinBox(this))
    , m_flatten(new QSpinBox(this))
    , m_falloff(new QComboBox(this))
    , m_deltaLabel(new QLabel(tr("Raise/lower step"), this))
    , m_flattenLabel(new QLabel(tr("Flatten target"), this))
    , m_falloffLabel(new QLabel(tr("Falloff"), this))
    , m_flipVertical(new QCheckBox(tr("Flip image vertically"), this))
    , m_paintTerrainType(new QComboBox(this))
    , m_paintRadius(new QDoubleSpinBox(this))
    , m_paintShape(new QComboBox(this))
    , m_manualTileSelection(new QCheckBox(tr("Manual atlas tile mode (no blending)"), this))
    , m_showUnderwaterFamilies(new QCheckBox(tr("Show underwater material families"), this))
    , m_allowLandFamiliesInOverlay(new QCheckBox(
          tr("Allow land-family tiles on overlays"), this))
    , m_showOverlayMaterialSlots(new QCheckBox(
          tr("Show surface material slot numbers on tiles"), this))
    , m_overlaySurfaceOperation(new QComboBox(this))
    , m_overlayMaterial(new QComboBox(this))
    , m_overlaySurfaceOperationLabel(new QLabel(tr("Overlay operation"), this))
    , m_overlayMaterialLabel(new QLabel(tr("Surface material (tint / animation)"), this))
    , m_showMainTerrain(new QCheckBox(tr("Show main terrain (M)"), this))
    , m_showOverlays(new QCheckBox(tr("Show overlays (O)"), this))
    , m_tilesetStatus(new QLabel(tr("Terrain materials are loaded from the MPR namespace template."), this))
    , m_sculptControls(new QWidget(this))
    , m_paintControls(new QWidget(this))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* warning = new QLabel(tr(
        "Terrain edits are undoable. Changed MPR saves use staged validation, backup, and atomic replacement."), this);
    warning->setWordWrap(true);
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

    m_tool->setObjectName(QStringLiteral("landscapeSculptTool"));
    m_tool->addItem(tr("Raise"), int(ELandscapeSculptTool::Raise));
    m_tool->addItem(tr("Lower"), int(ELandscapeSculptTool::Lower));
    m_tool->addItem(tr("Smooth"), int(ELandscapeSculptTool::Smooth));
    m_tool->addItem(tr("Flatten"), int(ELandscapeSculptTool::Flatten));
    m_tool->addItem(tr("Noise"), int(ELandscapeSculptTool::Noise));
    m_radius->setRange(0.5, 64.0);
    m_radius->setValue(4.0);
    m_radius->setSingleStep(0.5);
    m_strength->setRange(0.01, 1.0);
    m_strength->setValue(0.35);
    m_strength->setSingleStep(0.05);
    m_strength->setDecimals(2);
    m_delta->setRange(1.0, 10000.0);
    m_delta->setValue(180.0);
    m_delta->setSingleStep(100.0);
    m_delta->setObjectName(QStringLiteral("landscapeRaiseLowerStep"));
    m_flatten->setRange(0, 65535);
    m_flatten->setValue(20000);
    m_flatten->setSingleStep(100);
    m_falloff->addItem(tr("Smooth"), int(ETerrainBrushFalloff::Smooth));
    m_falloff->addItem(tr("Linear"), int(ETerrainBrushFalloff::Linear));
    m_falloff->addItem(tr("Constant"), int(ETerrainBrushFalloff::Constant));
    m_falloff->setCurrentIndex(qBound(0,
        QSettings().value("ui/landscape/sculptFalloff", 0).toInt(),
        m_falloff->count() - 1));
    m_flatten->setObjectName(QStringLiteral("landscapeFlattenTarget"));
    m_falloff->setObjectName(QStringLiteral("landscapeSculptFalloff"));

    QSettings settings;
    int savedTool = int(ELandscapeSculptTool::Raise);
    if(settings.contains("ui/landscape/sculptToolValueV2"))
    {
        savedTool = settings.value("ui/landscape/sculptToolValueV2").toInt();
    }
    else
    {
        // Migrate the original combo-index setting. The old display order was
        // Raise, Lower, Flatten, Smooth; the new order puts Smooth before
        // Flatten without reinterpreting a user's last selected tool.
        const int legacyIndex = qBound(0,
            settings.value("ui/landscape/sculptTool", 0).toInt(), 3);
        const ELandscapeSculptTool legacyTools[] = {
            ELandscapeSculptTool::Raise,
            ELandscapeSculptTool::Lower,
            ELandscapeSculptTool::Flatten,
            ELandscapeSculptTool::Smooth
        };
        savedTool = int(legacyTools[legacyIndex]);
    }
    const int savedToolIndex = m_tool->findData(savedTool);
    m_tool->setCurrentIndex(savedToolIndex >= 0 ? savedToolIndex : 0);
    m_radius->setValue(settings.value("ui/landscape/sculptRadius", 4.0).toDouble());
    m_strength->setValue(settings.value("ui/landscape/sculptStrength", 0.35).toDouble());
    m_delta->setValue(settings.value("ui/landscape/sculptDelta",
        settings.value("ui/landscape/sculptRate", 180.0)).toDouble());
    m_flatten->setValue(settings.value("ui/landscape/flattenHeight", 20000).toInt());

    QFormLayout* form = new QFormLayout(m_sculptControls);
    form->addRow(tr("Tool"), m_tool);
    form->addRow(tr("Radius (vertices)"), m_radius);
    form->addRow(tr("Strength"), m_strength);
    form->addRow(m_deltaLabel, m_delta);
    form->addRow(m_flattenLabel, m_flatten);
    form->addRow(m_falloffLabel, m_falloff);
    layout->addWidget(m_sculptControls);

    QLabel* help = new QLabel(tr(
        "Left-drag sculpts; right-click samples the raw height as the Flatten target. "
        "Alt+Wheel changes radius; Shift+Wheel changes strength; Alt+Shift+Wheel changes raise/lower step, Noise range, or the Flatten target. "
        "Z/X/C/V select Raise/Lower/Smooth/Flatten; Noise is available in the Tool list. Wheel keeps camera zoom. Each drag is one undo step."), this);
    help->setWordWrap(true);
    form->addRow(help);

    QVBoxLayout* paintLayout = new QVBoxLayout(m_paintControls);
    paintLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* paintHelp = new QLabel(tr(
        "Automatic mode always covers the brush core, uses an exact or constrained nearest authored seam when available, and otherwise leaves a hard edge. "
        "Manual mode paints the selected existing atlas tile and rotation without blending. Size 1 paints one core tile. Right-click samples from the map. "
        "Alt+Wheel changes brush size. Shift+Wheel selects the automatic material or rotates the manual tile; Wheel keeps camera zoom. "
        "Overlay surface mode adds/removes coverage. Its Terrain material selects the atlas texture; its Surface material independently controls tint, opacity, illumination, waves, and texture warp. "
        "Overlay painting only changes the atlas texture on cells that already exist."), this);
    paintHelp->setWordWrap(true);
    paintLayout->addWidget(paintHelp);
    m_tilesetStatus->setWordWrap(true);
    paintLayout->addWidget(m_tilesetStatus);
    m_paintRadius->setRange(1.0, 33.0);
    m_paintRadius->setValue(1.0);
    m_paintRadius->setSingleStep(1.0);
    m_paintRadius->setDecimals(0);
    m_paintShape->addItem(tr("Circle"));
    m_paintShape->addItem(tr("Square"));
    m_overlaySurfaceOperation->addItem(tr("Add surface"));
    m_overlaySurfaceOperation->addItem(tr("Remove surface"));
    m_overlayMaterial->setToolTip(tr(
        "Selects the existing MPR material record stored on newly added overlay cells. "
        "It controls liquid/surface rendering properties, not the atlas texture selected above."));
    QFormLayout* paintForm = new QFormLayout();
    paintForm->addRow(tr("Atlas texture material"), m_paintTerrainType);
    paintForm->addRow(tr("Brush size (tiles)"), m_paintRadius);
    paintForm->addRow(tr("Brush shape"), m_paintShape);
    paintForm->addRow(m_manualTileSelection);
    paintForm->addRow(m_showUnderwaterFamilies);
    paintForm->addRow(m_allowLandFamiliesInOverlay);
    m_showOverlayMaterialSlots->setObjectName(
        QStringLiteral("showOverlayMaterialSlotLabels"));
    m_showOverlayMaterialSlots->setToolTip(tr(
        "Labels every active overlay cell with the MPR surface-material table slot it references. "
        "This is an inspection overlay only and does not change the MPR."));
    paintForm->addRow(m_showOverlayMaterialSlots);
    paintForm->addRow(m_overlaySurfaceOperationLabel, m_overlaySurfaceOperation);
    paintForm->addRow(m_overlayMaterialLabel, m_overlayMaterial);
    paintLayout->addLayout(paintForm);
    QPushButton* tilePicker = new QPushButton(tr("Inspect atlas / choose tile..."), this);
    paintLayout->addWidget(tilePicker);
    QPushButton* reviewAtlas = new QPushButton(tr("Convert / review semantic atlas..."), this);
    reviewAtlas->setToolTip(tr(
        "Infer a namespace-scoped semantic profile, review every ambiguity, and explicitly approve it. "
        "This does not edit atlas images or MPR resources."));
    paintLayout->addWidget(reviewAtlas);
    QPushButton* exportDiagnostic = new QPushButton(tr("Export paint diagnostic..."), this);
    paintLayout->addWidget(exportDiagnostic);
    layout->addWidget(m_paintControls);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(tr("Heightmap import"), this));
    layout->addWidget(m_flipVertical);
    QPushButton* importButton = new QPushButton(tr("Import grayscale heightmap..."), this);
    layout->addWidget(importButton);
    QPushButton* mprSettingsButton = new QPushButton(tr("Open MPR Settings..."), this);
    mprSettingsButton->setObjectName(QStringLiteral("openMprSettingsFromLandscape"));
    layout->addWidget(mprSettingsButton);
    layout->addStretch(1);
    connect(importButton, &QPushButton::clicked, this, &CLandscapeTools::importHeightmapRequested);
    connect(mprSettingsButton, &QPushButton::clicked,
            this, &CLandscapeTools::openMprSettingsRequested);
    connect(tilePicker, &QPushButton::clicked, this, &CLandscapeTools::tilePickerRequested);
    connect(reviewAtlas, &QPushButton::clicked, this, &CLandscapeTools::atlasProfileReviewRequested);
    connect(exportDiagnostic, &QPushButton::clicked, this, &CLandscapeTools::exportPaintDiagnosticRequested);
    connect(m_paintTerrainType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int)
    {
        bool selected = false;
        const qint32 terrainType = paintTerrainType(&selected);
        emit paintTerrainTypeChanged(selected ? terrainType : -1);
    });
    connect(m_showUnderwaterFamilies, &QCheckBox::toggled, this,
            [this](bool) { refreshPaintTerrainTypes(); });
    connect(m_allowLandFamiliesInOverlay, &QCheckBox::toggled, this,
            [this](bool) { refreshPaintTerrainTypes(); });
    connect(m_showOverlayMaterialSlots, &QCheckBox::toggled, this,
            &CLandscapeTools::overlayMaterialSlotLabelsChanged);
    connect(m_overlaySurfaceOperation, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int)
    {
        updateModeVisibility();
        emit editingStateChanged();
    });
    connect(m_manualTileSelection, &QCheckBox::toggled, this, [this](bool manual)
    {
        m_paintTerrainType->setEnabled(!manual);
        if(manual)
            m_paintTerrainType->setCurrentIndex(0);
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
            [this](int)
    {
        QSettings().setValue("ui/landscape/sculptToolValueV2",
                             m_tool->currentData().toInt());
        updateModeVisibility();
        emit editingStateChanged();
    });
    connect(m_falloff, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int value)
    {
        QSettings().setValue("ui/landscape/sculptFalloff", value);
        emit brushGeometryChanged();
    });
    connect(m_radius, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double value)
    {
        QSettings().setValue("ui/landscape/sculptRadius", value);
        emit brushGeometryChanged();
    });
    connect(m_paintRadius, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { emit brushGeometryChanged(); });
    connect(m_paintShape, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { emit brushGeometryChanged(); });
    connect(m_strength, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [](double value) { QSettings().setValue("ui/landscape/sculptStrength", value); });
    connect(m_delta, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [](double value) {
                QSettings settings;
                settings.setValue("ui/landscape/sculptDelta", value);
                settings.setValue("ui/landscape/sculptRate", value);
            });
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
    m_showUnderwaterFamilies->setVisible(
        editMode() == ELandscapeEditMode::TilePaint);
    m_allowLandFamiliesInOverlay->setVisible(
        editMode() == ELandscapeEditMode::OverlayPaint);
    m_showOverlayMaterialSlots->setVisible(
        editMode() == ELandscapeEditMode::OverlayPaint);
    const bool overlaySurface = editMode() == ELandscapeEditMode::OverlaySurface;
    m_overlaySurfaceOperation->setVisible(overlaySurface);
    m_overlaySurfaceOperationLabel->setVisible(overlaySurface);
    const bool activating = overlaySurface
        && overlaySurfaceOperation() == EOverlaySurfaceOperation::Activate;
    m_overlayMaterial->setVisible(activating);
    m_overlayMaterialLabel->setVisible(activating);
    m_manualTileSelection->setVisible(!overlaySurface);
    m_paintTerrainType->setEnabled((overlaySurface || !m_manualTileSelection->isChecked())
        && (!overlaySurface || activating));
    const ELandscapeSculptTool tool = sculptTool();
    const bool flatten = tool == ELandscapeSculptTool::Flatten;
    const bool raiseLower = tool == ELandscapeSculptTool::Raise
        || tool == ELandscapeSculptTool::Lower;
    const bool noise = tool == ELandscapeSculptTool::Noise;
    m_flatten->setVisible(flatten);
    m_flattenLabel->setVisible(flatten);
    m_falloff->setVisible(sculpt);
    m_falloffLabel->setVisible(sculpt);
    m_deltaLabel->setText(noise ? tr("Noise range") : tr("Raise/lower step"));
    m_delta->setVisible(raiseLower || noise);
    m_deltaLabel->setVisible(raiseLower || noise);
}
ELandscapeSculptTool CLandscapeTools::sculptTool() const
{
    return static_cast<ELandscapeSculptTool>(m_tool->currentData().toInt());
}
void CLandscapeTools::setSculptTool(ELandscapeSculptTool tool)
{
    const int index = m_tool->findData(int(tool));
    if(index >= 0)
        m_tool->setCurrentIndex(index);
}
float CLandscapeTools::radius() const { return float(m_radius->value()); }
float CLandscapeTools::strength() const { return float(m_strength->value()); }
float CLandscapeTools::rawDelta() const { return float(m_delta->value()); }
quint16 CLandscapeTools::flattenHeight() const { return quint16(m_flatten->value()); }
void CLandscapeTools::setFlattenHeight(quint16 height)
{
    m_flatten->setValue(int(height));
}
ETerrainBrushFalloff CLandscapeTools::falloff() const
{
    return static_cast<ETerrainBrushFalloff>(m_falloff->currentData().toInt());
}
bool CLandscapeTools::flipHeightmapVertically() const { return m_flipVertical->isChecked(); }
float CLandscapeTools::paintRadius() const
{
    // UI size 1 is the discrete single-tile core. The terrain model uses a
    // zero-based radius so subsequent integer sizes grow symmetrically.
    return float(m_paintRadius->value() - 1.0);
}
ETerrainTileBrushShape CLandscapeTools::paintShape() const
{
    return m_paintShape->currentIndex() == 1
        ? ETerrainTileBrushShape::Square : ETerrainTileBrushShape::Circle;
}
bool CLandscapeTools::manualTileSelectionEnabled() const
{
    return editMode() != ELandscapeEditMode::OverlaySurface
        && m_manualTileSelection->isChecked();
}
bool CLandscapeTools::landFamiliesAllowedInOverlay() const
{
    return editMode() == ELandscapeEditMode::OverlayPaint
        && m_allowLandFamiliesInOverlay->isChecked();
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
bool CLandscapeTools::overlayMaterialSlotLabelsVisible() const
{
    return m_showOverlayMaterialSlots->isChecked();
}
void CLandscapeTools::setOverlayMaterialSlotLabelsVisible(bool visible)
{
    m_showOverlayMaterialSlots->setChecked(visible);
}
void CLandscapeTools::adjustRawDelta(int steps)
{
    m_delta->setValue(m_delta->value() + steps * m_delta->singleStep());
}
void CLandscapeTools::adjustFlattenHeight(int steps)
{
    m_flatten->setValue(m_flatten->value() + steps * m_flatten->singleStep());
}
void CLandscapeTools::adjustPaintSize(int steps)
{
    m_paintRadius->setValue(m_paintRadius->value() + steps);
}
void CLandscapeTools::adjustPaintTerrainType(int steps)
{
    if(m_paintTerrainType->count() <= 0)
        return;
    const int current = qMax(0, m_paintTerrainType->currentIndex());
    const int count = m_paintTerrainType->count();
    m_paintTerrainType->setCurrentIndex((current + steps % count + count) % count);
}
qint32 CLandscapeTools::paintTerrainType(bool* selected) const
{
    const QVariant value = m_paintTerrainType->currentData();
    if(selected)
        *selected = value.isValid();
    return value.isValid() ? value.toInt() : 0;
}
void CLandscapeTools::setPaintTerrainTypes(
    const QVector<QPair<qint32, QString>>& landTypes,
    const QVector<QPair<qint32, QString>>& underwaterTypes,
    const QVector<QPair<qint32, QString>>& overlayTypes,
    const QString& status)
{
    m_landPaintTypes = landTypes;
    m_underwaterPaintTypes = underwaterTypes;
    m_overlayPaintTypes = overlayTypes;
    m_paintCatalogStatus = status;
    refreshPaintTerrainTypes();
}
void CLandscapeTools::refreshPaintTerrainTypes()
{
    const QVariant previous = m_paintTerrainType->currentData();
    QVector<QPair<qint32, QString>> terrainTypes =
        (editMode() == ELandscapeEditMode::OverlayPaint
         || editMode() == ELandscapeEditMode::OverlaySurface)
            ? m_overlayPaintTypes : m_landPaintTypes;
    if(editMode() == ELandscapeEditMode::TilePaint
       && m_showUnderwaterFamilies->isChecked())
        terrainTypes += m_underwaterPaintTypes;
    if(editMode() == ELandscapeEditMode::OverlayPaint
       && m_allowLandFamiliesInOverlay->isChecked())
    {
        terrainTypes += m_landPaintTypes;
        terrainTypes += m_underwaterPaintTypes;
    }
    {
        const QSignalBlocker blocker(m_paintTerrainType);
        m_paintTerrainType->clear();
        for(const auto& terrainType : terrainTypes)
            m_paintTerrainType->addItem(terrainType.second, terrainType.first);
        const int previousIndex = previous.isValid()
            ? m_paintTerrainType->findData(previous) : -1;
        m_paintTerrainType->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
    }
    if(!m_paintCatalogStatus.isEmpty())
        m_tilesetStatus->setText(m_paintCatalogStatus);
    else
        m_tilesetStatus->setText(terrainTypes.isEmpty()
            ? tr("No registered materials are available for this layer.")
            : tr("Namespace template ready: %1 material(s) for this layer.")
                  .arg(terrainTypes.size()));
    bool selected = false;
    const qint32 terrainType = paintTerrainType(&selected);
    emit paintTerrainTypeChanged(selected ? terrainType : -1);
}
bool CLandscapeTools::selectPaintTerrainType(qint32 terrainType)
{
    const int index = m_paintTerrainType->findData(terrainType);
    if(m_paintTerrainType->count() > 0)
        m_paintTerrainType->setCurrentIndex(index >= 0 ? index : 0);
    return index >= 0;
}
bool CLandscapeTools::underwaterFamiliesVisible() const
{
    return m_showUnderwaterFamilies->isChecked();
}
void CLandscapeTools::setUnderwaterFamiliesVisible(bool visible)
{
    m_showUnderwaterFamilies->setChecked(visible);
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
