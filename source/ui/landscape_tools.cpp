#include "landscape_tools.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
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
    , m_flipVertical(new QCheckBox(tr("Flip image vertically"), this))
    , m_paintTerrainType(new QComboBox(this))
    , m_paintRadius(new QDoubleSpinBox(this))
    , m_paintShape(new QComboBox(this))
    , m_manualTileSelection(new QCheckBox(tr("Manual atlas tile mode (no blending)"), this))
    , m_showUnderwaterFamilies(new QCheckBox(tr("Show underwater material families"), this))
    , m_allowLandFamiliesInOverlay(new QCheckBox(
          tr("Allow land-family tiles on overlays"), this))
    , m_showMainTerrain(new QCheckBox(tr("Show main terrain (M)"), this))
    , m_showOverlays(new QCheckBox(tr("Show overlays (J)"), this))
    , m_tilesetStatus(new QLabel(tr("Terrain materials are loaded from the MPR namespace template."), this))
    , m_sculptControls(new QWidget(this))
    , m_paintControls(new QWidget(this))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* warning = new QLabel(tr(
        "Experimental live preview. Terrain edits are undoable, but saving a changed MPR is intentionally disabled."), this);
    warning->setWordWrap(true);
    warning->setStyleSheet("QLabel { color: #b06000; font-weight: bold; }");
    layout->addWidget(warning);
    m_showMainTerrain->setChecked(true);
    m_showOverlays->setChecked(true);
    layout->addWidget(m_showMainTerrain);
    layout->addWidget(m_showOverlays);

    m_mode->addItem(tr("Sculpt terrain"));
    m_mode->addItem(tr("Paint land tiles"));
    m_mode->addItem(tr("Paint existing overlays"));
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

    QFormLayout* form = new QFormLayout(m_sculptControls);
    form->addRow(tr("Tool"), m_tool);
    form->addRow(tr("Radius (vertices)"), m_radius);
    form->addRow(tr("Strength"), m_strength);
    form->addRow(tr("Raise/lower step"), m_delta);
    form->addRow(tr("Flatten target"), m_flatten);
    layout->addWidget(m_sculptControls);

    QLabel* help = new QLabel(tr(
        "Left-drag sculpts. Alt+Wheel changes radius; Ctrl+Wheel changes strength; Wheel keeps camera zoom. Each drag is one undo step."), this);
    help->setWordWrap(true);
    form->addRow(help);

    QVBoxLayout* paintLayout = new QVBoxLayout(m_paintControls);
    paintLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* paintHelp = new QLabel(tr(
        "Automatic mode always covers the brush core, uses an exact or constrained nearest authored seam when available, and otherwise leaves a hard edge. "
        "Manual mode paints the selected existing atlas tile and rotation without blending. Size 1 paints one core tile. Right-click samples from the map. "
        "Alt+Wheel changes brush size. Ctrl+Wheel selects the automatic material or rotates the manual tile; Wheel keeps camera zoom. "
        "Overlay painting changes only already-active overlay cells and never creates liquid implicitly."), this);
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
    QFormLayout* paintForm = new QFormLayout();
    paintForm->addRow(tr("Terrain material"), m_paintTerrainType);
    paintForm->addRow(tr("Brush size (tiles)"), m_paintRadius);
    paintForm->addRow(tr("Brush shape"), m_paintShape);
    paintForm->addRow(m_manualTileSelection);
    paintForm->addRow(m_showUnderwaterFamilies);
    paintForm->addRow(m_allowLandFamiliesInOverlay);
    paintLayout->addLayout(paintForm);
    QPushButton* tilePicker = new QPushButton(tr("Inspect atlas / choose tile..."), this);
    paintLayout->addWidget(tilePicker);
    QPushButton* exportDiagnostic = new QPushButton(tr("Export paint diagnostic..."), this);
    paintLayout->addWidget(exportDiagnostic);
    layout->addWidget(m_paintControls);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(tr("Heightmap import"), this));
    layout->addWidget(m_flipVertical);
    QPushButton* importButton = new QPushButton(tr("Import grayscale heightmap..."), this);
    layout->addWidget(importButton);
    layout->addStretch(1);
    connect(importButton, &QPushButton::clicked, this, &CLandscapeTools::importHeightmapRequested);
    connect(tilePicker, &QPushButton::clicked, this, &CLandscapeTools::tilePickerRequested);
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
    updateModeVisibility();
}

bool CLandscapeTools::editingEnabled() const { return m_editingActive; }
ELandscapeEditMode CLandscapeTools::editMode() const
{
    return static_cast<ELandscapeEditMode>(m_mode->currentIndex());
}
bool CLandscapeTools::sculptEnabled() const
{
    return editingEnabled() && editMode() == ELandscapeEditMode::Sculpt;
}
bool CLandscapeTools::tilePaintEnabled() const
{
    return editingEnabled() && editMode() == ELandscapeEditMode::TilePaint;
}
bool CLandscapeTools::overlayPaintEnabled() const
{
    return editingEnabled() && editMode() == ELandscapeEditMode::OverlayPaint;
}
void CLandscapeTools::activateMode(ELandscapeEditMode mode, bool notify)
{
    {
        const QSignalBlocker modeBlocker(m_mode);
        m_mode->setCurrentIndex(int(mode));
        m_editingActive = true;
    }
    updateModeVisibility();
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
    const bool sculpt = editMode() == ELandscapeEditMode::Sculpt;
    m_sculptControls->setVisible(sculpt);
    m_paintControls->setVisible(!sculpt);
    m_showUnderwaterFamilies->setVisible(
        editMode() == ELandscapeEditMode::TilePaint);
    m_allowLandFamiliesInOverlay->setVisible(
        editMode() == ELandscapeEditMode::OverlayPaint);
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
    return m_manualTileSelection->isChecked();
}
bool CLandscapeTools::landFamiliesAllowedInOverlay() const
{
    return editMode() == ELandscapeEditMode::OverlayPaint
        && m_allowLandFamiliesInOverlay->isChecked();
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
        editMode() == ELandscapeEditMode::OverlayPaint
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
