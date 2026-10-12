#ifndef LANDSCAPE_TOOLS_H
#define LANDSCAPE_TOOLS_H

#include <QWidget>
#include <QByteArray>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <QVector>

#include "landscape_edit_policy.h"
#include "terrain_edit_model.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QButtonGroup;
class QGridLayout;
class QHBoxLayout;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QScrollArea;
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
    QString familyName;
    QImage thumbnail;
    QVector<QImage> authoredThumbnails;
    // The semantic brush is already selectable while its exact reviewed-HD
    // artwork is verified off the UI thread.  A pending entry must never fall
    // back to misleading initials that look like a finished brush preview.
    bool thumbnailPending{false};
    bool animated{false};
    bool paintAvailable{true};
    QString paintUnavailableReason;
    int assignedSlots{0};
    bool cornerOnly{false};
    bool contextualStamp{false};
    bool used{false};
    QStringList generatedIds;
    QVector<QImage> generatedThumbnails;
};

enum class ETerrainPaintPaletteMode
{
    Modern,
    ClassicAuto,
    Manual
};

enum class ETerrainPaintTargetMode
{
    Tile,
    Vertex
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
    Smooth,
    Noise,
    BoundaryFill
};

class CLandscapeTools : public QWidget
{
    Q_OBJECT
public:
    explicit CLandscapeTools(QWidget* parent = nullptr);

    QSize minimumSizeHint() const override;
    void setWorldTerrainStatus(const QString& status, bool canUndo, bool canRedo, bool dirty, bool enabled, int neighbors);
    void setTerrainCatalogLoading(bool loading);

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
    bool moveMobObjectsWithTerrain() const;
    quint16 flattenHeight() const;
    void setFlattenHeight(quint16 height);
    ETerrainBrushFalloff falloff() const;
    bool flipHeightmapVertically() const;
    float paintRadius() const;
    ETerrainTileBrushShape paintShape() const;
    bool manualTileSelectionEnabled() const;
    bool classicAutoPaintingEnabled() const;
    ETerrainPaintPaletteMode paintPaletteMode() const;
    void setPaintPaletteMode(ETerrainPaintPaletteMode mode);
    ETerrainPaintTargetMode paintTargetMode() const;
    bool classicAutoSafeMode() const;
    bool modernAutoResolveBlends() const;
    bool modernMixedEdge() const;
    bool tilePaintPreviewEnabled() const;
    bool randomizeUniformRotation() const;
    bool randomPaintVariants() const;
    bool generatedHdVariants() const;
    bool generatedHdVariantsRequested() const;
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
    qint32 paintTerrainSupport() const;
    bool paintIssueMarkingEnabled() const;
    QString paintIssueCategory() const;
    void setPaintIssueMarkingEnabled(bool enabled);
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
    void setClassicAutoMaterials(
        const QVector<STerrainPaintMaterialChoice>& landTypes,
        const QVector<STerrainPaintMaterialChoice>& underwaterTypes,
        const QVector<STerrainPaintMaterialChoice>& overlayTypes);
    void beginTerrainPaletteUpdate();
    void endTerrainPaletteUpdate();
    void setManualAtlasTiles(const QVector<QImage>& tiles,
                              const QVector<qint32>& runtimeTypes,
                              const QStringList& descriptions = QStringList(),
                              // When supplied, each non-empty entry is the
                              // stable physical-pixel SHA-256 + runtime-type
                              // identity for the corresponding slot. Empty
                              // entries are unaddressable physical padding.
                              const QVector<QByteArray>& physicalIdentities = {});
    void setGeneratedAtlasTileCount(int count, int activeSlots = -1);
    void setUsedTerrainFamilies(const QSet<qint32>& landFamilies,
                                const QSet<qint32>& overlayFamilies);
    bool selectPaintTerrainType(qint32 terrainType);
    bool selectManualAtlasTile(int slot);
    int currentManualAtlasTile() const;
    bool underwaterFamiliesVisible() const;
    void setUnderwaterFamiliesVisible(bool visible);
    bool mainTerrainVisible() const;
    bool overlaysVisible() const;
    void setTerrainVisibility(bool mainTerrainVisible, bool overlaysVisible,
                              bool notify = false);

signals:
    void shuffleTerrainTilesRequested(bool selectionOnly);
    void worldSculptSelectionRequested();
    void worldSculptUndoRequested();
    void worldSculptRedoRequested();
    void worldSculptSaveRequested();
    void importHeightmapRequested();
    void resolveMissingBlendsRequested();
    void atlasProfileReviewRequested();
    void atlasHdRepublishRequested();
    void generateHdVariantsRequested();
    void generatedHdVariantsChanged(bool enabled);
    void exportPaintDiagnosticRequested();
    void openMprSettingsRequested();
    void openMprMaterialsRequested();
    void saveModernDraftRequested();
    void editingStateChanged();
    void brushGeometryChanged();
    void paintTerrainTypeChanged(qint32 terrainType);
    // The physical Manual palette is comparatively expensive to materialize.
    // Request it only when the user actually opens that paint mode.
    void manualAtlasPaletteRequested();
    void manualAtlasTileRequested(int slot);
    void terrainVisibilityChanged(bool mainTerrainVisible, bool overlaysVisible);
    void overlayMaterialSlotLabelsChanged(bool visible);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refreshResponsiveControls();
    const STerrainPaintMaterialChoice* selectedPaintChoice() const;
    bool semanticPaintOptionsApplicable() const;
    bool mixedEdgeApplicable() const;
    void updatePaintOptionApplicability();
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
    QCheckBox* m_moveMobObjects;
    QTabWidget* m_materialTabs;
    QTabWidget* m_paintModeTabs;
    QTabWidget* m_classicMaterialTabs;
    QLineEdit* m_paintFilter;
    QCheckBox* m_usedMaterialsOnly;
    QWidget* m_modernModePage;
    QWidget* m_classicModePage;
    QWidget* m_manualModePage;
    QScrollArea* m_groundMaterialScroll;
    QScrollArea* m_underwaterMaterialScroll;
    QScrollArea* m_overlayMaterialScroll;
    QWidget* m_groundMaterialPage;
    QWidget* m_underwaterMaterialPage;
    QWidget* m_overlayMaterialPage;
    QGridLayout* m_groundMaterialGrid;
    QGridLayout* m_underwaterMaterialGrid;
    QGridLayout* m_overlayMaterialGrid;
    QLabel* m_variantStripTitle{nullptr};
    QHBoxLayout* m_variantStripLayout{nullptr};
    QButtonGroup* m_groundMaterialButtons;
    QButtonGroup* m_overlayMaterialButtons;
    QScrollArea* m_classicGroundMaterialScroll;
    QScrollArea* m_classicOverlayMaterialScroll;
    QWidget* m_classicGroundMaterialPage;
    QWidget* m_classicOverlayMaterialPage;
    QGridLayout* m_classicGroundMaterialGrid;
    QGridLayout* m_classicUnderwaterMaterialGrid;
    QGridLayout* m_classicOverlayMaterialGrid;
    QButtonGroup* m_classicGroundMaterialButtons;
    QButtonGroup* m_classicOverlayMaterialButtons;
    QListWidget* m_manualTileList;
    QSlider* m_paintRadius;
    QLabel* m_paintRadiusValue;
    QToolButton* m_circlePaintShape;
    QToolButton* m_squarePaintShape;
    QToolButton* m_tilePaintTarget;
    QToolButton* m_vertexPaintTarget;
    QCheckBox* m_classicAutoSafe;
    QCheckBox* m_modernAutoResolveBlends;
    QCheckBox* m_modernMixedEdge{nullptr};
    QCheckBox* m_randomizeUniformRotation;
    QCheckBox* m_randomPaintVariants;
    QCheckBox* m_generatedHdVariants{nullptr};
    QComboBox* m_randomPaintScope;
    QComboBox* m_paintIssueCategory;
    QToolButton* m_markPaintIssue;
    QCheckBox* m_showOverlayMaterialSlots;
    QComboBox* m_overlaySurfaceOperation;
    QComboBox* m_overlayMaterial;
    QPushButton* m_overlayMprMaterials;
    QLabel* m_overlaySurfaceOperationLabel;
    QLabel* m_overlayMaterialLabel;
    QCheckBox* m_showMainTerrain;
    QCheckBox* m_showOverlays;
    QLabel* m_tilesetStatus;
    bool m_terrainCatalogLoading{false};
    QLabel* m_atlasCoverage;
    QLabel* m_generatedAtlasTiles;
    QProgressBar* m_atlasCapacity;
    int m_atlasActiveSlots{0};
    int m_workingAtlasActiveSlots{-1};
    QLabel* m_atlasCapacityWarning;
    QWidget* m_sculptControls;
    QLabel* m_worldTerrainStatus{nullptr};
    QPushButton* m_worldUndo{nullptr};
    QPushButton* m_worldRedo{nullptr};
    QPushButton* m_worldSave{nullptr};
    QWidget* m_paintControls;
    QVector<STerrainPaintMaterialChoice> m_landPaintTypes;
    QVector<STerrainPaintMaterialChoice> m_underwaterPaintTypes;
    QVector<STerrainPaintMaterialChoice> m_overlayPaintTypes;
    QVector<STerrainPaintMaterialChoice> m_classicLandPaintTypes;
    QVector<STerrainPaintMaterialChoice> m_classicUnderwaterPaintTypes;
    QVector<STerrainPaintMaterialChoice> m_classicOverlayPaintTypes;
    QString m_paintCatalogStatus;
    int m_paletteColumns{1};
    int m_authoredAtlasSlots{0};
    bool m_paletteRelayoutPending{false};
    int m_terrainPaletteUpdateDepth{0};
    bool m_terrainPaletteRebuildPending{false};
    bool m_terrainPaletteRefreshPending{false};
    void updateModeVisibility();
    void refreshPaintTerrainTypes();
    void rebuildMaterialPalette();
    void requestMaterialPaletteRebuild();
    void requestPaintTerrainTypesRefresh();
    void refreshAtlasCapacity(int activeSlots);
    void updatePaintIssueUi();
    void updatePaintShapeUi();
    void clearPaintMaterialSelection();
    void applyPaintFilter();
    void refreshPaletteColumns(bool rebuild = true);
    void schedulePaletteRelayout();
    void updateVariantStrip(const STerrainPaintMaterialChoice& choice);
    void refreshVariantStrip();
    int m_paletteCellSize{64};
    QButtonGroup* activeMaterialButtons() const;
};

#endif
