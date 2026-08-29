#ifndef CAMERA_TELEPHOTO_CONTROLS_H
#define CAMERA_TELEPHOTO_CONTROLS_H

#include <QWidget>

class QCheckBox;
class QComboBox;
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
    int telephotoDirection() const;
    void setTelephotoDirection(int direction);

signals:
    void telephotoEnabledChanged(bool enabled);
    void telephotoMultiplierChanged(double multiplier);
    void telephotoDirectionChanged(int direction);

private:
    QCheckBox* m_enabled;
    QDoubleSpinBox* m_multiplier;
    QComboBox* m_direction;
};

#endif // CAMERA_TELEPHOTO_CONTROLS_H
