#ifndef CAMERA_TELEPHOTO_CONTROLS_H
#define CAMERA_TELEPHOTO_CONTROLS_H

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;

class CCameraTelephotoControls : public QWidget
{
    Q_OBJECT

public:
    explicit CCameraTelephotoControls(QWidget* parent = nullptr);

    bool telephotoEnabled() const;
    void setTelephotoEnabled(bool enabled);
    double telephotoMultiplier() const;
    void setTelephotoMultiplier(double multiplier);

signals:
    void telephotoEnabledChanged(bool enabled);
    void telephotoMultiplierChanged(double multiplier);

private:
    QCheckBox* m_enabled;
    QDoubleSpinBox* m_multiplier;
};

#endif // CAMERA_TELEPHOTO_CONTROLS_H
