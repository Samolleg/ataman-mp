#include "resource_browser.h"

#include "mmp_inspector.h"
#include "backup_manager.h"
#include "resource_model_preview.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextCodec>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kEntryRole = Qt::UserRole + 80;
constexpr qint64 kMaximumPreviewBytes = 32ll * 1024ll * 1024ll;
constexpr qint64 kHexPrefixBytes = 4096;

QString suffixType(const QString& name)
{
    const QString suffix = QFileInfo(name).suffix().toLower();
    if(suffix == "mmp") return QStringLiteral("Image");
    if(suffix == "txt" || suffix == "lua" || suffix == "reg") return QStringLiteral("Text");
    if(suffix == "fig" || suffix == "mod" || suffix == "lnk" || suffix == "adb"
       || suffix == "anm") return QStringLiteral("3D / animation");
    if(suffix == "wav" || suffix == "mp3" || suffix == "ogg") return QStringLiteral("Audio");
    if(suffix == "res") return QStringLiteral("Archive");
    return QStringLiteral("Binary");
}

QString humanBytes(quint64 bytes)
{
    if(bytes >= 1024ull * 1024ull * 1024ull) return QString::number(bytes / 1073741824.0, 'f', 2) + " GiB";
    if(bytes >= 1024ull * 1024ull) return QString::number(bytes / 1048576.0, 'f', 2) + " MiB";
    if(bytes >= 1024ull) return QString::number(bytes / 1024.0, 'f', 1) + " KiB";
    return QString::number(bytes) + " B";
}

QString hexDump(const QByteArray& bytes)
{
    QString result;
    for(int offset = 0; offset < bytes.size(); offset += 16)
    {
        result += QString("%1  ").arg(offset, 8, 16, QLatin1Char('0'));
        const QByteArray row = bytes.mid(offset, 16);
        for(int index = 0; index < 16; ++index)
            result += index < row.size()
                ? QString("%1 ").arg(quint8(row.at(index)), 2, 16, QLatin1Char('0')) : "   ";
        result += " ";
        for(char value : row)
            result += value >= 32 && value < 127 ? QChar(value) : QChar('.');
        result += '\n';
    }
    return result;
}

SResourceBrowserPreview makePreview(CResArchiveIndex archive, int memberIndex,
                                    CResourceCatalog catalog)
{
    SResourceBrowserPreview result;
    if(memberIndex < 0 || memberIndex >= archive.memberCount())
    {
        result.error = QStringLiteral("The selected archive member no longer exists.");
        return result;
    }
    const SResIndexedMember member = archive.member(memberIndex);
    result.title = member.name;
    QString error;
    if(member.dataSize > kMaximumPreviewBytes)
    {
        const QByteArray prefix = archive.readMemberPrefix(memberIndex, kHexPrefixBytes, &error);
        if(prefix.isNull()) { result.error = error; return result; }
        result.text = hexDump(prefix);
        result.details = QStringLiteral("%1 | preview limited to first %2")
            .arg(humanBytes(member.dataSize), humanBytes(prefix.size()));
        result.valid = true;
        return result;
    }
    const QByteArray payload = archive.readMember(memberIndex, &error);
    if(payload.isNull()) { result.error = error; return result; }
    result.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    const QString type = suffixType(member.name);
    result.details = QStringLiteral("%1 | %2 | SHA-256 %3")
        .arg(type, humanBytes(member.dataSize), QString::fromLatin1(result.sha256));
    if(type == "Image")
    {
        const ei::SMmpInspection inspection = ei::inspectMmp(payload);
        if(inspection.valid)
        {
            result.image = ei::mmpEditorPreviewImage(inspection.baseImage);
            result.details += QStringLiteral(" | %1 %2 x %3 | %4 mip level(s)")
                .arg(inspection.formatName).arg(inspection.width).arg(inspection.height)
                .arg(inspection.authoredMipSizes.size());
        }
        else result.text = inspection.diagnostic;
    }
    else if(type == "3D / animation")
    {
        const QString suffix = QFileInfo(member.name).suffix().toLower();
        ei::SResourceModelPreview model;
        if(suffix == "fig") model = ei::previewFig8(payload);
        else if(suffix == "mod")
        {
            QByteArray bon;
            const QString companion = QFileInfo(member.name).completeBaseName() + ".bon";
            const QVector<SResourceCatalogEntry> providers = catalog.providers(companion);
            if(!providers.isEmpty())
            {
                const CResArchiveIndex* provider = catalog.archive(providers.first().archivePriority);
                if(provider) bon = provider->readMember(providers.first().memberIndex);
            }
            model = ei::previewModAssembly(payload, bon);
        }
        else if(suffix == "anm")
        {
            int actions = 0, channels = 0;
            const QStringList animation = ei::inspectAnimationArchive(payload, &actions, &channels);
            result.text = animation.join('\n');
            result.details += QStringLiteral(" | %1 action(s) | %2 component channel(s)")
                .arg(actions).arg(channels);
            const QString companion = QFileInfo(member.name).completeBaseName() + ".mod";
            const QVector<SResourceCatalogEntry> providers = catalog.providers(companion);
            if(!providers.isEmpty())
            {
                const CResArchiveIndex* provider = catalog.archive(providers.first().archivePriority);
                if(provider)
                {
                    const QByteArray mod = provider->readMember(providers.first().memberIndex);
                    const QString bonName = QFileInfo(companion).completeBaseName() + ".bon";
                    QByteArray bon;
                    const QVector<SResourceCatalogEntry> bonProviders = catalog.providers(bonName);
                    if(!bonProviders.isEmpty())
                    {
                        const CResArchiveIndex* bonArchive = catalog.archive(bonProviders.first().archivePriority);
                        if(bonArchive) bon = bonArchive->readMember(bonProviders.first().memberIndex);
                    }
                    model = ei::previewModAssembly(mod, bon);
                    result.details += QStringLiteral(" | paired model %1").arg(companion);
                }
            }
            result.text += QStringLiteral("\n\nPose playback requires provider ADB action timing; "
                                          "the browser does not infer it for a standalone ANM.");
        }
        if(model.valid)
        {
            result.image = model.image;
            result.details += QStringLiteral(" | ") + model.details;
        }
        if(!model.diagnostics.isEmpty())
            result.text += (result.text.isEmpty() ? QString() : QStringLiteral("\n\n"))
                + model.diagnostics.join('\n');
        if(suffix == "lnk" || suffix == "adb")
            result.text = hexDump(payload.left(kHexPrefixBytes));
    }
    else if(type == "Text")
    {
        QTextCodec* codec = nullptr;
        QString encoding;
        if(payload.startsWith("\xEF\xBB\xBF")) { codec = QTextCodec::codecForName("UTF-8"); encoding = "UTF-8 BOM"; }
        else if(payload.startsWith("\xFF\xFE")) { codec = QTextCodec::codecForName("UTF-16LE"); encoding = "UTF-16LE BOM"; }
        else
        {
            const QString utf8 = QString::fromUtf8(payload);
            if(utf8.toUtf8() == payload) { codec = QTextCodec::codecForName("UTF-8"); encoding = "UTF-8"; }
            else { codec = QTextCodec::codecForName("CP1251"); encoding = "CP1251"; }
        }
        result.text = codec ? codec->toUnicode(payload) : QString::fromLatin1(payload);
        const QString endings = payload.contains("\r\n") ? "CRLF" : payload.contains('\n') ? "LF" : "none";
        result.details += QStringLiteral(" | encoding %1 | line endings %2").arg(encoding, endings);
    }
    else result.text = hexDump(payload.left(kHexPrefixBytes));
    result.valid = true;
    return result;
}
}

CResourceBrowser::CResourceBrowser(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("resourceBrowserPanel"));
    auto* root = new QVBoxLayout(this);
    auto* controls = new QGridLayout;
    m_mode = new QComboBox(this);
    m_mode->setObjectName(QStringLiteral("resourceBrowserMode"));
    m_mode->addItems({tr("Active Profile"), tr("Open Archive")});
    m_openArchive = new QPushButton(tr("Open .res..."), this);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("resourceBrowserSearch"));
    m_search->setPlaceholderText(tr("Filter by member or archive..."));
    m_typeFilter = new QComboBox(this);
    m_typeFilter->addItems({tr("All Types"), tr("Image"), tr("Text"), tr("3D / animation"),
                            tr("Audio"), tr("Archive"), tr("Binary")});
    m_statusFilter = new QComboBox(this);
    m_statusFilter->addItems({tr("All Statuses"), tr("Winning"), tr("Overridden")});
    controls->addWidget(m_mode, 0, 0);
    controls->addWidget(m_openArchive, 0, 1);
    controls->addWidget(m_search, 1, 0, 1, 2);
    controls->addWidget(m_typeFilter, 1, 2);
    controls->addWidget(m_statusFilter, 1, 3);
    root->addLayout(controls);

    auto* horizontal = new QSplitter(Qt::Horizontal, this);
    m_archives = new QTreeWidget(horizontal);
    m_archives->setObjectName(QStringLiteral("resourceArchiveTree"));
    m_archives->setRootIsDecorated(false);
    m_archives->setHeaderLabels({tr("Archive"), tr("Members"), tr("Size")});
    m_archives->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_archives->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_archives->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_resources = new QTreeWidget(horizontal);
    m_resources->setObjectName(QStringLiteral("resourceContentView"));
    m_resources->setAlternatingRowColors(true);
    m_resources->setRootIsDecorated(false);
    m_resources->setSortingEnabled(true);
    m_resources->setHeaderLabels({tr("Name"), tr("Type"), tr("Size"), tr("Status"), tr("Provider")});
    m_resources->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_resources->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    auto* preview = new QWidget(horizontal);
    auto* previewLayout = new QVBoxLayout(preview);
    m_previewImage = new QLabel(preview);
    m_previewImage->setObjectName(QStringLiteral("resourcePreviewImage"));
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setMinimumSize(220, 160);
    m_previewDetails = new QLabel(tr("Select a resource to preview it."), preview);
    m_previewDetails->setWordWrap(true);
    m_previewText = new QPlainTextEdit(preview);
    m_previewText->setReadOnly(true);
    m_previewText->setLineWrapMode(QPlainTextEdit::NoWrap);
    previewLayout->addWidget(m_previewImage);
    previewLayout->addWidget(m_previewDetails);
    previewLayout->addWidget(m_previewText, 1);
    auto* actions = new QGridLayout;
    m_export = new QPushButton(tr("Export..."), preview);
    m_copyName = new QPushButton(tr("Copy Name"), preview);
    m_openLocation = new QPushButton(tr("Open Location"), preview);
    m_databaseEditor = new QPushButton(tr("Open in Database Editor"), preview);
    m_replace = new QPushButton(tr("Replace..."), preview);
    m_add = new QPushButton(tr("Add..."), preview);
    m_rename = new QPushButton(tr("Rename..."), preview);
    m_delete = new QPushButton(tr("Delete..."), preview);
    actions->addWidget(m_export, 0, 0);
    actions->addWidget(m_copyName, 0, 1);
    actions->addWidget(m_openLocation, 1, 0);
    actions->addWidget(m_databaseEditor, 1, 1);
    actions->addWidget(m_replace, 2, 0);
    actions->addWidget(m_add, 2, 1);
    actions->addWidget(m_rename, 3, 0);
    actions->addWidget(m_delete, 3, 1);
    previewLayout->addLayout(actions);
    horizontal->setStretchFactor(0, 1);
    horizontal->setStretchFactor(1, 3);
    horizontal->setStretchFactor(2, 2);
    root->addWidget(horizontal, 1);
    m_summary = new QLabel(tr("No resource catalog loaded."), this);
    root->addWidget(m_summary);

    m_loadWatcher = new QFutureWatcher<SResourceBrowserLoadResult>(this);
    m_previewWatcher = new QFutureWatcher<SResourceBrowserPreview>(this);
    connect(m_loadWatcher, &QFutureWatcher<SResourceBrowserLoadResult>::finished,
            this, &CResourceBrowser::finishLoad);
    connect(m_previewWatcher, &QFutureWatcher<SResourceBrowserPreview>::finished,
            this, &CResourceBrowser::finishPreview);
    connect(m_openArchive, &QPushButton::clicked, this, &CResourceBrowser::openExternalArchive);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
        if(mode == 0 && !m_activeSources.isEmpty())
            beginLoad(m_activeSources, true, m_activeProfileName);
        else if(mode == 1) openExternalArchive();
    });
    connect(m_search, &QLineEdit::textChanged, this, &CResourceBrowser::rebuildList);
    connect(m_typeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CResourceBrowser::rebuildList);
    connect(m_statusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CResourceBrowser::rebuildList);
    connect(m_resources, &QTreeWidget::itemSelectionChanged, this, [this]() {
        beginPreview(m_resources->currentItem());
    });
    connect(m_archives, &QTreeWidget::itemSelectionChanged,
            this, &CResourceBrowser::rebuildList);
    connect(m_export, &QPushButton::clicked, this, &CResourceBrowser::exportSelected);
    connect(m_copyName, &QPushButton::clicked, this, &CResourceBrowser::copySelectedName);
    connect(m_openLocation, &QPushButton::clicked, this, &CResourceBrowser::openSelectedLocation);
    connect(m_databaseEditor, &QPushButton::clicked, this, [this]() {
        if(const auto* entry = selectedEntry()) emit databaseEditorRequested(entry->archivePath);
    });
    connect(m_replace, &QPushButton::clicked, this, &CResourceBrowser::replaceSelected);
    connect(m_add, &QPushButton::clicked, this, &CResourceBrowser::addMember);
    connect(m_rename, &QPushButton::clicked, this, &CResourceBrowser::renameSelected);
    connect(m_delete, &QPushButton::clicked, this, &CResourceBrowser::deleteSelected);
}

void CResourceBrowser::setActiveProfileSources(const QVector<QString>& sources,
                                               const QString& profileName)
{
    m_activeSources = sources;
    m_activeProfileName = profileName;
    if(m_mode->currentIndex() == 0) beginLoad(sources, true, profileName);
}

void CResourceBrowser::beginLoad(const QVector<QString>& archives, bool, const QString& label)
{
    ++m_loadGeneration;
    m_summary->setText(tr("Indexing %1 archive(s) for %2...").arg(archives.size()).arg(label));
    m_resources->clear();
    m_archives->clear();
    m_loadWatcher->setFuture(QtConcurrent::run([archives]() {
        SResourceBrowserLoadResult result;
        result.catalog.build(archives, &result.errors);
        return result;
    }));
}

void CResourceBrowser::finishLoad()
{
    const SResourceBrowserLoadResult result = m_loadWatcher->result();
    m_catalog = result.catalog;
    auto* allArchives = new QTreeWidgetItem(m_archives,
        {tr("All archives"), QString::number(m_catalog.entries().size()), QString()});
    allArchives->setData(0, ArchivePathRole, QString());
    for(int priority = 0; ; ++priority)
    {
        const CResArchiveIndex* archive = m_catalog.archive(priority);
        if(!archive) break;
        auto* item = new QTreeWidgetItem(m_archives,
            {QFileInfo(archive->path()).fileName(), QString::number(archive->memberCount()),
             humanBytes(archive->sourceSize())});
        item->setToolTip(0, archive->path());
        item->setData(0, ArchivePathRole, archive->path());
    }
    m_archives->setCurrentItem(allArchives);
    rebuildList();
    m_summary->setText(tr("%1 members from %2 archive(s)%3")
        .arg(m_catalog.entries().size()).arg(qMax(0, m_archives->topLevelItemCount() - 1))
        .arg(result.errors.isEmpty() ? QString() : tr("; %1 archive(s) rejected").arg(result.errors.size())));
}

void CResourceBrowser::rebuildList()
{
    m_resources->clear();
    m_visibleEntries.clear();
    const QString search = m_search->text().trimmed();
    const QString requestedType = m_typeFilter->currentIndex() ? m_typeFilter->currentText() : QString();
    const QString archiveFilter = selectedArchivePath();
    const QVector<SResourceCatalogEntry> candidates = archiveFilter.isEmpty()
        ? m_catalog.entries() : m_catalog.entriesForArchive(archiveFilter);
    for(const SResourceCatalogEntry& entry : candidates)
    {
        const QString type = suffixType(entry.logicalName);
        if(!search.isEmpty() && !entry.logicalName.contains(search, Qt::CaseInsensitive)
           && !entry.archiveName.contains(search, Qt::CaseInsensitive)) continue;
        if(!requestedType.isEmpty() && type != requestedType) continue;
        if(m_statusFilter->currentIndex() == 1 && !entry.winning) continue;
        if(m_statusFilter->currentIndex() == 2 && entry.winning) continue;
        const int row = m_visibleEntries.size();
        m_visibleEntries.append(entry);
        auto* item = new QTreeWidgetItem(m_resources,
            {entry.logicalName, type, humanBytes(entry.size),
             entry.winning ? tr("Winning") : tr("Overridden"), entry.archiveName});
        item->setData(0, kEntryRole, row);
        item->setToolTip(4, entry.archivePath);
    }
}

QString CResourceBrowser::selectedArchivePath() const
{
    QTreeWidgetItem* item = m_archives ? m_archives->currentItem() : nullptr;
    return item ? item->data(0, ArchivePathRole).toString() : QString();
}

const SResourceCatalogEntry* CResourceBrowser::selectedEntry() const
{
    QTreeWidgetItem* item = m_resources->currentItem();
    if(!item) return nullptr;
    const int row = item->data(0, kEntryRole).toInt();
    return row >= 0 && row < m_visibleEntries.size() ? &m_visibleEntries.at(row) : nullptr;
}

void CResourceBrowser::beginPreview(QTreeWidgetItem*)
{
    const SResourceCatalogEntry* entry = selectedEntry();
    if(!entry) return;
    const CResArchiveIndex* archive = m_catalog.archive(entry->archivePriority);
    if(!archive) return;
    ++m_previewGeneration;
    m_previewImage->clear();
    m_previewText->setPlainText(tr("Loading preview..."));
    const QVector<SResourceCatalogEntry> chain = m_catalog.providers(entry->logicalName);
    QString chainText;
    for(const auto& provider : chain)
        chainText += QString("\n%1 %2").arg(provider.winning ? tr("Active:") : tr("Overridden:"), provider.archivePath);
    m_previewDetails->setText(entry->logicalName + chainText);
    m_databaseEditor->setVisible(QFileInfo(entry->archivePath).fileName().startsWith("database", Qt::CaseInsensitive));
    m_previewWatcher->setFuture(QtConcurrent::run(
        makePreview, *archive, entry->memberIndex, m_catalog));
}

void CResourceBrowser::finishPreview()
{
    const SResourceBrowserPreview result = m_previewWatcher->result();
    if(!result.valid)
    {
        m_previewDetails->setText(result.error);
        m_previewText->clear();
        return;
    }
    m_previewDetails->setText(result.title + "\n" + result.details);
    m_previewText->setPlainText(result.text);
    if(!result.image.isNull())
        m_previewImage->setPixmap(QPixmap::fromImage(result.image).scaled(
            m_previewImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CResourceBrowser::openExternalArchive()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open RES archive"), QString(),
                                                       tr("Evil Islands archives (*.res)"));
    if(path.isEmpty()) return;
    m_mode->setCurrentIndex(1);
    beginLoad({path}, false, QFileInfo(path).fileName());
}

void CResourceBrowser::exportSelected()
{
    const SResourceCatalogEntry* entry = selectedEntry();
    if(!entry) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Export resource"), entry->logicalName);
    if(path.isEmpty()) return;
    const CResArchiveIndex* archive = m_catalog.archive(entry->archivePriority);
    QString error;
    const QByteArray bytes = archive ? archive->readMember(entry->memberIndex, &error) : QByteArray();
    QFile output(path);
    if(bytes.isNull() || !output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size())
        QMessageBox::warning(this, tr("Export resource"), error.isEmpty() ? output.errorString() : error);
}

void CResourceBrowser::copySelectedName()
{
    if(const auto* entry = selectedEntry()) QApplication::clipboard()->setText(entry->logicalName);
}

void CResourceBrowser::openSelectedLocation()
{
    if(const auto* entry = selectedEntry())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(entry->archivePath).absolutePath()));
}

bool CResourceBrowser::publishTransaction(CResArchiveTransaction& transaction,
                                          const SResourceCatalogEntry& context,
                                          const QString& operationSummary)
{
    if(QFileInfo(context.archivePath).fileName().startsWith("database", Qt::CaseInsensitive))
    {
        QMessageBox::information(this, tr("Protected database archive"),
            tr("database.res and databaseJmv.res cannot be changed by the generic resource transaction. "
               "Use the dedicated Database Editor integration."));
        emit databaseEditorRequested(context.archivePath);
        return false;
    }
    const QString destination = QFileDialog::getSaveFileName(this,
        tr("Save reviewed RES transaction as"),
        QFileInfo(context.archivePath).absolutePath() + "/"
            + QFileInfo(context.archivePath).completeBaseName() + "-edited.res",
        tr("Evil Islands archives (*.res)"));
    if(destination.isEmpty()) return false;
    const QString normalizedDestination = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    const QString normalizedSource = QDir::cleanPath(QFileInfo(context.archivePath).absoluteFilePath());
    if(normalizedDestination.compare(normalizedSource, Qt::CaseInsensitive) == 0)
    {
        QMessageBox::warning(this, tr("Save As required"),
            tr("The Resource Browser never replaces its indexed source archive. "
               "Choose a copied, mod-owned, or new override archive as the destination."));
        return false;
    }
    if(QFileInfo(destination).fileName().startsWith("database", Qt::CaseInsensitive))
    {
        QMessageBox::warning(this, tr("Protected database destination"),
            tr("Generic RES transactions cannot publish to database.res or databaseJmv.res."));
        return false;
    }
    const bool replacingExisting = QFileInfo::exists(destination);
    const QString review = tr(
        "%1\n\nSource: %2\nDestination: %3\n\n"
        "The archive will be staged, freshly reopened, race-checked, and atomically published. "
        "An existing destination will be backed up first. The source archive is not modified.")
        .arg(operationSummary, context.archivePath, destination);
    if(QMessageBox::question(this, tr("Review RES transaction"), review)
       != QMessageBox::Yes) return false;
    if(replacingExisting)
    {
        const SBackupResult backup = CBackupManager().snapshotBeforeSave(QFileInfo(destination));
        if(!backup.success)
        {
            QMessageBox::warning(this, tr("RES transaction backup"), backup.error);
            return false;
        }
    }
    QString error;
    if(!transaction.writeCandidate(destination, &error))
    {
        QMessageBox::warning(this, tr("RES transaction failed"), error);
        return false;
    }
    QMessageBox::information(this, tr("RES transaction complete"),
        tr("The validated archive was written to:\n%1").arg(destination));
    return true;
}

void CResourceBrowser::replaceSelected()
{
    const auto* entry = selectedEntry();
    if(!entry) return;
    const CResArchiveIndex* archive = m_catalog.archive(entry->archivePriority);
    if(!archive) return;
    const QString replacementPath = QFileDialog::getOpenFileName(this,
        tr("Select replacement payload"), QString(), tr("All files (*)"));
    if(replacementPath.isEmpty()) return;
    QFile replacement(replacementPath);
    if(!replacement.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("Replace resource"), replacement.errorString());
        return;
    }
    QString error;
    const QByteArray expected = archive->memberSha256(entry->memberIndex, &error);
    CResArchiveTransaction transaction(*archive);
    if(expected.isEmpty() || !transaction.replace(entry->logicalName, expected,
                                                    replacement.readAll(), &error))
    {
        QMessageBox::warning(this, tr("Replace resource"), error);
        return;
    }
    publishTransaction(transaction, *entry,
        tr("Replace %1 with %2 (%3 -> %4).")
            .arg(entry->logicalName, replacementPath, humanBytes(entry->size),
                 humanBytes(QFileInfo(replacementPath).size())));
}

void CResourceBrowser::addMember()
{
    const auto* context = selectedEntry();
    if(!context) return;
    const CResArchiveIndex* archive = m_catalog.archive(context->archivePriority);
    if(!archive) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Select member payload"),
                                                       QString(), tr("All files (*)"));
    if(path.isEmpty()) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Add RES member"),
        tr("Archive member name:"), QLineEdit::Normal, QFileInfo(path).fileName(), &accepted);
    if(!accepted) return;
    QFile input(path);
    if(!input.open(QIODevice::ReadOnly)) return;
    CResArchiveTransaction transaction(*archive);
    QString error;
    if(!transaction.add(name, input.readAll(), quint32(QDateTime::currentSecsSinceEpoch()), &error))
    {
        QMessageBox::warning(this, tr("Add resource"), error);
        return;
    }
    publishTransaction(transaction, *context,
        tr("Add %1 from %2. CP1251 and case-insensitive collision checks passed.").arg(name, path));
}

void CResourceBrowser::renameSelected()
{
    const auto* entry = selectedEntry();
    if(!entry) return;
    const CResArchiveIndex* archive = m_catalog.archive(entry->archivePriority);
    if(!archive) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Rename RES member"),
        tr("New member name:"), QLineEdit::Normal, entry->logicalName, &accepted);
    if(!accepted || name == entry->logicalName) return;
    CResArchiveTransaction transaction(*archive);
    QString error;
    if(!transaction.rename(entry->logicalName, name, &error))
    {
        QMessageBox::warning(this, tr("Rename resource"), error);
        return;
    }
    publishTransaction(transaction, *entry,
        tr("Rename %1 to %2.\n\nReference coverage is incomplete: structured MOB/script and opaque binary "
           "references may still use the old name and must be reviewed manually.")
            .arg(entry->logicalName, name));
}

void CResourceBrowser::deleteSelected()
{
    const auto* entry = selectedEntry();
    if(!entry) return;
    const CResArchiveIndex* archive = m_catalog.archive(entry->archivePriority);
    if(!archive) return;
    CResArchiveTransaction transaction(*archive);
    QString error;
    if(!transaction.remove(entry->logicalName, &error))
    {
        QMessageBox::warning(this, tr("Delete resource"), error);
        return;
    }
    const QVector<SResourceCatalogEntry> providers = m_catalog.providers(entry->logicalName);
    QString consequence = tr("No lower-priority provider is currently indexed.");
    if(providers.size() > 1 && entry->winning)
        consequence = tr("Deleting the winning member exposes: %1").arg(providers.at(1).archivePath);
    publishTransaction(transaction, *entry,
        tr("Delete %1.\n\n%2\n\nReference coverage is incomplete: structured and opaque references "
           "must be reviewed manually.").arg(entry->logicalName, consequence));
}

bool CResourceBrowser::uiContractSatisfied() const
{
    return m_mode && m_search && m_typeFilter && m_statusFilter && m_archives && m_resources
        && m_previewImage && m_previewText && m_export && m_copyName && m_openLocation
        && m_replace && m_add && m_rename && m_delete;
}
