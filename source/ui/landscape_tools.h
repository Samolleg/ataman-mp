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
struct SMaterial;

enum class EOverlaySurfaceOperation
{
    PaintExisting,
    Activate,
    Deactivate
};

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
    bool overlaySculptEnabled() const;
    bool editingEnabled() const;
    bool tilePaintEnabled() const;
    bool overlayPaintEnabled() const;
    bool overlaySurfaceEnabled() const;
    ELandscapeEditMode editMode() const;
    void activateMode(ELandscapeEditMode mode, bool notify = true);
    void setEditingEnabled(bool enabled, bool notify = true);
    ELandscapeSculptTool sculptTool() const;
    void setSculptTool(ELandscapeSculptTool tool);
    float radius() const;
    float strength() const;
    float rawDelta() const;
    quint16 flattenHeight() const;
    void setFlattenHeight(quint16 height);
    ETerrainBrushFalloff falloff() const;
    bool flipHeightmapVertically() const;
    float paintRadius() const;
    ETerrainTileBrushShape paintShape() const;
    bool manualTileSelectionEnabled() const;
    bool landFamiliesAllowedInOverlay() const;
    bool overlayMaterialSlotLabelsVisible() const;
    void setOverlayMaterialSlotLabelsVisible(bool visible);
    EOverlaySurfaceOperation overlaySurfaceOperation() const;
    qint16 overlayMaterialIndex(bool* selected = nullptr) const;
    void setOverlayMaterials(const QVector<SMaterial>& materials);
    void adjustSculptRadius(int steps);
    void adjustSculptStrength(int steps);
    void adjustRawDelta(int steps);
    void adjustFlattenHeight(int steps);
    void adjustPaintSize(int steps);
    void adjustPaintTerrainType(int steps);
    qint32 paintTerrainType(bool* selected = nullptr) const;
    void setPaintTerrainTypes(const QVector<QPair<qint32, QString>>& landTypes,
                              const QVector<QPair<qint32, QString>>& underwaterTypes,
                              const QVector<QPair<qint32, QString>>& overlayTypes,
                              const QString& status = QString());
    bool selectPaintTerrainType(qint32 terrainType);
    bool underwaterFamiliesVisible() const;
    void setUnderwaterFamiliesVisible(bool visible);
    bool mainTerrainVisible() const;
    bool overlaysVisible() const;
    void setTerrainVisibility(bool mainTerrainVisible, bool overlaysVisible,
                              bool notify = false);

signals:
    void importHeightmapRequested();
    void tilePickerRequested();
    void atlasProfileReviewRequested();
    void exportPaintDiagnosticRequested();
    void openMprSettingsRequested();
    void editingStateChanged();
    void brushGeometryChanged();
    void paintTerrainTypeChanged(qint32 terrainType);
    void terrainVisibilityChanged(bool mainTerrainVisible, bool overlaysVisible);
    void overlayMaterialSlotLabelsChanged(bool visible);

private:
    bool m_editingActive{false};
    QComboBox* m_mode;
    QComboBox* m_tool;
    QDoubleSpinBox* m_radius;
    QDoubleSpinBox* m_strength;
    QDoubleSpinBox* m_delta;
    QSpinBox* m_flatten;
    QComboBox* m_falloff;
    QLabel* m_deltaLabel;
    QLabel* m_flattenLabel;
    QLabel* m_falloffLabel;
    QCheckBox* m_flipVertical;
    QComboBox* m_paintTerrainType;
    QDoubleSpinBox* m_paintRadius;
    QComboBox* m_paintShape;
    QCheckBox* m_manualTileSelection;
    QCheckBox* m_showUnderwaterFamilies;
    QCheckBox* m_allowLandFamiliesInOverlay;
    QCheckBox* m_showOverlayMaterialSlots;
    QComboBox* m_overlaySurfaceOperation;
    QComboBox* m_overlayMaterial;
    QLabel* m_overlaySurfaceOperationLabel;
    QLabel* m_overlayMaterialLabel;
    QCheckBox* m_showMainTerrain;
    QCheckBox* m_showOverlays;
    QLabel* m_tilesetStatus;
    QWidget* m_sculptControls;
    QWidget* m_paintControls;
    QVector<QPair<qint32, QString>> m_landPaintTypes;
    QVector<QPair<qint32, QString>> m_underwaterPaintTypes;
    QVector<QPair<qint32, QString>> m_overlayPaintTypes;
    QString m_paintCatalogStatus;
    void updateModeVisibility();
    void refreshPaintTerrainTypes();
};

#endif
