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

enum class ELandscapeEditMode
{
    Sculpt,
    TilePaint,
    OverlayPaint
};

class CLandscapeTools : public QWidget
{
    Q_OBJECT
public:
    explicit CLandscapeTools(QWidget* parent = nullptr);

    bool sculptEnabled() const;
    bool editingEnabled() const;
    bool tilePaintEnabled() const;
    bool overlayPaintEnabled() const;
    ELandscapeEditMode editMode() const;
    void activateMode(ELandscapeEditMode mode, bool notify = true);
    void setEditingEnabled(bool enabled, bool notify = true);
    ELandscapeSculptTool sculptTool() const;
    float radius() const;
    float strength() const;
    float rawDelta() const;
    quint16 flattenHeight() const;
    bool flipHeightmapVertically() const;

signals:
    void importHeightmapRequested();
    void tilePickerRequested();
    void editingStateChanged();

private:
    QCheckBox* m_enable;
    QComboBox* m_mode;
    QComboBox* m_tool;
    QDoubleSpinBox* m_radius;
    QDoubleSpinBox* m_strength;
    QDoubleSpinBox* m_delta;
    QSpinBox* m_flatten;
    QCheckBox* m_flipVertical;
    QWidget* m_sculptControls;
    QWidget* m_paintControls;
    void updateModeVisibility();
};

#endif
