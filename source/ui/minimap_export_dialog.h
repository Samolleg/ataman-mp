#ifndef MINIMAP_EXPORT_DIALOG_H
#define MINIMAP_EXPORT_DIALOG_H

#include <QDialog>

#include "view.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class CZoomableMinimapView;

class CMinimapExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CMinimapExportDialog(const SMinimapSceneCaptureResult& capture,
                                  const QImage& embeddedImage,
                                  const QString& embeddedName,
                                  const QString& suggestedTextureName,
                                  const QString& zoneId,
                                  const SMinimapGradeSettings& initialGrade,
                                  bool generationLocked,
                                  QWidget* parent = nullptr);

    bool exportSucceeded() const { return m_exportSucceeded; }
    bool catalogRegistrationRequested() const;
    QString textureName() const;
    QString destinationArchive() const;
    bool lockRequested() const { return m_lockRequested; }
    QImage gradedImage() const { return m_gradedImage; }
    SMinimapGradeSettings gradeSettings() const { return m_grade; }
    SMinimapSceneCaptureResult capture() const { return m_capture; }
    void setCapture(const SMinimapSceneCaptureResult& capture);
    void setGenerationLocked(bool locked);

signals:
    void regenerationRequested();
    void appliedAndLocked();
    void catalogRegistrationReady(const QString& textureName);

private slots:
    void chooseDestination();
    void exportTexture();
    void saveReviewPng();
    void updateReadyState();
    void updateGradePreview();
    void applyAndLock();

private:
    SMinimapSceneCaptureResult m_capture;
    QString m_zoneId;
    QImage m_embeddedImage;
    QString m_embeddedName;
    QImage m_gradedImage;
    SMinimapGradeSettings m_grade;
    CZoomableMinimapView* m_generatedPreview{nullptr};
    QSlider* m_shading{nullptr};
    QSlider* m_saturation{nullptr};
    QSlider* m_contrast{nullptr};
    QSlider* m_brightness{nullptr};
    QSlider* m_hue{nullptr};
    QSpinBox* m_shadingValue{nullptr};
    QSpinBox* m_saturationValue{nullptr};
    QSpinBox* m_contrastValue{nullptr};
    QSpinBox* m_brightnessValue{nullptr};
    QSpinBox* m_hueValue{nullptr};
    QPushButton* m_regenerateButton{nullptr};
    QPushButton* m_applyLockButton{nullptr};
    QLineEdit* m_textureName{nullptr};
    QLineEdit* m_destination{nullptr};
    QLabel* m_status{nullptr};
    QCheckBox* m_registerCatalog{nullptr};
    QPushButton* m_exportButton{nullptr};
    bool m_exportSucceeded{false};
    bool m_lockRequested{false};
    bool m_generationLocked{false};
};

#endif
