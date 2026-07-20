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
    , m_enable(new QCheckBox(tr("Enable landscape editing"), this))
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
    , m_tilesetStatus(new QLabel(tr("Visual atlas materials are analyzed automatically when an MPR loads."), this))
    , m_sculptControls(new QWidget(this))
    , m_paintControls(new QWidget(this))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* warning = new QLabel(tr(
        "Experimental live preview. Terrain edits are undoable, but saving a changed MPR is intentionally disabled."), this);
    warning->setWordWrap(true);
    warning->setStyleSheet("QLabel { color: #b06000; font-weight: bold; }");
    layout->addWidget(warning);
    layout->addWidget(m_enable);

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
        "Left-drag sculpts. Middle-drag keeps the normal camera control. Each drag is one undo step."), this);
    help->setWordWrap(true);
    form->addRow(help);

    QVBoxLayout* paintLayout = new QVBoxLayout(m_paintControls);
    paintLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* paintHelp = new QLabel(tr(
        "Left-drag paints an automatically identified visual atlas material. "
        "Endurance resolves compatible corner transitions and rotations in a one-cell seam halo. Right-click samples the material from the map. "
        "Overlay painting changes only already-active overlay cells and never creates liquid implicitly."), this);
    paintHelp->setWordWrap(true);
    paintLayout->addWidget(paintHelp);
    m_tilesetStatus->setWordWrap(true);
    paintLayout->addWidget(m_tilesetStatus);
    m_paintTerrainType->addItem(tr("Choose a material..."), QVariant());
    m_paintRadius->setRange(0.0, 32.0);
    m_paintRadius->setValue(1.0);
    m_paintRadius->setSingleStep(0.5);
    m_paintShape->addItem(tr("Circle"));
    m_paintShape->addItem(tr("Square"));
    QFormLayout* paintForm = new QFormLayout();
    paintForm->addRow(tr("Terrain material"), m_paintTerrainType);
    paintForm->addRow(tr("Radius (tiles)"), m_paintRadius);
    paintForm->addRow(tr("Brush shape"), m_paintShape);
    paintLayout->addLayout(paintForm);
    QPushButton* tilePicker = new QPushButton(tr("Inspect atlas / choose representative..."), this);
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
    connect(m_enable, &QCheckBox::toggled, this, &CLandscapeTools::editingStateChanged);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        updateModeVisibility();
        emit editingStateChanged();
    });
    updateModeVisibility();
}

bool CLandscapeTools::editingEnabled() const { return m_enable->isChecked(); }
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
        const QSignalBlocker enableBlocker(m_enable);
        m_mode->setCurrentIndex(int(mode));
        m_enable->setChecked(true);
    }
    updateModeVisibility();
    if(notify)
        emit editingStateChanged();
}
void CLandscapeTools::setEditingEnabled(bool enabled, bool notify)
{
    if(notify)
        m_enable->setChecked(enabled);
    else
    {
        const QSignalBlocker blocker(m_enable);
        m_enable->setChecked(enabled);
    }
}
void CLandscapeTools::updateModeVisibility()
{
    const bool sculpt = editMode() == ELandscapeEditMode::Sculpt;
    m_sculptControls->setVisible(sculpt);
    m_paintControls->setVisible(!sculpt);
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
float CLandscapeTools::paintRadius() const { return float(m_paintRadius->value()); }
ETerrainTileBrushShape CLandscapeTools::paintShape() const
{
    return m_paintShape->currentIndex() == 1
        ? ETerrainTileBrushShape::Square : ETerrainTileBrushShape::Circle;
}
qint32 CLandscapeTools::paintTerrainType(bool* selected) const
{
    const QVariant value = m_paintTerrainType->currentData();
    if(selected)
        *selected = value.isValid();
    return value.isValid() ? value.toInt() : 0;
}
void CLandscapeTools::setPaintTerrainTypes(
    const QVector<QPair<qint32, QString>>& terrainTypes)
{
    m_paintTerrainType->clear();
    m_paintTerrainType->addItem(tr("Choose a material..."), QVariant());
    for(const auto& terrainType : terrainTypes)
        m_paintTerrainType->addItem(terrainType.second, terrainType.first);
    m_tilesetStatus->setText(terrainTypes.isEmpty()
        ? tr("No safe visual materials were identified. Painting is disabled for this atlas.")
        : tr("Atlas analysis ready: %1 visual material(s).").arg(terrainTypes.size()));
}
void CLandscapeTools::selectPaintTerrainType(qint32 terrainType)
{
    const int index = m_paintTerrainType->findData(terrainType);
    m_paintTerrainType->setCurrentIndex(index >= 0 ? index : 0);
}
