#ifndef MINIMAP_EXPORT_DIALOG_H
#define MINIMAP_EXPORT_DIALOG_H

#include <QDialog>

#include "view.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class CMinimapExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CMinimapExportDialog(const SMinimapSceneCaptureResult& capture,
                                  const QString& suggestedTextureName,
                                  const QString& zoneId,
                                  QWidget* parent = nullptr);

    bool exportSucceeded() const { return m_exportSucceeded; }
    bool catalogRegistrationRequested() const;
    QString textureName() const;
    QString destinationArchive() const;

private slots:
    void chooseDestination();
    void exportTexture();
    void saveReviewPng();
    void updateReadyState();

private:
    SMinimapSceneCaptureResult m_capture;
    QString m_zoneId;
    QLineEdit* m_textureName{nullptr};
    QLineEdit* m_destination{nullptr};
    QLabel* m_status{nullptr};
    QCheckBox* m_registerCatalog{nullptr};
    QPushButton* m_exportButton{nullptr};
    bool m_exportSucceeded{false};
};

#endif
