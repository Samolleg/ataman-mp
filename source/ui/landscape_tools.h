#ifndef LANDSCAPE_TOOLS_H
#define LANDSCAPE_TOOLS_H

#include <QWidget>
#include <QVector>

#include "landscape_edit_policy.h"
#include "terrain_edit_model.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
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
    float paintRadius() const;
    ETerrainTileBrushShape paintShape() const;
    qint32 paintTerrainType(bool* selected = nullptr) const;
    void setPaintTerrainTypes(const QVector<QPair<qint32, QString>>& landTypes,
                              const QVector<QPair<qint32, QString>>& overlayTypes,
                              const QString& status = QString());
    bool selectPaintTerrainType(qint32 terrainType);

signals:
    void importHeightmapRequested();
    void tilePickerRequested();
    void exportPaintDiagnosticRequested();
    void editingStateChanged();
    void paintTerrainTypeChanged(qint32 terrainType);

private:
    QCheckBox* m_enable;
    QComboBox* m_mode;
    QComboBox* m_tool;
    QDoubleSpinBox* m_radius;
    QDoubleSpinBox* m_strength;
    QDoubleSpinBox* m_delta;
    QSpinBox* m_flatten;
    QCheckBox* m_flipVertical;
    QComboBox* m_paintTerrainType;
    QDoubleSpinBox* m_paintRadius;
    QComboBox* m_paintShape;
    QLabel* m_tilesetStatus;
    QWidget* m_sculptControls;
    QWidget* m_paintControls;
    QVector<QPair<qint32, QString>> m_landPaintTypes;
    QVector<QPair<qint32, QString>> m_overlayPaintTypes;
    QString m_paintCatalogStatus;
    void updateModeVisibility();
    void refreshPaintTerrainTypes();
};

#endif
