#include "camera_telephoto_controls.h"

#include "camera_telephoto.h"

#include <QCheckBox>
#include <QPainter>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

CCameraTelephotoControls::CCameraTelephotoControls(QWidget* parent)
    : QWidget(parent)
    , m_enabled(new QCheckBox(tr("Telephoto"), this))
    , m_multiplier(new QDoubleSpinBox(this))
    , m_direction(new QComboBox(this))
{
    setObjectName("cameraTelephotoControls");
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 0, 0);
    layout->setSpacing(4);

    m_enabled->setObjectName("telephotoCameraEnabled");
    m_enabled->setChecked(false);
    m_enabled->setText(QString());
    QPixmap camera(44,44);camera.setDevicePixelRatio(2);camera.fill(Qt::transparent);
    {QPainter p(&camera);p.setRenderHint(QPainter::Antialiasing);
     p.setPen(QPen(QColor("#c5deed"),1.6));p.setBrush(QColor("#477f98"));
     p.drawRoundedRect(2,6,17,13,2,2);p.drawRect(6,3,8,3);p.drawEllipse(7,9,7,7);}
    m_enabled->setIcon(QIcon(camera));m_enabled->setIconSize(QSize(22,22));
    const QString help = tr(
        "JamaisVu telephoto camera. Uses the selected fixed diagonal direction "
        "and a pitch of 35.264 degrees, moves the render eye back, and narrows "
        "perspective while keeping map movement and zoom available.");
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

    QLabel* directionLabel = new QLabel(tr("Direction:"), this);
    directionLabel->setBuddy(m_direction);
    m_direction->setObjectName("telephotoCameraDirection");
    m_direction->addItem(tr("SW → NE"),
        int(ei::ETelephotoDirection::SouthwestToNortheast));
    m_direction->addItem(tr("SE → NW"),
        int(ei::ETelephotoDirection::SoutheastToNorthwest));
    m_direction->addItem(tr("NE → SW"),
        int(ei::ETelephotoDirection::NortheastToSouthwest));
    m_direction->addItem(tr("NW → SE"),
        int(ei::ETelephotoDirection::NorthwestToSoutheast));
    m_direction->setCurrentIndex(int(ei::kTelephotoDirectionDefault));
    m_direction->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_direction->setToolTip(help);
    m_direction->setAccessibleName(tr("Direction:"));

    layout->addWidget(m_enabled);
    multiplierLabel->hide();
    layout->addWidget(m_multiplier);
    directionLabel->hide();
    layout->addWidget(m_direction);

    connect(m_enabled, &QCheckBox::toggled,
            this, &CCameraTelephotoControls::telephotoEnabledChanged);
    connect(m_multiplier, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CCameraTelephotoControls::telephotoMultiplierChanged);
    connect(m_direction, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index)
    {
        emit telephotoDirectionChanged(m_direction->itemData(index).toInt());
    });
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

int CCameraTelephotoControls::telephotoDirection() const
{
    return m_direction->currentData().toInt();
}

void CCameraTelephotoControls::setTelephotoDirection(int direction)
{
    const QSignalBlocker blocker(m_direction);
    const int bounded = int(ei::boundedTelephotoDirection(direction));
    const int index = m_direction->findData(bounded);
    m_direction->setCurrentIndex(index >= 0 ? index : 0);
}
