#ifndef LANDSCAPE_TOOLS_H
#define LANDSCAPE_TOOLS_H

#include <QWidget>
#include <QImage>
#include <QSet>
#include <QVector>

#include "landscape_edit_policy.h"
#include "terrain_edit_model.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QButtonGroup;
class QGridLayout;
class QSlider;
class QSpinBox;
class QTabWidget;
class QToolButton;
struct SMaterial;

struct STerrainPaintMaterialChoice
{
    qint32 id{0};
    qint32 supportId{0};
    QString name;
    QImage thumbnail;
    int assignedSlots{0};
    bool cornerOnly{false};
    bool contextualStamp{false};
    bool used{false};
};

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
    float radius() const;
    float strength() const;
    float rawDelta() const;
    quint16 flattenHeight() const;
    bool flipHeightmapVertically() const;
    float paintRadius() const;
    ETerrainTileBrushShape paintShape() const;
    bool manualTileSelectionEnabled() const;
    bool landFamiliesAllowedInOverlay() const;
    EOverlaySurfaceOperation overlaySurfaceOperation() const;
    qint16 overlayMaterialIndex(bool* selected = nullptr) const;
    void setOverlayMaterials(const QVector<SMaterial>& materials);
    void adjustSculptRadius(int steps);
    void adjustSculptStrength(int steps);
    void adjustPaintSize(int steps);
    void adjustPaintTerrainType(int steps);
    qint32 paintTerrainType(bool* selected = nullptr) const;
    qint32 paintTerrainSupport() const;
    void setPaintTerrainTypes(const QVector<QPair<qint32, QString>>& landTypes,
                              const QVector<QPair<qint32, QString>>& underwaterTypes,
                              const QVector<QPair<qint32, QString>>& overlayTypes,
                              const QString& status = QString());
    void setPaintMaterials(const QVector<STerrainPaintMaterialChoice>& landTypes,
                           const QVector<STerrainPaintMaterialChoice>& underwaterTypes,
                           const QVector<STerrainPaintMaterialChoice>& overlayTypes,
                           int assignedSlots, int totalSlots,
                           const QString& status = QString(),
                           const QString& atlasSource = QString(),
                           const QString& atlasSourcePath = QString());
    void setGeneratedAtlasTileCount(int count);
    void setUsedTerrainFamilies(const QSet<qint32>& landFamilies,
                                const QSet<qint32>& overlayFamilies);
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
    void finalizeVirtualAtlasRequested();
    void editingStateChanged();
    void paintTerrainTypeChanged(qint32 terrainType);
    void terrainVisibilityChanged(bool mainTerrainVisible, bool overlaysVisible);

private:
    bool m_editingActive{false};
    QComboBox* m_mode;
    QComboBox* m_tool;
    QDoubleSpinBox* m_radius;
    QDoubleSpinBox* m_strength;
    QDoubleSpinBox* m_delta;
    QSpinBox* m_flatten;
    QCheckBox* m_flipVertical;
    QTabWidget* m_materialTabs;
    QWidget* m_groundMaterialPage;
    QWidget* m_overlayMaterialPage;
    QGridLayout* m_groundMaterialGrid;
    QGridLayout* m_underwaterMaterialGrid;
    QGridLayout* m_overlayMaterialGrid;
    QButtonGroup* m_groundMaterialButtons;
    QButtonGroup* m_overlayMaterialButtons;
    QSlider* m_paintRadius;
    QLabel* m_paintRadiusValue;
    QToolButton* m_circlePaintShape;
    QToolButton* m_squarePaintShape;
    QCheckBox* m_manualTileSelection;
    QComboBox* m_overlaySurfaceOperation;
    QComboBox* m_overlayMaterial;
    QLabel* m_overlaySurfaceOperationLabel;
    QLabel* m_overlayMaterialLabel;
    QCheckBox* m_showMainTerrain;
    QCheckBox* m_showOverlays;
    QLabel* m_tilesetStatus;
    QLabel* m_atlasCoverage;
    QLabel* m_generatedAtlasTiles;
    QWidget* m_sculptControls;
    QWidget* m_paintControls;
    QVector<STerrainPaintMaterialChoice> m_landPaintTypes;
    QVector<STerrainPaintMaterialChoice> m_underwaterPaintTypes;
    QVector<STerrainPaintMaterialChoice> m_overlayPaintTypes;
    QString m_paintCatalogStatus;
    void updateModeVisibility();
    void refreshPaintTerrainTypes();
    void rebuildMaterialPalette();
};

#endif
