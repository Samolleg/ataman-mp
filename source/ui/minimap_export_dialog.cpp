#include "minimap_export_dialog.h"

#include <QtWidgets>
#include <QCryptographicHash>

#include "mmp_codec.h"
#include "res_archive_document.h"
#include "res_texture_export.h"

namespace {
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
    const QString& suggestedTextureName, const QString& zoneId, QWidget* parent)
    : QDialog(parent), m_capture(capture), m_zoneId(zoneId)
{
    setWindowTitle(tr("Generate Minimap"));
    setModal(true);
    QVBoxLayout* root = new QVBoxLayout(this);

    QLabel* summary = new QLabel(this);
    summary->setWordWrap(true);
    summary->setText(capture.bake.incomplete
        ? tr("Incomplete MPR-only capture. Terrain and overlays are included, but static collection geometry is absent. Load a collection for a production minimap.")
        : tr("Complete collection capture. Terrain, overlays, and %1 static objects are included; %2 dynamic units were excluded.")
            .arg(capture.staticObjectCount).arg(capture.excludedDynamicUnitCount));
    summary->setStyleSheet(capture.bake.incomplete
        ? "QLabel { color: #e6a23c; font-weight: 600; }" : QString());
    root->addWidget(summary);

    QLabel* details = new QLabel(tr(
        "Review: %1 x %2 (%3 x %4 sectors)\nFraming: %5\nOrientation: %6")
        .arg(capture.bake.reviewImage.width()).arg(capture.bake.reviewImage.height())
        .arg(capture.sectorWidth).arg(capture.sectorHeight)
        .arg(capture.framingDescription, capture.orientationDescription), this);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(details);

    QLabel* preview = new QLabel(this);
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumSize(300, 300);
    preview->setPixmap(QPixmap::fromImage(capture.bake.gameImage).scaled(
        384, 384, Qt::KeepAspectRatio, Qt::FastTransformation));
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidget(preview);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(410);
    root->addWidget(scroll, 1);

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
    QPushButton* stagePreview = buttons->addButton(
        tr("Stage for Next Test"), QDialogButtonBox::ActionRole);
    stagePreview->setToolTip(tr(
        "Uses the reviewed minimap through Endurance's run-owned temporary RES and private map table. No installed archive is changed."));
    m_exportButton = buttons->addButton(tr("Export to RES..."), QDialogButtonBox::AcceptRole);
    root->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &CMinimapExportDialog::chooseDestination);
    connect(savePng, &QPushButton::clicked, this, &CMinimapExportDialog::saveReviewPng);
    connect(stagePreview, &QPushButton::clicked,
            this, &CMinimapExportDialog::stageTemporaryPreview);
    connect(m_exportButton, &QPushButton::clicked, this, &CMinimapExportDialog::exportTexture);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_textureName, &QLineEdit::textChanged, this, &CMinimapExportDialog::updateReadyState);
    connect(m_destination, &QLineEdit::textChanged, this, &CMinimapExportDialog::updateReadyState);
    resize(760, 820);
    updateReadyState();
}

void CMinimapExportDialog::stageTemporaryPreview()
{
    if(!safeTextureName(textureName()))
    {
        QMessageBox::warning(this, tr("Generate Minimap"),
            tr("Enter a texture name without whitespace or path characters before staging."));
        return;
    }
    QString diagnostic;
    m_encodedMmp = ei::encodeMmp8888(m_capture.bake.gameImage, &diagnostic);
    if(m_encodedMmp.isEmpty())
    {
        m_status->setText(tr("MMP encoding failed: %1").arg(diagnostic));
        return;
    }
    m_temporaryPreviewRequested = true;
    accept();
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
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Select texture RES"), initial, tr("Evil Islands resource archive (*.res)"));
    if(path.isEmpty()) return;
    m_destination->setText(path);
    QSettings().setValue("minimap/lastDestinationDirectory", QFileInfo(path).absolutePath());
}

void CMinimapExportDialog::saveReviewPng()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save full-resolution minimap review"),
        textureName() + "-minimap-review.png", tr("PNG image (*.png)"));
    if(path.isEmpty()) return;
    if(!m_capture.bake.reviewImage.save(path, "PNG"))
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
        QMessageBox::warning(this, tr("Generate Minimap"),
            tr("Enter a unique texture name without whitespace or path characters."));
        return;
    }
    QString diagnostic;
    const QByteArray mmp = ei::encodeMmp8888(m_capture.bake.gameImage, &diagnostic);
    if(mmp.isEmpty())
    {
        m_status->setText(tr("MMP encoding failed: %1").arg(diagnostic));
        return;
    }
    m_encodedMmp = mmp;

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
        QFile input(path);
        QByteArray bytes;
        if(!input.open(QIODevice::ReadOnly))
        {
            m_status->setText(tr("Could not inspect destination: %1").arg(input.errorString()));
            return;
        }
        bytes = input.readAll();
        CResArchiveDocument archive;
        QStringList errors;
        if(!archive.loadBytes(bytes, path, &errors))
        {
            m_status->setText(tr("Destination is not a valid RES: %1").arg(errors.join(" | ")));
            return;
        }
        bool memberExists = false;
        for(const SResArchiveMember& existing : archive.members())
            memberExists = memberExists
                || existing.name.compare(member, Qt::CaseInsensitive) == 0;
        if(memberExists)
        {
            const auto answer = QMessageBox::warning(this, tr("Replace minimap texture?"),
                tr("%1 already exists in %2. Replace that member after creating a verified backup?")
                    .arg(member, QDir::toNativeSeparators(path)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if(answer != QMessageBox::Yes) return;
            policy = EResTextureConflictPolicy::Replace;
        }
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    SResTextureExportRequest request;
    request.destination = QFileInfo(path);
    request.memberName = member;
    request.mmpPayload = mmp;
    request.conflictPolicy = policy;
    const SResTextureExportResult result = exportMmpToRes(request);
    QApplication::restoreOverrideCursor();
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
        accept();
}
