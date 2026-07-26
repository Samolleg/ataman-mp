#ifndef TERRAIN_ATLAS_REVIEW_DIALOG_H
#define TERRAIN_ATLAS_REVIEW_DIALOG_H

#include <QDialog>
#include <QImage>

#include "terrain_atlas_profile.h"

class QLabel;
class QPushButton;
class QTableWidget;

class CTerrainAtlasReviewDialog : public QDialog
{
    Q_OBJECT
public:
    CTerrainAtlasReviewDialog(const STerrainAtlasProfile& profile,
                              const QVector<QImage>& tileImages,
                              QWidget* parent = nullptr);

    const STerrainAtlasProfile& reviewedProfile() const { return m_profile; }

private slots:
    void refreshSlots();
    void updatePreview();
    void applyFamilyCellChange(int row, int column);
    void splitSelectedFamily();
    void mergeSelectedFamilies();
    void markSelectedSlotsReviewed();
    void markSelectedSlotsForReview();
    void showGuide();
    void saveDraft();
    void approveAndSave();

private:
    void rebuildTables(qint32 selectFamilyId = -1);
    void updateDiagnostics();
    qint32 selectedFamilyId() const;
    QVector<qint32> selectedFamilyIds() const;
    QVector<int> selectedSlotIds() const;
    void showReport(const QString& title, const CValidationReport& report);

    STerrainAtlasProfile m_profile;
    QVector<QImage> m_tileImages;
    QTableWidget* m_families;
    QTableWidget* m_slots;
    QLabel* m_preview;
    QLabel* m_binding;
    QLabel* m_diagnostics;
    QPushButton* m_approve;
    bool m_rebuilding{false};
};

#endif
