#include "window_geometry.h"
#include "resource_browser.h"
#include "startup_phases.h"

#include "mmp_inspector.h"
#include "backup_manager.h"
#include "resource_model_preview.h"
#include "profile_database.h"
#include "unit_animation.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QApplication>
#include <QAbstractItemView>
#include <QClipboard>
#include "verified_clipboard.h"
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGridLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QMediaPlayer>
#include <QMediaContent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTextCodec>
#include <QTreeWidget>
#include <QTimer>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

class CZoomableResourceImageView final : public QGraphicsView
{
public:
    explicit CZoomableResourceImageView(QWidget* parent = nullptr)
        : QGraphicsView(parent), m_scene(new QGraphicsScene(this))
    {
        setScene(m_scene);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorViewCenter);
        setBackgroundBrush(QColor(30, 33, 39));
    }
    void setImage(const QImage& image)
    {
        m_scene->clear(); m_item = nullptr;
        if(image.isNull()) return;
        m_item = m_scene->addPixmap(QPixmap::fromImage(image));
        m_scene->setSceneRect(m_item->boundingRect());
        fitImage();
    }
    void fitImage() { if(m_item) fitInView(m_item, Qt::KeepAspectRatio); }
    void actualSize() { resetTransform(); }
protected:
    void wheelEvent(QWheelEvent* event) override
    {
        const qreal factor = event->angleDelta().y() >= 0 ? 1.2 : 1.0 / 1.2;
        scale(factor, factor); event->accept();
    }
private:
    QGraphicsScene* m_scene{nullptr};
    QGraphicsPixmapItem* m_item{nullptr};
};

class CResourceModelCanvas final : public QLabel
{
public:
    explicit CResourceModelCanvas(QWidget* parent = nullptr) : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(260, 220);
        setStyleSheet(QStringLiteral("background: #1e2127;"));
    }
    void setScene(const ei::SResourceModelScene& scene, const QSet<QString>& visibleParts)
    {
        m_scene = scene; m_view = ei::SResourceModelView();
        m_view.visibleParts = visibleParts;
        m_view.filterParts = true;
        redraw();
    }
    void setVisibleParts(const QSet<QString>& parts)
    { m_view.visibleParts = parts; m_view.filterParts = true; redraw(); }
    void setPose(const QMap<QString, QMatrix4x4>& poses,
                 const QMap<QString, QVector<QVector3D>>& morphs)
    { m_view.poses = poses; m_view.morphDeltas = morphs; redraw(); }
    void resetView() { m_view.azimuthDegrees = ei::resourceModelDefaultAzimuthDegrees();
        m_view.elevationDegrees = ei::resourceModelDefaultElevationDegrees();
        m_view.modelYawDegrees = 0; m_view.modelPitchDegrees = 0;
        m_view.zoom = 1; m_view.pan = {}; redraw(); }
    bool isInteracting() const { return m_button != Qt::NoButton; }
protected:
    void resizeEvent(QResizeEvent* event) override { QLabel::resizeEvent(event); redraw(); }
    void wheelEvent(QWheelEvent* event) override
    { m_view.zoom = qBound(0.08f, m_view.zoom * (event->angleDelta().y() >= 0 ? 1.15f : 1.0f/1.15f), 20.0f); redraw(); event->accept(); }
    void mousePressEvent(QMouseEvent* event) override { m_last = event->pos(); m_button = event->button(); event->accept(); }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QPoint delta = event->pos() - m_last; m_last = event->pos();
        if(m_button == Qt::LeftButton)
            m_view.pan += QVector3D(delta.x() / float(qMax(1, width())), 0,
                                    -delta.y() / float(qMax(1, height())));
        else if(m_button == Qt::MiddleButton)
        { m_view.modelYawDegrees += delta.x() * 0.45f;
          m_view.modelPitchDegrees = qBound(-89.0f, m_view.modelPitchDegrees + delta.y() * 0.45f, 89.0f); }
        else if(m_button == Qt::RightButton)
        { m_view.azimuthDegrees += delta.x() * 0.5f; m_view.elevationDegrees = qBound(-80.0f, m_view.elevationDegrees + delta.y() * 0.4f, 80.0f); }
        redraw();
    }
    void mouseReleaseEvent(QMouseEvent*) override { m_button = Qt::NoButton; }
private:
    void redraw()
    {
        if(!m_scene.valid || width() < 2 || height() < 2) { clear(); return; }
        setPixmap(QPixmap::fromImage(ei::renderResourceModelScene(m_scene, m_view, size())));
    }
    ei::SResourceModelScene m_scene;
    ei::SResourceModelView m_view;
    QPoint m_last;
    Qt::MouseButton m_button{Qt::NoButton};
};

class CJumpSlider final : public QSlider
{
public:
    using QSlider::QSlider;
protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if(event->button() == Qt::LeftButton)
        {
            const int position = orientation() == Qt::Horizontal ? event->pos().x() : event->pos().y();
            const int span = orientation() == Qt::Horizontal ? width() : height();
            setValue(QStyle::sliderValueFromPosition(minimum(), maximum(), position, span,
                                                      invertedAppearance()));
            emit sliderMoved(value());
            event->accept();
            return;
        }
        QSlider::mousePressEvent(event);
    }
};

namespace {
constexpr int kEntryRole = Qt::UserRole + 80;
constexpr qint64 kMaximumPreviewBytes = 32ll * 1024ll * 1024ll;
constexpr qint64 kHexPrefixBytes = 4096;

bool isDedicatedDatabaseArchive(const QString& path)
{
    const QString name = QFileInfo(path).fileName();
    return name.compare(QStringLiteral("database.res"), Qt::CaseInsensitive) == 0;
}

QString suffixType(const QString& name, const QString& archiveName = QString())
{
    const QString suffix = QFileInfo(name).suffix().toLower();
    if(suffix == "mmp") return QStringLiteral("Image");
    if(suffix == "txt" || suffix == "lua" || suffix == "reg") return QStringLiteral("Text");
    if(suffix == "fig" || suffix == "mod" || suffix == "lnk" || suffix == "adb"
       || suffix == "anm" || suffix == "bon") return QStringLiteral("3D / animation");
    if(suffix == "wav" || suffix == "mp3" || suffix == "ogg") return QStringLiteral("Audio");
    if(suffix == "res") return QStringLiteral("Archive");
    if(suffix.isEmpty() && QFileInfo(archiveName).completeBaseName().startsWith(
           QStringLiteral("texts"), Qt::CaseInsensitive)) return QStringLiteral("Text");
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
                                    CResourceCatalog catalog, quint64 generation)
{
    SResourceBrowserPreview result;
    result.generation = generation;
    if(memberIndex < 0 || memberIndex >= archive.memberCount())
    {
        result.error = QStringLiteral("The selected archive member no longer exists.");
        return result;
    }
    const SResIndexedMember member = archive.member(memberIndex);
    result.title = member.name;
    result.previewType = suffixType(member.name, QFileInfo(archive.path()).fileName());
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
    result.payload = payload;
    QString type = result.previewType;
    if(type == QStringLiteral("Binary") && ei::resourcePayloadLooksText(payload))
        type = QStringLiteral("Text");
    result.previewType = type;
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
        if(suffix == "fig")
        {
            result.modelScene = ei::buildFig8Scene(payload);
            model = ei::previewFig8(payload);
        }
        else if(suffix == "mod")
        {
            result.details += QStringLiteral(
                " | MOD is assembly geometry; motion comes from the effective same-model ANM provider");
            QByteArray bon;
            const QString companion = QFileInfo(member.name).completeBaseName() + ".bon";
            const QVector<SResourceCatalogEntry> providers = catalog.providers(companion);
            if(!providers.isEmpty())
            {
                const CResArchiveIndex* provider = catalog.archive(providers.first().archivePriority);
                if(provider) bon = provider->readMember(providers.first().memberIndex);
            }
            result.modelScene = ei::buildModScene(payload, bon);
            model = ei::previewModAssembly(payload, bon);
            result.animationModel = QFileInfo(member.name).completeBaseName();
            const QVector<SResourceCatalogEntry> animationProviders = catalog.providers(
                result.animationModel + ".anm");
            if(!animationProviders.isEmpty())
            {
                const CResArchiveIndex* animationArchive = catalog.archive(
                    animationProviders.first().archivePriority);
                if(animationArchive)
                {
                    result.animationPayload = animationArchive->readMember(
                        animationProviders.first().memberIndex);
                    result.details += QStringLiteral(" | ANM provider %1")
                        .arg(animationProviders.first().archiveName);
                }
            }
            else result.details += QStringLiteral(" | no effective same-model ANM provider found");
            const auto database = ei::ProfileDatabaseContext::instance().current();
            if(database)
                result.actions = parseAdbActions(database->actionTables.value(
                    result.animationModel.toLower() + ".adb"));
        }
        else if(suffix == "anm")
        {
            result.animationPayload = payload;
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
                    result.modelScene = ei::buildModScene(mod, bon);
                    model = ei::previewModAssembly(mod, bon);
                    result.animationModel = QFileInfo(companion).completeBaseName();
                    result.details += QStringLiteral(" | paired model %1").arg(companion);
                }
            }
            const auto database = ei::ProfileDatabaseContext::instance().current();
            if(database && !result.animationModel.isEmpty())
                result.actions = parseAdbActions(database->actionTables.value(
                    result.animationModel.toLower() + ".adb"));
            if(result.actions.isEmpty())
            {
                const QStringList names = ei::inspectAnimationArchive(payload);
                for(const QString& line : names)
                {
                    SAdbAction action;
                    action.name = line.section(QStringLiteral(" — "), 0, 0).trimmed();
                    const SAnimationClip probe = parseNestedAnimationClip(payload, action.name, 1);
                    action.length = quint32(ei::inferredAnimationLogicalLength(probe));
                    if(!action.name.isEmpty()) result.actions.append(action);
                }
                result.text += QStringLiteral("\n\nAction lengths inferred from ANM closing-loop samples; no matching provider ADB was available.");
            }
        }
        else if(suffix == "bon")
        {
            int components = 0;
            result.text = ei::inspectBoneArchive(payload, &components).join('\n');
            result.details += QStringLiteral(" | %1 bind/rest component record(s)").arg(components);
            const QString stem = QFileInfo(member.name).completeBaseName();
            const QVector<SResourceCatalogEntry> modelProviders = catalog.providers(stem + ".mod");
            if(!modelProviders.isEmpty())
            {
                const CResArchiveIndex* provider = catalog.archive(modelProviders.first().archivePriority);
                if(provider)
                {
                    const QByteArray mod = provider->readMember(modelProviders.first().memberIndex);
                    result.modelScene = ei::buildModScene(mod, payload);
                    model = ei::previewModAssembly(mod, payload);
                    result.animationModel = stem;
                    result.details += QStringLiteral(" | applied to %1 from %2")
                        .arg(stem + ".mod", modelProviders.first().archiveName);
                }
            }
        }
        else if(suffix == "adb")
        {
            QStringList diagnostics;
            result.actions = parseAdbActions(payload, &diagnostics);
            QStringList rows = {QStringLiteral("Action\tFrames\tNumber\tWeapon mask\tState\tType\tModifier\tStage\tProbability")};
            for(const SAdbAction& action : result.actions)
                rows.append(QStringLiteral("%1\t%2\t%3\t0x%4\t%5\t%6\t%7\t%8\t%9")
                    .arg(action.name).arg(action.length).arg(action.number)
                    .arg(action.weaponMask, 0, 16).arg(action.allowedState)
                    .arg(action.actionType).arg(action.modifier).arg(action.stage)
                    .arg(action.probability));
            result.text = rows.join('\n');
            if(!diagnostics.isEmpty()) result.text += QStringLiteral("\n\n") + diagnostics.join('\n');
            result.details += QStringLiteral(" | %1 animation action record(s); metadata only, motion samples live in ANM")
                .arg(result.actions.size());
        }
        if(model.valid)
        {
            result.image = model.image;
            result.details += QStringLiteral(" | ") + model.details;
        }
        if(!model.diagnostics.isEmpty())
            result.text += (result.text.isEmpty() ? QString() : QStringLiteral("\n\n"))
                + model.diagnostics.join('\n');
        if(suffix == "lnk")
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
    m_previewStack = new QStackedWidget(preview);
    m_previewStack->setObjectName(QStringLiteral("resourcePreviewStack"));
    m_previewImage = new QLabel(m_previewStack);
    m_previewImage->setVisible(false); // compatibility child for the existing UI contract
    m_previewImage->setObjectName(QStringLiteral("resourcePreviewImage"));
    QWidget* imagePane = new QWidget(m_previewStack);
    QVBoxLayout* imageLayout = new QVBoxLayout(imagePane);
    m_imageView = new CZoomableResourceImageView(imagePane);
    m_imageView->setObjectName(QStringLiteral("resourceImagePanZoom"));
    QHBoxLayout* imageActions = new QHBoxLayout;
    QPushButton* imageFit = new QPushButton(tr("Fit"), imagePane);
    QPushButton* imageActual = new QPushButton(tr("100%"), imagePane);
    imageActions->addStretch(); imageActions->addWidget(imageFit); imageActions->addWidget(imageActual);
    imageLayout->addWidget(m_imageView, 1); imageLayout->addLayout(imageActions);
    connect(imageFit, &QPushButton::clicked, m_imageView, &CZoomableResourceImageView::fitImage);
    connect(imageActual, &QPushButton::clicked, m_imageView, &CZoomableResourceImageView::actualSize);

    QWidget* modelPane = new QWidget(m_previewStack);
    QVBoxLayout* modelLayout = new QVBoxLayout(modelPane);
    m_modelView = new CResourceModelCanvas(modelPane);
    m_modelView->setObjectName(QStringLiteral("resourceModelCanvas"));
    QLabel* modelHint = new QLabel(tr("Left drag pans; middle drag rotates; right drag orbits; wheel zooms"), modelPane);
    m_animationList = new QComboBox(modelPane);
    m_animationList->setObjectName(QStringLiteral("resourceAnimationList"));
    m_animationPlay = new QPushButton(tr("Pause"), modelPane);
    m_animationPlay->setCheckable(true);
    m_animationSeek = new CJumpSlider(Qt::Horizontal, modelPane);
    m_animationSeek->setObjectName(QStringLiteral("resourceAnimationSeek"));
    QHBoxLayout* animationRow = new QHBoxLayout;
    animationRow->addWidget(new QLabel(tr("Animation"), modelPane));
    animationRow->addWidget(m_animationList, 1); animationRow->addWidget(m_animationPlay);
    animationRow->addWidget(m_animationSeek, 1);
    m_partFilter = new QLineEdit(modelPane);
    m_partFilter->setPlaceholderText(tr("Filter model parts..."));
    m_partList = new QTreeWidget(modelPane);
    m_partList->setObjectName(QStringLiteral("resourceModelParts"));
    m_partList->setHeaderLabels({tr("Part"), tr("Visible")});
    m_partList->setRootIsDecorated(false);
    m_partList->setMaximumHeight(170);
    QHBoxLayout* partPresets = new QHBoxLayout;
    QPushButton* normalParts = new QPushButton(tr("Normal"), modelPane);
    QPushButton* allParts = new QPushButton(tr("All"), modelPane);
    QPushButton* noParts = new QPushButton(tr("None"), modelPane);
    QPushButton* resetModel = new QPushButton(tr("Reset view"), modelPane);
    partPresets->addWidget(normalParts); partPresets->addWidget(allParts);
    partPresets->addWidget(noParts); partPresets->addStretch(); partPresets->addWidget(resetModel);
    modelLayout->addWidget(m_modelView, 1); modelLayout->addWidget(modelHint);
    modelLayout->addLayout(animationRow); modelLayout->addWidget(m_partFilter);
    modelLayout->addWidget(m_partList); modelLayout->addLayout(partPresets);

    QWidget* textPane = new QWidget(m_previewStack);
    QVBoxLayout* textLayout = new QVBoxLayout(textPane);
    m_previewText = new QPlainTextEdit(textPane);
    m_previewText->setReadOnly(true);
    m_previewText->setLineWrapMode(QPlainTextEdit::NoWrap);
    textLayout->addWidget(m_previewText);
    m_audioControls = new QWidget(textPane);
    QHBoxLayout* audioLayout = new QHBoxLayout(m_audioControls);
    audioLayout->setContentsMargins(0, 0, 0, 0);
    m_audioPlay = new QPushButton(tr("Play"), m_audioControls);
    m_audioSeek = new CJumpSlider(Qt::Horizontal, m_audioControls);
    m_audioVolume = new QSlider(Qt::Horizontal, m_audioControls);
    m_audioVolume->setRange(0, 100); m_audioVolume->setValue(80);
    m_audioVolume->setMaximumWidth(90);
    audioLayout->addWidget(m_audioPlay); audioLayout->addWidget(m_audioSeek, 1);
    audioLayout->addWidget(new QLabel(tr("Volume"), m_audioControls));
    audioLayout->addWidget(m_audioVolume);
    textLayout->addWidget(m_audioControls);
    m_audioControls->hide();
    m_previewStack->addWidget(textPane);
    m_previewStack->addWidget(imagePane);
    m_previewStack->addWidget(modelPane);
    m_previewDetails = new QLabel(tr("Select a resource to preview it."), preview);
    m_previewDetails->setWordWrap(true);
    previewLayout->addWidget(m_previewStack, 1);
    previewLayout->addWidget(m_previewDetails);
    auto* actions = new QGridLayout;
    m_export = new QPushButton(tr("Export..."), preview);
    m_copyName = new QPushButton(tr("Copy Name"), preview);
    m_openLocation = new QPushButton(tr("Open Location"), preview);
    m_databaseEditor = new QPushButton(tr("Open in Database Editor"), preview);
    m_databaseEditor->hide();
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
    m_archiveWatcher = new QFileSystemWatcher(this);
    connect(m_archiveWatcher, &QFileSystemWatcher::fileChanged,
            this, &CResourceBrowser::indexedArchiveChanged);
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
            this, [this]() {
        m_resources->clearSelection();
        ++m_previewGeneration;
        m_audioPlayer->stop();
        m_audioPlayer->setMedia(QMediaContent());
        m_audioSeek->setRange(0, 0);
        m_audioSeek->setValue(0);
        m_audioControls->hide();
        m_previewText->clear();
        m_imageView->setImage(QImage());
        m_previewStack->setCurrentIndex(0);
        m_previewDetails->setText(tr("Select a resource to preview it."));
        m_databaseEditor->setVisible(isDedicatedDatabaseArchive(selectedArchivePath()));
        rebuildList();
    });
    connect(m_export, &QPushButton::clicked, this, &CResourceBrowser::exportSelected);
    connect(m_copyName, &QPushButton::clicked, this, &CResourceBrowser::copySelectedName);
    connect(m_openLocation, &QPushButton::clicked, this, &CResourceBrowser::openSelectedLocation);
    connect(m_databaseEditor, &QPushButton::clicked, this, [this]() {
        if(const auto* entry = selectedEntry()) emit databaseEditorRequested(entry->archivePath);
        else
        {
            const QString archive = selectedArchivePath();
            if(isDedicatedDatabaseArchive(archive)) emit databaseEditorRequested(archive);
        }
    });
    connect(m_replace, &QPushButton::clicked, this, &CResourceBrowser::replaceSelected);
    connect(m_add, &QPushButton::clicked, this, &CResourceBrowser::addMember);
    connect(m_rename, &QPushButton::clicked, this, &CResourceBrowser::renameSelected);
    connect(m_delete, &QPushButton::clicked, this, &CResourceBrowser::deleteSelected);
    connect(m_animationList, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CResourceBrowser::selectAnimation);
    connect(m_animationPlay, &QPushButton::toggled, this, [this](bool paused) {
        m_animationPlay->setText(paused ? tr("Play") : tr("Pause"));
    });
    connect(m_animationSeek, &QSlider::sliderMoved, this, [this](int frame) {
        if(!m_currentClip.valid()) return;
        m_animationPlay->setChecked(true);
        const SAnimationLocalPose local = interpolateAnimationPose(
            m_currentClip, frame, &m_currentPreview.modelScene.skeleton);
        m_modelView->setPose(propagateAnimationHierarchy(
            local, m_currentPreview.modelScene.skeleton), local.morphDeltas);
    });
    connect(m_partFilter, &QLineEdit::textChanged, this, [this](const QString& filter) {
        for(int row = 0; row < m_partList->topLevelItemCount(); ++row)
            m_partList->topLevelItem(row)->setHidden(
                !m_partList->topLevelItem(row)->text(0).contains(filter, Qt::CaseInsensitive));
    });
    connect(m_partList, &QTreeWidget::itemChanged, this, &CResourceBrowser::updateModelParts);
    const auto setParts = [this](int preset) {
        const QSet<QString> defaults = ei::defaultVisibleModelParts(
            m_currentPreview.modelScene, m_currentPreview.animationModel);
        QSignalBlocker guard(m_partList);
        for(int row = 0; row < m_partList->topLevelItemCount(); ++row)
        {
            QTreeWidgetItem* item = m_partList->topLevelItem(row);
            const bool visible = preset == 1 || (preset == 0
                && defaults.contains(item->text(0).toLower()));
            item->setCheckState(1, visible ? Qt::Checked : Qt::Unchecked);
        }
        updateModelParts();
    };
    connect(normalParts, &QPushButton::clicked, this, [setParts]() { setParts(0); });
    connect(allParts, &QPushButton::clicked, this, [setParts]() { setParts(1); });
    connect(noParts, &QPushButton::clicked, this, [setParts]() { setParts(2); });
    connect(resetModel, &QPushButton::clicked, m_modelView, &CResourceModelCanvas::resetView);
    m_animationTimer = new QTimer(this);
    m_animationTimer->setInterval(33);
    m_animationClock = new QElapsedTimer;
    connect(m_animationTimer, &QTimer::timeout, this, &CResourceBrowser::advanceAnimation);
    m_animationTimer->start(); m_animationClock->start();
    m_audioPlayer = new QMediaPlayer(this);
    connect(m_audioPlay, &QPushButton::clicked, this, [this]() {
        if(m_audioPlayer->state() == QMediaPlayer::PlayingState) m_audioPlayer->pause();
        else m_audioPlayer->play();
    });
    connect(m_audioPlayer, &QMediaPlayer::stateChanged, this, [this](QMediaPlayer::State state) {
        m_audioPlay->setText(state == QMediaPlayer::PlayingState ? tr("Pause") : tr("Play"));
    });
    connect(m_audioPlayer, &QMediaPlayer::durationChanged, m_audioSeek, &QSlider::setMaximum);
    connect(m_audioPlayer, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if(!m_audioSeek->isSliderDown()) m_audioSeek->setValue(int(position));
    });
    connect(m_audioSeek, &QSlider::sliderMoved, m_audioPlayer, &QMediaPlayer::setPosition);
    connect(m_audioVolume, &QSlider::valueChanged, m_audioPlayer, &QMediaPlayer::setVolume);
    connect(m_audioPlayer, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error),
            this, [this](QMediaPlayer::Error error) {
        if(error != QMediaPlayer::NoError)
            m_previewText->setPlainText(tr("The deployed multimedia backend cannot decode this audio stream. Export remains available.\n\n%1")
                .arg(m_audioPlayer->errorString()));
    });
}

CResourceBrowser::~CResourceBrowser()
{
    if(m_audioPlayer)
    {
        m_audioPlayer->stop();
        m_audioPlayer->setMedia(QMediaContent());
    }
    delete m_animationClock;
    delete m_audioWorkspace;
}

void CResourceBrowser::setActiveProfileSources(const QVector<QString>& sources,
                                               const QString& profileName)
{
    m_activeSources = sources;
    m_activeProfileName = profileName;
    if(m_mode->currentIndex() == 0) beginLoad(sources, true, profileName);
}

bool CResourceBrowser::selectResourceReference(const QString& reference)
{
    const QString requested = reference.trimmed();
    if(requested.isEmpty() || m_activeSources.isEmpty()) return false;
    if(!m_activeProfileMode || m_mode->currentIndex() != 0)
    {
        m_pendingReference = requested;
        m_mode->setCurrentIndex(0);
        return true;
    }
    if(m_loadWatcher->isRunning())
    {
        m_pendingReference = requested;
        return true;
    }
    return applyResourceReferenceSelection(requested);
}

bool CResourceBrowser::applyResourceReferenceSelection(const QString& reference)
{
    const QString requestedName = QFileInfo(reference).fileName();
    const QString requestedStem = QFileInfo(requestedName).completeBaseName();
    int bestIndex = -1;
    int bestScore = -1;
    const QVector<SResourceCatalogEntry> entries = m_catalog.entries();
    for(int index = 0; index < entries.size(); ++index)
    {
        const SResourceCatalogEntry& entry = entries.at(index);
        const QString logicalName = QFileInfo(entry.logicalName).fileName();
        const QString logicalStem = QFileInfo(logicalName).completeBaseName();
        int score = -1;
        if(entry.logicalName.compare(reference, Qt::CaseInsensitive) == 0) score = 8;
        else if(logicalName.compare(requestedName, Qt::CaseInsensitive) == 0) score = 7;
        else if(logicalStem.compare(requestedStem, Qt::CaseInsensitive) == 0) score = 5;
        if(score >= 0 && entry.winning) ++score;
        if(score > bestScore) { bestScore = score; bestIndex = index; }
    }
    if(bestIndex < 0) return false;

    const SResourceCatalogEntry target = entries.at(bestIndex);
    // Do not rebuild every member in the active profile just to reveal one
    // reference. Show the exact logical-name filter immediately; preview
    // decoding remains on the existing background watcher.
    {
        const QSignalBlocker searchBlocker(m_search);
        const QSignalBlocker typeBlocker(m_typeFilter);
        const QSignalBlocker statusBlocker(m_statusFilter);
        const QSignalBlocker archiveBlocker(m_archives);
        const QSignalBlocker resourceBlocker(m_resources);
        m_search->setText(target.logicalName);
        m_typeFilter->setCurrentIndex(0);
        m_statusFilter->setCurrentIndex(0);
        if(m_archives->topLevelItemCount())m_archives->setCurrentItem(m_archives->topLevelItem(0));
        rebuildList();
    }
    int visibleEntryIndex = -1;
    for(int row = 0; row < m_visibleEntries.size(); ++row)
        if(m_visibleEntries.at(row).archivePriority == target.archivePriority
           && m_visibleEntries.at(row).memberIndex == target.memberIndex)
        {visibleEntryIndex = row;break;}
    for(int visualRow = 0; visibleEntryIndex >= 0
            && visualRow < m_resources->topLevelItemCount(); ++visualRow)
    {
        QTreeWidgetItem* item = m_resources->topLevelItem(visualRow);
        if(!item || item->data(0, kEntryRole).toInt() != visibleEntryIndex)continue;
        m_resources->setCurrentItem(item);
        m_resources->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        return true;
    }
    return false;
}

void CResourceBrowser::beginLoad(const QVector<QString>& archives, bool activeProfile, const QString& label)
{
    ++m_loadGeneration;
    m_activeProfileMode = activeProfile;
    m_loadedSources = archives;
    if(m_archiveWatcher)
    {
        const QStringList watched = m_archiveWatcher->files();
        if(!watched.isEmpty()) m_archiveWatcher->removePaths(watched);
        QStringList existing;
        for(const QString& path : archives)
            if(QFileInfo::exists(path)) existing.append(QFileInfo(path).absoluteFilePath());
        if(!existing.isEmpty()) m_archiveWatcher->addPaths(existing);
    }
    m_summary->setText(tr("Indexing %1 archive(s) for %2...").arg(archives.size()).arg(label));
    m_resources->clear();
    m_archives->clear();
    m_loadWatcher->setFuture(QtConcurrent::run([archives]() {
        SResourceBrowserLoadResult result;
        result.catalog.build(archives, &result.errors);
        return result;
    }));
}

void CResourceBrowser::indexedArchiveChanged(const QString& archivePath)
{
    const QString normalized = QDir::cleanPath(QFileInfo(archivePath).absoluteFilePath()).toLower();
    if(m_internalPublications.remove(normalized)) return;
    const QString label = m_activeProfileMode ? m_activeProfileName
        : QFileInfo(m_loadedSources.value(0)).fileName();
    const auto name=QFileInfo(archivePath).fileName().toLower();
    if(qApp->property("agentHeadless").toBool()||name=="hdpack_terrain.res"||name.endsWith("-atlas.res")) {
        // Independent map publication must neither prompt in the owner's
        // window nor reset that window's live atlas/dirty scene.214 refreshes
        // neighbor snapshots separately; only the browser index is rebuilt.
        beginLoad(m_loadedSources,m_activeProfileMode,label);
        ei::log(eLogInfo,"Shared terrain archive changed; asynchronous browser index refreshed without changing the active map: "+archivePath);
        return;
    }
    const auto answer = QMessageBox::question(this, tr("RES archive changed"),
        tr("A loaded archive changed on disk:\n%1\n\nRefresh the Resource Browser and live resource lists now?")
            .arg(archivePath), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if(answer == QMessageBox::Yes)
    {
        beginLoad(m_loadedSources, m_activeProfileMode, label);
        if(m_activeProfileMode) emit activeResourcesPublished(archivePath);
    }
    else if(QFileInfo::exists(archivePath))
        m_archiveWatcher->addPath(archivePath);
}

void CResourceBrowser::beginInternalPublication(const QString& archivePath)
{
    const QString absolute = QFileInfo(archivePath).absoluteFilePath();
    const QString normalized = QDir::cleanPath(absolute).toLower();
    if(normalized.isEmpty())
        return;
    m_internalPublications.insert(normalized);
    if(m_archiveWatcher && m_archiveWatcher->files().contains(
           absolute, Qt::CaseInsensitive))
        m_archiveWatcher->removePath(absolute);
}

void CResourceBrowser::finishInternalPublication(
    const QString& archivePath, bool committed, bool refreshVisualResources)
{
    const QString absolute = QFileInfo(archivePath).absoluteFilePath();
    const QString normalized = QDir::cleanPath(absolute).toLower();
    m_internalPublications.remove(normalized);

    bool loaded = false;
    for(const QString& source : m_loadedSources)
        if(QDir::cleanPath(QFileInfo(source).absoluteFilePath()).compare(
               QDir::cleanPath(absolute), Qt::CaseInsensitive) == 0)
        {
            loaded = true;
            break;
        }
    if(!loaded)
        return;

    if(committed)
    {
        const QString label = m_activeProfileMode ? m_activeProfileName
            : QFileInfo(m_loadedSources.value(0)).fileName();
        beginLoad(m_loadedSources, m_activeProfileMode, label);
        if(m_activeProfileMode && refreshVisualResources)
            emit activeResourcesPublished(absolute);
        return;
    }
    if(m_archiveWatcher && QFileInfo::exists(absolute)
       && !m_archiveWatcher->files().contains(
              absolute, Qt::CaseInsensitive))
        m_archiveWatcher->addPath(absolute);
}

void CResourceBrowser::finishLoad()
{
    const SResourceBrowserLoadResult result = m_loadWatcher->result();
    CStartupPhases phases(QString("resourceBrowserCatalog entries:%1").arg(result.catalog.entries().size()));
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
    // Selecting the new root emits itemSelectionChanged, whose handler resets
    // the old preview and builds the list. Rebuilding here again used to
    // destroy and recreate every row (tens of thousands) during first show.
    if(m_archives->signalsBlocked())rebuildList();
    m_summary->setText(tr("%1 members from %2 archive(s)%3")
        .arg(m_catalog.entries().size()).arg(qMax(0, m_archives->topLevelItemCount() - 1))
        .arg(result.errors.isEmpty() ? QString() : tr("; %1 archive(s) rejected").arg(result.errors.size())));
    if(!m_pendingReference.isEmpty())
    {
        const QString pending = m_pendingReference;
        m_pendingReference.clear();
        applyResourceReferenceSelection(pending);
    }
    phases.mark("publish");
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
        const QString type = suffixType(entry.logicalName, entry.archiveName);
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
    const quint64 generation = ++m_previewGeneration;
    m_audioPlayer->stop();
    m_audioPlayer->setMedia(QMediaContent());
    m_audioSeek->setRange(0, 0);
    m_audioSeek->setValue(0);
    m_audioPlay->setText(tr("Play"));
    m_audioControls->hide();
    m_imageView->setImage(QImage());
    m_previewStack->setCurrentIndex(0);
    m_previewText->setPlainText(tr("Loading preview..."));
    const QVector<SResourceCatalogEntry> chain = m_catalog.providers(entry->logicalName);
    QString chainText;
    for(const auto& provider : chain)
        chainText += QString("\n%1 %2").arg(provider.winning ? tr("Active:") : tr("Overridden:"), provider.archivePath);
    m_previewDetails->setText(entry->logicalName + chainText);
    m_databaseEditor->setVisible(isDedicatedDatabaseArchive(entry->archivePath));
    m_previewWatcher->setFuture(QtConcurrent::run(
        makePreview, *archive, entry->memberIndex, m_catalog, generation));
}

void CResourceBrowser::finishPreview()
{
    const SResourceBrowserPreview result = m_previewWatcher->result();
    if(result.generation != m_previewGeneration) return;
    if(!result.valid)
    {
        m_previewDetails->setText(result.error);
        m_previewText->clear();
        return;
    }
    m_currentPreview = result;
    m_previewDetails->setText(result.title + "\n" + result.details);
    m_previewText->setPlainText(result.text);
    const QString type = result.previewType;
    if(result.modelScene.valid)
    {
        m_previewStack->setCurrentIndex(2);
        const QString stem = result.animationModel.isEmpty()
            ? QFileInfo(result.title).completeBaseName() : result.animationModel;
        const QSet<QString> defaults = ei::defaultVisibleModelParts(result.modelScene, stem);
        m_modelView->setScene(result.modelScene, defaults);
        m_partList->clear();
        QSignalBlocker guard(m_partList);
        for(const ei::SResourceModelPart& part : result.modelScene.parts)
        {
            QTreeWidgetItem* item = new QTreeWidgetItem(m_partList,
                {part.name, QString()});
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(1, defaults.contains(part.name.toLower())
                ? Qt::Checked : Qt::Unchecked);
        }
        updateModelParts();
        m_animationList->clear();
        for(const SAdbAction& action : result.actions)
            if(action.length > 0)
                m_animationList->addItem(QStringLiteral("%1 (%2 frames)")
                    .arg(action.name).arg(action.length), action.name);
        m_animationList->setVisible(m_animationList->count() > 0);
        m_animationPlay->setVisible(m_animationList->count() > 0);
        m_animationSeek->setVisible(m_animationList->count() > 0);
        if(m_animationList->count())
        {
            m_animationList->insertItem(0, tr("Neutral assembly pose"), QString());
            m_animationList->setCurrentIndex(1);
            selectAnimation(1);
        }
        else m_animationSeek->setRange(0, 0);
    }
    else if(type == QStringLiteral("Image"))
    {
        m_previewStack->setCurrentIndex(1);
        m_imageView->setImage(result.image);
    }
    else
    {
        m_previewStack->setCurrentIndex(0);
        if(type == QStringLiteral("Audio"))
        {
            m_audioControls->show();
            if(!m_audioWorkspace) m_audioWorkspace = new QTemporaryDir();
            const QString suffix = QFileInfo(result.title).suffix().toLower();
            m_audioPath = m_audioWorkspace && m_audioWorkspace->isValid()
                ? m_audioWorkspace->filePath(QStringLiteral("preview.%1").arg(suffix)) : QString();
            QFile audio(m_audioPath);
            if(m_audioPath.isEmpty() || !audio.open(QIODevice::WriteOnly)
               || audio.write(result.payload) != result.payload.size())
            {
                m_previewText->setPlainText(tr("Could not stage this audio preview as a local file. Export remains available."));
                return;
            }
            audio.close();
            const QString mime = suffix == "wav"
                ? QStringLiteral("audio/wav") : QStringLiteral("audio/mpeg");
            m_audioPlayer->setMedia(QUrl::fromLocalFile(m_audioPath));
            m_previewText->setPlainText(tr("Audio preview ready (%1). WAV uses the deployed Windows audio backend; MP3 also requires an installed system decoder.")
                .arg(mime));
        }
    }
}

void CResourceBrowser::selectAnimation(int index)
{
    m_currentClip = SAnimationClip();
    if(index < 0 || index >= m_animationList->count()) return;
    const QString action = m_animationList->itemData(index).toString();
    if(action.isEmpty())
    {
        m_modelView->setPose({}, {});
        m_animationPlay->setChecked(true);
        m_animationSeek->setRange(0, 0);
        return;
    }
    int length = 0;
    for(const SAdbAction& candidate : m_currentPreview.actions)
        if(candidate.name.compare(action, Qt::CaseInsensitive) == 0)
        { length = int(candidate.length); break; }
    m_currentClip = parseNestedAnimationClip(m_currentPreview.animationPayload,
                                             action, qMax(1, length));
    if(length <= 0) m_currentClip.logicalLength = ei::inferredAnimationLogicalLength(m_currentClip);
    m_animationSeek->setRange(0, qMax(0, m_currentClip.logicalLength - 1));
    m_animationSeek->setValue(0);
    m_animationPlay->setChecked(false);
    m_animationClock->restart();
}

void CResourceBrowser::advanceAnimation()
{
    if(m_audioPlayer && m_audioPlayer->state() == QMediaPlayer::PlayingState
       && !m_audioSeek->isSliderDown())
        m_audioSeek->setValue(int(m_audioPlayer->position()));
    if(!m_currentClip.valid() || m_animationPlay->isChecked()
       || m_previewStack->currentIndex() != 2 || m_modelView->isInteracting()) return;
    const double frame = std::fmod(m_animationClock->elapsed() * 15.0 / 1000.0,
                                   double(m_currentClip.logicalLength));
    const SAnimationLocalPose local = interpolateAnimationPose(
        m_currentClip, frame, &m_currentPreview.modelScene.skeleton);
    m_modelView->setPose(propagateAnimationHierarchy(
        local, m_currentPreview.modelScene.skeleton), local.morphDeltas);
    QSignalBlocker guard(m_animationSeek);
    m_animationSeek->setValue(qBound(0, int(frame), m_animationSeek->maximum()));
}

void CResourceBrowser::updateModelParts()
{
    QSet<QString> visible;
    for(int row = 0; row < m_partList->topLevelItemCount(); ++row)
        if(m_partList->topLevelItem(row)->checkState(1) == Qt::Checked)
            visible.insert(m_partList->topLevelItem(row)->text(0).toLower());
    m_modelView->setVisibleParts(visible);
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
    if(const auto* entry = selectedEntry()) ei::copyTextToClipboard(entry->logicalName, this);
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
    if(isDedicatedDatabaseArchive(context.archivePath))
    {
        QMessageBox::information(this, tr("Protected database archive"),
            tr("database.res cannot be changed by the generic resource transaction. "
               "Use the dedicated Database Editor integration."));
        emit databaseEditorRequested(context.archivePath);
        return false;
    }
    auto samePath = [](const QString& left, const QString& right) {
        return QDir::cleanPath(QFileInfo(left).absoluteFilePath()).compare(
            QDir::cleanPath(QFileInfo(right).absoluteFilePath()), Qt::CaseInsensitive) == 0;
    };
    bool sourceIsActive = false;
    for(const QString& source : m_activeSources)
        if(samePath(source, context.archivePath)) { sourceIsActive = true; break; }
    const QString portableSource = QDir::fromNativeSeparators(context.archivePath).toLower();
    const bool modOwnedSource = portableSource.contains(QStringLiteral("/mods/"));
    const bool sourceCanBeUpdated = !sourceIsActive || modOwnedSource;

    QDialog reviewDialog(this);ei::rememberWindowGeometry(&reviewDialog, QStringLiteral("resourceChangeReview"));
    reviewDialog.setWindowTitle(tr("Apply RES changes"));
    auto* layout = new QVBoxLayout(&reviewDialog);
    auto* summary = new QLabel(operationSummary, &reviewDialog);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto* safety = new QLabel(&reviewDialog);
    safety->setWordWrap(true);
    safety->setText(sourceCanBeUpdated
        ? tr("The target is staged, semantically reopened, race-checked, and atomically replaced. "
             "The live resource index refreshes only after validation succeeds.")
        : tr("This archive belongs to the active base installation and is read-only here. "
             "Choose a mod-owned or copied archive as the target."));
    layout->addWidget(safety);
    auto* targetLayout = new QHBoxLayout();
    targetLayout->addWidget(new QLabel(tr("Target archive:"), &reviewDialog));
    auto* target = new QLineEdit(&reviewDialog);
    const QString editedDefault = QFileInfo(context.archivePath).absolutePath() + "/"
        + QFileInfo(context.archivePath).completeBaseName() + "-edited.res";
    target->setText(sourceCanBeUpdated ? context.archivePath : editedDefault);
    targetLayout->addWidget(target, 1);
    auto* browse = new QPushButton(tr("Browse..."), &reviewDialog);
    targetLayout->addWidget(browse);
    layout->addLayout(targetLayout);
    auto* backup = new QCheckBox(&reviewDialog);
    auto updateBackupText = [backup, target]() {
        const QFileInfo file(target->text());
        backup->setText(QObject::tr("Create a visible backup before replacement (%1 additional space)\n%2")
            .arg(file.exists() ? humanBytes(file.size()) : QObject::tr("target does not exist"),
                 CBackupManager::defaultRootPath()));
        backup->setEnabled(file.exists());
        if(!file.exists()) backup->setChecked(false);
    };
    backup->setChecked(QFileInfo::exists(target->text()));
    updateBackupText();
    layout->addWidget(backup);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                          Qt::Horizontal, &reviewDialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply changes"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &reviewDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &reviewDialog, &QDialog::reject);
    connect(target, &QLineEdit::textChanged, &reviewDialog, [updateBackupText]() { updateBackupText(); });
    connect(browse, &QPushButton::clicked, &reviewDialog, [this, target, updateBackupText]() {
        const QString chosen = QFileDialog::getSaveFileName(this, tr("Choose RES transaction target"),
            target->text(), tr("Evil Islands archives (*.res)"));
        if(!chosen.isEmpty()) { target->setText(chosen); updateBackupText(); }
    });
    if(reviewDialog.exec() != QDialog::Accepted) return false;
    const QString destination = target->text().trimmed();
    if(destination.isEmpty()) return false;
    const QString normalizedDestination = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    const QString normalizedSource = QDir::cleanPath(QFileInfo(context.archivePath).absoluteFilePath());
    if(!sourceCanBeUpdated && normalizedDestination.compare(normalizedSource, Qt::CaseInsensitive) == 0)
    {
        QMessageBox::warning(this, tr("Protected active archive"),
            tr("Base-installation archives are read-only. Choose a mod-owned, copied, or new override archive."));
        return false;
    }
    if(QFileInfo(destination).fileName().startsWith("database", Qt::CaseInsensitive))
    {
        QMessageBox::warning(this, tr("Protected database destination"),
            tr("Generic RES transactions cannot publish to database.res or databaseJmv.res."));
        return false;
    }
    for(const QString& activeSource : m_activeSources)
    {
        if(!samePath(activeSource, destination)) continue;
        const QString portableDestination = QDir::fromNativeSeparators(destination).toLower();
        if(!portableDestination.contains(QStringLiteral("/mods/")))
        {
            QMessageBox::warning(this, tr("Protected active archive"),
                tr("Base-installation archives are read-only. Choose a mod-owned, copied, or new override archive."));
            return false;
        }
    }
    QString backupPath;
    if(QFileInfo::exists(destination) && backup->isChecked())
    {
        const SBackupResult backup = CBackupManager().snapshotBeforeSave(QFileInfo(destination));
        if(!backup.success)
        {
            QMessageBox::warning(this, tr("RES transaction backup"), backup.error);
            return false;
        }
        backupPath = backup.backupPath;
    }
    QString error;
    m_internalPublications.insert(normalizedDestination.toLower());
    if(!transaction.writeCandidate(destination, &error))
    {
        m_internalPublications.remove(normalizedDestination.toLower());
        QMessageBox::warning(this, tr("RES transaction failed"), error);
        return false;
    }
    QTimer::singleShot(2000, this, [this, normalizedDestination]() {
        m_internalPublications.remove(normalizedDestination.toLower());
    });
    bool activeDestination = false;
    for(const QString& source : m_activeSources)
        if(samePath(source, destination)) { activeDestination = true; break; }
    if(activeDestination)
    {
        beginLoad(m_activeSources, true, m_activeProfileName);
        emit activeResourcesPublished(destination);
    }
    else if(!m_activeProfileMode && samePath(context.archivePath, destination))
        beginLoad({destination}, false, QFileInfo(destination).fileName());
    QMessageBox::information(this, tr("RES transaction complete"),
        backupPath.isEmpty()
            ? tr("The validated archive was written to:\n%1\n\nNo backup was created.").arg(destination)
            : tr("The validated archive was written to:\n%1\n\nBackup:\n%2")
                .arg(destination, backupPath));
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
