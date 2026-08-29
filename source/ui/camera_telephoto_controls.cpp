#include "camera_telephoto_controls.h"

#include "camera_telephoto.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

CCameraTelephotoControls::CCameraTelephotoControls(QWidget* parent)
    : QWidget(parent)
    , m_enabled(new QCheckBox(tr("Telephoto"), this))
    , m_multiplier(new QDoubleSpinBox(this))
{
    setObjectName("cameraTelephotoControls");
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 0, 0);
    layout->setSpacing(4);

    m_enabled->setObjectName("telephotoCameraEnabled");
    m_enabled->setChecked(false);
    const QString help = tr(
        "JamaisVu telephoto camera. Locks yaw to 45.0 degrees and pitch to "
        "35.264 degrees, moves the render eye back, and narrows perspective "
        "while keeping map movement and zoom available.");
    m_enabled->setToolTip(help);
    m_enabled->setAccessibleName(tr("Telephoto"));

    QLabel* multiplierLabel = new QLabel(tr("Multiplier:"), this);
    multiplierLabel->setBuddy(m_multiplier);
    m_multiplier->setObjectName("telephotoCameraMultiplier");
    m_multiplier->setRange(ei::kTelephotoMultiplierMinimum,
                           ei::kTelephotoMultiplierMaximum);
    m_multiplier->setSingleStep(0.1);
    m_multiplier->setDecimals(1);
    m_multiplier->setPrefix(QString::fromUtf8("\xC3\x97"));
    m_multiplier->setValue(ei::kTelephotoMultiplierDefault);
    m_multiplier->setFixedWidth(72);
    m_multiplier->setToolTip(help);
    m_multiplier->setAccessibleName(tr("Multiplier:"));

    layout->addWidget(m_enabled);
    layout->addWidget(multiplierLabel);
    layout->addWidget(m_multiplier);

    connect(m_enabled, &QCheckBox::toggled,
            this, &CCameraTelephotoControls::telephotoEnabledChanged);
    connect(m_multiplier, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CCameraTelephotoControls::telephotoMultiplierChanged);
}

bool CCameraTelephotoControls::telephotoEnabled() const
{
    return m_enabled->isChecked();
}

void CCameraTelephotoControls::setTelephotoEnabled(bool enabled)
{
    const QSignalBlocker blocker(m_enabled);
    m_enabled->setChecked(enabled);
}

double CCameraTelephotoControls::telephotoMultiplier() const
{
    return m_multiplier->value();
}

void CCameraTelephotoControls::setTelephotoMultiplier(double multiplier)
{
    const QSignalBlocker blocker(m_multiplier);
    m_multiplier->setValue(ei::boundedTelephotoMultiplier(float(multiplier)));
}
