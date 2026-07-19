#ifndef LANDSCAPE_TOOLS_H
#define LANDSCAPE_TOOLS_H

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

enum class ELandscapeSculptTool
{
    Raise,
    Lower,
    Flatten,
    Smooth
};

class CLandscapeTools : public QWidget
{
    Q_OBJECT
public:
    explicit CLandscapeTools(QWidget* parent = nullptr);

    bool sculptEnabled() const;
    ELandscapeSculptTool sculptTool() const;
    float radius() const;
    float strength() const;
    float rawDelta() const;
    quint16 flattenHeight() const;
    bool flipHeightmapVertically() const;

signals:
    void importHeightmapRequested();

private:
    QCheckBox* m_enable;
    QComboBox* m_tool;
    QDoubleSpinBox* m_radius;
    QDoubleSpinBox* m_strength;
    QDoubleSpinBox* m_delta;
    QSpinBox* m_flatten;
    QCheckBox* m_flipVertical;
};

#endif
