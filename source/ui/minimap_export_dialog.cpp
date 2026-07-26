#include "minimap_export_dialog.h"

#include <QtWidgets>
#include <QtConcurrent>
#include <QCryptographicHash>
#include <QFutureWatcher>

#include "mmp_codec.h"
#include "res_archive_document.h"
#include "res_texture_export.h"

class CZoomableMinimapView : public QGraphicsView
{
public:
    explicit CZoomableMinimapView(QWidget* parent = nullptr)
        : QGraphicsView(parent), m_scene(new QGraphicsScene(this))
    {
        setScene(m_scene);
        setAlignment(Qt::AlignCenter);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorViewCenter);
        setBackgroundBrush(QColor(47, 47, 47));
        setFrameShape(QFrame::StyledPanel);
    }

    void setImage(const QImage& image)
    {
        m_scene->clear();
        m_item = m_scene->addPixmap(QPixmap::fromImage(image));
        m_scene->setSceneRect(m_item->boundingRect());
        fitImage();
    }

    void fitImage()
    {
        m_userSized = false;
        if(m_item)
            fitInView(m_item, Qt::KeepAspectRatio);
    }

    void actualSize()
    {
        m_userSized = true;
        resetTransform();
    }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        m_userSized = true;
        const qreal factor = event->angleDelta().y() >= 0 ? 1.2 : (1.0 / 1.2);
        scale(factor, factor);
        event->accept();
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QGraphicsView::resizeEvent(event);
        if(!m_userSized)
            fitImage();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if(event->button() == Qt::LeftButton)
            m_userSized = true;
        QGraphicsView::mousePressEvent(event);
    }

private:
    QGraphicsScene* m_scene{nullptr};
    QGraphicsPixmapItem* m_item{nullptr};
    bool m_userSized{false};
};

namespace {

struct SResInspection
{
    bool success{false};
    bool memberExists{false};
    QString error;
};

template<typename Result, typename Function>
Result runAnimatedResOperation(QWidget* parent, const QString& title,
                               const QString& label, Function function)
{
    QProgressDialog progress(label, QString(), 0, 100, parent);
    progress.setWindowTitle(title);
    progress.setCancelButton(nullptr);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setValue(0);

    QFutureWatcher<Result> watcher;
    QEventLoop loop;
    QTimer animation;
    animation.setInterval(70);
    QObject::connect(&animation, &QTimer::timeout, &progress, [&progress]() {
        progress.setValue(qMin(94, progress.value() + 1));
    });
    QObject::connect(&watcher, &QFutureWatcher<Result>::finished,
                     &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(function));
    progress.show();
    animation.start();
    loop.exec();
    animation.stop();
    progress.setValue(100);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    progress.close();
    return watcher.result();
}

QString normalizedMemberName(QString name)
{
    name = name.trimmed();
    if(!name.endsWith(".mmp", Qt::CaseInsensitive))
        name += ".mmp";
    return name;
}

bool safeTextureName(const QString& name)
{
    const QString value = name.trimmed();
    return !value.isEmpty() && !value.contains(QRegExp("[\\\\/:*?\"<>|\\s]"));
}
}

CMinimapExportDialog::CMinimapExportDialog(
    const SMinimapSceneCaptureResult& capture,
    const QImage& embeddedImage, const QString& embeddedName,
    const QString& suggestedTextureName, const QString& zoneId,
    const SMinimapGradeSettings& initialGrade, bool generationLocked,
    QWidget* parent)
    : QDialog(parent), m_capture(capture), m_zoneId(zoneId),
      m_embeddedImage(embeddedImage), m_embeddedName(embeddedName),
      m_grade(initialGrade),
      m_generationLocked(generationLocked)
{
    setWindowTitle(tr("Export Minimap"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    QVBoxLayout* root = new QVBoxLayout(this);

    QLabel* summary = new QLabel(this);
    summary->setWordWrap(true);
    summary->setText(capture.bake.incomplete
        ? tr("Incomplete MPR-only capture. Terrain and overlays are included, but no MOB source is loaded. Load a MOB for a production minimap.")
        : tr("Complete MPR + MOB capture. Terrain, overlays, and %1 static objects are included; %2 dynamic units were excluded.")
            .arg(capture.staticObjectCount).arg(capture.excludedDynamicUnitCount));
    summary->setStyleSheet(capture.bake.incomplete
        ? "QLabel { color: #e6a23c; font-weight: 600; }" : QString());
    root->addWidget(summary);

    QLabel* details = new QLabel(tr(
        "Review: %1 x %2 (%3 x %4 sectors)\nOutput: %5\nFraming: %6\nOrientation: %7")
        .arg(capture.bake.reviewImage.width()).arg(capture.bake.reviewImage.height())
        .arg(capture.sectorWidth).arg(capture.sectorHeight)
        .arg(capture.exportProfileDescription, capture.framingDescription,
             capture.orientationDescription), this);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(details);

    QSplitter* comparison = new QSplitter(Qt::Horizontal, this);
    QGroupBox* generatedGroup = new QGroupBox(tr("Generated"), comparison);
    QVBoxLayout* generatedLayout = new QVBoxLayout(generatedGroup);
    m_generatedPreview = new CZoomableMinimapView(generatedGroup);
    m_generatedPreview->setObjectName("minimapExportPreview");
    m_generatedPreview->setMinimumHeight(360);
    generatedLayout->addWidget(m_generatedPreview);
    QGroupBox* embeddedGroup = new QGroupBox(
        m_embeddedImage.isNull() ? tr("Embedded - no embedded minimap found")
                                 : tr("Embedded: %1").arg(m_embeddedName), comparison);
    QVBoxLayout* embeddedLayout = new QVBoxLayout(embeddedGroup);
    CZoomableMinimapView* embeddedPreview = new CZoomableMinimapView(embeddedGroup);
    embeddedPreview->setImage(m_embeddedImage.isNull() ? QImage()
                                                       : m_embeddedImage.mirrored(false, true));
    embeddedLayout->addWidget(embeddedPreview);
    comparison->addWidget(generatedGroup);
    comparison->addWidget(embeddedGroup);
    comparison->setSizes({500, 500});
    root->addWidget(comparison, 1);
    QHBoxLayout* previewControls = new QHBoxLayout;
    QLabel* previewHint = new QLabel(
        tr("%1 x %1 game texture - mouse wheel zooms; drag to pan")
            .arg(capture.bake.gameImage.width()), this);
    QPushButton* fitPreview = new QPushButton(tr("Fit"), this);
    QPushButton* actualPreview = new QPushButton(tr("100%"), this);
    m_regenerateButton = new QPushButton(QString::fromUtf8("\xE2\x86\xBB ")
                                         + tr("Regenerate"), this);
    previewControls->addWidget(previewHint, 1);
    previewControls->addWidget(fitPreview);
    previewControls->addWidget(actualPreview);
    previewControls->addWidget(m_regenerateButton);
    root->addLayout(previewControls);
    connect(fitPreview, &QPushButton::clicked, this,
            [this, embeddedPreview]() {
        m_generatedPreview->fitImage();
        embeddedPreview->fitImage();
    });
    connect(actualPreview, &QPushButton::clicked, this,
            [this, embeddedPreview]() {
        m_generatedPreview->actualSize();
        embeddedPreview->actualSize();
    });
    connect(m_regenerateButton, &QPushButton::clicked,
            this, &CMinimapExportDialog::regenerationRequested);

    QFormLayout* gradeForm = new QFormLayout;
    const auto addGradeSlider = [this, gradeForm](const QString& name, int minimum,
                                                  int maximum, int value,
                                                  const QString& suffix,
                                                  QSlider** slider,
                                                  QSpinBox** valueBox) {
        QWidget* row = new QWidget(this);
        QHBoxLayout* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        *slider = new QSlider(Qt::Horizontal, row);
        (*slider)->setRange(minimum, maximum);
        (*slider)->setValue(value);
        *valueBox = new QSpinBox(row);
        (*valueBox)->setRange(minimum, maximum);
        (*valueBox)->setValue(value);
        (*valueBox)->setSuffix(suffix);
        (*valueBox)->setMinimumWidth(74);
        layout->addWidget(*slider, 1);
        layout->addWidget(*valueBox);
        gradeForm->addRow(name, row);
        connect(*slider, &QSlider::valueChanged,
                *valueBox, &QSpinBox::setValue);
        connect(*valueBox, QOverload<int>::of(&QSpinBox::valueChanged),
                *slider, &QSlider::setValue);
        connect(*slider, &QSlider::valueChanged,
                this, &CMinimapExportDialog::updateGradePreview);
    };
    addGradeSlider(tr("Shading intensity"), 0, 150, m_grade.shadingPercent, "%",
                   &m_shading, &m_shadingValue);
    addGradeSlider(tr("Saturation"), 0, 200, m_grade.saturationPercent, "%",
                   &m_saturation, &m_saturationValue);
    addGradeSlider(tr("Contrast"), 0, 200, m_grade.contrastPercent, "%",
                   &m_contrast, &m_contrastValue);
    addGradeSlider(tr("Brightness"), 0, 200, m_grade.brightnessPercent, "%",
                   &m_brightness, &m_brightnessValue);
    addGradeSlider(tr("Hue"), -180, 180, m_grade.hueDegrees,
                   QString::fromUtf8("\xC2\xB0"), &m_hue, &m_hueValue);
    m_shading->setObjectName("minimapShadingSlider");
    m_shadingValue->setObjectName("minimapShadingValue");
    m_saturation->setObjectName("minimapSaturationSlider");
    m_saturationValue->setObjectName("minimapSaturationValue");
    m_contrast->setObjectName("minimapContrastSlider");
    m_contrastValue->setObjectName("minimapContrastValue");
    m_brightness->setObjectName("minimapBrightnessSlider");
    m_brightnessValue->setObjectName("minimapBrightnessValue");
    m_hue->setObjectName("minimapHueSlider");
    m_hueValue->setObjectName("minimapHueValue");
    m_applyLockButton = new QPushButton(tr("Apply && Lock"), this);
    gradeForm->addRow(QString(), m_applyLockButton);
    root->addLayout(gradeForm);
    connect(m_applyLockButton, &QPushButton::clicked,
            this, &CMinimapExportDialog::applyAndLock);

    QFormLayout* form = new QFormLayout;
    m_textureName = new QLineEdit(suggestedTextureName, this);
    m_textureName->setObjectName("minimapTextureName");
    form->addRow(tr("Texture name"), m_textureName);
    QWidget* destinationRow = new QWidget(this);
    QHBoxLayout* destinationLayout = new QHBoxLayout(destinationRow);
    destinationLayout->setContentsMargins(0, 0, 0, 0);
    m_destination = new QLineEdit(this);
    m_destination->setObjectName("minimapDestinationRes");
    m_destination->setPlaceholderText(tr("Choose an existing or new .res archive"));
    QPushButton* browse = new QPushButton(tr("Browse..."), this);
    destinationLayout->addWidget(m_destination, 1);
    destinationLayout->addWidget(browse);
    form->addRow(tr("Destination RES"), destinationRow);
    root->addLayout(form);

    m_registerCatalog = new QCheckBox(
        tr("After export, open zone %1 in Map Catalog Editor with this minimap reference staged")
            .arg(zoneId.isEmpty() ? tr("(unregistered)") : zoneId), this);
    m_registerCatalog->setEnabled(!zoneId.isEmpty());
    m_registerCatalog->setChecked(!zoneId.isEmpty());
    root->addWidget(m_registerCatalog);
    QLabel* catalogNote = new QLabel(tr(
        "Catalog registration is a separate verified save. Objectives metadata is preserved, and no reference is staged if texture export fails."), this);
    catalogNote->setWordWrap(true);
    root->addWidget(catalogNote);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_status);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* savePng = buttons->addButton(tr("Save review PNG..."), QDialogButtonBox::ActionRole);
    m_exportButton = buttons->addButton(tr("Export to RES..."), QDialogButtonBox::AcceptRole);
    root->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &CMinimapExportDialog::chooseDestination);
    connect(savePng, &QPushButton::clicked, this, &CMinimapExportDialog::saveReviewPng);
    connect(m_exportButton, &QPushButton::clicked, this, &CMinimapExportDialog::exportTexture);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_textureName, &QLineEdit::textChanged, this, &CMinimapExportDialog::updateReadyState);
    connect(m_destination, &QLineEdit::textChanged, this, &CMinimapExportDialog::updateReadyState);
    if(m_generationLocked)
    {
        setGenerationLocked(true);
    }
    updateGradePreview();
    resize(1100, 860);
    updateReadyState();
}

void CMinimapExportDialog::setCapture(const SMinimapSceneCaptureResult& capture)
{
    if(m_generationLocked) return;
    m_capture = capture;
    updateGradePreview();
    m_status->setText(tr("Regenerated using current object visibility."));
}

void CMinimapExportDialog::updateGradePreview()
{
    m_grade.shadingPercent = m_shading->value();
    m_grade.saturationPercent = m_saturation->value();
    m_grade.contrastPercent = m_contrast->value();
    m_grade.brightnessPercent = m_brightness->value();
    m_grade.hueDegrees = m_hue->value();
    m_gradedImage = gradeMinimapImage(
        m_capture.unlitGameImage, m_capture.bake.gameImage, m_grade);
    m_generatedPreview->setImage(m_gradedImage.mirrored(false, true));
}

void CMinimapExportDialog::applyAndLock()
{
    updateGradePreview();
    m_lockRequested = true;
    setGenerationLocked(true);
    m_status->setStyleSheet("color: #2e9d44;");
    m_status->setText(tr(
        "This graded minimap is applied and locked. Use Unlock in the viewport minimap to permit regeneration."));
    emit appliedAndLocked();
}

void CMinimapExportDialog::setGenerationLocked(bool locked)
{
    m_generationLocked = locked;
    for(QSlider* slider : {m_shading, m_saturation, m_contrast,
                           m_brightness, m_hue})
        slider->setEnabled(!locked);
    for(QSpinBox* box : {m_shadingValue, m_saturationValue, m_contrastValue,
                         m_brightnessValue, m_hueValue})
        box->setEnabled(!locked);
    m_regenerateButton->setEnabled(!locked);
    m_applyLockButton->setEnabled(!locked);
    m_applyLockButton->setText(locked ? tr("Applied and locked")
                                      : tr("Apply && Lock"));
}

bool CMinimapExportDialog::catalogRegistrationRequested() const
{ return m_exportSucceeded && m_registerCatalog->isChecked() && !m_zoneId.isEmpty(); }

QString CMinimapExportDialog::textureName() const
{
    QString name = m_textureName->text().trimmed();
    if(name.endsWith(".mmp", Qt::CaseInsensitive))
        name.chop(4);
    return name;
}

QString CMinimapExportDialog::destinationArchive() const
{ return QFileInfo(m_destination->text().trimmed()).absoluteFilePath(); }

void CMinimapExportDialog::chooseDestination()
{
    const QString initial = m_destination->text().trimmed().isEmpty()
        ? QSettings().value("minimap/lastDestinationDirectory").toString()
        : m_destination->text().trimmed();
    QFileDialog picker(this, tr("Select texture RES"), initial,
                       tr("Evil Islands resource archive (*.res)"));
    picker.setAcceptMode(QFileDialog::AcceptSave);
    picker.setFileMode(QFileDialog::AnyFile);
    picker.setDefaultSuffix("res");
    picker.setOption(QFileDialog::DontConfirmOverwrite, true);
    if(picker.exec() != QDialog::Accepted || picker.selectedFiles().isEmpty())
        return;
    const QString path = picker.selectedFiles().first();
    if(path.isEmpty()) return;
    m_destination->setText(path);
    QSettings().setValue("minimap/lastDestinationDirectory", QFileInfo(path).absolutePath());
}

void CMinimapExportDialog::saveReviewPng()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save full-resolution minimap review"),
        textureName() + "-minimap-review.png", tr("PNG image (*.png)"));
    if(path.isEmpty()) return;
    if(!m_gradedImage.save(path, "PNG"))
        QMessageBox::warning(this, tr("Save minimap review"), tr("The PNG could not be written."));
}

void CMinimapExportDialog::updateReadyState()
{
    m_exportButton->setEnabled(safeTextureName(textureName())
                               && !m_destination->text().trimmed().isEmpty()
                               && m_capture.bake.success);
}

void CMinimapExportDialog::exportTexture()
{
    if(!safeTextureName(textureName()))
    {
        QMessageBox::warning(this, tr("Export Minimap"),
            tr("Enter a unique texture name without whitespace or path characters."));
        return;
    }
    QString diagnostic;
    const QByteArray mmp = m_capture.targetDxt1
        ? ei::encodeMmpDxt1(m_gradedImage, &diagnostic)
        : ei::encodeMmp8888(m_gradedImage, &diagnostic);
    if(mmp.isEmpty())
    {
        m_status->setText(tr("MMP encoding failed: %1").arg(diagnostic));
        return;
    }
    const QString member = normalizedMemberName(textureName());
    EResTextureConflictPolicy policy = EResTextureConflictPolicy::Refuse;
    const QString path = destinationArchive();
    if(QFileInfo::exists(path))
    {
        const qint64 archiveBytes = QFileInfo(path).size();
        if(archiveBytes >= 256ll * 1024ll * 1024ll
           && QMessageBox::question(this, tr("Large RES export"), tr(
                "%1 is %2 MiB. The verified rebuild needs temporary disk space and may take time. Continue?")
                .arg(QDir::toNativeSeparators(path))
                .arg(archiveBytes / (1024 * 1024)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                != QMessageBox::Yes)
            return;
        const SResInspection inspection = runAnimatedResOperation<SResInspection>(
            this, tr("Inspecting destination RES"),
            tr("Reading and validating the selected RES..."),
            [path, member]() {
                SResInspection result;
                QFile input(path);
                if(!input.open(QIODevice::ReadOnly))
                {
                    result.error = input.errorString();
                    return result;
                }
                CResArchiveDocument archive;
                QStringList errors;
                if(!archive.loadBytes(input.readAll(), path, &errors))
                {
                    result.error = errors.join(" | ");
                    return result;
                }
                for(const SResArchiveMember& existing : archive.members())
                    result.memberExists = result.memberExists
                        || existing.name.compare(member, Qt::CaseInsensitive) == 0;
                result.success = true;
                return result;
            });
        if(!inspection.success)
        {
            m_status->setText(tr("Could not inspect destination: %1")
                              .arg(inspection.error));
            return;
        }
        if(inspection.memberExists)
        {
            const auto answer = QMessageBox::warning(this, tr("Replace minimap texture?"),
                tr("%1 already exists in %2. Replace that member after creating a verified backup?")
                    .arg(member, QDir::toNativeSeparators(path)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if(answer != QMessageBox::Yes) return;
            policy = EResTextureConflictPolicy::Replace;
        }
    }

    SResTextureExportRequest request;
    request.destination = QFileInfo(path);
    request.memberName = member;
    request.mmpPayload = mmp;
    request.conflictPolicy = policy;
    const SResTextureExportResult result =
        runAnimatedResOperation<SResTextureExportResult>(
            this, tr("Exporting minimap texture"),
            tr("Backing up, rebuilding, and validating the destination RES..."),
            [request]() { return exportMmpToRes(request); });
    if(!result.success)
    {
        m_status->setStyleSheet("color: #b00020;");
        m_status->setText(tr("%1: %2").arg(result.phase, result.error));
        return;
    }
    m_exportSucceeded = true;
    m_status->setStyleSheet("color: #2e9d44;");
    m_status->setText(tr("Verified %1 %2 in %3. SHA-256: %4%5")
        .arg(result.createdArchive ? tr("new archive containing")
                                   : result.replacedMember ? tr("replacement of")
                                                           : tr("addition of"),
             member, QDir::toNativeSeparators(path),
             QString::fromLatin1(result.newArchiveSha256),
             result.backupPath.isEmpty() ? QString()
                 : tr("\nBackup: %1").arg(QDir::toNativeSeparators(result.backupPath))));
    m_exportButton->setEnabled(false);
    m_textureName->setEnabled(false);
    m_destination->setEnabled(false);
    if(catalogRegistrationRequested())
        emit catalogRegistrationReady(textureName());
}
