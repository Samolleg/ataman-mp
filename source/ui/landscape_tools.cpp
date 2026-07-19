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
        "Left-drag paints the selected atlas tile. Right-click picks from the map; the wheel rotates. "
        "Overlay painting changes only already-active overlay cells and never creates liquid implicitly."), this);
    paintHelp->setWordWrap(true);
    paintLayout->addWidget(paintHelp);
    QPushButton* tilePicker = new QPushButton(tr("Choose atlas tile..."), this);
    paintLayout->addWidget(tilePicker);
    layout->addWidget(m_paintControls);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(tr("Heightmap import"), this));
    layout->addWidget(m_flipVertical);
    QPushButton* importButton = new QPushButton(tr("Import grayscale heightmap..."), this);
    layout->addWidget(importButton);
    layout->addStretch(1);
    connect(importButton, &QPushButton::clicked, this, &CLandscapeTools::importHeightmapRequested);
    connect(tilePicker, &QPushButton::clicked, this, &CLandscapeTools::tilePickerRequested);
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
