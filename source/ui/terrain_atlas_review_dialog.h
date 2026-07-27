#ifndef TERRAIN_ATLAS_REVIEW_DIALOG_H
#define TERRAIN_ATLAS_REVIEW_DIALOG_H

#include <QDialog>
#include <QImage>

#include "terrain_atlas_profile.h"

class CAtlasMaterialTree;
class CAtlasTilePool;
class CFamilyCornerButton;
class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidgetItem;

class CTerrainAtlasReviewDialog : public QDialog
{
    Q_OBJECT
public:
    CTerrainAtlasReviewDialog(const STerrainAtlasProfile& profile,
                              const QVector<QImage>& tileImages,
                              const STerrainAtlasProfileSource& source = STerrainAtlasProfileSource(),
                              QWidget* parent = nullptr);

    const STerrainAtlasProfile& reviewedProfile() const { return m_profile; }
    bool shouldPersistOverride() const { return m_persistOverride; }
    bool shouldResetToDefault() const { return m_resetToDefault; }

private slots:
    void selectTreeItem(QTreeWidgetItem* item);
    void selectPoolItem();
    void renameSelectedFamily();
    void createMaterial();
    void deleteSelectedFamily();
    void changeSelectedClassification(int index);
    void confirmSelectedFamily();
    void useSelectedTileAsThumbnail();
    void applyAndClose();
    void exportCopy();
    void openProfile();
    void resetToDefault();

private:
    void rebuildVisuals(qint32 selectFamilyId = -1, int selectSlot = -1);
    void updateInspector();
    void updateDiagnostics();
    void assignTile(int slot, qint32 sourceFamily, qint32 targetFamily);
    void unassignTile(int slot);
    void assignCorner(int corner, qint32 familyId);
    void refreshPaintSelectableFamilies();
    void chooseCornerMaterial(int corner);
    int familyThumbnailSlot(qint32 familyId) const;
    QImage familyDisplayThumbnail(qint32 familyId) const;
    qint32 selectedFamilyId() const;
    STerrainAtlasProfileFamily* family(qint32 id);
    const STerrainAtlasProfileFamily* family(qint32 id) const;
    STerrainAtlasProfileSlot* slot(int id);
    const STerrainAtlasProfileSlot* slot(int id) const;
    QString familyName(qint32 id) const;
    void showReport(const QString& title, const CValidationReport& report);
    void markEdited();
    void updateStorage();

    STerrainAtlasProfile m_profile;
    STerrainAtlasProfileSource m_source;
    QVector<QImage> m_tileImages;
    CAtlasMaterialTree* m_materials;
    CAtlasTilePool* m_unassigned;
    QLabel* m_preview;
    QLabel* m_tileTitle;
    QLabel* m_tileHelp;
    QLabel* m_binding;
    QLabel* m_storage;
    QLabel* m_diagnostics;
    QLabel* m_familyReason;
    QComboBox* m_familyClass;
    QPushButton* m_confirmFamily;
    QPushButton* m_thumbnail;
    QPushButton* m_apply;
    CFamilyCornerButton* m_corners[4];
    int m_selectedSlot{-1};
    bool m_rebuilding{false};
    bool m_modified{false};
    bool m_persistOverride{false};
    bool m_resetToDefault{false};
};

#endif
