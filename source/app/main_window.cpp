#include "main_window.h"
#include "ui_main_window.h"

#include <QWidget>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QUndoView>
#include <QSettings>
#include <QAction>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include <QMenu>
#include <QApplication>
#include <QStatusBar>
#include <QProgressDialog>
#include <QMouseEvent>
#include <QDockWidget>
#include <QToolBar>
#include <QFile>
#include <QTimer>
#include <QEventLoop>
#include <QCheckBox>
#include <QToolButton>
#include <QComboBox>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QActionGroup>
#include <QInputDialog>
#include <QTreeWidget>
#include <QHeaderView>
#include <QScrollBar>
#include <QDialogButtonBox>
#include <QClipboard>
#include <QLineEdit>
#include <QFontMetrics>
#include <QSplitter>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFrame>
#include <QResizeEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDialog>
#include <QScrollArea>
#include <QListWidget>
#include <QGroupBox>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QVariant>
#include <QGuiApplication>
#include <QScreen>
#include <QImage>
#include <QImageReader>
#include <QRegularExpressionValidator>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>
#include <limits>

#include "resourcemanager.h"
#include "landscape.h"
#include "node.h"
#include "settings.h"
#include "ui/validation_panel_layout.h"
#include "ui/message_log_panel.h"
#include "ui/dock_hidden_tab.h"
#include "endurance_ui_scale.h"
#include "select_window.h"
#include "createobjectform.h"
#include "randomize_form.h"
#include "undo.h"
#include "mob\mob.h"
#include "mob\mob_parameters.h"
#include "layout_components/connectors_ui.h"
#include "preview.h"
#include "log.h"
#include "scene.h"
#include "backup_manager.h"
#include "resource_setup_dialog.h"
#include "mob_diff_dialog.h"
#include "window_lifecycle.h"
#include "maps_browser.h"
#include "map_catalog.h"
#include "map_spawn_planner.h"
#include "jmv_map_test_launcher.h"
#include "resource_profile.h"
#include "profile_database.h"
#include "endurance_document.h"
#include "ai_graph_document.h"
#include "ai_graph_persistence.h"
#include "mpr_document.h"
#include "terrain_edit_model.h"
#include "staged_save_validator.h"
#include "landscape_tools.h"
#include "collection_document.h"
#include "collection_editor_dialog.h"
#include "map_table_editor_dialog.h"
#include "database_editor_dialog.h"
#include "item_spell_constructor_dialog.h"
#include "database_document.h"
#include "minimap_export_dialog.h"
#include "viewport_minimap_widget.h"
#include "mmp_codec.h"
#include "mmp_inspector.h"
#include "script_reference_analyzer.h"
#include "unit.h"

namespace
{
constexpr int kValidationPathRole = Qt::UserRole;
constexpr int kValidationCopyRole = Qt::UserRole + 1;
constexpr int kValidationNidRole = Qt::UserRole + 2;
constexpr int kValidationSectionRole = Qt::UserRole + 3;
constexpr int kLoadedMapKindRole = Qt::UserRole + 20;
constexpr int kLoadedMapPathRole = Qt::UserRole + 21;
constexpr int kLoadedMapMpr = 1;
constexpr int kLoadedMapMob = 2;

class CDatabaseHistoryCommand final : public QUndoCommand
{
public:
    CDatabaseHistoryCommand(const QSharedPointer<ei::DatabaseDocument>& document,
                            ei::DatabaseEditorDialog* dialog, CView* view,
                            const QString& description,const QString& tableKey,
                            const QString& rowKey)
        : QUndoCommand(description), m_document(document), m_dialog(dialog),m_view(view),m_tableKey(tableKey),m_rowKey(rowKey) {}
    void undo() override { if(m_document->undo()) refresh(); }
    void redo() override { if(m_initialRedo){m_initialRedo=false;return;} if(m_document->redo()) refresh(); }
private:
    void refresh()
    {
        if(m_dialog)m_dialog->refreshFromHistory();else{QString diagnostic;const auto snapshot=m_document->currentSnapshot(&diagnostic);if(snapshot)ei::ProfileDatabaseContext::instance().publishEditedSnapshot(snapshot);}
        if(m_view)m_view->refreshDatabaseVisuals(m_tableKey,m_rowKey);
    }
    QSharedPointer<ei::DatabaseDocument> m_document;
    QPointer<ei::DatabaseEditorDialog> m_dialog;
    QPointer<CView> m_view;
    QString m_tableKey,m_rowKey;
    bool m_initialRedo{true};
};

class CElidedPathLabel final : public QLabel
{
public:
    explicit CElidedPathLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }

    void setFullText(const QString& text)
    {
        m_fullText = text;
        updateElision();
    }

    QSize sizeHint() const override
    {
        QSize result = QLabel::sizeHint();
        result.setWidth(0);
        return result;
    }

    QSize minimumSizeHint() const override
    {
        QSize result = QLabel::minimumSizeHint();
        result.setWidth(0);
        return result;
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        updateElision();
    }

private:
    void updateElision()
    {
        QLabel::setText(fontMetrics().elidedText(
            m_fullText, Qt::ElideLeft, qMax(0, contentsRect().width())));
    }

    QString m_fullText;
};

struct SNewMprHeightmap
{
    QImage image;
    QString error;
};

SNewMprHeightmap loadNewMprHeightmap(const QString& path)
{
    SNewMprHeightmap result;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        result.error = QObject::tr("The selected file could not be opened: %1")
            .arg(file.errorString());
        return result;
    }
    QImageReader reader(&file);
    reader.setAutoTransform(false);
    result.image = reader.read();
    if(result.image.isNull())
        result.error = QObject::tr("The selected image could not be decoded: %1")
            .arg(reader.errorString());
    return result;
}

class CNewMprCanvasPreview final : public QWidget
{
public:
    explicit CNewMprCanvasPreview(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(420, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setToolTip(QObject::tr(
            "The image is stretched to the complete terrain canvas. North is at the top. "
            "Thin lines are tiles; stronger lines are 16-tile SEC sectors."));
    }

    void setCanvas(int sectorsX, int sectorsY, const QImage& heightmap,
                   bool flipVertically, bool overlayEnabled,
                   bool terrainTapered, bool overlayTapered)
    {
        m_sectorsX = qMax(1, sectorsX);
        m_sectorsY = qMax(1, sectorsY);
        m_heightmap = heightmap;
        m_flipVertically = flipVertically;
        m_overlayEnabled = overlayEnabled;
        m_terrainTapered = terrainTapered;
        m_overlayTapered = overlayTapered;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(28, 28, 28));
        const int tilesX = m_sectorsX * 16;
        const int tilesY = m_sectorsY * 16;
        QRectF available = QRectF(rect()).adjusted(24, 24, -24, -30);
        const qreal scale = qMin(available.width() / qreal(tilesX),
                                 available.height() / qreal(tilesY));
        const QSizeF size(tilesX * scale, tilesY * scale);
        const QRectF mapRect(available.center().x() - size.width() / 2.0,
                             available.center().y() - size.height() / 2.0,
                             size.width(), size.height());
        painter.fillRect(mapRect, QColor(72, 72, 72));
        if(!m_heightmap.isNull())
        {
            QImage image = m_flipVertically
                ? m_heightmap.mirrored(false, true) : m_heightmap;
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(mapRect, image);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        }
        if(m_terrainTapered)
        {
            painter.setPen(QPen(QColor(0, 0, 0, 150), qMax(2.0, scale * 2.0)));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(mapRect.adjusted(scale, scale, -scale, -scale));
        }
        if(m_overlayEnabled)
        {
            painter.fillRect(mapRect, QColor(40, 145, 220, 62));
            painter.setPen(QPen(QColor(80, 190, 255, 210),
                                m_overlayTapered ? 3.0 : 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(mapRect.adjusted(1, 1, -1, -1));
        }
        if(scale >= 3.0)
        {
            painter.setPen(QPen(QColor(255, 255, 255, 35), 1));
            for(int x = 1; x < tilesX; ++x)
                painter.drawLine(QPointF(mapRect.left() + x * scale, mapRect.top()),
                                 QPointF(mapRect.left() + x * scale, mapRect.bottom()));
            for(int y = 1; y < tilesY; ++y)
                painter.drawLine(QPointF(mapRect.left(), mapRect.top() + y * scale),
                                 QPointF(mapRect.right(), mapRect.top() + y * scale));
        }
        painter.setPen(QPen(QColor(255, 196, 64, 190), 1.5));
        for(int x = 0; x <= m_sectorsX; ++x)
            painter.drawLine(QPointF(mapRect.left() + x * 16 * scale, mapRect.top()),
                             QPointF(mapRect.left() + x * 16 * scale, mapRect.bottom()));
        for(int y = 0; y <= m_sectorsY; ++y)
            painter.drawLine(QPointF(mapRect.left(), mapRect.top() + y * 16 * scale),
                             QPointF(mapRect.right(), mapRect.top() + y * 16 * scale));
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(QRectF(0, 2, width(), 20), Qt::AlignCenter,
                         QObject::tr("NORTH"));
        painter.drawText(QRectF(0, height() - 24, width(), 20), Qt::AlignCenter,
                         QObject::tr("%1 x %2 tiles (%3 x %4 sectors)")
                             .arg(tilesX).arg(tilesY)
                             .arg(m_sectorsX).arg(m_sectorsY));
    }

private:
    int m_sectorsX{1};
    int m_sectorsY{1};
    QImage m_heightmap;
    bool m_flipVertically{false};
    bool m_overlayEnabled{false};
    bool m_terrainTapered{false};
    bool m_overlayTapered{false};
};

QIcon entranceVisibilityIcon(bool visible, const QColor& color)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 1.8, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    QPainterPath eye;
    eye.moveTo(2.5, 12.0);
    eye.cubicTo(7.0, 4.3, 17.0, 4.3, 21.5, 12.0);
    eye.cubicTo(17.0, 19.7, 7.0, 19.7, 2.5, 12.0);
    painter.drawPath(eye);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(12.0, 12.0), 3.0, 3.0);
    if(!visible)
    {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 2.2, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(4.0, 20.0), QPointF(20.0, 4.0));
    }
    return QIcon(pixmap);
}

QIcon behaviorModeIcon(bool active, const QColor& foreground)
{
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color = active ? QColor(52, 152, 219) : foreground;
    painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    painter.drawEllipse(QPointF(9.0, 8.0), 4.2, 4.2);
    QPainterPath shoulders;
    shoulders.moveTo(2.8, 21.8);
    shoulders.cubicTo(3.5, 15.2, 7.0, 13.3, 10.0, 13.3);
    shoulders.cubicTo(12.0, 13.3, 13.6, 14.0, 14.8, 15.1);
    painter.drawPath(shoulders);

    const QPointF gearCenter(19.3, 18.6);
    painter.drawEllipse(gearCenter, 4.0, 4.0);
    painter.drawEllipse(gearCenter, 1.35, 1.35);
    for(int spoke = 0; spoke < 8; ++spoke)
    {
        const qreal angle = spoke * M_PI / 4.0;
        const QPointF inner(gearCenter.x() + std::cos(angle) * 4.4,
                            gearCenter.y() + std::sin(angle) * 4.4);
        const QPointF outer(gearCenter.x() + std::cos(angle) * 6.0,
                            gearCenter.y() + std::sin(angle) * 6.0);
        painter.drawLine(inner, outer);
    }
    return QIcon(pixmap);
}

void updateEntranceVisibilityButtonAppearance(QToolButton* button, bool visible)
{
    if(!button)
        return;
    button->setIcon(entranceVisibilityIcon(visible,
        button->palette().color(QPalette::ButtonText)));
    button->setToolTip(visible
        ? QObject::tr("Hide map.txt entrance areas and labels in the viewport.")
        : QObject::tr("Show map.txt entrance areas and labels in the viewport."));
    button->setAccessibleName(visible
        ? QObject::tr("Hide entrances") : QObject::tr("Show entrances"));
}

QRect preferredDialogScreenGeometry(const QWidget* owner)
{
    if(owner && owner->isVisible())
    {
        if(QScreen* screen = QGuiApplication::screenAt(owner->frameGeometry().center()))
            return screen->availableGeometry();
    }
    const QVariant startupGeometry = qApp->property("enduranceStartupScreenGeometry");
    if(startupGeometry.canConvert<QRect>() && startupGeometry.toRect().isValid())
        return startupGeometry.toRect();
    if(QScreen* screen = QGuiApplication::primaryScreen())
        return screen->availableGeometry();
    return QRect();
}

void centerDialogOnPreferredScreen(QWidget* dialog, const QWidget* owner)
{
    if(!dialog)
        return;
    const QRect available = preferredDialogScreenGeometry(owner);
    if(!available.isValid())
        return;
    dialog->adjustSize();
    const QSize size = dialog->frameGeometry().size();
    const int x = available.left() + qMax(0, (available.width() - size.width()) / 2);
    const int y = available.top() + qMax(0, (available.height() - size.height()) / 2);
    dialog->move(x, y);
}

QString conciseValidationLine(const SMobFamilyFinding& finding)
{
    QString severity;
    switch(finding.severity)
    {
    case EMobFamilyFindingSeverity::Critical: severity = "CRITICAL"; break;
    case EMobFamilyFindingSeverity::Warning: severity = "WARNING"; break;
    case EMobFamilyFindingSeverity::Incomplete: severity = "INCOMPLETE"; break;
    case EMobFamilyFindingSeverity::Info: severity = "INFO"; break;
    }
    const QString line = QString("%1: %2").arg(severity, finding.message.simplified());
    return QFontMetrics(QApplication::font()).elidedText(line, Qt::ElideRight, 960);
}

QString runtimeManifestProblem(const SResourceProfileEvaluation& profile)
{
    QStringList problems;
    for(const SRuntimeComponentEvaluation& component : profile.runtimeComponents)
    {
        if(!component.exists)
            problems.append(QObject::tr("Missing %1: %2").arg(component.id, component.path));
        else if(component.enforceHash && !component.hashAccepted)
            problems.append(QObject::tr("Unsupported %1: %2 (SHA-256 %3)")
                .arg(component.id, component.path, component.sha256));
    }
    return problems.join('\n');
}

void configureBlockingProgress(QProgressDialog* progress)
{
    if(!progress)
        return;
    progress->setCancelButton(nullptr);
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                             Qt::WindowTitleHint);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
}
}

void testFunc()
{
    ei::log(eLogDebug, "test func start");
    //auto aaa = CResourceManager::getInstance()->tempFolder();
    ei::log(eLogDebug, "test func end");
}


void MainWindow::on_toolButton_2_clicked()
{
    ei::log(eLogDebug, "btn test start");

    ei::log(eLogDebug, "btn test end");
}

void MainWindow::showItemSpellConstructor(const QString& initialCode)
{
    const auto snapshot = ei::ProfileDatabaseContext::instance().current();
    QString diagnostic;
    if(!m_databaseDocument || !snapshot
       || QFileInfo(m_databaseDocument->sourceArchivePath()).absoluteFilePath().compare(
              QFileInfo(snapshot->sourceArchivePath).absoluteFilePath(), Qt::CaseInsensitive) != 0)
        m_databaseDocument = ei::DatabaseDocument::fromSnapshot(snapshot, &diagnostic);
    if(!m_databaseDocument)
    {
        QMessageBox::warning(this, tr("Item / Spell Constructor"),
            tr("The active profile database cannot be opened.\n\n%1").arg(diagnostic));
        return;
    }
    const QString source = QFileInfo(m_databaseDocument->sourceArchivePath()).absoluteFilePath();
    if(m_itemSpellConstructorDialog
       && m_itemSpellConstructorDialog->property("databaseSource").toString().compare(
              source, Qt::CaseInsensitive) != 0)
    {
        delete m_itemSpellConstructorDialog;
        m_itemSpellConstructorDialog = nullptr;
    }
    if(!m_itemSpellConstructorDialog)
    {
        QStringList roots;
        if(COptStringList* option = dynamic_cast<COptStringList*>(
               m_settings->opt(eOptSetResource, "resourceRoots")))
            for(const QString& root : option->value()) roots << root;
        m_itemSpellConstructorDialog = new ei::ItemSpellConstructorDialog(
            m_databaseDocument, roots, m_pView, nullptr);
        m_itemSpellConstructorDialog->setProperty("databaseSource", source);
    }
    if(!initialCode.trimmed().isEmpty()) m_itemSpellConstructorDialog->setCode(initialCode);
    m_itemSpellConstructorDialog->show();
    m_itemSpellConstructorDialog->raise();
    m_itemSpellConstructorDialog->activateWindow();
}

void MainWindow::showDatabaseEditor(const QString& prototype)
{
    ei::log(eLogInfo, QString("Database editor navigation requested for prototype '%1'")
                        .arg(prototype));
    if(qApp->property("enduranceDatabasePrototypeNavigationProbe").toBool())
    {
        qApp->setProperty("enduranceDatabasePrototypeNavigationReceived", prototype);
        return;
    }
    if(!m_databaseEditorDialog)
    {
        const auto snapshot = ei::ProfileDatabaseContext::instance().current();
        QString diagnostic;
        if(!m_databaseDocument || !snapshot
           || QFileInfo(m_databaseDocument->sourceArchivePath()).absoluteFilePath()
                .compare(QFileInfo(snapshot->sourceArchivePath).absoluteFilePath(),
                         Qt::CaseInsensitive) != 0)
            m_databaseDocument = ei::DatabaseDocument::fromSnapshot(snapshot, &diagnostic);
        if(!m_databaseDocument)
        {
            QMessageBox::warning(this, tr("Database Editor"),
                tr("The active profile database cannot be opened for authoring.\n\n%1")
                    .arg(diagnostic));
            return;
        }

        m_databaseEditorDialog = new ei::DatabaseEditorDialog(m_databaseDocument, this);
        connect(m_databaseEditorDialog, &ei::DatabaseEditorDialog::transactionCommitted,
                this, [this](const QString& description, const QString& tableKey,
                             const QString& rowKey)
        {
            m_undoStack->push(new CDatabaseHistoryCommand(
                m_databaseDocument, m_databaseEditorDialog, m_pView,
                description, tableKey, rowKey));
            m_pView->refreshDatabaseVisuals(tableKey, rowKey);
        });
        connect(m_databaseEditorDialog, &ei::DatabaseEditorDialog::itemSpellConstructorRequested,
                this, &MainWindow::showItemSpellConstructor);
    }

    m_databaseEditorDialog->show();
    m_databaseEditorDialog->raise();
    m_databaseEditorDialog->activateWindow();
    if(!prototype.trimmed().isEmpty()
       && !m_databaseEditorDialog->selectPrototype(prototype))
    {
        QMessageBox::warning(m_databaseEditorDialog, tr("Prototype not found"),
            tr("The active profile database does not contain monster prototype '%1'.")
                .arg(prototype));
    }
}

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    m_ui(new Ui::MainWindow),
    m_undoStack(nullptr),
    m_undoView(nullptr),
    m_pView(nullptr),
    m_testTable(nullptr),
    m_recentFilesMenu(nullptr),
    m_externalScriptAction(nullptr),
    m_convertWorldObjectToLeverAction(nullptr),
    m_mapsDock(nullptr),
    m_historyDock(nullptr),
    m_validationDock(nullptr),
    m_outlinerDock(nullptr),
    m_propertiesDock(nullptr),
    m_loadedMapDock(nullptr),
    m_loadedMapTree(nullptr),
    m_validationTree(nullptr),
    m_validationSummary(nullptr),
    m_validationTimer(nullptr),
    m_mapsBrowser(nullptr),
    m_landscapeToolsDock(nullptr),
    m_landscapeTools(nullptr),
    m_mapTestAction(nullptr),
    m_stopMapTestAction(nullptr),
    m_mapTestLauncher(nullptr),
    m_closeAfterTestStop(false),
    m_entranceCombo(nullptr),
    m_showEntrancesButton(nullptr),
    m_playFromHereButton(nullptr),
    m_gotoEntranceButton(nullptr),
    m_godmodeCheckBox(nullptr),
    m_spawnPositionLabel(nullptr),
    m_borrowedMapIdentity(false),
    m_pickSpawnPending(false)
{
#ifdef QT_DEBUG
    testFunc();
#endif
    CIconManager::getInstance()->init();

    m_settings.reset(new CSettings(this));
    m_selector.reset(new CSelectForm(this));
    m_createDialog.reset(new CCreateObjectForm(this));
    m_randomizeForm.reset(new CRandomizeForm(this));
    m_pTileForm.reset(new CTileForm(this));
    m_settings->attachMainWindow(this);
    m_ui->setupUi(this); //init CView core also
    setDockOptions(dockOptions() | QMainWindow::AllowTabbedDocks |
                   QMainWindow::AllowNestedDocks | QMainWindow::GroupedDragging);
    QWidget* outlinerContainer = new QWidget(this);
    QVBoxLayout* outlinerLayout = new QVBoxLayout(outlinerContainer);
    outlinerLayout->setContentsMargins(0, 0, 0, 0);
    outlinerLayout->setSpacing(3);
    QHBoxLayout* filterLayout = new QHBoxLayout;
    filterLayout->setContentsMargins(0, 0, 0, 0);
    m_outlinerFilterEdit = new QLineEdit(outlinerContainer);
    m_outlinerFilterEdit->setClearButtonEnabled(true);
    m_outlinerFilterEdit->setPlaceholderText(tr("Filter objects (name, ID, model, texture, database...)") );
    m_outlinerFilterEdit->setToolTip(tr(
        "Searches for contiguous text by default. Combine terms with spaces. "
        "Use field=value for an exact match or prefix a value with ~ for fuzzy subsequence matching "
        "(for example id:123, id=123, or id:~123). Optional fields: "
        "name:, id:, type:, model:, texture:, db:, prototype:, mob:, validation:."));
    m_outlinerFilterStatus = new QLabel(tr("0 / 0"), outlinerContainer);
    m_outlinerFilterStatus->setMinimumWidth(72);
    m_outlinerFilterStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    filterLayout->addWidget(m_outlinerFilterEdit, 1);
    filterLayout->addWidget(m_outlinerFilterStatus);
    outlinerLayout->addLayout(filterLayout);
    m_outlinerSelectionPath = new CElidedPathLabel(outlinerContainer);
    m_outlinerSelectionPath->setTextFormat(Qt::PlainText);
    m_outlinerSelectionPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_outlinerSelectionPath->setStyleSheet(
        "QLabel { background: #29424a; color: white; padding: 5px 7px; "
        "border-left: 3px solid #32c8e6; }");
    m_outlinerSelectionPath->hide();
    m_ui->treeWidget->setParent(outlinerContainer);
    outlinerLayout->addWidget(m_ui->treeWidget, 1);
    m_outlinerDock = new QDockWidget(tr("Object Outliner"), this);
    m_outlinerDock->setObjectName("objectOutlinerDock");
    m_outlinerDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_outlinerDock->setFeatures(QDockWidget::DockWidgetClosable |
                               QDockWidget::DockWidgetMovable |
                               QDockWidget::DockWidgetFloatable);
    m_outlinerDock->setWidget(outlinerContainer);

    QWidget* propertiesContainer = new QWidget(this);
    QVBoxLayout* propertiesLayout = new QVBoxLayout(propertiesContainer);
    propertiesLayout->setContentsMargins(0, 0, 0, 0);
    propertiesLayout->setSpacing(3);
    m_objectPropertiesHeader = new QWidget(propertiesContainer);
    QHBoxLayout* propertiesHeaderLayout = new QHBoxLayout(m_objectPropertiesHeader);
    propertiesHeaderLayout->setContentsMargins(0, 0, 0, 0);
    propertiesHeaderLayout->setSpacing(2);
    propertiesHeaderLayout->addWidget(m_outlinerSelectionPath, 1);
    m_objectPropertiesHeader->hide();
    propertiesLayout->addWidget(m_objectPropertiesHeader);
    m_logicSlotBar = new QWidget(propertiesContainer);
    QHBoxLayout* logicSlotLayout = new QHBoxLayout(m_logicSlotBar);
    logicSlotLayout->setContentsMargins(4, 3, 4, 3);
    logicSlotLayout->addWidget(new QLabel(tr("Behavior setup:"), m_logicSlotBar));
    m_logicSlotCombo = new QComboBox(m_logicSlotBar);
    m_logicSlotCombo->addItem(tr("Logic 0 (default)"), 0);
    m_logicSlotCombo->addItem(tr("Logic 1 (responds to alarm 1)"), 1);
    m_logicSlotCombo->addItem(tr("Logic 2 (responds to alarm 2)"), 2);
    m_logicSlotCombo->addItem(tr("Logic 3 (responds to alarm 3)"), 3);
    m_logicSlotCombo->addItem(tr("Logic 4 (responds to alarm 4)"), 4);
    m_logicSlotCombo->setItemData(0, tr("The unit's normal behavior setup."), Qt::ToolTipRole);
    for(int slot = 1; slot < m_logicSlotCombo->count(); ++slot)
        m_logicSlotCombo->setItemData(slot,
            tr("Activates when world alarm %1 is raised, if Logic enabled is on.").arg(slot),
            Qt::ToolTipRole);
    m_logicSlotCombo->setToolTip(tr(
        "Select which of the unit's five behavior setups is shown and edited. "
        "The first setup is the default; the others activate from world alarms 1 to 4."));
    logicSlotLayout->addWidget(m_logicSlotCombo, 1);
    m_logicSlotBar->hide();
    propertiesLayout->addWidget(m_logicSlotBar);
    m_behaviorNavigationBar = new QWidget(propertiesContainer);
    QHBoxLayout* behaviorNavigationLayout = new QHBoxLayout(m_behaviorNavigationBar);
    behaviorNavigationLayout->setContentsMargins(4, 0, 4, 0);
    behaviorNavigationLayout->setSpacing(2);
    m_selectBehaviorParentButton = new QToolButton(m_behaviorNavigationBar);
    m_selectBehaviorParentButton->setText(tr("Parent"));
    m_selectBehaviorParentButton->setToolTip(tr("Select owning object"));
    m_selectPreviousBehaviorPointButton = new QToolButton(m_behaviorNavigationBar);
    m_selectPreviousBehaviorPointButton->setText(QString::fromUtf8("\u25c0"));
    m_selectPreviousBehaviorPointButton->setToolTip(tr("Select previous sibling point"));
    m_selectNextBehaviorPointButton = new QToolButton(m_behaviorNavigationBar);
    m_selectNextBehaviorPointButton->setText(QString::fromUtf8("\u25b6"));
    m_selectNextBehaviorPointButton->setToolTip(tr("Select next sibling point"));
    behaviorNavigationLayout->addWidget(m_selectBehaviorParentButton);
    behaviorNavigationLayout->addStretch(1);
    behaviorNavigationLayout->addWidget(m_selectPreviousBehaviorPointButton);
    behaviorNavigationLayout->addWidget(m_selectNextBehaviorPointButton);
    m_behaviorNavigationBar->hide();
    propertiesLayout->addWidget(m_behaviorNavigationBar);
    m_ui->tableWidget->setParent(propertiesContainer);
    propertiesLayout->addWidget(m_ui->tableWidget, 1);

    m_propertiesDock = new QDockWidget(tr("Object Properties"), this);
    m_propertiesDock->setObjectName("objectPropertiesDock");
    m_propertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_propertiesDock->setFeatures(QDockWidget::DockWidgetClosable |
                                 QDockWidget::DockWidgetMovable |
                                 QDockWidget::DockWidgetFloatable);
    m_propertiesDock->setWidget(propertiesContainer);
    connect(m_selectBehaviorParentButton, &QToolButton::clicked, this,
            [this]() { if(m_pView) m_pView->selectBehaviorParent(); });
    connect(m_selectPreviousBehaviorPointButton, &QToolButton::clicked, this,
            [this]() { if(m_pView) m_pView->selectPreviousBehaviorPoint(); });
    connect(m_selectNextBehaviorPointButton, &QToolButton::clicked, this,
            [this]() { if(m_pView) m_pView->selectNextBehaviorPoint(); });
    addDockWidget(Qt::RightDockWidgetArea, m_outlinerDock);
    splitDockWidget(m_outlinerDock, m_propertiesDock, Qt::Vertical);
    m_ui->outlinerSplitter->hide();
    connect(m_outlinerFilterEdit, &QLineEdit::textChanged,
            m_ui->treeWidget, &CTreeView::setFilterText);
    connect(m_ui->treeWidget, &CTreeView::filterMatchCountChanged,
            this, [this](int visible, int total)
    {
        m_outlinerFilterStatus->setText(tr("%1 / %2").arg(visible).arg(total));
        m_outlinerFilterStatus->setToolTip(tr("%1 matching objects out of %2").arg(visible).arg(total));
        updateOutlinerSelectionPath();
    });
    connect(m_ui->treeWidget, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem*, QTreeWidgetItem*) { updateOutlinerSelectionPath(); });
    connect(m_ui->treeWidget->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) { updateOutlinerSelectionPath(); });
    connect(m_ui->treeWidget, &QTreeWidget::itemExpanded,
            this, [this](QTreeWidgetItem*) { updateOutlinerSelectionPath(); });
    connect(m_ui->treeWidget, &QTreeWidget::itemCollapsed,
            this, [this](QTreeWidgetItem*) { updateOutlinerSelectionPath(); });
    m_mapsBrowser = new CMapsBrowser(this);
    m_mapsDock = new QDockWidget(tr("Maps and Collections"), this);
    m_mapsDock->setObjectName("mapsAndCollectionsDock");
    m_mapsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_mapsDock->setWidget(m_mapsBrowser);
    addDockWidget(Qt::LeftDockWidgetArea, m_mapsDock);
    m_loadedMapTree = new QTreeWidget(this);
    m_loadedMapTree->setColumnCount(3);
    m_loadedMapTree->setHeaderLabels(QStringList() << tr("Loaded document")
                                      << tr("Visible") << tr("State"));
    m_loadedMapTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_loadedMapTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_loadedMapTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_loadedMapTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_loadedMapTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_loadedMapTree->viewport()->installEventFilter(this);
    m_loadedMapDock = new QDockWidget(tr("Loaded Map"), this);
    m_loadedMapDock->setObjectName("loadedMapDock");
    m_loadedMapDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_loadedMapDock->setFeatures(QDockWidget::DockWidgetClosable |
                                 QDockWidget::DockWidgetMovable |
                                 QDockWidget::DockWidgetFloatable);
    m_loadedMapDock->setWidget(m_loadedMapTree);
    addDockWidget(Qt::RightDockWidgetArea, m_loadedMapDock);
    splitDockWidget(m_loadedMapDock, m_outlinerDock, Qt::Vertical);
    connect(m_loadedMapTree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column)
    {
        if(!item || column != 1 || !m_pView || m_loadedMapVisibilityDragging)
            return;
        const bool visible = item->checkState(1) == Qt::Checked;
        const int kind = item->data(0, kLoadedMapKindRole).toInt();
        if(kind == kLoadedMapMpr)
            m_pView->setLandscapeVisible(visible);
        else if(kind == kLoadedMapMob)
        {
            CMob* mob = m_pView->mob(item->data(0, kLoadedMapPathRole).toString());
            m_pView->setMobVisible(mob, visible);
        }
    });
    connect(m_loadedMapTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int column)
    {
        Q_UNUSED(column);
        if(!item || !m_pView
           || item->data(0, kLoadedMapKindRole).toInt() != kLoadedMapMob)
            return;
        CMob* mob = m_pView->mob(item->data(0, kLoadedMapPathRole).toString());
        if(mob && mob != m_pView->currentMob())
            m_pView->changeCurrentMob(mob);
    });
    connect(m_loadedMapTree, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& point)
    {
        QTreeWidgetItem* item = m_loadedMapTree->itemAt(point);
        if(!item)
            return;
        const int kind = item->data(0, kLoadedMapKindRole).toInt();
        if(kind == kLoadedMapMpr)
        {
            if(!item->isSelected())
                m_loadedMapTree->setCurrentItem(item, 0,
                    QItemSelectionModel::ClearAndSelect);
            updateDocumentActionAvailability();
            QMenu menu(this);
            menu.addAction(m_ui->actionMap_parameters);
            menu.addSeparator();
            menu.addAction(m_ui->actionSave_landscape_MPR);
            menu.addAction(m_ui->actionSave_landscape_MPR_as);
            menu.exec(m_loadedMapTree->viewport()->mapToGlobal(point));
            return;
        }
        if(kind != kLoadedMapMob)
            return;
        if(!item->isSelected())
            m_loadedMapTree->setCurrentItem(item, 0,
                QItemSelectionModel::ClearAndSelect);
        QList<CMob*> selectedMobs;
        for(QTreeWidgetItem* selectedItem : m_loadedMapTree->selectedItems())
        {
            if(!selectedItem || selectedItem->data(0, kLoadedMapKindRole).toInt() != kLoadedMapMob)
                continue;
            CMob* selectedMob = m_pView->mob(
                selectedItem->data(0, kLoadedMapPathRole).toString());
            if(selectedMob && !selectedMobs.contains(selectedMob))
                selectedMobs.append(selectedMob);
        }
        CMob* mob = m_pView->mob(item->data(0, kLoadedMapPathRole).toString());
        if(!mob)
            return;
        QMenu menu(this);
        QAction* activate = menu.addAction(tr("Make Active"));
        activate->setEnabled(mob != m_pView->currentMob());
        menu.addSeparator();
        QAction* parameters = menu.addAction(tr("Open MOB Parameters"));
        parameters->setShortcut(m_ui->action_Mob_parameters->shortcut());
        QAction* script = menu.addAction(tr("Open MOB Script"));
        script->setShortcut(m_ui->actionOpen_MOB_Script->shortcut());
        menu.addSeparator();
        QAction* save = menu.addAction(tr("Save MOB"));
        save->setShortcut(m_ui->actionSave->shortcut());
        save->setEnabled(mob->isDirty());
        QAction* saveAs = menu.addAction(tr("Save MOB As..."));
        saveAs->setShortcut(m_ui->actionSave_as->shortcut());
        menu.addSeparator();
        QAction* unload = menu.addAction(selectedMobs.size() > 1
            ? tr("Unload %1 MOBs").arg(selectedMobs.size()) : tr("Unload MOB"));
        QAction* selected = menu.exec(m_loadedMapTree->viewport()->mapToGlobal(point));
        if(selected == activate)
            m_pView->changeCurrentMob(mob);
        else if(selected == parameters || selected == script
                || selected == save || selected == saveAs)
        {
            if(mob != m_pView->currentMob())
                m_pView->changeCurrentMob(mob);
            if(selected == parameters)
                on_action_Mob_parameters_triggered();
            else if(selected == script)
                on_actionOpen_MOB_Script_triggered();
            else if(selected == save)
                on_actionSave_triggered();
            else
                on_actionSave_as_triggered();
        }
        else if(selected == unload)
        {
            QList<CMob*> dirty;
            for(CMob* selectedMob : selectedMobs)
                if(selectedMob && selectedMob->isDirty()) dirty.append(selectedMob);
            bool proceed = true;
            bool saveDirty = false;
            if(!dirty.isEmpty())
            {
                QMessageBox prompt(QMessageBox::Question, tr("Unload MOBs"),
                    tr("%1 selected MOB(s) have unsaved changes.").arg(dirty.size()),
                    QMessageBox::NoButton, this);
                QPushButton* saveAll = prompt.addButton(tr("Save All"), QMessageBox::AcceptRole);
                QPushButton* discard = prompt.addButton(tr("Discard"), QMessageBox::DestructiveRole);
                prompt.addButton(QMessageBox::Cancel);
                prompt.exec();
                proceed = prompt.clickedButton() != prompt.button(QMessageBox::Cancel);
                saveDirty = prompt.clickedButton() == saveAll;
                Q_UNUSED(discard);
            }
            if(proceed && saveDirty)
                for(CMob* dirtyMob : dirty)
                    if(!m_pView->saveMobDocument(dirtyMob, dirtyMob->filePath()))
                    { proceed = false; break; }
            if(proceed)
                for(CMob* selectedMob : selectedMobs)
                    if(selectedMob && m_pView->mobs().contains(selectedMob))
                        if(!m_pView->unloadMobDocument(selectedMob, false)) break;
        }
    });
    QWidget* validationWidget = new QWidget(this);
    QVBoxLayout* validationLayout = new QVBoxLayout(validationWidget);
    validationLayout->setContentsMargins(4, 4, 4, 4);
    m_validationSummary = new QLabel(tr("No active MOB family."), validationWidget);
    m_validationSummary->setWordWrap(true);
    m_validationTree = new QTreeWidget(validationWidget);
    m_validationTree->setColumnCount(4);
    m_validationTree->setHeaderLabels(QStringList() << tr("Severity") << tr("MOB / Type")
                                      << tr("Source path") << tr("Details"));
    ei::configureValidationTreeHeader(m_validationTree->header());
    m_validationTree->setUniformRowHeights(true);
    m_validationTree->setContextMenuPolicy(Qt::CustomContextMenu);
    validationLayout->addWidget(m_validationSummary);
    validationLayout->addWidget(m_validationTree);
    m_validationShowMore = new QPushButton(tr("Show 250 more findings"), validationWidget);
    m_validationShowMore->hide();
    validationLayout->addWidget(m_validationShowMore);
    m_validationDock = new QDockWidget(tr("MOB Family Validation"), this);
    m_validationDock->setObjectName("mobFamilyValidationDock");
    m_validationDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::LeftDockWidgetArea |
                                      Qt::RightDockWidgetArea);
    m_validationDock->setFeatures(QDockWidget::DockWidgetClosable |
                                  QDockWidget::DockWidgetMovable |
                                  QDockWidget::DockWidgetFloatable);
    m_validationDock->setWidget(validationWidget);
    addDockWidget(Qt::BottomDockWidgetArea, m_validationDock);
    ei::enableNativeFloatingDockControls(m_validationDock);
    m_validationDock->setFloating(true);
    m_validationDock->resize(1100, 650);
    m_validationDock->hide();

    m_messageLogPanel = new CMessageLogPanel(this);
    m_messageLogDock = new QDockWidget(tr("Message Log"), this);
    m_messageLogDock->setObjectName(QStringLiteral("messageLogDock"));
    m_messageLogDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::LeftDockWidgetArea |
                                      Qt::RightDockWidgetArea);
    m_messageLogDock->setFeatures(QDockWidget::DockWidgetClosable |
                                  QDockWidget::DockWidgetMovable |
                                  QDockWidget::DockWidgetFloatable);
    m_messageLogDock->setWidget(m_messageLogPanel);
    addDockWidget(Qt::BottomDockWidgetArea, m_messageLogDock);
    ei::enableNativeFloatingDockControls(m_messageLogDock);
    connect(this, &QMainWindow::tabifiedDockWidgetActivated, this,
            [this](QDockWidget* active) {
        if(active == m_messageLogDock)
            ei::refreshFloatingDockTabs(m_messageLogDock, m_validationDock);
        else if(active == m_validationDock)
            ei::refreshFloatingDockTabs(m_validationDock, m_messageLogDock);
    });
    resizeDocks({m_messageLogDock}, {190}, Qt::Vertical);
    m_validationTimer = new QTimer(this);
    m_validationTimer->setSingleShot(true);
    connect(m_validationTimer, &QTimer::timeout, this, &MainWindow::refreshMobFamilyValidation);
    m_validationWatcher = new QFutureWatcher<CMobFamilyValidationReport>(this);
    connect(m_validationWatcher, &QFutureWatcher<CMobFamilyValidationReport>::finished,
            this, &MainWindow::finishMobFamilyValidation);
    connect(m_validationShowMore, &QPushButton::clicked, this, [this]() {
        m_validationFindingLimit += 250;
        updateMobFamilyValidationPanel();
    });
    connect(m_validationTree, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& point) {
        QTreeWidgetItem* item = m_validationTree->itemAt(point);
        if(!item) return;
        QMenu menu(this);
        QAction* copyPath = menu.addAction(tr("Copy source path"));
        QAction* copyFinding = menu.addAction(tr("Copy complete entry"));
        menu.addSeparator();
        QAction* selectObject = menu.addAction(tr("Select object in editor"));
        copyPath->setEnabled(!item->data(0, kValidationPathRole).toString().isEmpty());
        copyFinding->setEnabled(!item->data(0, kValidationCopyRole).toString().isEmpty());
        selectObject->setEnabled(item->data(0, kValidationNidRole).isValid());
        QAction* selected = menu.exec(m_validationTree->viewport()->mapToGlobal(point));
        if(selected == copyPath)
            QApplication::clipboard()->setText(item->data(0, kValidationPathRole).toString());
        else if(selected == copyFinding)
            QApplication::clipboard()->setText(item->data(0, kValidationCopyRole).toString());
        else if(selected == selectObject)
            selectValidationObject(item);
    });
    connect(m_validationTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) { selectValidationObject(item); });
    QMenu* viewMenu = new QMenu(tr("View"), this);
    m_ui->menuBar->insertMenu(m_ui->menuSettings->menuAction(), viewMenu);
    viewMenu->addAction(m_mapsDock->toggleViewAction());
    viewMenu->addAction(m_messageLogDock->toggleViewAction());
    m_landscapeTools = new CLandscapeTools(this);
    m_landscapeToolsDock = new QDockWidget(tr("Landscape Tools"), this);
    m_landscapeToolsDock->setObjectName("landscapeToolsDock");
    m_landscapeToolsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_landscapeToolsDock->setFeatures(QDockWidget::DockWidgetClosable |
                                      QDockWidget::DockWidgetMovable |
                                      QDockWidget::DockWidgetFloatable);
    m_landscapeToolsDock->setWidget(m_landscapeTools);
    addDockWidget(Qt::RightDockWidgetArea, m_landscapeToolsDock);
    splitDockWidget(m_landscapeToolsDock, m_loadedMapDock, Qt::Vertical);
    m_landscapeToolsDock->hide();
    m_landscapeToolsAction = viewMenu->addAction(tr("Landscape Tools"));
    m_landscapeToolsAction->setCheckable(true);
    m_landscapeToolsAction->setIcon(*ei::icon("landscape.ico"));
    connect(m_landscapeToolsAction, &QAction::triggered,
            this, &MainWindow::toggleLandscapeTools);
    QAction* tileGridAction = viewMenu->addAction(tr("Show Terrain Tile Grid"));
    tileGridAction->setCheckable(true);
    tileGridAction->setToolTip(tr("Draw the 2 x 2 terrain-tile boundaries over the landscape."));
    connect(tileGridAction, &QAction::toggled, this, [this](bool visible)
    {
        m_pView->setTileGridVisible(visible);
    });
    viewMenu->addAction(m_loadedMapDock->toggleViewAction());
    viewMenu->addAction(m_outlinerDock->toggleViewAction());
    viewMenu->addAction(m_propertiesDock->toggleViewAction());
    m_ui->menuTools->removeAction(m_ui->actionShow_undo_redo);
    QAction* openValidationPanel = m_validationDock->toggleViewAction();
    openValidationPanel->setText(tr("MOB Family Validation"));
    viewMenu->addAction(openValidationPanel);
    connect(openValidationPanel, &QAction::toggled, this, [this](bool visible)
    {
        if(!visible) return;
        QTimer::singleShot(0, this, [this]()
        {
            if(!m_validationDock || !m_validationDock->isVisible()) return;
            ei::refreshFloatingDockTabs(m_validationDock, m_messageLogDock);
            m_validationDock->raise();
            m_validationDock->activateWindow();
            m_validationDock->setFocus();
        });
    });
    connect(m_mapsBrowser, &CMapsBrowser::openMapRequested,
            this, &MainWindow::openMapPair);
    connect(m_mapsBrowser, &CMapsBrowser::editMapRequested,
            this, [this](const QString& sourceArchive, const QString& zoneId)
    {
        CMapTableEditorDialog dialog(this);
        if(!dialog.openArchive(sourceArchive))
        {
            QMessageBox::warning(this, tr("Map Catalog Editor"),
                tr("Could not open the map catalog source:\n\n%1")
                    .arg(QDir::toNativeSeparators(sourceArchive)));
            return;
        }
        dialog.selectZoneById(zoneId);
        dialog.exec();
        processMapTableSaveReports(dialog.saveReports());
    });
    connect(m_mapsBrowser, &CMapsBrowser::duplicateMapAsCollectionRequested,
            this, &MainWindow::duplicateMapAsCollection);
    connect(m_mapsBrowser, &CMapsBrowser::openCollectionRequested,
            this, &MainWindow::openCollection);
    connect(m_mapsBrowser, &CMapsBrowser::editCollectionRequested,
            this, QOverload<const QString&>::of(&MainWindow::editCollection));
    connect(m_mapsBrowser, &CMapsBrowser::duplicateCollectionRequested,
            this, &MainWindow::duplicateCollection);
    connect(m_mapsBrowser, &CMapsBrowser::saveCollectionAsRequested,
            this, &MainWindow::saveCollectionAs);
    connect(m_mapsBrowser, &CMapsBrowser::deleteCollectionRequested,
            this, &MainWindow::deleteCollection);
    connect(m_mapsBrowser, &CMapsBrowser::newCollectionRequested,
            this, &MainWindow::newCollection);
    connect(m_mapsBrowser, &CMapsBrowser::refreshRequested,
            this, [this]() { refreshActiveCatalog(true); });
    m_externalScriptAction = new QAction(tr("Open MOB Script in External Editor"), this);
    m_ui->menuEdit->addAction(m_externalScriptAction);
    connect(m_externalScriptAction, &QAction::triggered, this, [this]()
    {
        if(m_pView && m_pView->hasActiveMob())
            m_pView->openExternalMobScript(m_pView->currentMob());
    });
    QMenu* objectEditMenu = m_ui->menuEdit->addMenu(tr("Object"));
    m_convertWorldObjectToLeverAction = objectEditMenu->addAction(
        tr("Convert World Object to Lever..."));
    m_convertWorldObjectToLeverAction->setEnabled(false);
    m_convertWorldObjectToLeverAction->setToolTip(
        tr("Converts one selected basic world object into an interactable lever as one undoable edit."));
    connect(m_convertWorldObjectToLeverAction, &QAction::triggered, this, [this]() {
        if(m_pView)
            m_pView->convertSelectedWorldObjectToLever();
    });
    m_recentFilesMenu = new QMenu(tr("Open Recent"), this);
    m_ui->menuFile->insertMenu(m_ui->actionOpen_recent, m_recentFilesMenu);
    m_ui->actionCreate_new_object->setEnabled(false);

    QMenu* collectionMenu = m_ui->menuFile->addMenu(tr("Collections"));
    QAction* newCollectionAction = collectionMenu->addAction(tr("New Collection..."));
    QAction* openCollectionAction = collectionMenu->addAction(tr("Open Collection..."));
    QAction* editCollectionAction = collectionMenu->addAction(tr("Edit Collection..."));
    collectionMenu->addSeparator();
    QAction* saveCollectionAction = collectionMenu->addAction(tr("Save Collection"));
    QAction* saveCollectionAsAction = collectionMenu->addAction(tr("Save Collection As..."));
    connect(newCollectionAction, &QAction::triggered, this, &MainWindow::newCollection);
    connect(openCollectionAction, &QAction::triggered, this, [this]()
    {
        QDir().mkpath(CCollectionDocument::defaultDirectory());
        const QString path = QFileDialog::getOpenFileName(this, tr("Open Collection"),
            CCollectionDocument::defaultDirectory(), tr("Endurance Collection (*.collection)"));
        if(!path.isEmpty()) openCollection(path);
    });
    connect(editCollectionAction, &QAction::triggered,
            this, QOverload<>::of(&MainWindow::editCollection));
    connect(saveCollectionAction, &QAction::triggered, this, [this]() { saveCollection(false); });
    connect(saveCollectionAsAction, &QAction::triggered, this, [this]() { saveCollection(true); });

    QAction* openBackupFolder = new QAction(tr("Open Backup Folder"), this);
    QAction* openLogsFolder = new QAction(tr("Open Logs Folder"), this);
    QAction* openTestRunsFolder = new QAction(tr("Open JMV Test Runs Folder"), this);
    QAction* openBugReportingGuide = new QAction(tr("Open Bug Reporting Guide"), this);
    QAction* gameAndModSetup = new QAction(tr("Game and Mod Setup..."), this);
    QAction* compareMobFiles = new QAction(tr("Compare MOB Files..."), this);
    QAction* showUnitTexture = new QAction(tr("Show Selected Unit Final Texture..."), this);
    QAction* captureShadowDiagnostics = new QAction(tr("Capture Shadow Diagnostics..."), this);
    QAction* validateMobFamily = new QAction(tr("Validate Active MOB Family"), this);
    QAction* validateCurrentMob = new QAction(tr("Validate Current MOB Only"), this);
    QAction* validateSelectedMobs = new QAction(tr("Validate Selected MOB Files..."), this);
    QAction* repairMobFamily = new QAction(tr("Review and Repair Object IDs..."), this);
    QAction* openMapCatalog = new QAction(tr("Map Catalog Editor..."), this);
    QAction* openDatabaseEditor = new QAction(tr("Database Editor..."), this);
    QAction* openItemSpellConstructor = new QAction(tr("Item / Spell Constructor..."), this);
    openItemSpellConstructor->setObjectName(QStringLiteral("openItemSpellConstructorAction"));
    QAction* generateMinimapAction = new QAction(tr("Export Minimap..."), this);
    QAction* persistAiGraphAction = new QAction(tr("Bake / Export AI Graph..."), this);
    m_mapTestAction = new QAction(tr("Test Current Map"), this);
    m_mapTestAction->setToolTip(tr("Stages unique temporary MPR/MOB copies and launches the active validated runtime profile. JMV uses hash-owned temporary aliases in the mod Maps directory; source maps are never overwritten."));
    m_stopMapTestAction = new QAction(tr("Stop Test"), this);
    m_stopMapTestAction->setEnabled(false);
    m_stopMapTestAction->setToolTip(tr("Stops only the game process launched by Endurance."));
    m_ui->menuSettings->insertAction(m_ui->actionOpen_2, gameAndModSetup);
    m_ui->menuSettings->insertSeparator(m_ui->actionOpen_2);
    m_ui->menuTools->addSeparator();
    m_ui->menuTools->addAction(compareMobFiles);
    m_ui->menuTools->addAction(validateMobFamily);
    m_ui->menuTools->addAction(validateCurrentMob);
    m_ui->menuTools->addAction(validateSelectedMobs);
    m_ui->menuTools->addAction(repairMobFamily);
    m_ui->menuTools->addAction(openMapCatalog);
    m_ui->menuTools->addAction(openDatabaseEditor);
    m_ui->menuTools->addAction(openItemSpellConstructor);
    m_ui->menuTools->addAction(generateMinimapAction);
    m_ui->menuTools->addAction(persistAiGraphAction);
    m_ui->menuTools->addAction(m_mapTestAction);
    m_ui->menuTools->addAction(m_stopMapTestAction);
    m_ui->menuTools->addAction(openBackupFolder);
    m_ui->menuTools->addAction(openLogsFolder);
    m_ui->menuTools->addAction(captureShadowDiagnostics);
    m_ui->menuTools->addAction(openTestRunsFolder);
    m_ui->menuTools->addAction(openBugReportingGuide);
    viewMenu->addAction(showUnitTexture);

    m_mapsHiddenTab = new CDockHiddenTab(this, m_mapsDock,
        tr("Maps && Collections"), QStringLiteral("mapsAndCollectionsHiddenTab"));
    m_messageLogHiddenTab = new CDockHiddenTab(this, m_messageLogDock,
        tr("Message Log"), QStringLiteral("messageLogHiddenTab"));
    connect(openMapCatalog, &QAction::triggered, this, [this]()
    {
        CMapTableEditorDialog dialog(this);
        dialog.exec();
        processMapTableSaveReports(dialog.saveReports());
    });
    connect(openDatabaseEditor, &QAction::triggered, this,
            [this]() { showDatabaseEditor(); });
    connect(openItemSpellConstructor, &QAction::triggered, this,
            [this]() { showItemSpellConstructor(); });
    connect(generateMinimapAction, &QAction::triggered,
            this, &MainWindow::generateMinimap);
    connect(persistAiGraphAction, &QAction::triggered,
            this, &MainWindow::persistAiGraph);
    connect(openBackupFolder, &QAction::triggered, this, [this]()
    {
        QString error;
        if(!CBackupManager::openBackupFolder(&error))
            QMessageBox::warning(this, tr("Endurance backups"), error);
    });
    connect(openLogsFolder, &QAction::triggered, this, [this]()
    {
        const QString path = QDir::current().absoluteFilePath("logs");
        QDir().mkpath(path);
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this, tr("Endurance logs"),
                tr("Could not open the logs folder:\n%1").arg(path));
    });
    connect(captureShadowDiagnostics, &QAction::triggered, this, [this]()
    {
        if(!m_pView)
            return;
        QString error;
        const QString path = m_pView->captureShadowDiagnostics(&error);
        if(path.isEmpty())
        {
            QMessageBox::warning(this, tr("Shadow diagnostics"), error);
            return;
        }
        QMessageBox::information(this, tr("Shadow diagnostics"),
            tr("Shadow diagnostics captured. Send the entire folder:\n%1").arg(path));
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(openTestRunsFolder, &QAction::triggered, this, [this]()
    {
        const QString path = CJmvMapTestLauncher::testRunsRootPath();
        QDir().mkpath(path);
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this, tr("JMV test runs"),
                tr("Could not open the JMV test-runs folder:\n%1").arg(path));
    });
    connect(openBugReportingGuide, &QAction::triggered, this, [this]()
    {
        QString path = QDir::current().absoluteFilePath("BUG_REPORTING.md");
        if(!QFileInfo::exists(path))
            path = QDir::current().absoluteFilePath("docs/BUG_REPORTING.md");
        if(!QFileInfo::exists(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this, tr("Endurance bug reports"),
                tr("BUG_REPORTING.md was not found beside the editor executable."));
    });
    connect(gameAndModSetup, &QAction::triggered, this, &MainWindow::showGameAndModSetup);
    connect(compareMobFiles, &QAction::triggered, this, [this]()
    {
        activateWindow();
        raise();
        QString startDirectory;
        if(COptString* lastFolder = dynamic_cast<COptString*>(m_settings->opt("lastVisitedFolder")))
            startDirectory = lastFolder->value();
        const QString baseline = QFileDialog::getOpenFileName(this, tr("Select baseline MOB"),
            startDirectory, tr("Evil Islands MOB (*.mob)"));
        if(baseline.isEmpty())
            return;
        const QString candidate = QFileDialog::getOpenFileName(this, tr("Select candidate MOB"),
            QFileInfo(baseline).absolutePath(), tr("Evil Islands MOB (*.mob)"));
        if(candidate.isEmpty())
            return;
        CMobDiffDialog* dialog = new CMobDiffDialog(QFileInfo(baseline), QFileInfo(candidate), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    });
    connect(showUnitTexture, &QAction::triggered,
            this, &MainWindow::showUnitTexturePreview);
    connect(validateMobFamily, &QAction::triggered, this, &MainWindow::showMobFamilyValidation);
    connect(validateCurrentMob, &QAction::triggered, this, [this]()
    {
        if(!m_pView || !m_pView->currentMob())
        {
            QMessageBox::information(this, tr("MOB validation"), tr("No active MOB is loaded."));
            return;
        }
        SMobFamilyValidationRequest request;
        request.familyLabel = m_pView->currentMob()->filePath().fileName();
        request.profileId = activeResourceProfile().profileId;
        request.explicitComposition = true;
        request.includeProfileWideMobs = false;
        request.mapRoots = activeMapRoots().toList();
        request.database = ei::ProfileDatabaseContext::instance().current();
        const QString path = m_pView->currentMob()->filePath().absoluteFilePath();
        request.seedMobPaths << path;
        QByteArray data;
        CValidationReport candidateReport;
        if(m_pView->currentMob()->buildValidatedCandidate(&data, &candidateReport, nullptr))
            request.inMemoryMobData.insert(QDir::cleanPath(path).toLower(), data);
        m_familyValidation = CMobFamilyValidator::validate(request);
        updateMobFamilyValidationPanel();
        m_validationDock->show();
        m_validationDock->raise();
        ei::log(m_familyValidation.criticalCount() ? eLogWarning : eLogInfo,
                m_familyValidation.toText());
    });
    connect(validateSelectedMobs, &QAction::triggered, this, [this]()
    {
        const QString start = activeMapRoots().isEmpty() ? QString()
            : activeMapRoots().first();
        const QStringList files = QFileDialog::getOpenFileNames(this,
            tr("Validate Selected MOB Files"), start, tr("MOB files (*.mob)"));
        if(files.isEmpty())
            return;
        SMobFamilyValidationRequest request;
        request.familyLabel = tr("selected files");
        request.profileId = activeResourceProfile().profileId;
        request.explicitComposition = true;
        request.includeProfileWideMobs = false;
        request.seedMobPaths = files;
        request.mapRoots = activeMapRoots().toList();
        request.database = ei::ProfileDatabaseContext::instance().current();
        m_familyValidation = CMobFamilyValidator::validate(request);
        updateMobFamilyValidationPanel();
        m_validationDock->show();
        m_validationDock->raise();
        ei::log(m_familyValidation.criticalCount() ? eLogWarning : eLogInfo,
                m_familyValidation.toText());
    });
    connect(repairMobFamily, &QAction::triggered, this, &MainWindow::reviewAndRepairNidCollisions);
    connect(m_mapTestAction, &QAction::triggered, this, [this]()
    {
        launchCurrentMapTest();
    });
    connect(m_stopMapTestAction, &QAction::triggered, this, [this]()
    {
        if(m_mapTestLauncher && m_mapTestLauncher->isRunning() &&
           QMessageBox::question(this, tr("Stop JMV test"),
                tr("Stop the game process launched by Endurance?")) == QMessageBox::Yes)
            m_mapTestLauncher->stop();
    });

    const bool firstRunSetup = !m_settings->hasConfiguredResources();
    // Complete first-run setup before constructing the OpenGL view. Opening the
    // wizard later allowed initializeGL() to run against empty legacy settings.
    if(firstRunSetup)
        showGameAndModSetup();

    m_undoStack = new QUndoStack(this);
    int undoHistoryLimit = 512;
    if(COptInt* configuredLimit = dynamic_cast<COptInt*>(m_settings->opt(eOptSetGeneral, "undoHistoryLimit")))
        undoHistoryLimit = qBound(1, configuredLimit->value(), 1000000);
    m_undoStack->setUndoLimit(undoHistoryLimit);
    ei::log(eLogInfo, QString("Undo history limit:%1").arg(undoHistoryLimit));
    createUndoView();
    viewMenu->insertAction(openValidationPanel, m_historyDock->toggleViewAction());

    QSettings uiSettings;
    const QByteArray savedMainWindowState =
            uiSettings.value("ui/mainWindowState").toByteArray();
    const bool initializeDefaultDockLayout = firstRunSetup && savedMainWindowState.isEmpty();
    restoreGeometry(uiSettings.value("ui/mainWindowGeometry").toByteArray());
    restoreState(savedMainWindowState);
    ei::restoreFloatingDockWindowState(m_messageLogDock);
    ei::restoreFloatingDockWindowState(m_validationDock);
    m_ui->workspaceSplitter->restoreState(uiSettings.value("ui/workspaceSplitter").toByteArray());
    m_ui->outlinerSplitter->restoreState(uiSettings.value("ui/outlinerSplitter").toByteArray());
    // QMainWindow's opaque state reliably restores ownership and tab topology,
    // but Qt 5.12 can collapse the other dock columns to their minimum widths
    // after a floating tab group is reconstructed. Restore explicit extents
    // once the shown window has its final screen geometry.
    QTimer::singleShot(0, this, &MainWindow::restoreMainDockExtents);
    // Landscape editing is an explicit working mode, not session layout.
    // Always start closed while preserving the user's last sculpt controls.
    m_landscapeToolsDock->hide();
    m_landscapeTools->setEditingEnabled(false, false);
    if(initializeDefaultDockLayout)
    {
        // A new profile must start from the documented usable workspace even
        // if Windows still contains dock state from an older/test install.
        m_mapsDock->setFloating(false);
        m_loadedMapDock->setFloating(false);
        m_outlinerDock->setFloating(false);
        m_propertiesDock->setFloating(false);
        addDockWidget(Qt::LeftDockWidgetArea, m_mapsDock);
        addDockWidget(Qt::RightDockWidgetArea, m_loadedMapDock);
        addDockWidget(Qt::RightDockWidgetArea, m_outlinerDock);
        splitDockWidget(m_loadedMapDock, m_outlinerDock, Qt::Vertical);
        splitDockWidget(m_outlinerDock, m_propertiesDock, Qt::Vertical);
        m_mapsDock->show();
        m_loadedMapDock->show();
        m_outlinerDock->show();
        m_propertiesDock->show();
        m_messageLogDock->setFloating(false);
        addDockWidget(Qt::BottomDockWidgetArea, m_messageLogDock);
        m_messageLogDock->show();
        uiSettings.setValue("ui/mapsBrowser/mapsExpanded", true);
        uiSettings.setValue("ui/mapsBrowser/collectionsExpanded", true);
    }

    m_pView = new CView(m_ui->centralWidget, m_createDialog.get()->viewWidget());
    const QMetaObject::Connection databasePrototypeNavigation = connect(
        m_pView, &CView::openDatabasePrototypeRequested,
        this, &MainWindow::showDatabaseEditor);
    Q_ASSERT(databasePrototypeNavigation);
    ei::log(databasePrototypeNavigation ? eLogInfo : eLogWarning,
            QString("Database prototype MainWindow route connected=%1 view=%2")
                .arg(bool(databasePrototypeNavigation))
                .arg(reinterpret_cast<quintptr>(m_pView), 0, 16));
    m_mapTestLauncher = new CJmvMapTestLauncher(this);
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::testStarted, this,
            [this](const QString& runId, const QString& mapName)
    {
        ei::log(eLogInfo, QString("JMV direct map test started run:%1 staged-map:%2").arg(runId, mapName));
        statusBar()->showMessage(tr("JMV direct map test is waiting for the normal menu lifecycle, then will load the staged map automatically."));
        updateMapTestAction();
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::testFinished, this,
            [this](const QString& runId, int exitCode, const QString& cleanupMessage)
    {
        closeHotReloadProgress();
        ei::log(eLogInfo, QString("JMV test launch finished run:%1 exit:%2 cleanup:%3")
                .arg(runId).arg(exitCode).arg(cleanupMessage));
        statusBar()->showMessage(tr("JMV direct map test finished (exit code %1). Temporary files were cleaned up.").arg(exitCode), 8000);
        updateMapTestAction();
        if(m_closeAfterTestStop)
        {
            m_closeAfterTestStop = false;
            QTimer::singleShot(0, this, &MainWindow::close);
        }
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::testReportReady,
            this, [this](const QJsonObject& report)
    {
        // The report is emitted immediately before the final lifecycle signal.
        // Let that sequence settle before showing the non-intrusive toast.
        QTimer::singleShot(100, this, [this, report]()
        {
            showMapTestResultNotification(report);
        });
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::hotReloadReady, this, [this]()
    {
        statusBar()->showMessage(tr("JMV map test is ready for Ctrl+F5 hot reload."), 6000);
        updateMapTestAction();
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::hotReloadFinished, this,
            [this](quint64, bool success, const QString& message)
    {
        closeHotReloadProgress();
        ei::log(success ? eLogInfo : eLogWarning, message);
        statusBar()->showMessage(message, 10000);
        updateMapTestAction();
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::testError, this,
            [this](const QString& message)
    {
        closeHotReloadProgress();
        ei::log(eLogFatal, message);
        QMessageBox::critical(this, tr("JMV test launch"), message);
        updateMapTestAction();
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::recoveryStatusChanged, this,
            [this](const QString& message, bool pending)
    {
        if(pending)
        {
            statusBar()->showMessage(tr("Waiting for an interrupted Endurance test game to exit; cleanup will run automatically."));
            if(!message.isEmpty())
                ei::log(eLogWarning, message);
        }
        else
        {
            if(!message.isEmpty())
            {
                statusBar()->showMessage(tr("Interrupted test-run cleanup completed."), 8000);
                ei::log(eLogInfo, message);
            }
        }
        updateMapTestAction();
    });
    m_mapTestLauncher->refreshInterruptedRecovery();
    QString interruptedTest;
    if(CJmvMapTestLauncher::hasActiveInterruptedRun(&interruptedTest))
    {
        QTimer::singleShot(0, this, [this, interruptedTest]()
        {
            QMessageBox prompt(QMessageBox::Warning,
                tr("Interrupted map test is still running"),
                tr("Endurance found a verified game process left by an interrupted map-test session.\n\n%1\n\n"
                   "Close it now? Any unsaved progress in that game process will be lost. "
                   "If you leave it running, Endurance will monitor it and clean up automatically after it exits.")
                    .arg(interruptedTest),
                QMessageBox::NoButton, this);
            QPushButton* closeGame = prompt.addButton(tr("Close Game"), QMessageBox::AcceptRole);
            prompt.addButton(tr("Leave Running"), QMessageBox::RejectRole);
            prompt.exec();
            if(prompt.clickedButton() != closeGame)
                return;
            QString error;
            if(!CJmvMapTestLauncher::requestCloseActiveInterruptedRun(&error))
                QMessageBox::warning(this, tr("Interrupted map test"), error);
            else
                statusBar()->showMessage(tr("Close requested. Waiting for game exit and automatic cleanup."));
        });
    }
    m_createDialog->viewWidget()->attachView(m_pView);
    m_createDialog->viewWidget()->attachSettings(m_settings.get());

    m_pView->setObjectName(QString::fromUtf8("myGLWidget"));
    QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);
    sizePolicy.setHeightForWidth(m_pView->sizePolicy().hasHeightForWidth());
    m_pView->setSizePolicy(sizePolicy);
    m_pView->setMinimumSize(QSize(200, 200));
    m_pView->setMouseTracking(true);
    createViewportToolbars();
    m_viewportMinimap = new CViewportMinimapWidget(m_pView);
    m_viewportMinimap->hide();
    m_minimapRefreshTimer = new QTimer(this);
    m_minimapRefreshTimer->setSingleShot(true);
    connect(m_minimapRefreshTimer, &QTimer::timeout,
            this, &MainWindow::refreshViewportMinimap);
    m_minimapCameraTimer = new QTimer(this);
    m_minimapCameraTimer->setInterval(75);
    connect(m_minimapCameraTimer, &QTimer::timeout, this, [this]() {
        if(!m_pView || !m_viewportMinimap
           || !CLandscape::getInstance()->isMprLoad())
            return;
        const double worldWidth = CLandscape::getInstance()->widthInWorldUnits();
        const double worldHeight = CLandscape::getInstance()->heightInWorldUnits();
        if(worldWidth <= 0.0 || worldHeight <= 0.0) return;
        QPolygonF normalized;
        for(const QPointF& point : m_pView->minimapCameraFootprint())
            normalized << QPointF(point.x() / worldWidth,
                                  1.0 - point.y() / worldHeight);
        m_viewportMinimap->setCameraFootprint(normalized);
    });
    m_minimapCameraTimer->start();
    connect(m_viewportMinimap, &CViewportMinimapWidget::exportRequested,
            this, &MainWindow::generateMinimap);
    connect(m_viewportMinimap, &CViewportMinimapWidget::sizeModeChanged,
            this, &MainWindow::positionViewportMinimap);
    connect(m_viewportMinimap, &CViewportMinimapWidget::regenerationRequested,
            this, [this]() { scheduleViewportMinimapRefresh(0, true); });
    connect(m_viewportMinimap, &CViewportMinimapWidget::generationLockRequested,
            this, &MainWindow::setMinimapGenerationLocked);
    connect(m_viewportMinimap, &CViewportMinimapWidget::cameraMoveRequested,
            m_pView, &CView::moveCameraToMinimapPoint);
    connect(m_viewportMinimap, &CViewportMinimapWidget::sourceModeChanged,
            this, [this](bool generated) {
        statusBar()->showMessage(generated
            ? tr("Generated minimap preview will be staged automatically for Test Current Map.")
            : tr("Embedded minimap selected; Test Current Map will use the active profile texture."),
            5000);
    });
    connect(m_pView, &CView::loadedMapStateChanged,
            this, [this]() { scheduleViewportMinimapRefresh(); });
    connect(m_pView, &CView::aiGraphPersistenceRequested,
            this, &MainWindow::persistAiGraph);
    connect(m_pView, &CView::mobStateChanged,
            this, [this]() { scheduleViewportMinimapRefresh(); });
    connect(m_pView, &CView::updateMainWindowTitle,
            this, [this](eTitleTypeData type, const QString&) {
        if(type == eTitleTypeData::eTitleTypeDataMprDirtyFlag
           || type == eTitleTypeData::eTitleTypeDataDirtyFlag)
            scheduleViewportMinimapRefresh();
    });
    m_ui->workspaceSplitter->setStretchFactor(0, 1);
    if(firstRunSetup)
    {
        // Do not carry a cramped, partially initialized splitter state into the
        // first usable session after setup.
        resizeDocks(QList<QDockWidget*>() << m_outlinerDock << m_propertiesDock,
                    QList<int>() << 420 << 300, Qt::Vertical);
    }

#ifndef QT_DEBUG
  m_ui->toolButton_2->hide();
#endif

    m_selector->attachParents(m_pView);
    m_randomizeForm->attachView(m_pView);
    // View construction and reparenting can dispatch events immediately. Do
    // not let eventFilter() inspect constructor-owned widgets until every
    // pointer it uses (notably m_undoView) has been initialized.
    m_pView->installEventFilter(this);
    CStatusConnector::getInstance()->attach(m_ui->statusIco, m_ui->statusBar);
    connectUiButtons();
    m_pView->attach(m_settings.get(), m_ui->tableWidget, m_undoStack, m_ui->progressBar, m_ui->mousePosText, m_ui->treeWidget, m_pTileForm.get());
    m_pView->attachLandscapeTools(m_landscapeTools);
    m_ui->actionMap_parameters->setText(tr("MPR Settings..."));
    m_ui->tileBrushButton->setText(tr("Landscape Tools"));
    m_ui->tileBrushButton->setIcon(*ei::icon("landscape.ico"));
    m_ui->tileBrushButton->setIconSize(QSize(32, 32));
    m_ui->tileBrushButton->setToolTip(tr("Landscape tools (L)"));
    connect(m_pView, &CView::mobStateChanged, this, [this]()
    {
        updateLoadedMapPanel();
        bool hasDirtyMob = false;
        for(CMob* mob : m_pView->mobs())
            hasDirtyMob = hasDirtyMob || (mob && mob->isDirty());
        if(!hasDirtyMob)
            scheduleMobFamilyValidation(2500);
        else
        {
            // Dirty MOB validation deferred until explicit refresh. Building
            // a validated in-memory candidate is GUI-owned and used to pause
            // large Behavior-editing sessions every time the debounce fired.
            if(m_validationTimer)
                m_validationTimer->stop();
            m_lastValidationRequestDigest.clear();
            if(m_validationSummary)
                m_validationSummary->setText(tr(
                    "Validation pending for edited MOB data. Open or refresh MOB Family Validation to run it."));
        }
    });
    connect(m_pView, &CView::loadedMapStateChanged,
            this, &MainWindow::updateLoadedMapPanel);
    connect(m_pView, &CView::updateMsg, this, [this](const QString& message)
    {
        statusBar()->showMessage(message, 5000);
    });
    connect(m_pView, &CView::nidChangeProposed, this, &MainWindow::notifyProposedNid);
    connect(m_pView, &CView::nidAllocationRequested, this,
            [this](const QString& mobPath, quint32* suggestion)
    {
        if(!suggestion)
            return;
        bool familyContainsOwner = false;
        for(const SMobFamilyMember& member : m_familyValidation.members)
            familyContainsOwner = familyContainsOwner || QDir::cleanPath(member.path).compare(
                QDir::cleanPath(mobPath), Qt::CaseInsensitive) == 0;
        if(!familyContainsOwner)
            m_familyValidation = CMobFamilyValidator::validate(mobFamilyValidationRequest());
        quint32 familySuggestion = 0;
        bool shared = false;
        if(m_familyValidation.suggestReplacementNid(mobPath, &familySuggestion, &shared,
                                                     m_sessionAllocatedNids))
        {
            *suggestion = familySuggestion;
            m_sessionAllocatedNids.insert(familySuggestion);
            ei::log(shared ? eLogWarning : eLogInfo,
                QString("Allocated family-aware NID %1 for %2%3")
                    .arg(familySuggestion).arg(QFileInfo(mobPath).fileName())
                    .arg(shared ? " inside a legacy-shared range" : ""));
            const QString foldedMob = QDir::cleanPath(mobPath).toLower();
            if(shared && !m_warnedSharedAllocationMobs.contains(foldedMob))
            {
                m_warnedSharedAllocationMobs.insert(foldedMob);
                QMessageBox::warning(this, tr("Shared MOB ID range"),
                    tr("Every available ID in %1's effective range is also declared by another "
                       "runtime-family MOB. Endurance selected unused NID %2 and will continue, "
                       "but the family ranges should eventually be reviewed.")
                        .arg(QFileInfo(mobPath).fileName()).arg(familySuggestion));
            }
        }
    }, Qt::DirectConnection);
    initShortcuts();
    QAction* viewportOnlyAction = new QAction(tr("Maximize viewport"), this);
    viewportOnlyAction->setObjectName("actionViewportOnly");
    viewportOnlyAction->setShortcut(QKeySequence(Qt::Key_F11));
    viewportOnlyAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(viewportOnlyAction);
    connect(viewportOnlyAction, &QAction::triggered, this, [this, viewportOnlyAction]()
    {
        const bool entering = !property("enduranceViewportOnly").toBool();
        setProperty("enduranceViewportOnly", entering);
        viewportOnlyAction->setText(entering
            ? tr("Restore editor UI")
            : tr("Maximize viewport"));
        if(entering)
        {
            setProperty("enduranceViewportSplitterState",
                        m_ui->workspaceSplitter->saveState());
            setProperty("enduranceMenuVisible", menuBar()->isVisible());
            setProperty("enduranceStatusVisible", statusBar()->isVisible());
            for(QDockWidget* dock : findChildren<QDockWidget*>())
            {
                dock->setProperty("enduranceWasVisible", dock->isVisible());
                dock->hide();
            }
            for(QToolBar* toolbar : findChildren<QToolBar*>())
            {
                toolbar->setProperty("enduranceWasVisible", toolbar->isVisible());
                toolbar->hide();
            }
            menuBar()->hide();
            statusBar()->hide();
            m_ui->workspaceSplitter->setSizes({1, 0});
            if(QWidget* toolbar = m_ui->workspaceContainer->findChild<QWidget*>(
                    "mapTestToolbar", Qt::FindDirectChildrenOnly))
            {
                toolbar->setProperty("enduranceWasVisible", toolbar->isVisible());
                toolbar->hide();
            }
            if(QWidget* toolbar = m_ui->workspaceContainer->findChild<QWidget*>(
                    "viewportOperationToolbar", Qt::FindDirectChildrenOnly))
            {
                toolbar->setProperty("enduranceWasVisible", toolbar->isVisible());
                toolbar->hide();
            }
        }
        else
        {
            const QByteArray splitterState = property(
                "enduranceViewportSplitterState").toByteArray();
            if(!splitterState.isEmpty())
                m_ui->workspaceSplitter->restoreState(splitterState);
            for(QDockWidget* dock : findChildren<QDockWidget*>())
                if(dock->property("enduranceWasVisible").toBool())
                    dock->show();
            for(QToolBar* toolbar : findChildren<QToolBar*>())
                if(toolbar->property("enduranceWasVisible").toBool())
                    toolbar->show();
            menuBar()->setVisible(property("enduranceMenuVisible").toBool());
            statusBar()->setVisible(property("enduranceStatusVisible").toBool());
            if(QWidget* toolbar = m_ui->workspaceContainer->findChild<QWidget*>(
                    "mapTestToolbar", Qt::FindDirectChildrenOnly))
                toolbar->setVisible(toolbar->property("enduranceWasVisible").toBool());
            if(QWidget* toolbar = m_ui->workspaceContainer->findChild<QWidget*>(
                    "viewportOperationToolbar", Qt::FindDirectChildrenOnly))
                toolbar->setVisible(toolbar->property("enduranceWasVisible").toBool());
        }
        if(m_pView)
            m_pView->setFocus();
        if(m_mapsHiddenTab) m_mapsHiddenTab->refresh();
        if(m_messageLogHiddenTab) m_messageLogHiddenTab->refresh();
    });
    QAction* fullScreenAction = new QAction(tr("Toggle fullscreen"), this);
    fullScreenAction->setObjectName("actionToggleFullscreen");
    fullScreenAction->setShortcut(QKeySequence(Qt::Key_F12));
    fullScreenAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(fullScreenAction);
    connect(fullScreenAction, &QAction::triggered, this, [this]()
    {
        if(isFullScreen())
        {
            const Qt::WindowStates previous = Qt::WindowStates(
                property("endurancePreviousWindowState").toInt());
            showNormal();
            if(previous.testFlag(Qt::WindowMaximized))
                showMaximized();
        }
        else
        {
            setProperty("endurancePreviousWindowState", int(windowState()));
            showFullScreen();
        }
        if(m_pView)
            m_pView->setFocus();
    });
    connect(m_settings.get(), &CSettings::shortcutsChanged, this, &MainWindow::initShortcuts);
    connect(m_settings.get(), &CSettings::uiScaleApplied, this, [this](int percent)
    {
        ei::applyUiScalePercent(percent);
        if(m_pView)
            m_pView->refreshUiScale();
        updateGeometry();
        update();
    });
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setTimerType(Qt::PreciseTimer);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::beginAutosaveCountdown);
    connect(m_settings.get(), &CSettings::autosaveSettingsApplied,
            this, static_cast<void (MainWindow::*)(bool, int)>(
                &MainWindow::configureAutosaveTimer));
    connect(m_settings.get(), &CSettings::autosaveRequested,
            this, &MainWindow::performAutosave);
    connect(m_settings.get(), &CSettings::backupFolderRequested, this, [this]()
    {
        QString error;
        if(!CBackupManager::openBackupFolder(&error))
            QMessageBox::warning(this, tr("Endurance backups"), error);
    });
    configureAutosaveTimer();
    m_ui->action_Mob_parameters->setEnabled(false);
    m_ui->actionOpen_MOB_Script->setEnabled(false);
    m_externalScriptAction->setEnabled(false);
    m_ui->actionSave->setEnabled(false);
    m_ui->actionSave_as->setEnabled(false);
    m_ui->actionSave_all_MOB_s->setEnabled(false);
    QObject::connect(m_pView, SIGNAL(updateMainWindowTitle(eTitleTypeData,QString)), this, SLOT(updateWindowTitle(eTitleTypeData,QString)));
    QObject::connect(CScene::getInstance(), SIGNAL(modeChanged()), m_pView, SLOT(viewParameters()));
    connect(m_logicSlotCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int comboIndex)
    {
        if(m_pView)
            m_pView->setLogicSlotIndex(m_logicSlotCombo->itemData(comboIndex).toInt());
    });
    connect(CScene::getInstance(), &CScene::modeChanged, this, [this]()
    {
        if(m_pView)
            m_pView->collectObjectTreeData();
        updateLogicSlotBarVisibility();
        updateBehaviorModeUi();
    });
    QObject::connect(m_pView, SIGNAL(showOutlinerSignal(bool)), this, SLOT(showOutliner(bool)));
    connect(m_pView, &CView::showLandscapeToolsSignal, this, [this]()
    {
        showLandscapeTools();
    });
    connect(m_pView, &CView::hideLandscapeToolsSignal,
            this, &MainWindow::hideLandscapeTools);
    const auto applyLandscapeVisibility = [this](bool visible)
    {
        if(m_landscapeToolsAction)
        {
            const QSignalBlocker blocker(m_landscapeToolsAction);
            m_landscapeToolsAction->setChecked(visible);
        }
        if(QAction* operationAction = m_operationActions.value(EButtonOpTilebrush, nullptr))
        {
            const QSignalBlocker blocker(operationAction);
            operationAction->setChecked(visible);
        }
        if(m_ui->tileBrushButton)
        {
            const QSignalBlocker blocker(m_ui->tileBrushButton);
            m_ui->tileBrushButton->setChecked(visible);
        }
        if(m_ui->selectButton) m_ui->selectButton->setVisible(!visible);
        if(m_ui->moveButton) m_ui->moveButton->setVisible(!visible);
        if(m_ui->rotateButton) m_ui->rotateButton->setVisible(!visible);
        if(m_ui->scaleButton) m_ui->scaleButton->setVisible(!visible);
        if(!m_pView)
            return;
        if(!visible)
        {
            // Layer isolation belongs to Landscape Tools. Closing it restores
            // the ordinary viewport instead of leaving terrain hidden.
            m_pView->setDrawLand(true);
            m_pView->setDrawWater(true);
        }
        m_pView->setLandscapeEditingEnabled(visible);
        if(visible)
            m_landscapeToolsDock->raise();
        // Like Maps and Collections, hiding Landscape Tools changes only
        // visibility. Qt retains its dock area, split, tab, and floating state.
    };
    connect(m_landscapeToolsDock, &QDockWidget::visibilityChanged, this,
            [this, applyLandscapeVisibility](bool visible)
    {
        const quint64 visibilityGeneration = ++m_landscapeVisibilityGeneration;
        if(visible)
        {
            applyLandscapeVisibility(true);
            return;
        }
        // QDockWidget emits a transient hidden state while changing between
        // docked and floating windows. Deferring the complete hidden-state
        // transition avoids both cancelling the active brush and re-entering
        // Qt's dock layout engine during that native-window move.
        QTimer::singleShot(0, this, [this, visibilityGeneration,
                                    applyLandscapeVisibility]()
        {
            if(visibilityGeneration != m_landscapeVisibilityGeneration
               || !m_pView || !m_landscapeToolsDock
               || m_landscapeToolsDock->isVisible())
                return;
            applyLandscapeVisibility(false);
        });
    });
    connect(m_pView, &CView::selectionChanged,
            this, &MainWindow::updateUnitTexturePreview);
    connect(m_pView, &CView::selectionChanged, this, [this]()
    {
        if(m_customSpawnSelected && !m_pView->selectionSnapshot().isEmpty())
        {
            m_customSpawnSelected = false;
            m_pView->setTestSpawnMarkerSelected(false);
            updateOperationAvailability();
        }
        updateLogicSlotBarVisibility();
        updateOutlinerSelectionPath();
    });
    connect(m_pView, &CView::selectionStateChanged, this,
            [this](bool) { updateLogicSlotBarVisibility(); });


//    m_ui->progressBar->setValue(0);
    m_ui->progressBar->reset();
//    m_ui->progressBar->setVisible(false);

    m_ui->mousePosText->setStyleSheet("* { background-color: rgba(0, 0, 0, 0); }");
    CTextureList::getInstance()->attachSettings(m_settings.get());
    CObjectList::getInstance()->attachSettings(m_settings.get());
    m_createDialog.get()->attach(m_pView, m_undoStack);
    rebuildRecentFilesMenu();
    refreshActiveCatalog(false);
    updateMapTestAction();
    updateWindowTitle(eTitleTypeData::eTitleTypeDataMpr, QString());
    updateLoadedMapPanel();
    QVector<SRecoverableAutosave> recoveries;
    QString backupSessionError;
    CBackupManager backupManager;
    const bool workspaceSmoke = qApp->property("workspaceLoadSmoke").toBool()
        || qApp->property("workspaceVisualSmoke").toBool()
        || qApp->property("workspaceVisualSequence").isValid();
    m_backupSessionStarted = workspaceSmoke ? false : backupManager.beginEditorSession(
        &recoveries, &backupSessionError);
    if(workspaceSmoke)
        ei::log(eLogInfo, "Automated workspace smoke: backup session notifications disabled");
    else if(!m_backupSessionStarted)
        ei::log(eLogWarning, "Editor backup session journal unavailable: "
                + backupSessionError);
    else
    {
        m_recoverableAutosaveNoticeCount = recoveries.size();
        for(const SRecoverableAutosave& recovery : recoveries)
            m_recoverableAutosaveNoticeKeys.append(recovery.notificationKey);
        if(m_recoverableAutosaveNoticeCount > 0)
            QTimer::singleShot(0, this, &MainWindow::showRecoverableAutosaves);
    }
    bool hasStartupDocument = false;
    for(int index = 1; index < QCoreApplication::arguments().size(); ++index)
    {
        const QFileInfo candidate(QCoreApplication::arguments().at(index));
        const QString suffix = candidate.suffix().toLower();
        if(candidate.exists() && (suffix == "mpr" || suffix == "mob"))
        {
            hasStartupDocument = true;
            break;
        }
    }
    if(!firstRunSetup && !hasStartupDocument)
    {
        COptBool* openLastSession = dynamic_cast<COptBool*>(
            m_settings->opt(eOptSetGeneral, "openLastSessionOnStartup"));
        if(openLastSession && openLastSession->value())
        {
            QTimer::singleShot(0, this, [this]()
            {
                ei::log(eLogInfo, "Opening the previous editor session by user preference.");
                loadLastSession();
            });
        }
    }
    if(firstRunSetup)
    {
        // First-run setup can queue a dock-state restoration after the early
        // hide. Landscape Tools is working-mode state, never startup state.
        QTimer::singleShot(0, this, [this]()
        {
            hideLandscapeTools();
            if(m_landscapeTools)
                m_landscapeTools->setEditingEnabled(false, false);
        });
    }
//    if(m_pView->isRecentAvailable())
//        m_ui->actionOpen_recent->setEnabled(true);
}

MainWindow::~MainWindow()
{
    saveUiState();
    delete m_itemSpellConstructorDialog;
    m_itemSpellConstructorDialog=nullptr;
    delete m_databaseEditorDialog;
    m_databaseEditorDialog=nullptr;
    delete m_minimapExportDialog;
    m_minimapExportDialog = nullptr;
    if(m_pView && m_pView->prepareForApplicationShutdown())
        delete m_pView;
    else if(m_pView)
    {
        // A closed native window can lose its OpenGL surface before this
        // destructor runs. Destroying QOpenGL* members then crashes inside Qt.
        // This is application shutdown, so detach the stopped view and allow
        // process teardown to reclaim it instead of invoking unsafe GL cleanup.
        ei::log(eLogWarning,
                "Viewport retained for process teardown because its OpenGL surface is unavailable");
        m_pView->hide();
        m_pView->setParent(nullptr);
    }
    m_pView = nullptr;
    delete m_ui;
    CIconManager::getInstance()->~CIconManager();
}

void MainWindow::showUnitTexturePreview()
{
    if(!m_unitTextureDialog)
    {
        m_unitTextureDialog = new QDialog(this);
        m_unitTextureDialog->setAttribute(Qt::WA_QuitOnClose, false);
        m_unitTextureDialog->setWindowTitle(tr("Final character texture"));
        QVBoxLayout* layout = new QVBoxLayout(m_unitTextureDialog);
        m_unitTextureDetails = new QLabel(m_unitTextureDialog);
        m_unitTextureDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(m_unitTextureDetails);

        QHBoxLayout* controls = new QHBoxLayout;
        QGroupBox* layerGroup = new QGroupBox(tr("Texture layers"), m_unitTextureDialog);
        QVBoxLayout* layerLayout = new QVBoxLayout(layerGroup);
        m_unitTextureLayers = new QListWidget(layerGroup);
        m_unitTextureLayers->setMaximumHeight(180);
        layerLayout->addWidget(m_unitTextureLayers);
        QHBoxLayout* layerButtons = new QHBoxLayout;
        QPushButton* showLayers = new QPushButton(tr("Show all"), layerGroup);
        QPushButton* hideLayers = new QPushButton(tr("Hide all"), layerGroup);
        layerButtons->addWidget(showLayers);
        layerButtons->addWidget(hideLayers);
        layerLayout->addLayout(layerButtons);
        controls->addWidget(layerGroup);

        QGroupBox* partGroup = new QGroupBox(tr("Part UV projections"), m_unitTextureDialog);
        QVBoxLayout* partLayout = new QVBoxLayout(partGroup);
        m_unitTextureParts = new QListWidget(partGroup);
        m_unitTextureParts->setMaximumHeight(180);
        partLayout->addWidget(m_unitTextureParts);
        QHBoxLayout* partButtons = new QHBoxLayout;
        QPushButton* showParts = new QPushButton(tr("Show all"), partGroup);
        QPushButton* hideParts = new QPushButton(tr("Hide all"), partGroup);
        partButtons->addWidget(showParts);
        partButtons->addWidget(hideParts);
        partLayout->addLayout(partButtons);
        controls->addWidget(partGroup);
        layout->addLayout(controls);

        auto setAllChecked = [this](QListWidget* list, bool checked)
        {
            if(!list)
                return;
            m_unitTextureUpdating = true;
            for(int row = 0; row < list->count(); ++row)
                list->item(row)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            m_unitTextureUpdating = false;
            renderUnitTexturePreview();
        };
        connect(showLayers, &QPushButton::clicked, this,
                [setAllChecked, this]() { setAllChecked(m_unitTextureLayers, true); });
        connect(hideLayers, &QPushButton::clicked, this,
                [setAllChecked, this]() { setAllChecked(m_unitTextureLayers, false); });
        connect(showParts, &QPushButton::clicked, this,
                [setAllChecked, this]() { setAllChecked(m_unitTextureParts, true); });
        connect(hideParts, &QPushButton::clicked, this,
                [setAllChecked, this]() { setAllChecked(m_unitTextureParts, false); });
        connect(m_unitTextureLayers, &QListWidget::itemChanged, this,
                [this](QListWidgetItem*)
        {
            if(!m_unitTextureUpdating)
                renderUnitTexturePreview();
        });
        connect(m_unitTextureParts, &QListWidget::itemChanged, this,
                [this](QListWidgetItem*)
        {
            if(!m_unitTextureUpdating)
                renderUnitTexturePreview();
        });

        m_unitTextureImage = new QLabel;
        m_unitTextureImage->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_unitTextureScroll = new QScrollArea(m_unitTextureDialog);
        m_unitTextureScroll->setWidget(m_unitTextureImage);
        m_unitTextureScroll->setWidgetResizable(false);
        layout->addWidget(m_unitTextureScroll, 1);
        QDialogButtonBox* buttons = new QDialogButtonBox(
            QDialogButtonBox::Close, m_unitTextureDialog);
        connect(buttons, &QDialogButtonBox::rejected,
                m_unitTextureDialog, &QDialog::close);
        layout->addWidget(buttons);
        m_unitTextureDialog->resize(1100, 900);
    }
    m_unitTextureDialog->show();
    updateUnitTexturePreview();
    m_unitTextureDialog->raise();
    m_unitTextureDialog->activateWindow();
}

void MainWindow::updateUnitTexturePreview()
{
    if(!m_unitTextureDialog || !m_unitTextureDialog->isVisible())
        return;

    const QList<CNode*> selected = m_pView ? m_pView->selectedNodes() : QList<CNode*>();
    CUnit* unit = selected.size() == 1 ? dynamic_cast<CUnit*>(selected.first()) : nullptr;
    if(!unit)
    {
        m_unitTextureDialog->setWindowTitle(tr("Final character texture"));
        m_unitTextureDetails->setText(tr("Select a single unit to preview its texture and UVs."));
        m_unitTextureImage->clear();
        m_unitTextureImage->resize(0, 0);
        m_unitTextureUpdating = true;
        m_unitTextureLayers->clear();
        m_unitTextureParts->clear();
        m_unitTextureUpdating = false;
        return;
    }

    QStringList layers = unit->renderedBodyTextureLayers();
    if(layers.isEmpty())
        layers.append(unit->renderedPrimaryTextureName());
    const QStringList partNames = unit->renderedBodyPartUvTriangles().keys();
    m_unitTextureUpdating = true;
    m_unitTextureLayers->clear();
    for(const QString& layer : layers)
    {
        QListWidgetItem* item = new QListWidgetItem(layer, m_unitTextureLayers);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    m_unitTextureParts->clear();
    for(const QString& part : partNames)
    {
        QListWidgetItem* item = new QListWidgetItem(part, m_unitTextureParts);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    m_unitTextureUpdating = false;
    renderUnitTexturePreview();
}

void MainWindow::updateLogicSlotBarVisibility()
{
    const bool show = CScene::getInstance()->getMode() == eEditModeLogic;
    QList<CUnit*> units;
    bool unitSelection = show && m_pView;
    if(unitSelection)
    {
        const QList<CNode*> selected = m_pView->selectionSnapshot();
        unitSelection = !selected.isEmpty();
        for(CNode* node : selected)
        {
            CUnit* unit = dynamic_cast<CUnit*>(node);
            if(!unit)
            {
                unitSelection = false;
                units.clear();
                break;
            }
            units.append(unit);
        }
    }
    if(show && m_logicSlotCombo)
    {
        const QSignalBlocker blocker(m_logicSlotCombo);
        m_logicSlotCombo->setItemText(0, tr("Logic 0 (default)"));
        m_logicSlotCombo->setItemData(0, tr(
            "The normal behavior and the reset target for alarm 0. Logic 0 is enabled by default and ignores the Logic enabled setting."),
            Qt::ToolTipRole);
        for(int slot = 1; slot < m_logicSlotCombo->count(); ++slot)
        {
            int enabledCount = 0;
            if(unitSelection)
            {
                for(CUnit* unit : units)
                {
                    QSharedPointer<IPropertyBase> enabled;
                    unit->getLogicParamAt(slot, enabled, eObjParam_LOGIC_USE);
                    const propBool* value = dynamic_cast<const propBool*>(enabled.get());
                    if(value && value->isInit() && value->value())
                        ++enabledCount;
                }
            }
            if(unitSelection)
            {
                const QString state = enabledCount == 0 ? tr("disabled")
                    : (enabledCount == units.size() ? tr("enabled") : tr("mixed"));
                m_logicSlotCombo->setItemText(slot,
                    tr("Logic %1 (alarm %1; selection: %2)").arg(slot).arg(state));
                m_logicSlotCombo->setItemData(slot, tr(
                    "Responds to world alarm %1. Unit selection: %2.").arg(slot).arg(state),
                    Qt::ToolTipRole);
            }
            else
            {
                m_logicSlotCombo->setItemText(slot, tr("Logic %1 (alarm %1)").arg(slot));
                m_logicSlotCombo->setItemData(slot,
                    tr("Responds to world alarm %1, if Logic enabled is on.").arg(slot),
                    Qt::ToolTipRole);
            }
        }
    }
    if(m_logicSlotBar)
        m_logicSlotBar->setVisible(show);
}

void MainWindow::updateBehaviorModeUi()
{
    const bool behaviorMode = CScene::getInstance()->getMode() == eEditModeLogic;
    const QString target = behaviorMode
        ? tr("Switch to Object mode") : tr("Switch to Behavior mode");
    const QString shortcutText = m_ui->actionChange_mod_e->shortcut()
        .toString(QKeySequence::NativeText);
    const QString tooltip = shortcutText.isEmpty()
        ? target : target + " (" + shortcutText + ")";
    {
        const QSignalBlocker actionBlocker(m_ui->actionChange_mod_e);
        m_ui->actionChange_mod_e->setChecked(behaviorMode);
        m_ui->actionChange_mod_e->setText(target);
        m_ui->actionChange_mod_e->setToolTip(tooltip);
        m_ui->actionChange_mod_e->setStatusTip(tooltip);
    }
    if(m_behaviorModeButton)
    {
        const QSignalBlocker buttonBlocker(m_behaviorModeButton);
        m_behaviorModeButton->setChecked(behaviorMode);
        m_behaviorModeButton->setIcon(behaviorModeIcon(behaviorMode,
            m_behaviorModeButton->palette().color(QPalette::ButtonText)));
        m_behaviorModeButton->setText(target);
        m_behaviorModeButton->setToolTip(tooltip);
        m_behaviorModeButton->setAccessibleName(target);
    }
}

void MainWindow::renderUnitTexturePreview()
{
    if(!m_unitTextureDialog || !m_unitTextureDialog->isVisible() || m_unitTextureUpdating)
        return;
    const QList<CNode*> selected = m_pView ? m_pView->selectedNodes() : QList<CNode*>();
    CUnit* unit = selected.size() == 1 ? dynamic_cast<CUnit*>(selected.first()) : nullptr;
    if(!unit)
    {
        updateUnitTexturePreview();
        return;
    }

    QStringList allLayers = unit->renderedBodyTextureLayers();
    if(allLayers.isEmpty())
        allLayers.append(unit->renderedPrimaryTextureName());
    QStringList visibleLayers;
    for(int row = 0; row < m_unitTextureLayers->count(); ++row)
        if(m_unitTextureLayers->item(row)->checkState() == Qt::Checked)
            visibleLayers.append(m_unitTextureLayers->item(row)->text());

    QString diagnostic;
    QImage texture;
    if(!visibleLayers.isEmpty())
        texture = CTextureList::getInstance()->layeredTextureImage(
            visibleLayers, unit->renderedModelName(), &diagnostic);
    else if(!allLayers.isEmpty())
    {
        const QImage sizeReference = CTextureList::getInstance()->textureImage(
            allLayers.first(), unit->renderedModelName());
        if(!sizeReference.isNull())
        {
            texture = QImage(sizeReference.size(), QImage::Format_ARGB32_Premultiplied);
            texture.fill(Qt::transparent);
        }
    }

    m_unitTextureDialog->setWindowTitle(
        tr("Final character texture — %1").arg(unit->databaseName()));
    if(texture.isNull())
    {
        if(diagnostic.isEmpty())
            diagnostic = tr("The selected texture layers could not be decoded as a CPU image.");
        m_unitTextureDetails->setText(diagnostic);
        m_unitTextureImage->clear();
        m_unitTextureImage->resize(0, 0);
        return;
    }

    QImage preview(texture.size(), QImage::Format_RGB32);
    QPainter painter(&preview);
    const int cell = 16;
    for(int y = 0; y < preview.height(); y += cell)
        for(int x = 0; x < preview.width(); x += cell)
            painter.fillRect(x, y, cell, cell,
                ((x / cell) + (y / cell)) % 2
                    ? QColor(190, 190, 190) : QColor(235, 235, 235));
    // Display the authoring atlas rotated 180 degrees, but keep UI labels in
    // normal reading orientation. Rotate the texture and UV coordinates here
    // instead of mirroring the finished image after labels have been painted.
    painter.drawImage(0, 0, texture.mirrored(true, true));

    QSet<QString> visibleParts;
    for(int row = 0; row < m_unitTextureParts->count(); ++row)
        if(m_unitTextureParts->item(row)->checkState() == Qt::Checked)
            visibleParts.insert(m_unitTextureParts->item(row)->text());
    const QMap<QString, QVector<QVector2D>> projections =
        unit->renderedBodyPartUvTriangles();
    painter.setRenderHint(QPainter::Antialiasing, false);
    auto texturePoint = [&preview](const QVector2D& uv)
    {
        const double wrappedU = uv.x() - std::floor(double(uv.x()));
        const double wrappedV = uv.y() - std::floor(double(uv.y()));
        return QPointF(qBound(0.0, (1.0 - wrappedU) * preview.width() - 1.0,
                              double(preview.width() - 1)),
                       qBound(0.0, (1.0 - wrappedV) * preview.height() - 1.0,
                              double(preview.height() - 1)));
    };
    for(auto it = projections.constBegin(); it != projections.constEnd(); ++it)
    {
        if(!visibleParts.contains(it.key()))
            continue;
        const QColor color = QColor::fromHsv(int(qHash(it.key()) % 360), 230, 255, 220);
        painter.setPen(QPen(color, 1.0));
        QRectF bounds;
        bool boundsInitialized = false;
        const QVector<QVector2D>& coordinates = it.value();
        for(int vertex = 0; vertex + 2 < coordinates.size(); vertex += 3)
        {
            QPolygonF triangle;
            for(int corner = 0; corner < 3; ++corner)
            {
                const QPointF point = texturePoint(coordinates.at(vertex + corner));
                triangle.append(point);
                if(!boundsInitialized)
                {
                    bounds = QRectF(point, QSizeF(0, 0));
                    boundsInitialized = true;
                }
                else
                    bounds |= QRectF(point, QSizeF(0, 0));
            }
            painter.drawPolygon(triangle);
        }
        if(boundsInitialized)
        {
            const QString label = it.key();
            const QFontMetrics metrics(painter.font());
            QRectF labelRect = metrics.boundingRect(label);
            labelRect.moveCenter(bounds.center());
            labelRect.adjust(-3, -1, 3, 1);
            painter.fillRect(labelRect, QColor(0, 0, 0, 190));
            painter.setPen(color);
            painter.drawText(labelRect, Qt::AlignCenter, label);
        }
    }
    painter.end();

    m_unitTextureDetails->setText(
        tr("Model: %1\nSize: %2 × %3\nVisible layers: %4 / %5\n"
           "UV projection: renderer-effective QImage upload, Repeat wrapping")
            .arg(unit->renderedModelName()).arg(texture.width()).arg(texture.height())
            .arg(visibleLayers.size()).arg(allLayers.size()));
    m_unitTextureImage->setPixmap(QPixmap::fromImage(preview));
    m_unitTextureImage->adjustSize();
}

void MainWindow::testFunc1()
{
    ei::log(eLogDebug, "test func 1 started");
    QMap<QString, QString> mapName;
    QFile inputFile("c:\\konst\\unit_rename.txt");
    if (inputFile.open(QIODevice::ReadOnly))
    {
       QTextStream in(&inputFile);
       while (!in.atEnd())
       {
          QString line = in.readLine();
          QStringList list = line.split(";");
          mapName.insert(list[0], list[1]);
       }
       inputFile.close();
    }


    QDir mobFolder("c:\\konst\\Проклятые Земли (Дополнение)\\Mods\\ferneo_mod\\Maps");
    QFileInfo land(mobFolder.absolutePath() + "\\zone1g.mpr");
    m_pView->loadLandscape(land);
    QStringList mobFilter;
    mobFilter << "*.mob";
    QFileInfoList mobFiles = mobFolder.entryInfoList(mobFilter);
    int n=0;
    foreach(QFileInfo filename, mobFiles) {
        m_pView->loadMob(filename);
        m_pView->roundActiveMob();
        n+=m_pView->renameActiveMobUnits(mapName);
        m_pView->saveActiveMob();
        m_pView->unloadActiveMob();
    }
    ei::log(eLogInfo, "renamed:" + QString::number(n) + "units");
    ei::log(eLogDebug, "test func 1 ended");
}

void MainWindow::createUndoView()
{
    m_undoView = new QUndoView(m_undoStack);
    m_historyDock = new QDockWidget(tr("History"), this);
    m_historyDock->setObjectName("undoHistoryDock");
    m_historyDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::LeftDockWidgetArea |
                                   Qt::RightDockWidgetArea);
    m_historyDock->setFeatures(QDockWidget::DockWidgetClosable |
                               QDockWidget::DockWidgetMovable |
                               QDockWidget::DockWidgetFloatable);
    m_historyDock->setWidget(m_undoView);
    addDockWidget(Qt::BottomDockWidgetArea, m_historyDock);
    ei::enableNativeFloatingDockControls(m_historyDock);
    m_historyDock->setFloating(true);
    m_historyDock->resize(520, 560);
    m_historyDock->hide();
    m_undoView->viewport()->installEventFilter(this);
}

namespace {
QByteArray mobFamilyRequestDigest(const SMobFamilyValidationRequest& request)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto addString = [&hash](const QString& value)
    {
        const QByteArray encoded = value.toUtf8();
        hash.addData(encoded);
        hash.addData("\0", 1);
    };
    addString(request.familyLabel);
    addString(request.profileId);
    addString(request.mappedPrimaryMobPath);
    addString(request.preferredMobPath);
    hash.addData(request.explicitComposition ? "1" : "0", 1);
    hash.addData(request.includeProfileWideMobs ? "1" : "0", 1);
    QStringList paths = request.seedMobPaths;
    paths.append(request.workspaceRoots);
    paths.append(request.mapRoots);
    for(const QString& path : paths)
        addString(QDir::cleanPath(path).toCaseFolded());
    for(auto it = request.inMemoryMobData.constBegin();
        it != request.inMemoryMobData.constEnd(); ++it)
    {
        addString(it.key());
        hash.addData(QCryptographicHash::hash(it.value(), QCryptographicHash::Sha256));
    }
    if(!request.database.isNull())
    {
        addString(request.database->profileId);
        addString(request.database->providerId);
        addString(request.database->cacheKey);
    }
    for(auto model = request.assemblyPartsByModel.constBegin();
        model != request.assemblyPartsByModel.constEnd(); ++model)
    {
        addString(model.key());
        for(const QString& part : model.value())
            addString(part);
    }
    for(auto geometry = request.assemblyHairGeometryByPart.constBegin();
        geometry != request.assemblyHairGeometryByPart.constEnd(); ++geometry)
    {
        addString(geometry.key());
        addString(QString::number(int(geometry.value())));
    }
    addString(request.assemblyResourceFingerprint);
    for(const QString& source : request.assemblyResourceSources)
        addString(QDir::cleanPath(source).toCaseFolded());
    return hash.result();
}

int undoWorkUnits(const QUndoCommand* command)
{
    if(!command)
        return 0;
    int units = 1;
    for(int index = 0; index < command->childCount(); ++index)
        units += undoWorkUnits(command->child(index));
    return units;
}
}

void MainWindow::runUndoToIndex(int targetIndex)
{
    if(!m_undoStack)
        return;
    targetIndex = qBound(0, targetIndex, m_undoStack->count());
    const int currentIndex = m_undoStack->index();
    if(targetIndex == currentIndex)
    {
        keepFloatingHistoryRaised();
        return;
    }
    int workUnits = 0;
    const int first = qMin(currentIndex, targetIndex);
    const int last = qMax(currentIndex, targetIndex);
    for(int index = first; index < last; ++index)
        workUnits += undoWorkUnits(m_undoStack->command(index));

    QScopedPointer<QProgressDialog> progress;
    if(last - first > 1 || workUnits >= 8)
    {
        progress.reset(new QProgressDialog(
            targetIndex < currentIndex ? tr("Applying undo history...")
                                       : tr("Applying redo history..."),
            QString(), 0, 0, this));
        progress->setWindowTitle(tr("Updating map"));
        configureBlockingProgress(progress.data());
        progress->show();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    ei::log(eLogInfo, QString("Applying undo history current:%1 target:%2 workUnits:%3 progress:%4")
            .arg(currentIndex).arg(targetIndex).arg(workUnits).arg(!progress.isNull()));
    m_undoStack->setIndex(targetIndex);
    if(progress)
        progress->close();
    keepFloatingHistoryRaised();
}

void MainWindow::keepFloatingHistoryRaised()
{
    if(!m_historyDock || !m_historyDock->isVisible() || !m_historyDock->isFloating())
        return;
    // Undo commands rebuild selection/property widgets and a progress dialog
    // reactivates its main-window parent when it closes. Restore the separate
    // native History window after those queued focus changes settle.
    QTimer::singleShot(0, m_historyDock, [this]()
    {
        if(!m_historyDock || !m_historyDock->isVisible()
           || !m_historyDock->isFloating())
            return;
        QWidget* historyWindow = m_historyDock->window();
        historyWindow->raise();
        historyWindow->activateWindow();
    });
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if(m_pView && watched == m_pView && event->type() == QEvent::Resize)
        positionViewportMinimap();
    if(m_pView && watched == m_pView && event->type() == QEvent::KeyPress)
    {
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        if(key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier)
        {
            handleEscapeShortcut();
            return true;
        }
    }
    if(m_loadedMapTree && watched == m_loadedMapTree->viewport())
    {
        if(event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            QTreeWidgetItem* item = m_loadedMapTree->itemAt(mouse->pos());
            if(mouse->button() == Qt::LeftButton && item
               && m_loadedMapTree->columnAt(mouse->pos().x()) == 1
               && item->flags().testFlag(Qt::ItemIsUserCheckable))
            {
                m_loadedMapVisibilityDragging = true;
                m_loadedMapVisibilityTarget = item->checkState(1) == Qt::Checked
                    ? Qt::Unchecked : Qt::Checked;
                m_loadedMapVisibilityKeys.clear();
                previewLoadedMapVisibility(item);
                m_loadedMapTree->viewport()->grabMouse();
                return true;
            }
        }
        if(m_loadedMapVisibilityDragging && event->type() == QEvent::MouseMove)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            if(mouse->buttons().testFlag(Qt::LeftButton)
               && m_loadedMapTree->columnAt(mouse->pos().x()) == 1)
                previewLoadedMapVisibility(m_loadedMapTree->itemAt(mouse->pos()));
            return true;
        }
        if(m_loadedMapVisibilityDragging && event->type() == QEvent::MouseButtonRelease)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            if(mouse->button() == Qt::LeftButton)
            {
                m_loadedMapTree->viewport()->releaseMouse();
                commitLoadedMapVisibilityDrag();
                return true;
            }
        }
    }
    if(m_pView && watched == m_pView && m_suppressSpawnRelease)
    {
        // Placement/marker selection owns the complete pointer sequence. If
        // move events leak through before the matching release, CView can
        // begin a landscape selection rectangle from the same click.
        if(event->type() == QEvent::MouseMove)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            if(mouse->buttons().testFlag(Qt::LeftButton))
                return true;
            m_suppressSpawnRelease = false;
        }
        if(event->type() == QEvent::MouseButtonRelease)
        {
            m_suppressSpawnRelease = false;
            return true;
        }
    }
    if(m_pView && watched == m_pView && m_customSpawn.valid && !m_pickSpawnPending)
    {
        if(event->type() == QEvent::KeyPress && m_customSpawnSelected)
        {
            QKeyEvent* key = static_cast<QKeyEvent*>(event);
            if(key->key() == Qt::Key_Delete)
            {
                m_playFromHereButton->setChecked(false);
                return true;
            }
        }
        if(event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            if(mouse->button() == Qt::LeftButton)
            {
                const bool hit = m_pView->testSpawnMarkerAt(mouse->pos());
                if(hit)
                {
                    m_pView->setLandscapeInteractionSuspended(true);
                    m_pView->clearObjectSelection();
                }
                else
                    m_pView->setLandscapeInteractionSuspended(false);
                m_customSpawnSelected = hit;
                m_pView->setTestSpawnMarkerSelected(hit);
                updateOperationAvailability();
                updateOutlinerSelectionPath();
                if(hit)
                {
                    m_suppressSpawnRelease = true;
                    return true;
                }
            }
        }
    }
    if(m_pView && watched == m_pView && m_pickSpawnPending)
    {
        if(event->type() == QEvent::KeyPress)
        {
            QKeyEvent* key = static_cast<QKeyEvent*>(event);
            if(key->key() == Qt::Key_Escape)
                return true;
        }
        if(event->type() == QEvent::MouseMove)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            // MMB always belongs to viewport camera navigation. Do not move
            // the provisional Play-from-Here marker while orbiting the view.
            if(mouse->buttons().testFlag(Qt::MiddleButton))
                return false;
            QVector3D preview;
            if(m_pView->terrainPositionAt(mouse->pos(), &preview))
            {
                QVector3D navigationPreview;
                if(m_pView->projectToRegeneratedNavigation(
                       QPointF(preview.x(), preview.y()), &navigationPreview))
                    preview = navigationPreview;
                m_pView->setTestSpawnMarker(preview, false);
            }
            else
                m_pView->clearTestSpawnMarker();
        }
        if(event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            if(mouse->button() == Qt::LeftButton)
            {
                const SMapTravelZone* entrance = selectedTravelZone();
                if(!entrance && !m_mapTestEntry.zoneId.isEmpty())
                {
                    m_playFromHereButton->setChecked(false);
                    return true;
                }
                QVector3D requested;
                if(!m_pView->terrainPositionAt(mouse->pos(), &requested))
                {
                    statusBar()->showMessage(tr("The terrain was not hit at that point. Pick visible terrain."), 5000);
                    return true;
                }
                SMapSpawnPlan plan = CMapSpawnPlanner::chooseCustom(
                    QPointF(requested.x(), requested.y()),
                    entrance ? entrance->exitId : 1,
                    mapTestObstacles());
                QVector3D resolved(plan.position.x(), plan.position.y(), 0.0f);
                QVector3D navigationResolved;
                if(m_pView->projectToRegeneratedNavigation(
                       QPointF(requested.x(), requested.y()), &navigationResolved))
                {
                    resolved = navigationResolved;
                    plan.valid = true;
                    plan.unobstructed = true;
                    plan.position = QPointF(resolved.x(), resolved.y());
                    plan.requestedPosition = QPointF(requested.x(), requested.y());
                    plan.custom = true;
                    plan.exitId = entrance ? entrance->exitId : 1;
                    const QPointF graphAdjustment = plan.position - plan.requestedPosition;
                    plan.adjusted = graphAdjustment.x() * graphAdjustment.x()
                        + graphAdjustment.y() * graphAdjustment.y() > 0.0001;
                    plan.reason = plan.adjusted
                        ? tr("The regenerated navigation preview moved the spawn to the nearest passable point.")
                        : tr("The regenerated navigation preview confirms this passable point.");
                }
                else if(plan.valid && !CLandscape::getInstance()->projectPt(resolved))
                    plan.valid = false;
                if(!plan.valid)
                {
                    statusBar()->showMessage(tr("No passable spawn was found near that point. Pick another location."), 5000);
                    return true;
                }
                m_customSpawn = plan;
                m_customSpawnPosition = resolved;
                m_customSpawnContextKey = currentMapContextKey();
                m_pickSpawnPending = false;
                m_movingCustomSpawn = false;
                m_pView->clearObjectSelection();
                m_customSpawnSelected = true;
                m_suppressSpawnRelease = true;
                if(m_playFromHereButton && !m_playFromHereButton->isChecked())
                {
                    const QSignalBlocker blocker(m_playFromHereButton);
                    m_playFromHereButton->setChecked(true);
                }
                m_pView->unsetCursor();
                m_pView->setTestSpawnMarker(resolved, plan.adjusted);
                m_pView->setTestSpawnMarkerSelected(true);
                m_pView->setLandscapeInteractionSuspended(false);
                m_spawnPositionLabel->setText(tr("Custom: %1, %2, %3%4")
                    .arg(resolved.x(), 0, 'f', 2).arg(resolved.y(), 0, 'f', 2)
                    .arg(resolved.z(), 0, 'f', 2)
                    .arg(plan.adjusted ? tr(" (adjusted)") : QString()));
                updateCustomSpawnControls();
                updateEntranceOverlays();
                updateOperationAvailability();
                updateMapTestAction();
                updateOutlinerSelectionPath();
                return true;
            }
        }
    }
    if(m_undoView && watched == m_undoView->viewport()
       && event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
        if(mouse->button() == Qt::LeftButton)
        {
            const QModelIndex item = m_undoView->indexAt(mouse->pos());
            if(item.isValid())
            {
                if(!m_pView->prepareHistoryNavigation())
                {
                    keepFloatingHistoryRaised();
                    return true;
                }
                runUndoToIndex(item.row());
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::previewLoadedMapVisibility(QTreeWidgetItem* item)
{
    if(!item || !m_loadedMapTree || !item->flags().testFlag(Qt::ItemIsUserCheckable))
        return;
    const int kind = item->data(0, kLoadedMapKindRole).toInt();
    const QString key = kind == kLoadedMapMpr ? QString("__mpr__")
        : (kind == kLoadedMapMob ? item->data(0, kLoadedMapPathRole).toString()
                                : QString());
    if(key.isEmpty())
        return;
    m_loadedMapVisibilityKeys.insert(key);
    const QSignalBlocker blocker(m_loadedMapTree);
    item->setCheckState(1, m_loadedMapVisibilityTarget);
}

void MainWindow::commitLoadedMapVisibilityDrag()
{
    const QSet<QString> keys = m_loadedMapVisibilityKeys;
    const bool visible = m_loadedMapVisibilityTarget == Qt::Checked;
    m_loadedMapVisibilityDragging = false;
    m_loadedMapVisibilityKeys.clear();
    if(!m_pView || keys.isEmpty())
        return;
    QList<CMob*> mobs;
    bool includeLandscape = keys.contains("__mpr__");
    for(const QString& path : keys)
    {
        if(path == "__mpr__")
            continue;
        if(CMob* mob = m_pView->mob(path))
            mobs.append(mob);
    }
    m_pView->setWorkspaceVisibility(mobs, includeLandscape, visible);
}

SResourceProfileEvaluation MainWindow::activeResourceProfile() const
{
    COptString* profileId = dynamic_cast<COptString*>(
        m_settings->opt(eOptSetResource, "resourceProfile"));
    for(const SResourceProfile& candidate : loadResourceProfiles())
        if(profileId && candidate.id.compare(profileId->value(), Qt::CaseInsensitive) == 0)
            return evaluateResourceProfile(m_settings->configuredGameRoot(), candidate);
    return SResourceProfileEvaluation();
}

QString MainWindow::activeProfileDisplayName() const
{
    const SResourceProfileEvaluation profile = activeResourceProfile();
    return profile.profileName.isEmpty() ? tr("Not configured") : profile.profileName;
}

QVector<SMapSpawnObstacle> MainWindow::mapTestObstacles() const
{
    QVector<SMapSpawnObstacle> obstacles;
    if(!m_pView || !m_pView->currentMob())
        return obstacles;
    for(CNode* node : m_pView->currentMob()->nodes())
    {
        if(!node || node->isMarkDeleted())
            continue;
        switch(node->nodeType())
        {
        case eWorldObject:
        case eUnit:
        case eTorch:
        case eLever:
            break;
        default:
            continue;
        }
        CBox box = node->getBBox();
        SMapSpawnObstacle obstacle;
        obstacle.position = QPointF(node->position().x(), node->position().y());
        obstacle.radius = box.isInit() ? qMax(0.75, double(box.radius()) * 0.5) : 0.75;
        obstacles.append(obstacle);
    }
    return obstacles;
}

const SMapTravelZone* MainWindow::selectedTravelZone() const
{
    if(!m_entranceCombo)
        return nullptr;
    const int index = m_entranceCombo->currentData().toInt();
    return index >= 0 && index < m_validEntrances.size()
        ? &m_validEntrances[index] : nullptr;
}

bool MainWindow::automaticLooseMapBootstrap(SMapCatalogEntry* selectedEntry,
                                            SMapTravelZone* selectedEntrance) const
{
    if(!selectedEntry || !selectedEntrance)
        return false;
    int bestScore = -1;
    for(const SMapCatalogEntry& entry : m_mapCatalog.entries)
    {
        if(entry.zoneId.isEmpty()
           || entry.zoneType.compare("game", Qt::CaseInsensitive) != 0)
            continue;
        for(const SMapTravelZone& entrance : entry.travelZones)
        {
            if(entrance.exitId <= 0 || !entrance.hasDeployRectangle
               || entrance.deployRectangle.normalized().isEmpty())
                continue;
            int score = 10;
            if(entry.mprName.compare("zone1", Qt::CaseInsensitive) == 0)
                score += 20;
            if(entry.zoneId.compare("gz1g", Qt::CaseInsensitive) == 0)
                score += 30;
            if(score > bestScore)
            {
                bestScore = score;
                *selectedEntry = entry;
                *selectedEntrance = entrance;
            }
        }
    }
    return bestScore >= 0;
}

void MainWindow::populateEntranceCombo()
{
    if(!m_entranceCombo)
        return;
    const QSignalBlocker blocker(m_entranceCombo);
    m_entranceCombo->clear();
    m_validEntrances.clear();
    if(!CLandscape::getInstance()->isMprLoad())
    {
        m_entranceCombo->addItem(tr("Open an MPR"), -2);
        m_entranceCombo->setEnabled(false);
        updateEntranceOverlays();
        return;
    }
    if(m_mapTestEntry.zoneId.isEmpty())
    {
        m_entranceCombo->addItem(tr("Choose map.txt identity..."), -1);
        m_entranceCombo->setEnabled(true);
        updateEntranceOverlays();
        return;
    }
    for(const SMapTravelZone& entrance : m_mapTestEntry.travelZones)
    {
        if(entrance.exitId <= 0 || !entrance.hasDeployRectangle
           || entrance.deployRectangle.normalized().isEmpty())
            continue;
        const int dataIndex = m_validEntrances.size();
        m_validEntrances.append(entrance);
        const QPointF center = entrance.spawnPoint();
        const QString destination = entrance.destinationZoneId.isEmpty()
            ? tr("none") : entrance.destinationZoneId;
        m_entranceCombo->addItem(tr("Entrance %1 -> %2:%3 — (%4, %5)")
            .arg(entrance.exitId).arg(destination).arg(entrance.destinationExitId)
            .arg(center.x(), 0, 'f', 1).arg(center.y(), 0, 'f', 1), dataIndex);
    }
    if(m_validEntrances.isEmpty())
    {
        m_entranceCombo->addItem(tr("No valid #deploy entrances"), -2);
        m_entranceCombo->setEnabled(false);
    }
    else
        m_entranceCombo->setEnabled(true);
    updateEntranceOverlays();
}

void MainWindow::updateEntranceOverlays()
{
    if(!m_pView)
        return;
    QVector<QRectF> rectangles;
    QStringList labels;
    const bool showEntrances = !m_showEntrancesButton
        || m_showEntrancesButton->isChecked();
    if(showEntrances)
    {
        for(const SMapTravelZone& entrance : m_validEntrances)
        {
            rectangles.append(entrance.deployRectangle.normalized());
            labels.append(entrance.destinationZoneId.isEmpty()
                ? tr("Entrance %1").arg(entrance.exitId)
                : tr("Entrance %1 -> %2:%3")
                    .arg(entrance.exitId)
                    .arg(entrance.destinationZoneId)
                    .arg(entrance.destinationExitId));
        }
    }
    const bool customSpawnActive = m_customSpawn.valid && m_customSpawn.custom
        && m_customSpawnContextKey == currentMapContextKey();
    const int selected = (!showEntrances || customSpawnActive) ? -1
        : (m_entranceCombo ? m_entranceCombo->currentData().toInt() : -1);
    m_pView->setTestEntranceOverlays(rectangles, labels, selected);
    updateCustomSpawnControls();
}

void MainWindow::refreshMapTestContext(bool preserveWorkspaceState, bool reloadCatalog)
{
    const int previousEntranceIndex = m_entranceCombo
        ? m_entranceCombo->currentData().toInt() : -1;
    const QString previousRouteKey = mapTravelRouteKey(m_validEntrances,
                                                        previousEntranceIndex);
    m_mapTestEntry = SMapCatalogEntry();
    m_borrowedMapIdentity = false;
    if(!preserveWorkspaceState)
    {
        m_customSpawn = SMapSpawnPlan();
        m_customSpawnPosition = QVector3D();
        m_customSpawnContextKey.clear();
        m_pickSpawnPending = false;
        m_movingCustomSpawn = false;
        m_customSpawnSelected = false;
        m_suppressSpawnRelease = false;
        if(m_pView)
            m_pView->clearTestSpawnMarker();
        if(m_playFromHereButton)
        {
            const QSignalBlocker blocker(m_playFromHereButton);
            m_playFromHereButton->setChecked(false);
        }
        if(m_spawnPositionLabel)
            m_spawnPositionLabel->setText(tr("Entrance spawn"));
    }
    updateOutlinerSelectionPath();

    // refreshMapsBrowser() owns the potentially expensive archive/map-root
    // scan. Map identity changes only query that already-current snapshot;
    // loading a map must not rescan the same resources a second time. Explicit
    // catalog-refresh callers can still request a fresh snapshot.
    if(reloadCatalog)
    {
        QVector<QString> resources;
        QVector<QString> maps;
        if(COptStringList* value = dynamic_cast<COptStringList*>(
               m_settings->opt(eOptSetResource, "resourceRoots")))
            resources = value->value();
        if(COptStringList* value = dynamic_cast<COptStringList*>(
               m_settings->opt(eOptSetResource, "mapRoots")))
            maps = value->value();
        m_mapCatalog = CMapCatalog::load(resources, maps);
    }
    if(CLandscape::getInstance()->isMprLoad())
    {
        // An explicitly unmapped collection must stay unmapped. Do not infer
        // map.txt metadata from a same-named installed MPR, otherwise a reload
        // can resurrect entrances and runtime identity from the prior map.
        const bool allowCatalogInference = m_currentCollectionPath.isEmpty()
            || !m_openedMapZoneId.isEmpty();
        const QString loaded = QFileInfo(CLandscape::getInstance()->filePath()).absoluteFilePath();
        QVector<SMapCatalogEntry> nameMatches;
        for(const SMapCatalogEntry& entry : m_mapCatalog.entries)
        {
            if(!m_openedMapZoneId.isEmpty()
               && entry.zoneId.compare(m_openedMapZoneId, Qt::CaseInsensitive) == 0)
            {
                m_mapTestEntry = entry;
                break;
            }
            if(!allowCatalogInference)
                continue;
            if(!entry.mprPath.isEmpty()
               && QFileInfo(entry.mprPath).absoluteFilePath().compare(loaded, Qt::CaseInsensitive) == 0)
            {
                m_mapTestEntry = entry;
                break;
            }
            if(entry.mprName.compare(QFileInfo(loaded).completeBaseName(), Qt::CaseInsensitive) == 0)
                nameMatches.append(entry);
        }
        if(m_mapTestEntry.zoneId.isEmpty() && nameMatches.size() == 1)
            m_mapTestEntry = nameMatches.first();
    }
    if(m_pView)
        m_pView->setMapSkyType(m_mapTestEntry.skyType, m_mapTestEntry.allodId);
    populateEntranceCombo();
    if(preserveWorkspaceState && !previousRouteKey.isEmpty())
    {
        for(int index = 0; index < m_validEntrances.size(); ++index)
            if(mapTravelRouteKey(m_validEntrances, index) == previousRouteKey)
            {
                m_entranceCombo->setCurrentIndex(m_entranceCombo->findData(index));
                break;
            }
        updateEntranceOverlays();
    }
    updateCustomSpawnControls();
    updateMapTestAction();
    refreshViewportMinimapSources();
}

void MainWindow::chooseBorrowedMapIdentity()
{
    QStringList labels;
    QVector<SMapCatalogEntry> choices;
    for(const SMapCatalogEntry& entry : m_mapCatalog.entries)
    {
        bool valid = false;
        for(const SMapTravelZone& zone : entry.travelZones)
            valid = valid || (zone.exitId > 0 && zone.hasDeployRectangle
                              && !zone.deployRectangle.normalized().isEmpty());
        if(valid)
        {
            choices.append(entry);
            labels.append(QString("%1 — %2 / %3").arg(entry.zoneId, entry.mprName, entry.mobName));
        }
    }
    bool accepted = false;
    const QString selected = QInputDialog::getItem(this, tr("Borrow map.txt identity"),
        tr("This gives the test map another zone's minimap, quest, travel and script identity. It is remembered only while the current MPR is loaded."),
        labels, 0, true, &accepted);
    const int index = labels.indexOf(selected);
    if(!accepted || index < 0)
    {
        populateEntranceCombo();
        return;
    }
    m_mapTestEntry = choices[index];
    m_borrowedMapIdentity = true;
    ei::log(eLogWarning, QString("Borrowed map.txt identity zone:%1 for unregistered MPR:%2")
        .arg(m_mapTestEntry.zoneId, CLandscape::getInstance()->filePath().absoluteFilePath()));
    populateEntranceCombo();
    m_entranceCombo->setToolTip(tr("Borrowed map identity: %1. Runtime metadata and scripts belong to that registered zone.")
        .arg(m_mapTestEntry.zoneId));
    updateMapTestAction();
}

void MainWindow::setCustomSpawnPicking(bool enabled)
{
    if(!m_pView || !enabled)
    {
        m_pickSpawnPending = false;
        m_movingCustomSpawn = false;
        m_customSpawnSelected = false;
        m_customSpawn = SMapSpawnPlan();
        m_customSpawnPosition = QVector3D();
        m_customSpawnContextKey.clear();
        if(m_pView)
        {
            m_pView->setLandscapeInteractionSuspended(false);
            m_pView->unsetCursor();
            m_pView->clearTestSpawnMarker();
        }
        if(m_spawnPositionLabel)
            m_spawnPositionLabel->setText(tr("Entrance spawn"));
        updateCustomSpawnControls();
        updateEntranceOverlays();
        updateOperationAvailability();
        updateMapTestAction();
        updateOutlinerSelectionPath();
        return;
    }
    if(!selectedTravelZone() && !m_mapTestEntry.zoneId.isEmpty())
    {
        QSignalBlocker blocker(m_playFromHereButton);
        m_playFromHereButton->setChecked(false);
        statusBar()->showMessage(tr("Select a valid map.txt entrance before choosing a custom spawn."), 5000);
        return;
    }
    // Play-from-Here is an exclusive viewport interaction. Do not leave an
    // object selected behind its preview marker or an object transform active.
    m_pView->cancelPendingOperation();
    m_pView->clearObjectSelection();
    m_pView->setLandscapeInteractionSuspended(true);
    m_pickSpawnPending = true;
    m_movingCustomSpawn = false;
    m_customSpawnSelected = false;
    m_customSpawn = SMapSpawnPlan();
    m_customSpawnContextKey.clear();
    m_pView->setCursor(Qt::CrossCursor);
    m_pView->setFocus();
    m_spawnPositionLabel->setText(tr("Click terrain (Esc cancels)"));
    updateCustomSpawnControls();
    updateEntranceOverlays();
    updateOperationAvailability();
    updateMapTestAction();
}

void MainWindow::updateCustomSpawnControls()
{
    const bool customActive = m_customSpawn.valid && m_customSpawn.custom
        && m_customSpawnContextKey == currentMapContextKey();
    if(m_playFromHereButton)
        m_playFromHereButton->setText(customActive && !m_pickSpawnPending ? tr("Clear entrance")
                                                   : (m_pickSpawnPending ? (m_movingCustomSpawn
                                                       ? tr("Cancel Move Entrance") : tr("Cancel Play from Here"))
                                                                         : tr("Play from Here")));
    if(m_entranceCombo && (!m_mapTestLauncher || !m_mapTestLauncher->isRunning()))
        m_entranceCombo->setEnabled(!customActive && !m_pickSpawnPending
            && CLandscape::getInstance()->isMprLoad()
            && (!m_validEntrances.isEmpty() || m_entranceCombo->findData(-1) >= 0));
    if(m_gotoEntranceButton)
        m_gotoEntranceButton->setEnabled(customActive || selectedTravelZone());
}

QString MainWindow::currentMapContextKey() const
{
    const QString mprPath = CLandscape::getInstance()->isMprLoad()
        ? QFileInfo(CLandscape::getInstance()->filePath()).absoluteFilePath()
        : QString();
    return QString("%1|%2").arg(QDir::cleanPath(mprPath), m_mapTestEntry.zoneId)
        .toCaseFolded();
}

void MainWindow::moveCameraToActiveSpawn()
{
    if(!m_pView)
        return;
    if(m_customSpawn.valid && m_customSpawn.custom
       && m_customSpawnContextKey == currentMapContextKey())
    {
        m_pView->moveCameraToPoint(m_customSpawnPosition);
        return;
    }
    const SMapTravelZone* entrance = selectedTravelZone();
    if(!entrance)
        return;
    const QPointF center = entrance->spawnPoint();
    m_pView->moveCameraToPoint(QVector3D(center.x(), center.y(), 0.0f));
}

void MainWindow::initShortcuts()
{
    bool migratedShortcuts = false;
    if(COptString* landscapeShortcut = dynamic_cast<COptString*>(
           m_settings->opt(eOptSetKeyBinding, "shortcutTileBrushTool")))
    {
        const QString current = landscapeShortcut->value().trimmed();
        if(current.compare(QStringLiteral("B"), Qt::CaseInsensitive) == 0
           || current.compare(QStringLiteral("L"), Qt::CaseInsensitive) == 0)
        {
            landscapeShortcut->setValue(QStringLiteral("Ctrl+L"));
            migratedShortcuts = true;
        }
    }
    if(COptString* historyShortcut = dynamic_cast<COptString*>(
           m_settings->opt(eOptSetKeyBinding, "shortcutHistory")))
    {
        if(historyShortcut->value().trimmed().compare(
               QStringLiteral("Ctrl+L"), Qt::CaseInsensitive) == 0)
        {
            historyShortcut->setValue(QStringLiteral("Ctrl+H"));
            migratedShortcuts = true;
        }
    }
    if(migratedShortcuts)
        m_settings->persistOptions();
    const auto shortcut = [this](const char* name, const char* fallback)
    {
        COptString* option = dynamic_cast<COptString*>(m_settings->opt(eOptSetKeyBinding, name));
        return QKeySequence(option ? option->value() : QString::fromLatin1(fallback));
    };
    m_ui->actionSelect_by->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_F));
    m_ui->actionOpen->setShortcut(shortcut("shortcutOpen", "Ctrl+O"));
    m_ui->actionOpen_MOB->setShortcut(shortcut("shortcutOpenMob", "Alt+Shift+O"));
    m_ui->actionOpen_MPR->setShortcut(shortcut("shortcutOpenMpr", "Ctrl+Shift+O"));

    m_ui->actionSave->setShortcut(shortcut("shortcutSaveActiveMob", "Ctrl+S"));
    m_ui->actionSave_all_MOB_s->setShortcut(shortcut("shortcutSaveAllMobs", "Alt+Shift+S"));
    //m_ui->actionSave_all_MOB_s->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + eKey_S));
    //m_ui->actionSave_as->setShortcut(QKeySequence(Qt::CTRL + Qt::ALT + eKey_S));
    m_ui->actionSave_landscape_MPR->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + eKey_S));

    m_ui->actionSelect_All->setShortcut(QKeySequence(Qt::CTRL + eKey_A));
    m_ui->actionClose_all->setShortcut(QKeySequence(Qt::CTRL + eKey_Q));
    m_ui->actionUndo->setShortcut(shortcut("shortcutUndo", "Ctrl+Z"));
    m_ui->actionRedo->setShortcut(shortcut("shortcutRedo", "Ctrl+Y"));
    m_historyDock->toggleViewAction()->setShortcut(shortcut("shortcutHistory", "Ctrl+H"));
    m_ui->actionCreate_new_object->setShortcut(shortcut("shortcutCreateObject", "Ctrl+N"));
    m_ui->actionDuplicate_selected_objects->setShortcut(shortcut("shortcutDuplicateObjects", "Ctrl+D"));
    m_ui->actionReset_cam_position->setShortcut(shortcut("shortcutResetCamera", "Ctrl+R"));
    m_ui->actionChange_mod_e->setShortcut(shortcut("shortcutSwitchMode", "Ctrl+Tab"));
    m_ui->action_Mob_parameters->setShortcut(shortcut("shortcutMobParameters", "Ctrl+M"));
    m_ui->actionOpen_MOB_Script->setShortcut(shortcut("shortcutMobScript", "Ctrl+Shift+M"));
    m_externalScriptAction->setShortcut(shortcut("shortcutExternalMobScript", "Ctrl+Alt+M"));
    m_ui->actionReset_logic_paths->setShortcut(shortcut("shortcutResetLogicPaths", "Ctrl+P"));
    m_mapsDock->toggleViewAction()->setShortcut(shortcut("shortcutMapsBrowser", "Ctrl+Space"));
    m_messageLogDock->toggleViewAction()->setShortcut(shortcut("shortcutMessageLog", "Alt+L"));
    m_mapTestAction->setShortcut(shortcut("shortcutTestMap", "Ctrl+F5"));
    m_stopMapTestAction->setShortcut(shortcut("shortcutStopMapTest", "Shift+Esc"));
    const struct { EButtonOp type; const char* option; const char* fallback; const char* label; } operations[] = {
        {EButtonOpSelect, "shortcutSelectTool", "Esc", "Select / cancel tool"},
        {EButtonOpMove, "shortcutMoveTool", "G", "Move selected objects"},
        {EButtonOpRotate, "shortcutRotateTool", "R", "Rotate selected objects"},
        {EButtonOpScale, "shortcutScaleTool", "T", "Scale selected objects"},
        {EButtonOpTilebrush, "shortcutTileBrushTool", "Ctrl+L", "Landscape Tools"}
    };
    for(const auto& operation : operations)
    {
        QAction* action = m_operationActions.value(operation.type, nullptr);
        if(!action)
            continue;
        // Escape has stateful precedence (spawn anchor, Landscape Tools, then
        // Select), so a dedicated application shortcut owns it below.
        if(operation.type != EButtonOpSelect)
            action->setShortcut(shortcut(operation.option, operation.fallback));
        action->setShortcutContext(Qt::ApplicationShortcut);
        const QString shortcutText = operation.type == EButtonOpSelect
            ? QKeySequence(Qt::Key_Escape).toString(QKeySequence::NativeText)
            : action->shortcut().toString(QKeySequence::NativeText);
        const QString tip = tr(operation.label) + " (" + shortcutText + ")";
        action->setToolTip(tip);
    }
    if(m_ui->selectButton) m_ui->selectButton->setToolTip(m_operationActions.value(EButtonOpSelect)->toolTip());
    if(m_ui->moveButton) m_ui->moveButton->setToolTip(m_operationActions.value(EButtonOpMove)->toolTip());
    if(m_ui->rotateButton) m_ui->rotateButton->setToolTip(m_operationActions.value(EButtonOpRotate)->toolTip());
    if(m_ui->scaleButton) m_ui->scaleButton->setToolTip(m_operationActions.value(EButtonOpScale)->toolTip());
    if(m_ui->tileBrushButton) m_ui->tileBrushButton->setToolTip(m_operationActions.value(EButtonOpTilebrush)->toolTip());

    QShortcut* escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::ApplicationShortcut);
    connect(escapeShortcut, &QShortcut::activated,
            this, &MainWindow::handleEscapeShortcut);
    updateBehaviorModeUi();
}

void MainWindow::createViewportToolbars()
{
    QWidget* testBar = new QWidget(m_ui->workspaceContainer);
    testBar->setObjectName("mapTestToolbar");
    QHBoxLayout* testLayout = new QHBoxLayout(testBar);
    testLayout->setContentsMargins(4, 3, 4, 3);
    testLayout->setSpacing(5);
    QToolButton* testButton = new QToolButton(testBar);
    testButton->setDefaultAction(m_mapTestAction);
    testButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QToolButton* stopButton = new QToolButton(testBar);
    stopButton->setDefaultAction(m_stopMapTestAction);
    stopButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_entranceCombo = new QComboBox(testBar);
    m_entranceCombo->setMinimumContentsLength(28);
    m_entranceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_entranceCombo->setToolTip(tr("Selects the map.txt entrance used to initialize the runtime session."));
    m_showEntrancesButton = new QToolButton(testBar);
    m_showEntrancesButton->setObjectName("showEntrancesButton");
    m_showEntrancesButton->setCheckable(true);
    m_showEntrancesButton->setAutoRaise(true);
    m_showEntrancesButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_showEntrancesButton->setChecked(
        QSettings().value("ui/showEntranceOverlays", true).toBool());
    updateEntranceVisibilityButtonAppearance(m_showEntrancesButton,
        m_showEntrancesButton->isChecked());
    m_playFromHereButton = new QToolButton(testBar);
    m_playFromHereButton->setText(tr("Play from Here"));
    m_playFromHereButton->setCheckable(true);
    m_playFromHereButton->setToolTip(tr("Pick a custom terrain spawn. The selected entrance still initializes zone metadata."));
    m_gotoEntranceButton = new QToolButton(testBar);
    m_gotoEntranceButton->setText(tr("Go to Entrance"));
    m_gotoEntranceButton->setToolTip(tr("Centers the editor camera on the selected map.txt deploy area."));
    m_godmodeCheckBox = new QCheckBox(tr("Godmode"), testBar);
    m_godmodeCheckBox->setToolTip(tr("For this JMV test process, invoke GodMode(0,3) through a run-local lossless copy of SpellAddon's autorun MOB. The installed MOB is never changed."));
    if(COptBool* option = dynamic_cast<COptBool*>(m_settings->opt(eOptSetGeneral, "mapTestGodmode")))
        m_godmodeCheckBox->setChecked(option->value());
    m_spawnPositionLabel = new QLabel(tr("Entrance spawn"), testBar);
    m_spawnPositionLabel->setMinimumWidth(150);
    testLayout->addWidget(testButton);
    testLayout->addWidget(stopButton);
    testLayout->addSpacing(8);
    testLayout->addWidget(new QLabel(tr("Entrance:"), testBar));
    testLayout->addWidget(m_entranceCombo, 1);
    testLayout->addWidget(m_showEntrancesButton);
    testLayout->addWidget(m_gotoEntranceButton);
    testLayout->addWidget(m_playFromHereButton);
    testLayout->addWidget(m_spawnPositionLabel);
    testLayout->addWidget(m_godmodeCheckBox);

    QWidget* operationBar = new QWidget(m_ui->workspaceContainer);
    operationBar->setObjectName("viewportOperationToolbar");
    QHBoxLayout* operationLayout = new QHBoxLayout(operationBar);
    operationLayout->setContentsMargins(4, 2, 4, 2);
    operationLayout->setSpacing(4);
    const struct { EButtonOp type; QToolButton* button; const char* text; } operations[] = {
        {EButtonOpSelect, m_ui->selectButton, "Select"},
        {EButtonOpMove, m_ui->moveButton, "Move"},
        {EButtonOpRotate, m_ui->rotateButton, "Rotate"},
        {EButtonOpScale, m_ui->scaleButton, "Scale"},
        {EButtonOpTilebrush, m_ui->tileBrushButton, "Landscape Tools"}
    };
    QActionGroup* actionGroup = new QActionGroup(this);
    actionGroup->setExclusive(true);
    for(const auto& operation : operations)
    {
        QAction* action = new QAction(tr(operation.text), this);
        action->setCheckable(true);
        if(operation.type != EButtonOpTilebrush)
            actionGroup->addAction(action);
        addAction(action);
        m_operationActions.insert(operation.type, action);
        operation.button->setCheckable(true);
        operation.button->setAutoRaise(true);
        operation.button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        operation.button->setText(tr(operation.text));
        operationLayout->addWidget(operation.button);
        connect(action, &QAction::triggered, this, [this, operation]()
        {
            if(!m_pView)
                return;
            if(operation.type == EButtonOpTilebrush)
            {
                toggleLandscapeTools();
                m_pView->setFocus();
                return;
            }
            if(operation.type == EButtonOpMove && m_customSpawnSelected
               && m_customSpawn.valid && m_customSpawn.custom
               && m_customSpawnContextKey == currentMapContextKey())
            {
                m_pView->setLandscapeInteractionSuspended(true);
                m_pickSpawnPending = true;
                m_movingCustomSpawn = true;
                m_pView->setCursor(Qt::CrossCursor);
                m_pView->setFocus();
                m_spawnPositionLabel->setText(tr("Click terrain to move entrance (Esc cancels)"));
                updateCustomSpawnControls();
                updateEntranceOverlays();
                updateMapTestAction();
                return;
            }
            m_pView->setLandscapeInteractionSuspended(false);
            if(operation.type == EButtonOpSelect)
            {
                if(m_pickSpawnPending && m_playFromHereButton)
                {
                    m_playFromHereButton->setChecked(false);
                    return;
                }
                if(!m_pView->cancelPendingOperation())
                    m_pView->clearObjectSelection();
                CButtonConnector::getInstance()->pressButton(EButtonOpSelect);
            }
            else
                CButtonConnector::getInstance()->clickButton(operation.type);
            m_pView->setFocus();
        });
    }
    m_ui->actionChange_mod_e->setCheckable(true);
    m_behaviorModeButton = new QToolButton(operationBar);
    m_behaviorModeButton->setObjectName("behaviorModeButton");
    m_behaviorModeButton->setCheckable(true);
    m_behaviorModeButton->setAutoRaise(true);
    m_behaviorModeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    operationLayout->addWidget(m_behaviorModeButton);
    connect(m_behaviorModeButton, &QToolButton::clicked,
            m_ui->actionChange_mod_e, &QAction::trigger);
    updateBehaviorModeUi();
    operationLayout->addStretch(1);
    operationLayout->addWidget(new QLabel(tr("Camera Speed:"), operationBar));
    m_cameraSpeedSlider = new QSlider(Qt::Horizontal, operationBar);
    m_cameraSpeedSlider->setObjectName("cameraSpeedSlider");
    m_cameraSpeedSlider->setRange(1, 100);
    m_cameraSpeedSlider->setSingleStep(1);
    m_cameraSpeedSlider->setPageStep(5);
    m_cameraSpeedSlider->setFixedWidth(150);
    m_cameraSpeedSlider->setToolTip(tr(
        "Camera movement and zoom speed. Hold the configured modifier (Ctrl by default) "
        "and use the mouse wheel over the viewport to adjust it."));
    m_cameraSpeedSpin = new QDoubleSpinBox(operationBar);
    m_cameraSpeedSpin->setObjectName("cameraSpeedMultiplier");
    m_cameraSpeedSpin->setRange(0.1, 10.0);
    m_cameraSpeedSpin->setSingleStep(0.1);
    m_cameraSpeedSpin->setDecimals(1);
    m_cameraSpeedSpin->setPrefix(QString::fromUtf8("\xC3\x97"));
    m_cameraSpeedSpin->setFixedWidth(72);
    m_cameraSpeedSpin->setToolTip(m_cameraSpeedSlider->toolTip());
    double cameraSpeed = 1.0;
    if(const COptDouble* option = dynamic_cast<const COptDouble*>(
           m_settings->opt(eOptSetGeneral, "cameraSpeedMultiplier")))
        cameraSpeed = qBound(0.1, option->value(), 10.0);
    m_cameraSpeedSlider->setValue(qRound(cameraSpeed * 10.0));
    m_cameraSpeedSpin->setValue(cameraSpeed);
    m_pView->setCameraSpeedMultiplier(cameraSpeed);
    operationLayout->addWidget(m_cameraSpeedSlider);
    operationLayout->addWidget(m_cameraSpeedSpin);

    m_cameraSpeedPersistTimer = new QTimer(this);
    m_cameraSpeedPersistTimer->setSingleShot(true);
    connect(m_cameraSpeedPersistTimer, &QTimer::timeout,
            m_settings.get(), &CSettings::persistOptions);
    connect(m_cameraSpeedSlider, &QSlider::valueChanged, this, [this](int value)
    {
        m_pView->setCameraSpeedMultiplier(value / 10.0);
    });
    connect(m_cameraSpeedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            m_pView, &CView::setCameraSpeedMultiplier);
    connect(m_pView, &CView::cameraSpeedMultiplierChanged, this, [this](double value)
    {
        const QSignalBlocker sliderBlocker(m_cameraSpeedSlider);
        const QSignalBlocker spinBlocker(m_cameraSpeedSpin);
        m_cameraSpeedSlider->setValue(qRound(value * 10.0));
        m_cameraSpeedSpin->setValue(value);
        if(COptDouble* option = dynamic_cast<COptDouble*>(
               m_settings->opt(eOptSetGeneral, "cameraSpeedMultiplier")))
            option->setValue(value);
        m_cameraSpeedPersistTimer->start(250);
        statusBar()->showMessage(tr("Camera speed: %1x").arg(value, 0, 'f', 1), 1500);
    });
    m_ui->toolsLayout->setContentsMargins(0, 0, 0, 0);
    m_ui->toolsLayout->setSpacing(0);

    m_ui->workSpaceLayout->addWidget(testBar);
    m_ui->workSpaceLayout->addWidget(m_pView, 1);
    m_ui->workSpaceLayout->addWidget(operationBar);

    connect(m_entranceCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index)
    {
        if(m_entranceCombo->itemData(index).toInt() == -1)
            chooseBorrowedMapIdentity();
        if(m_playFromHereButton->isChecked())
            m_playFromHereButton->setChecked(false);
        updateEntranceOverlays();
        updateMapTestAction();
    });
    connect(m_showEntrancesButton, &QToolButton::toggled, this, [this](bool checked)
    {
        QSettings().setValue("ui/showEntranceOverlays", checked);
        updateEntranceVisibilityButtonAppearance(m_showEntrancesButton, checked);
        updateEntranceOverlays();
    });
    connect(m_gotoEntranceButton, &QToolButton::clicked, this, [this]()
    {
        moveCameraToActiveSpawn();
    });
    connect(m_playFromHereButton, &QToolButton::toggled,
            this, &MainWindow::setCustomSpawnPicking);
    connect(m_godmodeCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if(COptBool* option = dynamic_cast<COptBool*>(m_settings->opt(eOptSetGeneral, "mapTestGodmode")))
        {
            option->setValue(checked);
            m_settings->persistOptions();
        }
    });
}

void MainWindow::updateMapTestAction()
{
    if(!m_mapTestAction || !m_stopMapTestAction)
        return;
    const SResourceProfileEvaluation profile = activeResourceProfile();
    const bool isJmv = profile.profileId.compare("jamais-vu", Qt::CaseInsensitive) == 0;
    const bool isBaseProbe = profile.profileId.compare("base-game", Qt::CaseInsensitive) == 0;
    CMob* stagingMob = mapTestStagingMob();
    const bool hasMobs = m_pView && m_pView->hasActiveMob()
            && !m_pView->mobs().isEmpty() && CLandscape::getInstance()->isMprLoad();
    const bool looseCompositionValid = !isJmv || !m_mapTestEntry.zoneId.isEmpty()
        || (m_pView && m_pView->mobs().size() == 1);
    const bool hasPair = hasMobs && (isJmv || m_pView->mobs().size() == 1)
        && looseCompositionValid;
    const bool hasEntrance = selectedTravelZone() != nullptr;
    const bool customForCurrentMap = m_customSpawn.valid && m_customSpawn.custom
        && m_customSpawnContextKey == currentMapContextKey();
    QString autoSpellMobReason;
    bool hasAutoSpellMob = CJmvMapTestLauncher::hasAutoSpellMob(
        profile, m_mapTestEntry.zoneType, m_mapTestEntry.zoneId.isEmpty(),
        &autoSpellMobReason);
    if(hasAutoSpellMob && isJmv && (!stagingMob || stagingMob->isQuestMob()))
    {
        hasAutoSpellMob = false;
        autoSpellMobReason = tr(
            "Play from Here, Godmode, and time override are unavailable for this loaded MOB set. Test Current Map and Hot Reload remain available.");
    }
    const bool loosePairReady = isJmv && hasAutoSpellMob
        && m_mapTestEntry.zoneId.isEmpty()
        && customForCurrentMap && !m_pickSpawnPending;
    const bool hasBootstrap = hasEntrance || loosePairReady;
    const bool customRequested = hasAutoSpellMob && (customForCurrentMap
        || (m_playFromHereButton && m_playFromHereButton->isChecked()));
    const bool customReady = !customRequested
        || (hasAutoSpellMob && m_customSpawn.valid && !m_pickSpawnPending);
    const bool profileReady = !m_workspaceLoadInProgress
        && (isJmv || isBaseProbe) && profile.valid
        && profile.runtimeStackValidated;
    if(m_mapTestLauncher && m_mapTestLauncher->isRunning())
    {
        const bool canEditTestSetup = isJmv && profileReady && hasPair;
        const bool canConfigureReload = canEditTestSetup && hasBootstrap;
        const bool runtimeReady = m_mapTestLauncher->isHotReloadReady();
        const bool pendingReload = m_mapTestLauncher->isHotReloadPending();
        const QString runtimeState = m_mapTestLauncher->runtimeState();
        const quint64 saveGeneration = m_mapTestLauncher->saveGeneration();
        const bool canReplaceDeferred = pendingReload
            && (runtimeState == "Transitioning" || runtimeState == "Initializing"
                || runtimeState == "Unknown");
        const bool canHotReload = canConfigureReload && customReady
            && runtimeReady && (!pendingReload || canReplaceDeferred);
        m_mapTestAction->setText(tr("Hot Reload Map"));
        m_mapTestAction->setEnabled(canHotReload);
        m_stopMapTestAction->setEnabled(true);
        if(m_entranceCombo)
            m_entranceCombo->setEnabled(canEditTestSetup && !customRequested);
        if(m_gotoEntranceButton) m_gotoEntranceButton->setEnabled(canConfigureReload);
        if(m_playFromHereButton)
        {
            m_playFromHereButton->setEnabled(canEditTestSetup && hasAutoSpellMob);
            m_playFromHereButton->setToolTip(hasAutoSpellMob
                ? tr("Place a temporary player start point for this test session.")
                : autoSpellMobReason);
        }
        if(m_godmodeCheckBox)
            m_godmodeCheckBox->setEnabled(canEditTestSetup && hasAutoSpellMob);
        m_mapTestAction->setToolTip(canHotReload
            ? tr("Restage the exact editor pair and reload it in the Endurance-owned JMV process (Ctrl+F5).\nRuntime state: %1\nSave generation: %2")
                .arg(runtimeState).arg(saveGeneration)
            : (pendingReload && !canReplaceDeferred
                ? tr("Waiting for the current hot reload to complete.")
                : (!runtimeReady
                ? tr("Waiting for the initial staged map to report hot-reload readiness.")
                : tr("Hot reload requires a validated JMV profile, one MPR with its MOB set, and a valid entrance."))));
        return;
    }
    m_mapTestAction->setText(tr("Test Current Map"));
    m_stopMapTestAction->setEnabled(false);
    QString interruptedRun;
    const bool recoveryPending = (m_mapTestLauncher && m_mapTestLauncher->isRecoveryPending())
        || CJmvMapTestLauncher::hasActiveInterruptedRun(&interruptedRun);
    // JMV applies a placed custom entrance through its process-owned autorun
    // provider. Base keeps the picker unavailable until it has an equivalent
    // exact-hash runtime position provider.
    m_mapTestAction->setEnabled(!recoveryPending && profileReady && hasPair
                                && hasBootstrap && customReady);
    if(m_entranceCombo)
        m_entranceCombo->setEnabled(hasPair && !customRequested &&
            (!m_validEntrances.isEmpty() || m_entranceCombo->findData(-1) >= 0));
    if(m_gotoEntranceButton)
        m_gotoEntranceButton->setEnabled(hasPair && (hasEntrance || customForCurrentMap));
    if(m_playFromHereButton)
    {
        m_playFromHereButton->setEnabled(isJmv && hasPair && hasAutoSpellMob);
        m_playFromHereButton->setToolTip(hasAutoSpellMob
            ? tr("Place a temporary player start point for this test session.")
            : autoSpellMobReason);
    }
    if(m_godmodeCheckBox)
        m_godmodeCheckBox->setEnabled(isJmv && hasPair && hasAutoSpellMob);
    if(recoveryPending)
        m_mapTestAction->setToolTip(interruptedRun.isEmpty()
            ? tr("Endurance is waiting for an interrupted test game to finish exiting and for hash-owned cleanup to complete.")
            : tr("An orphaned Endurance test game is still running. Exit it normally; cleanup will run automatically before another test.\n%1")
                .arg(interruptedRun));
    else if(!isJmv && !isBaseProbe)
        m_mapTestAction->setToolTip(tr("The active profile has no Endurance direct-map-test provider."));
    else if(!profileReady)
    {
        const QString detail = runtimeManifestProblem(profile);
        m_mapTestAction->setToolTip(detail.isEmpty()
            ? tr("The active runtime does not match an exact experimental manifest.")
            : tr("The active runtime does not match an exact experimental manifest.\n%1")
                .arg(detail));
    }
    else if(!hasPair)
        m_mapTestAction->setToolTip(isJmv
            ? tr("Load one MPR and at least one MOB before launching a map test.")
            : tr("The Base probe requires exactly one MPR/MOB pair."));
    else if(!hasBootstrap)
        m_mapTestAction->setToolTip(isJmv && m_mapTestEntry.zoneId.isEmpty()
            ? tr("This loose MPR/MOB pair has no map.txt identity. Place Play from Here, or explicitly borrow a registered map identity.")
            : tr("Select a valid map.txt entrance or explicitly borrow a registered map identity."));
    else if(!customReady)
        m_mapTestAction->setToolTip(tr("Finish placing the JMV custom entrance before launching the map test."));
    else
        m_mapTestAction->setToolTip(isBaseProbe
            ? tr("Runs the exact-hash Base EI direct-launch feasibility probe (Ctrl+F5).")
            : tr("Stages the current pair and launches it through JMV's normal single-player New Game path (Ctrl+F5)."));
}

CMob* MainWindow::mapTestStagingMob() const
{
    if(!m_pView)
        return nullptr;

    const QString expected = m_mapTestEntry.mobName.trimmed();
    if(!expected.isEmpty())
    {
        for(CMob* candidate : m_pView->mobs())
        {
            if(!candidate)
                continue;
            const QFileInfo file(candidate->filePath());
            if(file.completeBaseName().compare(expected, Qt::CaseInsensitive) == 0
               || file.fileName().compare(expected, Qt::CaseInsensitive) == 0)
                return candidate;
        }
    }

    // Prefer the unique structurally primary MOB when map.txt naming cannot
    // identify a copied/renamed source. A supplemental MOB remains a valid
    // staging source; it merely lacks SpellAddon's autorun-dependent features.
    CMob* structuralPrimary = nullptr;
    int structuralPrimaryCount = 0;
    for(CMob* candidate : m_pView->mobs())
    {
        if(!candidate || candidate->isQuestMob())
            continue;
        ++structuralPrimaryCount;
        structuralPrimary = candidate;
    }
    if(structuralPrimaryCount == 1)
        return structuralPrimary;
    if(m_pView->currentMob())
        return m_pView->currentMob();
    return m_pView->mobs().isEmpty() ? nullptr : m_pView->mobs().first();
}

void MainWindow::launchCurrentMapTest()
{
    activateWindow();
    raise();
    const bool hotReloadRequested = m_mapTestLauncher && m_mapTestLauncher->isRunning();
    if(!m_pView || !m_pView->hasActiveMob() || m_pView->mobs().isEmpty()
       || !CLandscape::getInstance()->isMprLoad())
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("Load one MPR and at least one MOB before starting a test."));
        return;
    }
    const SMobFamilyValidationRequest runtimePreflightRequest = mobFamilyValidationRequest();
    const CMobFamilyValidationReport runtimePreflight =
        CMobFamilyValidator::validate(runtimePreflightRequest);
    ++m_validationGeneration;
    m_hasPendingValidation = false;
    m_lastValidationRequestDigest = mobFamilyRequestDigest(runtimePreflightRequest);
    applyMobFamilyValidationReport(runtimePreflight);
    if(runtimePreflight.hasBlockingRuntimeFindings())
    {
        const QVector<SMobFamilyFinding> blockers =
            runtimePreflight.blockingRuntimeFindings();
        ei::log(eLogFatal, QString("Blocked %1 because authored object NID 0 is present:\n%2")
            .arg(hotReloadRequested ? tr("Hot Reload Map") : tr("Test Current Map"),
                 validationWarningPreview(blockers)));
        QMessageBox::critical(this, tr("Map test blocked"), tr(
            "This map contains an object with NID 0. NID 0 is invalid for map objects and "
            "may cause the game to crash. Assign the affected object a unique positive NID "
            "before testing the map.\n\n%1").arg(validationWarningPreview(blockers)));
        return;
    }
    notifyFamilyPreflight(hotReloadRequested ? tr("Hot Reload Map") : tr("Test Current Map"));
    QScopedPointer<QProgressDialog> progress(new QProgressDialog(
        hotReloadRequested ? tr("Preparing hot reload...") : tr("Preparing map test..."),
        QString(), 0, 6, this));
    progress->setWindowTitle(hotReloadRequested ? tr("JMV hot reload")
                                                : tr("Map test launch"));
    configureBlockingProgress(progress.data());
    progress->setValue(0);
    progress->show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    if(m_pView->prepareDocumentTransition())
        statusBar()->showMessage(tr("Pending object movement was cancelled before staging the test."), 4000);
    CLandscape* landscape = CLandscape::getInstance();
    progress->setLabelText(tr("Validating the active runtime profile..."));
    progress->setValue(1);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const SResourceProfileEvaluation profile = activeResourceProfile();
    const bool isJmv = profile.profileId.compare("jamais-vu", Qt::CaseInsensitive) == 0;
    const bool isBase = profile.profileId.compare("base-game", Qt::CaseInsensitive) == 0;
    if(hotReloadRequested && !isJmv)
    {
        QMessageBox::warning(this, tr("Hot reload"),
            tr("Hot reload is currently available only for an Endurance-owned JMV test process."));
        return;
    }
    if((!isJmv && !isBase) || !profile.valid || !profile.runtimeStackValidated)
    {
        const QString detail = runtimeManifestProblem(profile);
        QMessageBox::warning(this, tr("Map test launch"),
            detail.isEmpty()
                ? tr("The active installation does not match an exact supported or experimental runtime manifest. Run Game and Mod Setup again and review its diagnostics.")
                : tr("The active installation does not match an exact supported or experimental runtime manifest.\n\n%1\n\nRun Game and Mod Setup again if this is not the intended installation.")
                    .arg(detail));
        return;
    }
    progress->setLabelText(tr("Reading map-table metadata..."));
    progress->setValue(2);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    SMapCatalogEntry entry = m_mapTestEntry;
    CMob* mob = mapTestStagingMob();
    if(!mob)
    {
        QMessageBox::warning(this, tr("Map test launch"),
            tr("No loaded MOB is available for staging."));
        return;
    }
    SMapTravelZone looseBootstrapEntrance;
    const SMapTravelZone* entrance = selectedTravelZone();
    const bool customContextMatched = m_customSpawn.valid && m_customSpawn.custom
        && m_customSpawnContextKey == currentMapContextKey();
    bool customForCurrentMap = customContextMatched;
    const bool loosePair = entry.zoneId.isEmpty();
    QString autoSpellMobReason;
    bool hasAutoSpellMob = CJmvMapTestLauncher::hasAutoSpellMob(
        profile, entry.zoneType, loosePair, &autoSpellMobReason);
    if(hasAutoSpellMob && mob->isQuestMob())
    {
        hasAutoSpellMob = false;
        autoSpellMobReason = tr(
            "Play from Here, Godmode, and time override are unavailable for this loaded MOB set. Test Current Map and Hot Reload remain available.");
    }
    if(customForCurrentMap && !hasAutoSpellMob)
    {
        ei::log(eLogWarning, QString("Ignoring unavailable Play from Here override: %1")
            .arg(autoSpellMobReason));
        customForCurrentMap = false;
    }
    if(loosePair)
    {
        if(!isJmv || m_pView->mobs().size() != 1)
        {
            QMessageBox::warning(this, tr("Loose map test"),
                tr("An unmapped test requires exactly one loaded MPR and one loaded MOB. Use a collection or map.txt mapping for supplemental MOB sets."));
            return;
        }
        if(!customForCurrentMap)
        {
            QMessageBox::warning(this, tr("Loose map test"),
                tr("This MPR/MOB pair has no map.txt identity. Place a Play from Here entrance before starting the test."));
            return;
        }
        if(!automaticLooseMapBootstrap(&entry, &looseBootstrapEntrance))
        {
            QMessageBox::warning(this, tr("Loose map test"),
                tr("The active profile has no valid gameplay map.txt entrance that can initialize a temporary player session."));
            return;
        }
        entrance = &looseBootstrapEntrance;
        ei::log(eLogWarning, QString("Loose MPR/MOB test has no map.txt mapping; using bootstrap identity zone:%1 exit:%2 mpr:%3 mob:%4")
            .arg(entry.zoneId).arg(entrance->exitId)
            .arg(landscape->filePath().absoluteFilePath(),
                 m_pView->currentMob()->filePath().absoluteFilePath()));
    }
    else if(!entrance)
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("No travel/deploy zone was found for the loaded MPR in the active map table. Select a valid entrance."));
        return;
    }
    progress->setLabelText(tr("Choosing a safe map-table entrance..."));
    progress->setValue(3);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    if(isBase && m_pView->mobs().size() != 1)
    {
        QMessageBox::warning(this, tr("Base EI feasibility probe"),
            tr("The Base probe currently accepts exactly one MOB. Supplemental MOB redirection is JMV-only."));
        return;
    }
    if(m_customSpawn.valid && m_customSpawn.custom && !customContextMatched)
        ei::log(eLogWarning, QString("Ignored stale Play from Here spawn context:%1 active:%2")
            .arg(m_customSpawnContextKey, currentMapContextKey()));
    SMapSpawnPlan spawn = customForCurrentMap
        ? m_customSpawn
        : CMapSpawnPlanner::chooseEntrance(*entrance, mapTestObstacles());
    if(loosePair)
        spawn.exitId = entrance->exitId;
    if(!spawn.valid)
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("No deploy rectangle was found in the selected zone's map-table entrances. Endurance will not invent a player entrance.\n\n%1").arg(spawn.reason));
        return;
    }
    progress->setLabelText(tr("Validating the current in-memory map pair..."));
    progress->setValue(4);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QByteArray mprData;
    CValidationReport mprReport;
    if(!m_pView->buildValidatedLandscapeCandidate(&mprData, &mprReport))
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("The current MPR edits could not be staged safely:\n\n%1")
                .arg(mprReport.toText()));
        return;
    }
    QByteArray mobData;
    CValidationReport mobReport;
    QString saveClass;
    if(!mob->buildValidatedCandidate(&mobData, &mobReport, &saveClass))
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("The current MOB edits could not be staged safely:\n\n%1").arg(mobReport.toText()));
        return;
    }
    progress->setLabelText(tr("Preparing and validating the selected run-local AI graph..."));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QByteArray generatedAiGraphData;
    QString generatedGraphError;
    QString generatedGraphSource;
    if(!m_pView->buildGeneratedAiGraphCandidate(
           &generatedAiGraphData, &generatedGraphError, &generatedGraphSource))
    {
        QMessageBox::critical(this, tr("Map test launch"),
            tr("Endurance could not prepare a runtime-compatible AI graph for "
               "the current terrain and objects:\n\n%1")
                .arg(generatedGraphError));
        return;
    }
    CMobDocument stagedMobDocument;
    CValidationReport stagedMobReport;
    if(!stagedMobDocument.loadBytes(mobData, mob->filePath().fileName(),
                                    &stagedMobReport)
       || stagedMobReport.hasErrors()
       || !stagedMobDocument.removeEmbeddedAiGraph(&mobData, &stagedMobReport)
       || stagedMobReport.hasErrors())
    {
        QMessageBox::critical(this, tr("Map test launch"),
            tr("The run-local MOB copy could not be prepared for the generated AI graph:\n\n%1")
                .arg(stagedMobReport.toText().trimmed()));
        return;
    }
    ei::log(eLogInfo, QString(
        "Prepared selected run-local AI graph source:%1 bytes:%2 primaryMobGraphStripped:true")
        .arg(generatedGraphSource).arg(generatedAiGraphData.size()));
    QVector<SJmvSupplementalMobInput> supplementalMobs;
    for(CMob* supplemental : m_pView->mobs())
    {
        if(!supplemental || supplemental == mob)
            continue;
        QByteArray data;
        CValidationReport supplementalReport;
        QString supplementalSaveClass;
        if(!supplemental->buildValidatedCandidate(
               &data, &supplementalReport, &supplementalSaveClass))
        {
            QMessageBox::warning(this, tr("JMV test launch"),
                tr("Supplemental MOB '%1' could not be staged safely:\n\n%2")
                    .arg(supplemental->filePath().fileName(), supplementalReport.toText()));
            return;
        }
        SJmvSupplementalMobInput staged;
        staged.sourceMob = supplemental->filePath();
        staged.mobData = data;
        supplementalMobs.append(staged);
    }

    const QString entranceAssessment = spawn.unobstructed
        ? tr("its checked clear point is (%1, %2)")
              .arg(spawn.position.x(), 0, 'f', 2).arg(spawn.position.y(), 0, 'f', 2)
        : tr("the editor could not prove that its center is clear, so the game will perform the final deployment");
    QString confirmation = isJmv ? tr(
        "This experimental JMV-only test stages the exact current MPR/MOB pair, including unsaved in-memory edits, under a unique temporary name. After JMV completes its normal menu load, Endurance invokes the fingerprinted single-player New Game path on the game thread and supplies the staged pair. No console/server command or fixed delay is used.\n\n"
        "JMV initializes one normal player through map-table zone %1 and hand-placed entrance %2; %3.\n\n"
        "Source maps, JMV configuration, and live campaign saves are not changed. Complete payloads remain under Endurance AppData; the proven JMV loader also uses one randomly named, hash-owned MPR/MOB alias in the mod Maps directory and removes it on guarded cleanup. Every save/load path is redirected into a persistent profile-specific AppData folder shared by Endurance test runs. Intro movies are hidden only for the test process. Returning to the menu or travelling away may use original installed maps; use Stop Test when finished.\n\nStart the direct JMV map test?")
        .arg(entry.zoneId).arg(spawn.exitId).arg(entranceAssessment)
        : tr("This exact-hash Base EI feasibility probe uses an editor-owned 32-bit host. It creates Base game.exe suspended, loads the run-local bridge only into that owned process, calls its explicit initializer outside DllMain, and resumes only after fingerprint and byte guards pass.\n\nRun the probe levels in order. Source maps and the Base game installation are not modified.\n\nContinue to probe-level selection?");
    if(loosePair)
        confirmation.prepend(tr(
            "LOOSE MAP WARNING: this MPR/MOB pair has no map.txt entry. Endurance will create a process-local synthetic zone with the loaded MPR dimensions and the Play from Here point as its only entrance. It has no inherited minimap, travel zones, or registered quest identity. The installed map table and source files are not changed.\n\n"));
    constexpr int kMapTestWarningVersion = 4;
    COptInt* warningVersion = dynamic_cast<COptInt*>(
                m_settings->opt(eOptSetGeneral, "mapTestWarningVersion"));
    if(!hotReloadRequested
       && (!warningVersion || warningVersion->value() < kMapTestWarningVersion))
    {
        progress->hide();
        QMessageBox confirmationBox(QMessageBox::Question,
            isJmv ? tr("Direct JMV map test (experimental)")
                  : tr("Base EI feasibility probe (experimental)"), confirmation,
            QMessageBox::Yes | QMessageBox::Cancel, this);
        QCheckBox* dontShowAgain = new QCheckBox(tr("Don't show this warning again"), &confirmationBox);
        confirmationBox.setCheckBox(dontShowAgain);
        if(confirmationBox.exec() != QMessageBox::Yes)
            return;
        if(warningVersion && dontShowAgain->isChecked())
        {
            warningVersion->setValue(kMapTestWarningVersion);
            m_settings->persistOptions();
        }
        progress->show();
    }
    progress->setLabelText(hotReloadRequested
        ? tr("Staging the next hot-reload generation...")
        : tr("Staging temporary maps and isolated test state..."));
    progress->setValue(5);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    SJmvMapTestLaunchInput input;
    input.profile = profile;
    input.sourceMpr = landscape->filePath();
    input.sourceMob = mob->filePath();
    input.mprData = mprData;
    input.mobData = mobData;
    input.generatedAiGraphData = generatedAiGraphData;
    const auto databaseSnapshot = ei::ProfileDatabaseContext::instance().current();
    if(!databaseSnapshot || databaseSnapshot->sourceArchivePath.trimmed().isEmpty())
    {
        QMessageBox::critical(this, tr("Map test launch"),
            tr("The active profile database cannot be resolved for runtime staging."));
        return;
    }
    input.databaseSourcePath = databaseSnapshot->sourceArchivePath;
    QString databaseDiagnostic;
    if(m_databaseDocument
       && QFileInfo(m_databaseDocument->sourceArchivePath()).absoluteFilePath().compare(
              QFileInfo(input.databaseSourcePath).absoluteFilePath(),
              Qt::CaseInsensitive) == 0)
        input.databaseData = m_databaseDocument->buildArchive(&databaseDiagnostic);
    else
    {
        QFile databaseFile(input.databaseSourcePath);
        if(databaseFile.open(QIODevice::ReadOnly))
            input.databaseData = databaseFile.readAll();
        else
            databaseDiagnostic = databaseFile.errorString();
    }
    if(input.databaseData.isEmpty())
    {
        QMessageBox::critical(this, tr("Map test launch"),
            tr("The active database could not be staged safely.\n\n%1")
                .arg(databaseDiagnostic));
        return;
    }
    input.supplementalMobs = supplementalMobs;
    input.embeddedScript = mob->script();
    input.mapEntry = entry;
    input.spawn = spawn;
    QVector3D runtimeSpawn = customForCurrentMap
        ? m_customSpawnPosition
        : QVector3D(spawn.position.x(), spawn.position.y(), 0.0f);
    if(!customForCurrentMap && !CLandscape::getInstance()->projectPt(runtimeSpawn))
    {
        QMessageBox::warning(this, tr("Map test launch"),
            tr("The selected entrance could not be projected onto the loaded terrain."));
        return;
    }
    input.spawnZ = runtimeSpawn.z();
    input.godmode = hasAutoSpellMob && m_godmodeCheckBox
        && m_godmodeCheckBox->isChecked();
    input.overrideWorldTime = hasAutoSpellMob && m_pView->overridesWorldTime();
    input.worldTimeHours = m_pView->effectiveWorldTimeHours();
    input.borrowedMapIdentity = m_borrowedMapIdentity;
    input.syntheticMapIdentity = loosePair;
    input.mapSizeX = int(landscape->widthInWorldUnits() / 32u);
    input.mapSizeY = int(landscape->heightInWorldUnits() / 32u);
    input.allowMissingAiGraph = false;
    const bool usesGeneratedMinimap = isJmv
        && m_viewportMinimap && m_viewportMinimap->usesGeneratedPreview();
    if(usesGeneratedMinimap && !ensureGeneratedMinimap())
    {
        QMessageBox::warning(this, tr("Map test launch"), tr(
            "Generated minimap mode is selected, but the current preview could not be captured. "
            "Choose Embedded in the viewport minimap or resolve the capture error before testing."));
        return;
    }
    if(usesGeneratedMinimap)
    {
        input.temporaryMinimapMmp = m_generatedMinimapMmp;
    }
    if(isBase && !hotReloadRequested)
    {
        const QStringList levels = {
            tr("1 - Observation only (menu and normal exit)"),
            tr("2 - Pass-through hooks (start Base New Game manually)"),
            tr("3 - Byte-identical staged redirect"),
            tr("4 - Current editor-state redirect")
        };
        bool accepted = false;
        const QString selected = QInputDialog::getItem(this,
            tr("Base EI feasibility probe"),
            tr("Run probe levels in order. A failure leaves Base direct launch unsupported."),
            levels, 0, false, &accepted);
        if(!accepted)
            return;
        input.baseProbeLevel = levels.indexOf(selected) + 1;
    }
    QString error;
    const bool accepted = hotReloadRequested
        ? m_mapTestLauncher->hotReload(input, &error)
        : m_mapTestLauncher->launch(input, &error);
    if(!accepted)
    {
        ei::log(eLogWarning, QString("Map test %1 refused: %2")
            .arg(hotReloadRequested ? "hot reload" : "launch", error));
        QMessageBox::critical(this,
            hotReloadRequested ? tr("Hot reload") : tr("Map test launch"), error);
    }
    else
    {
        if(usesGeneratedMinimap)
        {
            statusBar()->showMessage(tr(
                hotReloadRequested
                    ? "The selected generated minimap was staged for this hot-reload generation."
                    : "The generated minimap was staged under a run-unique texture name in this test's private RES and map table."),
                7000);
        }
        if(hotReloadRequested)
        {
            // Keep this application-modal progress window alive until the bridge
            // commits or rejects the staged generation. The launcher focuses the
            // game only after hotReloadFinished, so editor input stays blocked.
            progress->setRange(0, 0);
            progress->setLabelText(tr("Applying the staged map generation in the game..."));
            m_hotReloadProgress = progress.take();
            statusBar()->showMessage(tr("Hot reload queued on the verified JMV game thread."), 6000);
        }
    }
    if(progress)
        progress->setValue(6);
    updateMapTestAction();
}

void MainWindow::scheduleViewportMinimapRefresh(int delayMs, bool force)
{
    Q_UNUSED(force);
    if(!m_minimapRefreshTimer || m_minimapCaptureRunning)
        return;
    if(m_minimapGenerationLocked)
        return;
    if(m_workspaceLoadInProgress)
    {
        m_minimapRefreshTimer->stop();
        return;
    }
    if(!CLandscape::getInstance()->isMprLoad())
    {
        m_generatedMinimapMmp.clear();
        m_generatedMinimapImage = QImage();
        m_generatedMinimapContextKey.clear();
        m_minimapGenerationLocked = false;
        m_viewportMinimap->setGenerationLocked(false);
        if(m_minimapExportDialog)
            m_minimapExportDialog->setGenerationLocked(false);
        if(m_viewportMinimap)
        {
            m_viewportMinimap->clearGeneratedPreview();
            m_viewportMinimap->hide();
        }
        if(m_minimapExportDialog)
            m_minimapExportDialog->setUnavailable(tr("No map loaded."));
        return;
    }
    if(m_mapTestEntry.isBriefing())
    {
        m_minimapRefreshTimer->stop();
        m_generatedMinimapMmp.clear();
        m_generatedMinimapImage = QImage();
        m_generatedMinimapContextKey.clear();
        m_viewportMinimap->clearGeneratedPreview(
            tr("Minimaps are not available in briefing zones."));
        m_viewportMinimap->setCameraFootprint(QPolygonF());
        m_viewportMinimap->hide();
        if(m_minimapExportDialog)
            m_minimapExportDialog->setUnavailable(
                tr("Minimaps are not available in briefing zones."));
        return;
    }
    m_minimapRefreshTimer->start(qMax(0, delayMs));
}

void MainWindow::refreshViewportMinimapSources()
{
    if(!m_viewportMinimap)
        return;
    CLandscape* landscape = CLandscape::getInstance();
    m_viewportMinimap->setMapDimensions(
        landscape && landscape->isMprLoad()
            ? int(landscape->widthInWorldUnits() / 32u) : 0,
        landscape && landscape->isMprLoad()
            ? int(landscape->heightInWorldUnits() / 32u) : 0);
    const QString context = currentMapContextKey();
    const bool contextChanged = context != m_minimapSourceContextKey;
    if(contextChanged)
    {
        m_minimapSourceContextKey = context;
        m_generatedMinimapMmp.clear();
        m_generatedMinimapImage = QImage();
        m_generatedMinimapContextKey.clear();
        m_minimapGenerationLocked = false;
        m_viewportMinimap->setGenerationLocked(false);
        if(m_minimapExportDialog)
            m_minimapExportDialog->setGenerationLocked(false);
        m_viewportMinimap->clearGeneratedPreview();
        m_viewportMinimap->clearEmbeddedPreview();
    }
    if(!landscape || !landscape->isMprLoad())
    {
        m_viewportMinimap->hide();
        m_viewportMinimap->setCameraFootprint(QPolygonF());
        if(m_minimapExportDialog)
            m_minimapExportDialog->setUnavailable(tr("No map loaded."));
        return;
    }
    if(m_mapTestEntry.isBriefing())
    {
        m_generatedMinimapMmp.clear();
        m_generatedMinimapImage = QImage();
        m_generatedMinimapContextKey.clear();
        m_embeddedMinimapImage = QImage();
        m_embeddedMinimapName.clear();
        m_embeddedMinimapSource.clear();
        m_viewportMinimap->clearGeneratedPreview(
            tr("Minimaps are not available in briefing zones."));
        m_viewportMinimap->clearEmbeddedPreview();
        m_viewportMinimap->setCameraFootprint(QPolygonF());
        m_viewportMinimap->hide();
        if(m_minimapExportDialog)
            m_minimapExportDialog->setUnavailable(
                tr("Minimaps are not available in briefing zones."));
        return;
    }
    QString textureName = m_mapTestEntry.minimapTexture.trimmed();
    QString source;
    QImage embedded;
    ei::SMmpInspection embeddedInspection;
    if(!textureName.isEmpty())
    {
        QString memberName = textureName;
        if(!memberName.endsWith(".mmp", Qt::CaseInsensitive))
            memberName += ".mmp";
        const QByteArray bytes = CObjectList::getInstance()->resourceData(memberName, &source);
        embeddedInspection = ei::inspectMmp(bytes);
        if(embeddedInspection.valid)
            embedded = embeddedInspection.baseImage;
    }
    m_minimapTargetTextureSize = 1024;
    m_minimapTargetDxt1 = true;
    m_minimapExportProfileDescription = tr(
        "1024 x 1024 DXT1 (JMV zone default; no embedded profile available)");
    if(embeddedInspection.valid && embeddedInspection.width == embeddedInspection.height
       && embeddedInspection.width >= 64 && embeddedInspection.width <= 2048
       && (embeddedInspection.width & (embeddedInspection.width - 1)) == 0)
    {
        m_minimapTargetTextureSize = embeddedInspection.width;
        m_minimapTargetDxt1 = embeddedInspection.formatName == QStringLiteral("DXT1");
        if(embeddedInspection.formatName != QStringLiteral("DXT1")
           && embeddedInspection.formatName != QStringLiteral("8888"))
            m_minimapTargetDxt1 = true;
        m_minimapExportProfileDescription = tr("%1 x %1 %2 (matched embedded %3)")
            .arg(m_minimapTargetTextureSize)
            .arg(m_minimapTargetDxt1 ? QStringLiteral("DXT1")
                                      : QStringLiteral("8888"), textureName);
    }
    if(!embedded.isNull())
        m_viewportMinimap->setEmbeddedPreview(textureName, embedded, source);
    else
        m_viewportMinimap->clearEmbeddedPreview(textureName);
    m_embeddedMinimapImage = embedded;
    m_embeddedMinimapName = textureName;
    m_embeddedMinimapSource = source;
    if(m_minimapExportDialog)
        m_minimapExportDialog->setMapContext(
            embedded, textureName, m_mapTestEntry.zoneId,
            m_mapTestEntry.minimapTexture,
            landscape && landscape->isMprLoad()
                ? int(landscape->widthInWorldUnits() / 32u) : 0,
            landscape && landscape->isMprLoad()
                ? int(landscape->heightInWorldUnits() / 32u) : 0);
    if(contextChanged)
        m_viewportMinimap->preferGenerated(embedded.isNull());
    m_viewportMinimap->show();
    positionViewportMinimap();
    m_viewportMinimap->raise();
    scheduleViewportMinimapRefresh(0);
}

void MainWindow::refreshViewportMinimap()
{
    if(m_minimapCaptureRunning || !m_pView || !m_viewportMinimap
       || !CLandscape::getInstance()->isMprLoad()
       || m_mapTestEntry.isBriefing())
        return;
    m_minimapCaptureRunning = true;
    const bool includeLoadedStaticGeometry = !m_pView->mobs().isEmpty();
    SMinimapSceneCaptureResult capture = m_pView->captureMinimapScene(
        includeLoadedStaticGeometry, m_minimapTargetTextureSize);
    capture.targetDxt1 = m_minimapTargetDxt1;
    capture.exportProfileDescription = m_minimapExportProfileDescription;
    if(!capture.bake.success)
    {
        m_generatedMinimapMmp.clear();
        m_generatedMinimapContextKey.clear();
        m_viewportMinimap->clearGeneratedPreview(capture.bake.error);
        m_minimapCaptureRunning = false;
        return;
    }
    if(m_minimapExportDialog)
        m_minimapExportDialog->setCapture(capture);
    const SMinimapGradeSettings activeGrade = m_minimapExportDialog
        ? m_minimapExportDialog->gradeSettings() : m_minimapGrade;
    if(!applyMinimapCapture(capture, activeGrade))
    {
        m_minimapCaptureRunning = false;
        return;
    }
    m_viewportMinimap->show();
    positionViewportMinimap();
    m_viewportMinimap->raise();
    m_minimapCaptureRunning = false;
}

bool MainWindow::applyMinimapCapture(const SMinimapSceneCaptureResult& capture,
                                     const SMinimapGradeSettings& grade)
{
    const QImage graded = gradeMinimapImage(
        capture.unlitGameImage, capture.bake.gameImage, grade);
    QString diagnostic;
    const QByteArray mmp = m_minimapTargetDxt1
        ? ei::encodeMmpDxt1(graded, &diagnostic)
        : ei::encodeMmp8888(graded, &diagnostic);
    if(mmp.isEmpty())
    {
        m_generatedMinimapMmp.clear();
        m_generatedMinimapImage = QImage();
        m_generatedMinimapContextKey.clear();
        m_viewportMinimap->clearGeneratedPreview(
            tr("Generated preview encoding failed: %1").arg(diagnostic));
        return false;
    }
    m_minimapGrade = grade;
    m_generatedMinimapMmp = mmp;
    m_generatedMinimapImage = graded;
    m_generatedMinimapContextKey = currentMapContextKey();
    m_viewportMinimap->setGeneratedPreview(
        graded, capture.bake.incomplete,
        tr("Generated: %1 static objects; %2 hidden; %3 units excluded\n%4")
            .arg(capture.staticObjectCount).arg(capture.hiddenStaticObjectCount)
            .arg(capture.excludedDynamicUnitCount)
            .arg(m_minimapExportProfileDescription));
    return true;
}

void MainWindow::setMinimapGenerationLocked(bool locked)
{
    if(locked && (!m_viewportMinimap
                  || !m_viewportMinimap->hasGeneratedPreview()))
        locked = false;
    m_minimapGenerationLocked = locked;
    if(m_viewportMinimap)
        m_viewportMinimap->setGenerationLocked(locked);
    if(m_minimapExportDialog)
        m_minimapExportDialog->setGenerationLocked(locked);
    statusBar()->showMessage(locked
        ? tr("Generated minimap locked; automatic regeneration is paused.")
        : tr("Generated minimap unlocked; automatic regeneration is enabled."),
        4000);
    if(!locked)
        scheduleViewportMinimapRefresh(0, true);
}

bool MainWindow::ensureGeneratedMinimap()
{
    if(m_minimapRefreshTimer && m_minimapRefreshTimer->isActive())
    {
        m_minimapRefreshTimer->stop();
        refreshViewportMinimap();
    }
    if(!m_generatedMinimapMmp.isEmpty()
       && m_generatedMinimapContextKey == currentMapContextKey())
        return true;
    refreshViewportMinimap();
    return !m_generatedMinimapMmp.isEmpty()
        && m_generatedMinimapContextKey == currentMapContextKey();
}

void MainWindow::positionViewportMinimap()
{
    if(!m_pView || !m_viewportMinimap)
        return;
    const int margin = 10;
    m_viewportMinimap->move(
        qMax(margin, m_pView->width() - m_viewportMinimap->width() - margin),
        margin);
    m_viewportMinimap->raise();
}

void MainWindow::persistAiGraph()
{
    if(!m_pView || !CLandscape::getInstance()->isMprLoad())
    {
        QMessageBox::information(this, tr("Bake / Export AI Graph"),
            tr("Load an MPR and its primary MOB before persisting an AI graph."));
        return;
    }
    CMob* primaryMob = mapTestStagingMob();
    if(!primaryMob)
    {
        QMessageBox::information(this, tr("Bake / Export AI Graph"),
            tr("Load the primary MOB before baking or exporting an AI graph."));
        return;
    }

    QByteArray graphBytes;
    QString graphError;
    QString graphSource;
    if(!m_pView->buildGeneratedAiGraphCandidate(
           &graphBytes, &graphError, &graphSource))
    {
        QMessageBox::critical(this, tr("Bake / Export AI Graph"),
            tr("Endurance could not prepare a runtime-compatible AI graph:\n\n%1")
                .arg(graphError));
        return;
    }

    const QFileInfo activeMpr(CLandscape::getInstance()->filePath());
    const QString externalPath = aiGraphRuntimeSiblingPath(activeMpr);
    const bool embedded = !primaryMob->aiGraphData().isEmpty();
    QStringList mapOwners;
    const QString primaryBase = primaryMob->filePath().completeBaseName();
    for(const SMapCatalogEntry& entry : m_mapCatalog.entries)
    {
        const QString registeredMob = QFileInfo(entry.mobName).completeBaseName();
        if(!registeredMob.isEmpty()
           && registeredMob.compare(primaryBase, Qt::CaseInsensitive) == 0)
            mapOwners.append(QStringLiteral("%1 / %2")
                .arg(entry.zoneId, entry.mprName));
    }
    mapOwners.removeDuplicates();
    const QString sharedWarning = aiGraphSharedMobWarning(
        primaryMob->filePath().fileName(), mapOwners);

    QString detail = aiGraphPersistenceActionNotice(graphSource) + tr(
        "\n\nActive MPR: %1\n"
        "Runtime external graph: %2\n"
        "Primary MOB: %3\n\n"
        "Runtime priority is the primary MOB's embedded graph first, then the exact active-MPR sibling .grp.")
        .arg(activeMpr.fileName(), externalPath,
             primaryMob->filePath().absoluteFilePath());
    if(embedded)
        detail += tr("\n\nThe primary MOB currently has an embedded graph. Export Runtime .grp writes the selected graph. Switch MOB to External .grp never writes the existing .grp; it only validates that file and removes embedded ownership from the MOB.");
    if(primaryMob->isDirty())
        detail += tr("\n\nThe primary MOB has unsaved edits. Bake or Switch will commit those edits together with the graph through the verified MOB save path.");
    if(!sharedWarning.isEmpty())
        detail += QStringLiteral("\n\n") + sharedWarning;

    QMessageBox prompt(this);
    prompt.setWindowTitle(tr("Bake / Export AI Graph"));
    prompt.setIcon(QMessageBox::Question);
    prompt.setText(tr("Choose how '%1' should be persisted.").arg(graphSource));
    prompt.setInformativeText(detail);
    QPushButton* exportButton = prompt.addButton(
        tr("Export Runtime .grp"), QMessageBox::AcceptRole);
    QPushButton* bakeButton = prompt.addButton(
        tr("Bake into Primary MOB"), QMessageBox::ActionRole);
    QPushButton* switchButton = prompt.addButton(
        tr("Switch MOB to External .grp"), QMessageBox::ActionRole);
    prompt.addButton(QMessageBox::Cancel);
    prompt.exec();
    QAbstractButton* selected = prompt.clickedButton();
    if(selected != exportButton && selected != bakeButton
       && selected != switchButton)
        return;

    QProgressDialog progress(tr("Preparing AI graph persistence..."), QString(),
                             0, 6, this);
    configureBlockingProgress(&progress);
    progress.show();
    const auto updateProgress = [&progress](const QString& message,
                                             int value, int maximum)
    {
        progress.setMaximum(maximum);
        progress.setValue(value);
        progress.setLabelText(message);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    const auto exportExternal = [&]() -> SAiGraphFileCommitResult
    {
        updateProgress(tr("Staging and validating %1...")
                           .arg(QFileInfo(externalPath).fileName()), 1, 6);
        SAiGraphFileCommitRequest request;
        request.destination = QFileInfo(externalPath);
        request.graphBytes = graphBytes;
        return commitValidatedAiGraphFile(request);
    };

    QString successText;
    QString failureText;
    if(selected == exportButton)
    {
        const SAiGraphFileCommitResult result = exportExternal();
        if(!result.success)
            failureText = tr("The runtime .grp was not changed.\n\nPhase: %1\n%2")
                .arg(result.phase, result.error);
        else
        {
            successText = result.noOp
                ? tr("The exact runtime .grp already contained the selected graph:\n%1")
                    .arg(externalPath)
                : tr("Exported and freshly validated the exact runtime .grp:\n%1")
                    .arg(externalPath);
            if(embedded)
                successText += tr("\n\nIt is currently shadowed by the primary MOB's embedded graph.");
        }
    }
    else if(selected == bakeButton)
    {
        if(!primaryMob->persistAiGraph(&graphBytes, &graphError, updateProgress))
            failureText = tr("The primary MOB was not changed.\n\n%1")
                .arg(graphError);
        else
            successText = tr("Baked the selected graph into %1. It now has runtime priority over any sibling .grp.")
                .arg(primaryMob->filePath().fileName());
    }
    else
    {
        updateProgress(tr("Validating existing %1 without changing it...")
                           .arg(QFileInfo(externalPath).fileName()), 1, 6);
        CAiGraphDocument selectedGraph;
        QString selectedGraphError;
        QStringList ownershipWarnings;
        const bool selectedGraphParsed = selectedGraph.parse(
            graphBytes, &selectedGraphError);
        if(!selectedGraphParsed)
            failureText = tr("The selected graph dimensions could not be established. Nothing was changed.\n\n%1")
                .arg(selectedGraphError);
        else if(!validateExistingAiGraphFile(
                    QFileInfo(externalPath), selectedGraph.width(),
                    selectedGraph.height(), &ownershipWarnings, &graphError))
            failureText = tr("The embedded graph was left untouched because no valid existing runtime .grp is available. Export a graph first, then retry the ownership switch.\n\n%1")
                .arg(graphError);
        else if(!primaryMob->persistAiGraph(nullptr, &graphError, updateProgress))
            failureText = tr("The existing runtime .grp was not changed, but the embedded graph could not be removed. The MOB still shadows the external file, so runtime ownership has not changed.\n\n%1")
                .arg(graphError);
        else
        {
            successText = tr("Preserved the existing runtime graph %1 unchanged and removed the embedded graph from %2. The active MPR now resolves that sibling .grp at runtime.")
                .arg(externalPath, primaryMob->filePath().fileName());
            if(!ownershipWarnings.isEmpty())
            {
                successText += tr("\n\nExisting graph warning: %1")
                    .arg(ownershipWarnings.join(QStringLiteral("\n")));
                for(const QString& warning : ownershipWarnings)
                    ei::log(eLogWarning, QStringLiteral(
                        "Preserved external AI graph warning: ") + warning);
            }
        }
    }
    progress.setValue(progress.maximum());
    progress.close();

    if(!failureText.isEmpty())
    {
        QMessageBox::critical(this, tr("Bake / Export AI Graph"), failureText);
        return;
    }
    m_pView->refreshAiGraphOwnership();
    if(primaryMob == m_pView->currentMob())
        m_pView->changeCurrentMob(primaryMob);
    else
        updateLoadedMapPanel();
    QMessageBox::information(this, tr("Bake / Export AI Graph"), successText);
}

void MainWindow::generateMinimap()
{
    if(!m_pView || !CLandscape::getInstance()->isMprLoad())
    {
        QMessageBox::information(this, tr("Export Minimap"),
            tr("Load an MPR or collection before exporting a minimap."));
        return;
    }
    if(m_mapTestEntry.isBriefing())
    {
        if(m_minimapExportDialog)
        {
            m_minimapExportDialog->setUnavailable(
                tr("Minimaps are not available in briefing zones."));
            m_minimapExportDialog->show();
            m_minimapExportDialog->raise();
            m_minimapExportDialog->activateWindow();
        }
        else
            QMessageBox::information(this, tr("Export Minimap"),
                tr("Minimaps are not available in briefing zones."));
        return;
    }
    if(m_minimapExportDialog)
    {
        m_minimapExportDialog->show();
        m_minimapExportDialog->raise();
        m_minimapExportDialog->activateWindow();
        return;
    }
    const bool includeLoadedStaticGeometry = !m_pView->mobs().isEmpty();
    SMinimapSceneCaptureResult capture;
    SMinimapGradeSettings dialogGrade = m_minimapGrade;
    if(m_minimapGenerationLocked && !m_generatedMinimapImage.isNull())
    {
        capture.bake.success = true;
        capture.bake.gameImage = m_generatedMinimapImage;
        capture.bake.reviewImage = m_generatedMinimapImage;
        capture.unlitGameImage = m_generatedMinimapImage;
        CLandscape* landscape = CLandscape::getInstance();
        capture.sectorWidth = landscape
            ? int(landscape->widthInWorldUnits() / 32u) : 0;
        capture.sectorHeight = landscape
            ? int(landscape->heightInWorldUnits() / 32u) : 0;
        capture.targetTextureSize = m_generatedMinimapImage.width();
        capture.targetDxt1 = m_minimapTargetDxt1;
        capture.exportProfileDescription = m_minimapExportProfileDescription;
        capture.framingDescription = tr("Applied and locked generated texture");
        capture.orientationDescription = tr("Game payload orientation; editor comparison is corrected vertically");
        dialogGrade = {100, 100, 100, 100};
    }
    else
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        capture = m_pView->captureMinimapScene(
            includeLoadedStaticGeometry, m_minimapTargetTextureSize);
        QApplication::restoreOverrideCursor();
        capture.targetDxt1 = m_minimapTargetDxt1;
        capture.exportProfileDescription = m_minimapExportProfileDescription;
    }
    if(!capture.bake.success)
    {
        QMessageBox::warning(this, tr("Export Minimap"), capture.bake.error);
        return;
    }

    // Reusing an authored catalog texture name is explicit provenance, not a
    // generated default. Standalone/unregistered MPRs intentionally start
    // blank so the author must choose their namespace.
    const QString suggestedName = m_mapTestEntry.minimapTexture;
    m_minimapExportDialog = new CMinimapExportDialog(
        capture, m_embeddedMinimapImage, m_embeddedMinimapName, suggestedName,
        m_mapTestEntry.zoneId, dialogGrade, m_minimapGenerationLocked, this);
    connect(m_minimapExportDialog, &CMinimapExportDialog::regenerationRequested,
            this, [this]() {
        if(!m_minimapExportDialog || !m_pView
           || !CLandscape::getInstance()->isMprLoad())
            return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        SMinimapSceneCaptureResult refreshed = m_pView->captureMinimapScene(
            !m_pView->mobs().isEmpty(), m_minimapTargetTextureSize);
        QApplication::restoreOverrideCursor();
        refreshed.targetDxt1 = m_minimapTargetDxt1;
        refreshed.exportProfileDescription = m_minimapExportProfileDescription;
        if(refreshed.bake.success)
            m_minimapExportDialog->setCapture(refreshed);
    });
    connect(m_minimapExportDialog, &CMinimapExportDialog::appliedAndLocked,
            this, [this]() {
        if(!m_minimapExportDialog
           || !applyMinimapCapture(m_minimapExportDialog->capture(),
                                   m_minimapExportDialog->gradeSettings()))
            return;
        m_minimapGenerationLocked = true;
        m_viewportMinimap->preferGenerated(true);
        setMinimapGenerationLocked(true);
        statusBar()->showMessage(tr("Generated minimap applied and locked."), 5000);
    });
    connect(m_minimapExportDialog, &CMinimapExportDialog::lockStateRequested,
            this, &MainWindow::setMinimapGenerationLocked);
    connect(m_minimapExportDialog,
            &CMinimapExportDialog::catalogRegistrationReady,
            this, &MainWindow::registerExportedMinimap);
    m_minimapExportDialog->show();
    m_minimapExportDialog->raise();
    m_minimapExportDialog->activateWindow();
}

void MainWindow::registerExportedMinimap(const QString& textureName)
{
    if(m_mapCatalog.sourceArchive.isEmpty())
    {
        QMessageBox::warning(this, tr("Register minimap"), tr(
            "Texture export succeeded, but the active catalog archive is unavailable. No catalog reference was changed."));
        return;
    }
    CMapTableEditorDialog catalog(this);
    if(!catalog.openArchive(m_mapCatalog.sourceArchive)
       || !catalog.stageMinimapTextureForZone(m_mapTestEntry.zoneId,
                                               textureName))
    {
        QMessageBox::warning(this, tr("Register minimap"), tr(
            "Texture export succeeded, but the selected zone could not be staged in Map Catalog Editor. No catalog reference was changed."));
        return;
    }
    catalog.exec();
    processMapTableSaveReports(catalog.saveReports());
}

void MainWindow::refreshMapsBrowser()
{
    if(!m_mapsBrowser)
        return;
    QVector<QString> resources;
    QVector<QString> maps;
    if(COptStringList* option = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource, "resourceRoots")))
        resources = option->value();
    if(COptStringList* option = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource, "mapRoots")))
        maps = option->value();
    QDir().mkpath(CCollectionDocument::defaultDirectory());
    m_mapCatalog = m_mapsBrowser->refresh(
        resources, maps, CCollectionDocument::defaultDirectory());
}

void MainWindow::refreshActiveCatalog(bool preserveWorkspaceState)
{
    QVector<QString> resources;
    QVector<QString> maps;
    if(COptStringList* option = dynamic_cast<COptStringList*>(
           m_settings->opt(eOptSetResource, "resourceRoots")))
        resources = option->value();
    if(COptStringList* option = dynamic_cast<COptStringList*>(
           m_settings->opt(eOptSetResource, "mapRoots")))
        maps = option->value();

    const SMapCatalogEntry previousEntry = m_mapTestEntry;
    m_mapCatalog = CMapCatalog::load(resources, maps);
    QDir().mkpath(CCollectionDocument::defaultDirectory());
    if(m_mapsBrowser)
        m_mapsBrowser->refresh(m_mapCatalog, CCollectionDocument::defaultDirectory());

    if(!preserveWorkspaceState || !CLandscape::getInstance()->isMprLoad())
    {
        refreshMapTestContext(false, false);
        return;
    }

    SMapCatalogEntry updatedEntry;
    for(const SMapCatalogEntry& entry : m_mapCatalog.entries)
        if(!m_openedMapZoneId.isEmpty()
           && entry.zoneId.compare(m_openedMapZoneId, Qt::CaseInsensitive) == 0)
        {
            updatedEntry = entry;
            break;
        }
    const QString loadedMpr = QFileInfo(
        CLandscape::getInstance()->filePath()).absoluteFilePath();
    CMob* primaryMob = mapTestStagingMob();
    const QString loadedMob = primaryMob
        ? primaryMob->filePath().absoluteFilePath() : QString();
    const bool physicalMismatch = !updatedEntry.zoneId.isEmpty()
        && (!updatedEntry.mprPath.isEmpty() || !updatedEntry.mobPath.isEmpty())
        && (QFileInfo(updatedEntry.mprPath).absoluteFilePath().compare(
                loadedMpr, Qt::CaseInsensitive) != 0
            || QFileInfo(updatedEntry.mobPath).absoluteFilePath().compare(
                loadedMob, Qt::CaseInsensitive) != 0);

    if(physicalMismatch && m_workspaceIdentityKind.compare(
           "map", Qt::CaseInsensitive) == 0)
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Catalog resources changed"),
            tr("The active catalog now maps %1 to a different MPR or MOB. Reload the updated pair now?\n\n"
               "Reload Now uses the normal unsaved-change checks. Keep Working preserves the current workspace; the catalog save is already complete.")
                .arg(m_openedMapZoneId),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if(answer == QMessageBox::Yes)
        {
            openMapPair(updatedEntry.mprPath, updatedEntry.mobPath,
                        updatedEntry.zoneId);
            return;
        }
        m_mapTestEntry = SMapCatalogEntry();
        populateEntranceCombo();
        updateMapTestAction();
        statusBar()->showMessage(
            tr("Catalog changed — workspace refresh deferred; map testing is unavailable."),
            8000);
        return;
    }

    refreshMapTestContext(true, false);
    if(physicalMismatch && !m_currentCollectionPath.isEmpty())
        statusBar()->showMessage(tr(
            "Catalog metadata refreshed. The collection keeps its explicit MPR/MOB composition, which now differs from map.txt."), 8000);
    else if(!previousEntry.zoneId.isEmpty() && updatedEntry.zoneId.isEmpty()
            && !m_openedMapZoneId.isEmpty())
        statusBar()->showMessage(tr(
            "Catalog refreshed; the loaded workspace identity is no longer registered."), 8000);
}

void MainWindow::processMapTableSaveReports(
    const QVector<SMapTableSaveReport>& reports)
{
    if(reports.isEmpty())
        return;
    const QString activeArchive = QFileInfo(
        m_mapCatalog.sourceArchive).absoluteFilePath();
    const SMapTableSaveReport* activeReport = nullptr;
    for(const SMapTableSaveReport& report : reports)
        if(QFileInfo(report.destinationArchive).absoluteFilePath().compare(
               activeArchive, Qt::CaseInsensitive) == 0)
            activeReport = &report;
    if(!activeReport)
    {
        statusBar()->showMessage(tr(
            "Catalog saved outside the active resource profile; the active map list was not refreshed."), 7000);
        return;
    }
    refreshActiveCatalog(true);
    statusBar()->showMessage(tr("Active catalog refreshed: %1 (%2)")
        .arg(activeReport->changes.affectedZoneIds().join(", "),
             activeReport->changes.categories.join(", ")), 7000);
}

QVector<QString> MainWindow::activeMapRoots() const
{
    if(COptStringList* option = dynamic_cast<COptStringList*>(
           m_settings->opt(eOptSetResource, "mapRoots")))
        return option->value();
    return QVector<QString>();
}

SMobFamilyValidationRequest MainWindow::mobFamilyValidationRequest()
{
    SMobFamilyValidationRequest request;
    const SResourceProfileEvaluation profile = activeResourceProfile();
    request.profileId = profile.profileId;
    request.database = ei::ProfileDatabaseContext::instance().current();
    request.assemblyResourceSources = configuredResourceSourcePaths(m_settings.get());
    request.assemblyResourceFingerprint = configuredResourceFingerprint(m_settings.get());
    request.mapRoots = activeMapRoots().toList();
    request.explicitComposition = !m_currentCollectionPath.isEmpty();
    if(!request.explicitComposition && !m_mapTestEntry.mobPath.isEmpty())
        request.mappedPrimaryMobPath = m_mapTestEntry.mobPath;
    request.familyLabel = !m_mapTestEntry.zoneId.isEmpty()
        ? m_mapTestEntry.zoneId
        : (!m_currentCollectionPath.isEmpty()
            ? QFileInfo(m_currentCollectionPath).completeBaseName()
            : QString("standalone"));
    if(!m_pView)
        return request;
    if(m_pView->currentMob())
        request.preferredMobPath = m_pView->currentMob()->filePath().absoluteFilePath();
    for(CMob* mob : m_pView->mobs())
    {
        if(!mob)
            continue;
        const QString path = mob->filePath().absoluteFilePath();
        request.seedMobPaths.append(path);
        const QString workspaceRoot = QFileInfo(path).absolutePath();
        if(!request.workspaceRoots.contains(workspaceRoot, Qt::CaseInsensitive))
            request.workspaceRoots.prepend(workspaceRoot);
        // Clean documents are already represented exactly by their source
        // files. Re-serializing every loaded MOB here used to stall the GUI
        // on every selection/undo gesture. Only dirty documents need an
        // in-memory candidate override; the worker reads clean sources.
        if(mob->isDirty())
        {
            QByteArray data;
            CValidationReport candidateReport;
            QString saveClass;
            if(mob->buildValidatedCandidate(&data, &candidateReport, &saveClass))
                request.inMemoryMobData.insert(QDir::cleanPath(path).toLower(), data);
            else
                ei::log(eLogWarning, QString("Family validation used the on-disk MOB because the current in-memory candidate could not be mapped safely: %1\n%2")
                    .arg(path, candidateReport.toText()));
        }
    }
    request.seedMobPaths.removeDuplicates();
    return request;
}

void MainWindow::scheduleMobFamilyValidation(int delayMs)
{
    // Workspace visual smoke/sequence modes measure load and visual-cache
    // behavior. A full runtime-family audit can emit thousands of legacy
    // findings for large JMV families and dominate the very timings these
    // non-interactive diagnostics are meant to measure. Interactive loads and
    // every normal validation trigger remain unchanged.
    if(qApp && (qApp->property("workspaceLoadSmoke").toBool()
            || qApp->property("workspaceVisualSmoke").toBool()
            || qApp->property("workspaceVisualSequence").isValid()))
        return;
    if(m_validationTimer)
        m_validationTimer->start(qMax(0, delayMs));
}

void MainWindow::refreshMobFamilyValidation()
{
    if(m_pView && m_pView->hasPendingOperation())
    {
        // Never snapshot/serialize MOB state while the user is positioning a
        // paste or transform preview. Validate once the gesture settles.
        scheduleMobFamilyValidation(1000);
        return;
    }
    if(!m_pView || m_pView->mobs().isEmpty())
    {
        ++m_validationGeneration;
        m_hasPendingValidation = false;
        m_familyValidation = CMobFamilyValidationReport();
        m_acknowledgedFindingDigest.clear();
        m_previousFamilyRiskKeys.clear();
        m_sessionAllocatedNids.clear();
        m_warnedSharedAllocationMobs.clear();
        m_familyRiskBaselineReady = false;
        m_lastValidationRequestDigest.clear();
        if(m_ui && m_ui->treeWidget)
            m_ui->treeWidget->setValidationNids(QSet<uint>(), QSet<uint>());
        updateMobFamilyValidationPanel();
        return;
    }
    SMobFamilyValidationRequest request = mobFamilyValidationRequest();
    const QByteArray requestDigest = mobFamilyRequestDigest(request);
    if(requestDigest == m_lastValidationRequestDigest)
        return;
    m_lastValidationRequestDigest = requestDigest;
    m_pendingValidationRequest = request;
    m_hasPendingValidation = true;
    ++m_validationGeneration;
    m_validationSummary->setText(tr("Validating runtime MOB family…"));
    startPendingMobFamilyValidation();
}

void MainWindow::startPendingMobFamilyValidation()
{
    if(!m_validationWatcher || m_validationWatcher->isRunning() || !m_hasPendingValidation)
        return;
    const SMobFamilyValidationRequest request = m_pendingValidationRequest;
    m_hasPendingValidation = false;
    m_runningValidationGeneration = m_validationGeneration;
    m_validationWatcher->setFuture(QtConcurrent::run([request]() {
        QElapsedTimer elapsed;
        elapsed.start();
        CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
        ei::log(eLogInfo, QString("MOB validation worker finished context:%1 elapsedMs:%2 members:%3 findings:%4")
                .arg(request.familyLabel).arg(elapsed.elapsed())
                .arg(report.members.size()).arg(report.findings.size()));
        return report;
    }));
}

void MainWindow::finishMobFamilyValidation()
{
    const CMobFamilyValidationReport result = m_validationWatcher->result();
    if(m_runningValidationGeneration == m_validationGeneration)
        applyMobFamilyValidationReport(result);
    startPendingMobFamilyValidation();
}

void MainWindow::applyMobFamilyValidationReport(const CMobFamilyValidationReport& report)
{
    QElapsedTimer elapsed;
    elapsed.start();
    const QByteArray previousDigest = m_familyValidation.findingDigest();
    bool previouslyMissingTrapDiplomacy = false;
    for(const SMobFamilyFinding& finding : m_familyValidation.findings)
        previouslyMissingTrapDiplomacy = previouslyMissingTrapDiplomacy
            || finding.code == "diplomacy.magic_trap_table_missing";
    m_familyValidation = report;
    // Visual providers resolve against live profile/database/resource state on
    // the GUI-owned unit projection. Surface their fail-closed diagnostics in
    // the same navigable validation report without repeating that work in the
    // background structural validator.
    if(m_pView)
    {
        for(CMob* mob : m_pView->mobs())
        {
            if(!mob)
                continue;
            const QString mobPath = mob->filePath().absoluteFilePath();
            for(CNode* node : mob->nodes())
            {
                CUnit* unit = dynamic_cast<CUnit*>(node);
                if(!unit)
                    continue;
                for(const QString& diagnostic : unit->visualDiagnostics())
                {
                    SMobFamilyFinding finding;
                    finding.severity = EMobFamilyFindingSeverity::Warning;
                    finding.code = "visual.profile_incompatible";
                    finding.category = tr("Unit visual");
                    finding.message = tr("NID %1 (%2): %3")
                        .arg(unit->mapId())
                        .arg(unit->databaseName().isEmpty() ? tr("unnamed prototype")
                                                            : unit->databaseName(),
                             diagnostic);
                    finding.locations.append(mobPath);
                    finding.nid = unit->mapId();
                    finding.hasNid = true;
                    m_familyValidation.findings.append(finding);
                }
            }
        }
    }
    m_validationFindingLimit = 250;
    const QByteArray currentDigest = m_familyValidation.findingDigest();
    if(previousDigest != currentDigest)
        m_acknowledgedFindingDigest.clear();
    QSet<QString> currentRisks;
    for(const SMobFamilyFinding& finding : m_familyValidation.findings)
    {
        if(finding.code != "nid.zero_reserved"
           && finding.code != "nid.collision" && finding.code != "nid.workspace_collision"
           && finding.code != "range.legacy_overlap" && finding.code != "range.workspace_overlap")
            continue;
        QStringList locations = finding.locations;
        locations.sort(Qt::CaseInsensitive);
        currentRisks.insert(QString("%1|%2|%3")
            .arg(finding.code).arg(finding.nid).arg(locations.join('|')));
    }
    if(m_familyRiskBaselineReady)
    {
        QSet<QString> introduced = currentRisks;
        introduced.subtract(m_previousFamilyRiskKeys);
        if(!introduced.isEmpty())
        {
            QVector<SMobFamilyFinding> introducedFindings;
            for(const SMobFamilyFinding& finding : m_familyValidation.findings)
            {
                QStringList locations = finding.locations;
                locations.sort(Qt::CaseInsensitive);
                const QString key = QString("%1|%2|%3")
                    .arg(finding.code).arg(finding.nid).arg(locations.join('|'));
                if(introduced.contains(key))
                    introducedFindings.append(finding);
            }
            ei::log(eLogWarning, QString("Editing introduced %1 new runtime-family NID/range risk(s)")
                .arg(introduced.size()));
            showValidationWarning(tr("New family ID risk"),
                                  validationWarningPreview(introducedFindings));
        }
    }
    m_previousFamilyRiskKeys = currentRisks;
    m_familyRiskBaselineReady = true;
    QSet<uint> criticalNids;
    QSet<uint> warningNids;
    for(const SMobFamilyFinding& finding : m_familyValidation.findings)
    {
        if(!finding.hasNid)
            continue;
        if(finding.severity == EMobFamilyFindingSeverity::Critical)
            criticalNids.insert(finding.nid);
        else if(finding.severity == EMobFamilyFindingSeverity::Warning
                || finding.severity == EMobFamilyFindingSeverity::Incomplete)
            warningNids.insert(finding.nid);
    }
    m_ui->treeWidget->setValidationNids(criticalNids, warningNids);
    updateMobFamilyValidationPanel();
    bool newlyMissingTrapDiplomacy = false;
    for(const SMobFamilyFinding& finding : m_familyValidation.findings)
        newlyMissingTrapDiplomacy = newlyMissingTrapDiplomacy
            || finding.code == "diplomacy.magic_trap_table_missing";
    if(newlyMissingTrapDiplomacy && !previouslyMissingTrapDiplomacy && m_validationDock)
    {
        m_validationDock->show();
        m_validationDock->raise();
    }
    ei::log(eLogInfo, QString("MOB validation UI applied context:%1 elapsedMs:%2 members:%3 findings:%4")
            .arg(m_familyValidation.familyLabel).arg(elapsed.elapsed())
            .arg(m_familyValidation.members.size()).arg(m_familyValidation.findings.size()));
    ei::log(m_familyValidation.criticalCount() > 0 ? eLogWarning : eLogInfo,
            m_familyValidation.toText());
}

void MainWindow::updateMobFamilyValidationPanel()
{
    if(!m_validationTree || !m_validationSummary)
        return;
    m_validationTree->clear();
    if(m_validationShowMore)
        m_validationShowMore->hide();
    if(m_familyValidation.members.isEmpty())
    {
        m_validationSummary->setText(tr("No active MOB family."));
        return;
    }
    m_validationSummary->setText(m_familyValidation.summary());
    QTreeWidgetItem* membersRoot = new QTreeWidgetItem(m_validationTree,
        QStringList() << tr("INFO") << tr("Family members")
                      << QString() << tr("%1 MOB(s)").arg(m_familyValidation.members.size()));
    for(const SMobFamilyMember& member : m_familyValidation.members)
    {
        const QString status = member.missing ? tr("missing")
            : (!member.error.isEmpty() ? tr("unreadable")
                                       : (member.inMemory ? tr("in-memory") : tr("disk")));
        QTreeWidgetItem* item = new QTreeWidgetItem(membersRoot,
            QStringList() << status.toUpper() << member.fileName << member.path
                          << QString("%1; %2").arg(member.sourceRole, member.inclusionReason));
        item->setData(0, kValidationPathRole, member.path);
        item->setData(0, kValidationCopyRole,
                      QString("%1\nSource: %2\nRole: %3\nReason: %4")
                          .arg(member.fileName, member.path, member.sourceRole,
                               member.inclusionReason));
        item->setToolTip(2, member.path + (member.requestedBy.isEmpty()
            ? QString() : QString("\nRequested by: %1").arg(member.requestedBy))
            + (member.sourceSha256.isEmpty() ? QString()
               : QString("\nSHA-256: %1").arg(QString::fromLatin1(member.sourceSha256.toHex()))));
        for(const QString& shadowed : member.shadowedPaths)
        {
            QTreeWidgetItem* shadow = new QTreeWidgetItem(item,
                QStringList() << tr("SHADOWED") << member.fileName << shadowed
                              << tr("Not collision-checked; workspace source overrides it"));
            shadow->setData(0, kValidationPathRole, shadowed);
            shadow->setData(0, kValidationCopyRole,
                            tr("Shadowed source for %1\n%2").arg(member.fileName, shadowed));
        }
    }
    const int visibleFindingCount = qMin(m_validationFindingLimit,
                                         m_familyValidation.findings.size());
    for(int findingIndex = 0; findingIndex < visibleFindingCount; ++findingIndex)
    {
        const SMobFamilyFinding& finding = m_familyValidation.findings.at(findingIndex);
        QString severity;
        QColor color;
        switch(finding.severity)
        {
        case EMobFamilyFindingSeverity::Critical:
            severity = tr("CRITICAL"); color = QColor(220, 70, 70); break;
        case EMobFamilyFindingSeverity::Warning:
            severity = tr("WARNING"); color = QColor(230, 160, 45); break;
        case EMobFamilyFindingSeverity::Incomplete:
            severity = tr("INCOMPLETE"); color = QColor(190, 120, 230); break;
        case EMobFamilyFindingSeverity::Info:
            severity = tr("INFO"); color = QColor(130, 160, 190); break;
        }
        QTreeWidgetItem* item = new QTreeWidgetItem(m_validationTree,
            QStringList() << severity << finding.category
                          << (finding.locations.isEmpty() ? QString() : finding.locations.first())
                          << finding.message);
        item->setForeground(0, QBrush(color));
        item->setToolTip(2, finding.locations.join('\n'));
        item->setData(0, kValidationPathRole,
                      finding.locations.isEmpty() ? QString() : finding.locations.first());
        item->setData(0, kValidationCopyRole,
                      QString("%1 %2\n%3\n%4")
                          .arg(severity, finding.category, finding.message,
                               finding.locations.join('\n')));
        if(finding.hasNid)
        {
            const QVector<SMobNidOwner> owners = m_familyValidation.nidOwners.value(finding.nid);
            const SMobNidOwner* matchingOwner = nullptr;
            for(const SMobNidOwner& owner : owners)
            {
                for(const QString& location : finding.locations)
                {
                    if(location.startsWith(owner.mobPath + ":", Qt::CaseInsensitive)
                       || location.compare(owner.mobPath, Qt::CaseInsensitive) == 0)
                    {
                        matchingOwner = &owner;
                        break;
                    }
                }
                if(matchingOwner)
                    break;
            }
            if(matchingOwner)
            {
                item->setData(0, kValidationPathRole, matchingOwner->mobPath);
                item->setData(0, kValidationNidRole, matchingOwner->nid);
                item->setData(0, kValidationSectionRole, matchingOwner->sectionPath);
                item->setToolTip(1, tr("Double-click to load its MOB if needed, then select this object."));
            }
        }
        for(const QString& location : finding.locations)
            new QTreeWidgetItem(item, QStringList() << QString() << tr("Location")
                                                    << location << QString());
        if(finding.hasNid)
        {
            const QVector<SMobNidOwner> owners = m_familyValidation.nidOwners.value(finding.nid);
            for(const SMobNidOwner& owner : owners)
            {
                QString detail = tr("NID %1 — %2")
                    .arg(owner.nid)
                    .arg(owner.objectName.isEmpty() ? owner.sectionPath : owner.objectName);
                if(finding.code == "nid.collision" || finding.code == "nid.zero_reserved")
                    detail += tr("; use Review and Repair to calculate a replacement");
                QTreeWidgetItem* ownerItem = new QTreeWidgetItem(item,
                    QStringList() << QString() << tr("Object") << owner.mobPath << detail);
                ownerItem->setData(0, kValidationPathRole, owner.mobPath);
                ownerItem->setData(0, kValidationCopyRole,
                    QString("NID %1\nMOB: %2\nObject: %3\nSection: %4")
                        .arg(owner.nid).arg(owner.mobPath,
                            owner.objectName.isEmpty() ? owner.sectionPath : owner.objectName,
                            owner.sectionPath));
                ownerItem->setData(0, kValidationNidRole, owner.nid);
                ownerItem->setData(0, kValidationSectionRole, owner.sectionPath);
                ownerItem->setToolTip(1, tr("Double-click to load its MOB if needed, then select this object."));
            }
        }
    }
    if(m_validationShowMore && visibleFindingCount < m_familyValidation.findings.size())
    {
        m_validationShowMore->setText(tr("Show 250 more findings (%1 remaining)")
            .arg(m_familyValidation.findings.size() - visibleFindingCount));
        m_validationShowMore->show();
    }
    membersRoot->setExpanded(true);
    if(m_familyValidation.criticalCount() > 0 && m_validationDock)
        m_validationDock->show();
}

void MainWindow::showMobFamilyValidation()
{
    refreshMobFamilyValidation();
    if(m_validationDock)
    {
        m_validationDock->show();
        ei::refreshFloatingDockTabs(m_validationDock, m_messageLogDock);
        m_validationDock->raise();
        m_validationDock->activateWindow();
        m_validationDock->setFocus();
    }
}

void MainWindow::showValidationWarning(const QString& title, const QString& text)
{
    QMessageBox message(QMessageBox::Warning, title, text, QMessageBox::NoButton, this);
    message.setTextFormat(Qt::PlainText);
    // Target only the text label. Applying a minimum width to every QLabel also
    // expands QMessageBox's icon label and pushes the useful text offscreen.
    message.setStyleSheet("QLabel#qt_msgbox_label { min-width: 980px; }");
    message.setMinimumWidth(1100);
    if(QLabel* textLabel = message.findChild<QLabel*>("qt_msgbox_label"))
    {
        textLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        textLabel->setWordWrap(false);
        textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    QPushButton* openPanel = message.addButton(tr("Open Validation Panel"),
                                               QMessageBox::ActionRole);
    message.addButton(QMessageBox::Ok);
    message.exec();
    if(message.clickedButton() == openPanel)
        showMobFamilyValidation();
}

void MainWindow::showMapTestResultNotification(const QJsonObject& report)
{
    const int errors = report.value("errorCount").toInt();
    const int warnings = report.value("warningCount").toInt();
    const QString reportText = QString::fromUtf8(
        QJsonDocument(report).toJson(QJsonDocument::Compact));
    ei::log(errors > 0 ? eLogFatal : (warnings > 0 ? eLogWarning : eLogInfo),
            "JMV authoritative test result: " + reportText);
    const bool failed = errors > 0;
    const bool warned = !failed && warnings > 0;
    QString detail;
    const QJsonArray problems = failed ? report.value("errors").toArray()
                                       : report.value("warnings").toArray();
    if(!problems.isEmpty())
        detail = problems.first().toString().simplified();
    if(detail.size() > 180)
        detail = detail.left(177) + "...";
    const QString status = failed
        ? tr("Map test finished with %1 error(s).").arg(errors)
        : (warned ? tr("Map test finished with %1 warning(s).").arg(warnings)
                  : tr("Map test completed successfully."));
    QString text = status + tr("\nRequested: %1 / entrance %2   Runtime: %3 / entrance %4")
        .arg(report.value("requestedZone").toString())
        .arg(report.value("requestedEntrance").toInt())
        .arg(report.value("actualZone").toString())
        .arg(report.value("actualEntrance").toInt());
    if(!detail.isEmpty())
        text += "\n" + detail;
    m_mapTestResultLogPath = report.value("runLogPath").toString();
    if(m_mapTestResultLogPath.isEmpty())
        m_mapTestResultLogPath = report.value("bridgeResultPath").toString();
    showTransientResultNotification(text, failed, warned, tr("Open Log"),
        ETransientNotificationAction::OpenLog,
        failed ? 9000 : (warned ? 7000 : 5000));
}

void MainWindow::showTransientResultNotification(
    const QString& text, bool failed, bool warned, const QString& actionText,
    ETransientNotificationAction action, int timeoutMs)
{
    if(m_mapTestResultTimer)
        m_mapTestResultTimer->stop();
    if(!m_mapTestResultNotification)
    {
        m_mapTestResultNotification = new QFrame(this);
        m_mapTestResultNotification->setObjectName("mapTestResultNotification");
        m_mapTestResultNotification->setFrameShape(QFrame::StyledPanel);
        m_mapTestResultNotification->setAttribute(Qt::WA_ShowWithoutActivating);
        m_mapTestResultNotification->setFocusPolicy(Qt::NoFocus);
        m_mapTestResultNotification->setStyleSheet(
            "QFrame#mapTestResultNotification { background: #292929; border: 1px solid #808080; border-radius: 4px; }"
            "QLabel { color: #eeeeee; } QPushButton { min-width: 88px; }");
        QVBoxLayout* layout = new QVBoxLayout(m_mapTestResultNotification);
        layout->setContentsMargins(12, 10, 12, 10);
        QHBoxLayout* summary = new QHBoxLayout;
        m_mapTestResultIcon = new QLabel(m_mapTestResultNotification);
        QFont iconFont = m_mapTestResultIcon->font();
        iconFont.setPointSize(24);
        iconFont.setBold(true);
        m_mapTestResultIcon->setFont(iconFont);
        m_mapTestResultIcon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        m_mapTestResultIcon->setFixedWidth(38);
        m_mapTestResultLabel = new QLabel(m_mapTestResultNotification);
        m_mapTestResultLabel->setWordWrap(true);
        m_mapTestResultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_mapTestResultLabel->setMaximumWidth(520);
        summary->addWidget(m_mapTestResultIcon);
        summary->addWidget(m_mapTestResultLabel, 1);
        layout->addLayout(summary);
        QHBoxLayout* buttons = new QHBoxLayout;
        buttons->addStretch(1);
        m_mapTestResultActionButton = new QPushButton(m_mapTestResultNotification);
        QPushButton* dismiss = new QPushButton(tr("Dismiss"), m_mapTestResultNotification);
        buttons->addWidget(m_mapTestResultActionButton);
        buttons->addWidget(dismiss);
        layout->addLayout(buttons);
        m_mapTestResultTimer = new QTimer(m_mapTestResultNotification);
        m_mapTestResultTimer->setSingleShot(true);
        connect(m_mapTestResultTimer, &QTimer::timeout, this, [this]()
        {
            m_mapTestResultNotification->hide();
            positionAutosaveNotification();
        });
        connect(dismiss, &QPushButton::clicked, this, [this]()
        {
            m_mapTestResultTimer->stop();
            m_mapTestResultNotification->hide();
            positionAutosaveNotification();
        });
        connect(m_mapTestResultActionButton, &QPushButton::clicked, this, [this]()
        {
            m_mapTestResultTimer->stop();
            if(m_transientNotificationAction == ETransientNotificationAction::OpenLog)
            {
                if(m_mapTestResultLogPath.isEmpty()
                   || !QDesktopServices::openUrl(QUrl::fromLocalFile(m_mapTestResultLogPath)))
                    statusBar()->showMessage(tr("The test-run log could not be opened."), 6000);
            }
            else if(m_transientNotificationAction
                    == ETransientNotificationAction::OpenBackupFolder)
            {
                QString error;
                if(!CBackupManager::openBackupFolder(&error))
                    QMessageBox::warning(this, tr("Endurance backups"), error);
            }
            m_mapTestResultNotification->hide();
            positionAutosaveNotification();
        });
    }

    m_mapTestResultIcon->setText(failed ? QString::fromUtf8("\xE2\x9C\x95")
                                       : (warned ? "!" : QString::fromUtf8("\xE2\x9C\x93")));
    m_mapTestResultIcon->setStyleSheet(QString("color: %1;")
        .arg(failed ? "#ff5c5c" : (warned ? "#ffc547" : "#4bd66f")));
    m_mapTestResultLabel->setText(text);
    m_transientNotificationAction = action;
    m_mapTestResultActionButton->setText(actionText);
    m_mapTestResultActionButton->setVisible(
        action != ETransientNotificationAction::None && !actionText.isEmpty());
    m_mapTestResultNotification->adjustSize();
    positionMapTestResultNotification();
    m_mapTestResultNotification->show();
    m_mapTestResultNotification->raise();
    m_mapTestResultTimer->start(qMax(1000, timeoutMs));
    positionAutosaveNotification();
}

void MainWindow::positionMapTestResultNotification()
{
    if(!m_mapTestResultNotification)
        return;
    const int right = 18;
    const int bottom = (statusBar() ? statusBar()->height() : 0) + 18;
    m_mapTestResultNotification->move(
        qMax(0, width() - m_mapTestResultNotification->width() - right),
        qMax(0, height() - m_mapTestResultNotification->height() - bottom));
}

void MainWindow::positionAutosaveNotification()
{
    if(!m_autosaveNotification)
        return;
    const int right = 18;
    int bottom = (statusBar() ? statusBar()->height() : 0) + 18;
    if(m_mapTestResultNotification && m_mapTestResultNotification->isVisible())
        bottom += m_mapTestResultNotification->height() + 10;
    m_autosaveNotification->move(
        qMax(0, width() - m_autosaveNotification->width() - right),
        qMax(0, height() - m_autosaveNotification->height() - bottom));
}

QString MainWindow::validationWarningPreview(
    const QVector<SMobFamilyFinding>& findings) const
{
    QStringList lines;
    const int shown = qMin(3, findings.size());
    for(int index = 0; index < shown; ++index)
        lines.append(conciseValidationLine(findings.at(index)));
    if(findings.size() > shown)
        lines.append("...");
    return lines.isEmpty() ? tr("Validation found a possible MOB-family problem.")
                           : lines.join('\n');
}

void MainWindow::selectValidationObject(QTreeWidgetItem* item)
{
    if(!item || !m_pView || !item->data(0, kValidationNidRole).isValid())
        return;
    const QString mobPath = item->data(0, kValidationPathRole).toString();
    const quint32 nid = item->data(0, kValidationNidRole).toUInt();
    const QString sectionPath = item->data(0, kValidationSectionRole).toString();
    bool mobLoaded = false;
    const QString wantedPath = QDir::cleanPath(QFileInfo(mobPath).absoluteFilePath());
    for(CMob* mob : m_pView->mobs())
    {
        if(mob && QDir::cleanPath(mob->filePath().absoluteFilePath()).compare(
               wantedPath, Qt::CaseInsensitive) == 0)
        {
            mobLoaded = true;
            break;
        }
    }
    if(!mobLoaded)
    {
        const QFileInfo source(mobPath);
        if(!source.isFile())
        {
            statusBar()->showMessage(
                tr("Cannot select NID %1 because its source MOB was not found: %2")
                    .arg(nid).arg(mobPath), 9000);
            return;
        }
        statusBar()->showMessage(tr("Loading %1 to select NID %2...")
            .arg(source.fileName()).arg(nid));
        if(!loadMobWithProgress(source))
        {
            statusBar()->showMessage(
                tr("Could not load %1 to select NID %2.").arg(source.fileName()).arg(nid),
                8000);
            return;
        }
    }
    if(!m_pView->selectObjectByProvenance(mobPath, nid, sectionPath))
    {
        statusBar()->showMessage(
            tr("Object NID %1 is not selectable: its provenance is ambiguous or no longer present.")
                .arg(nid), 7000);
        return;
    }
    activateWindow();
    raise();
    statusBar()->showMessage(tr("Selected NID %1 from MOB validation.").arg(nid), 4000);
}

void MainWindow::reviewAndRepairNidCollisions()
{
    ++m_validationGeneration;
    m_hasPendingValidation = false;
    const SMobFamilyValidationRequest request = mobFamilyValidationRequest();
    m_lastValidationRequestDigest = mobFamilyRequestDigest(request);
    applyMobFamilyValidationReport(CMobFamilyValidator::validate(request));
    QVector<SMobNidOwner> rows = m_familyValidation.nidOwners.value(0);
    for(auto it = m_familyValidation.nidOwners.constBegin();
        it != m_familyValidation.nidOwners.constEnd(); ++it)
    {
        if(it.key() == 0)
            continue;
        if(it.value().size() < 2)
            continue;
        bool critical = false;
        for(const SMobFamilyFinding& finding : m_familyValidation.findings)
            if(finding.code == "nid.collision" && finding.hasNid && finding.nid == it.key())
            {
                critical = true;
                break;
            }
        if(critical)
            rows += it.value();
    }
    if(rows.isEmpty())
    {
        QMessageBox::information(this, tr("NID repair"),
            tr("No repairable reserved object ID or critical NID collision is present in the active runtime family."));
        return;
    }

    QSet<QString> loadedPaths;
    for(CMob* mob : m_pView->mobs())
        if(mob)
            loadedPaths.insert(QDir::cleanPath(mob->filePath().absoluteFilePath()).toLower());
    const QString primaryPath = QDir::cleanPath(m_mapTestEntry.mobPath).toLower();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Review and Repair Object IDs"));
    dialog.resize(1080, 520);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* explanation = new QLabel(tr(
        "Nothing is changed automatically. Select the newly added/edited owners to renumber. "
        "Endurance preserves the established collision owner by default and refuses automatic repair when "
        "a script or typed ParentID reference is ambiguous. Ordinary save remains available for legacy "
        "data, but Test Current Map is blocked while an authored NID 0 remains."),
        &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    QTableWidget* table = new QTableWidget(rows.size(), 7, &dialog);
    table->setHorizontalHeaderLabels(QStringList() << tr("Repair") << tr("Current NID")
        << tr("MOB") << tr("Object") << tr("OBJTYPE") << tr("Replacement") << tr("Safety"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);

    QSet<quint32> suggestedIds;
    for(int row = 0; row < rows.size(); ++row)
    {
        const SMobNidOwner& owner = rows.at(row);
        QTableWidgetItem* check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        const QString ownerPath = QDir::cleanPath(owner.mobPath).toLower();
        const bool loaded = loadedPaths.contains(ownerPath);
        QStringList blockers;
        QVector<quint32> ids{owner.nid};
        for(const SMobFamilyMember& member : m_familyValidation.members)
        {
            const QVector<SMobScriptReference> references =
                CScriptReferenceAnalyzer::findObjectIds(member.script, ids);
            for(const SMobScriptReference& reference : references)
                blockers.append(tr("script %1:%2").arg(member.fileName).arg(reference.line));
            for(const SMobObjectSnapshot& object : member.objects)
                if(owner.nid != 0 && object.hasParentId && object.parentId == owner.nid)
                    blockers.append(tr("ParentID in %1:%2").arg(member.fileName, object.sectionPath));
        }
        if(!loaded)
            blockers.prepend(tr("MOB is not loaded for editing"));
        const bool safe = blockers.isEmpty();
        check->setCheckState(safe && (owner.nid == 0 || ownerPath != primaryPath)
            ? Qt::Checked : Qt::Unchecked);
        if(!safe)
            check->setFlags(Qt::NoItemFlags);
        table->setItem(row, 0, check);
        table->setItem(row, 1, new QTableWidgetItem(QString::number(owner.nid)));
        table->setItem(row, 2, new QTableWidgetItem(owner.mobName));
        table->setItem(row, 3, new QTableWidgetItem(QString("%1 / %2")
            .arg(owner.objectName.isEmpty() ? owner.sectionPath : owner.objectName,
                 owner.prototype.isEmpty() ? QString("-") : owner.prototype)));
        table->setItem(row, 4, new QTableWidgetItem(owner.hasObjectType
            ? QString::number(owner.objectType) : tr("unknown")));
        quint32 suggestion = 0;
        bool shared = false;
        const bool hasSuggestion = m_familyValidation.suggestReplacementNid(
            owner.mobPath, &suggestion, &shared, suggestedIds);
        if(hasSuggestion)
            suggestedIds.insert(suggestion);
        QTableWidgetItem* replacement = new QTableWidgetItem(
            hasSuggestion ? QString::number(suggestion) : QString());
        if(!safe || !hasSuggestion)
            replacement->setFlags(Qt::NoItemFlags);
        table->setItem(row, 5, replacement);
        QString safety = blockers.isEmpty()
            ? (shared ? tr("Unused, but declared by another MOB range") : tr("Ready"))
            : tr("Manual resolution required: %1").arg(blockers.join(", "));
        table->setItem(row, 6, new QTableWidgetItem(safety));
        for(int column = 1; column < 5; ++column)
            table->item(row, column)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table->item(row, 6)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
    layout->addWidget(table);
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Cancel), &QPushButton::clicked,
            &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            &dialog, &QDialog::accept);
    if(dialog.exec() != QDialog::Accepted)
        return;

    QVector<CView::SNidRepair> repairs;
    QSet<quint32> replacements;
    QMap<quint32, int> selectedPerCollision;
    for(int row = 0; row < rows.size(); ++row)
    {
        if(!table->item(row, 0) || table->item(row, 0)->checkState() != Qt::Checked)
            continue;
        bool valid = false;
        const quint64 parsed = table->item(row, 5)->text().toULongLong(&valid);
        if(!valid || parsed > std::numeric_limits<quint32>::max())
        {
            QMessageBox::warning(this, tr("NID repair"), tr("Row %1 has an invalid replacement ID.").arg(row + 1));
            return;
        }
        const quint32 replacement = quint32(parsed);
        if(!CMobFamilyValidator::isAuthorableObjectNid(replacement))
        {
            QMessageBox::warning(this, tr("NID repair"),
                tr("Replacement object IDs must be positive; NID 0 is reserved."));
            return;
        }
        if(replacements.contains(replacement) || m_familyValidation.nidOwners.contains(replacement))
        {
            QMessageBox::warning(this, tr("NID repair"),
                tr("Replacement NID %1 is already used by the family or another selected repair.").arg(replacement));
            return;
        }
        replacements.insert(replacement);
        if(rows.at(row).nid != 0)
            selectedPerCollision[rows.at(row).nid] += 1;
        CView::SNidRepair repair;
        repair.mobPath = rows.at(row).mobPath;
        repair.sectionPath = rows.at(row).sectionPath;
        repair.oldNid = rows.at(row).nid;
        repair.newNid = replacement;
        repairs.append(repair);
    }
    if(repairs.isEmpty())
    {
        QMessageBox::information(this, tr("NID repair"), tr("No repair rows were selected."));
        return;
    }
    for(auto it = selectedPerCollision.constBegin(); it != selectedPerCollision.constEnd(); ++it)
        if(it.value() >= m_familyValidation.nidOwners.value(it.key()).size())
        {
            QMessageBox::warning(this, tr("NID repair"),
                tr("At least one owner of NID %1 must keep the established ID.").arg(it.key()));
            return;
        }
    QString error;
    if(!m_pView->applyFamilyNidRepairs(repairs, &error))
    {
        QMessageBox::warning(this, tr("NID repair"), error);
        return;
    }
    ei::log(eLogInfo, QString("Applied one undoable family NID repair transaction with %1 object(s)")
        .arg(repairs.size()));
    scheduleMobFamilyValidation(0);
}

void MainWindow::notifyFamilyPreflight(const QString& operation)
{
    if(!m_pView || m_pView->mobs().isEmpty())
        return;
    refreshMobFamilyValidation();
    if(!m_familyValidation.hasActionableFindings())
        return;
    const QByteArray digest = m_familyValidation.findingDigest();
    if(digest == m_acknowledgedFindingDigest)
        return;
    m_acknowledgedFindingDigest = digest;
    ei::log(eLogWarning, QString("User continued with %1 after non-blocking MOB family validation findings:\n%2")
        .arg(operation, m_familyValidation.toText()));
    QVector<SMobFamilyFinding> actionable;
    for(const SMobFamilyFinding& finding : m_familyValidation.findings)
        // Character-mask diagnostics are intentionally report-only. They can
        // be numerous in authored/legacy data and must never interpose a modal
        // acknowledgement in Save or Test Current Map.
        if(finding.severity != EMobFamilyFindingSeverity::Info
           && finding.category != "Character model parts")
            actionable.append(finding);
    if(actionable.isEmpty())
        return;
    showValidationWarning(tr("MOB family validation"),
                          validationWarningPreview(actionable));
}

void MainWindow::notifyProposedNid(quint32 nid, const QString& mobPath)
{
    refreshMobFamilyValidation();
    const QString details = m_familyValidation.describeProposedNid(nid, mobPath);
    if(details.isEmpty())
        return;
    ei::log(eLogWarning, QString("Proposed NID %1 overlaps the active family:\n%2")
        .arg(nid).arg(details));
    QStringList detailLines = details.split('\n', QString::SkipEmptyParts);
    QVector<SMobFamilyFinding> preview;
    for(const QString& detail : detailLines)
    {
        SMobFamilyFinding finding;
        finding.severity = EMobFamilyFindingSeverity::Warning;
        finding.message = tr("NID %1: %2").arg(nid).arg(detail);
        preview.append(finding);
    }
    showValidationWarning(tr("NID overlap"), validationWarningPreview(preview));
}

void MainWindow::openCollection(const QString& path)
{
    if(m_workspaceLoadInProgress)
    {
        statusBar()->showMessage(tr("Finish loading the current workspace first."), 4000);
        return;
    }
    SEnduranceCollection document;
    QString error;
    if(!CCollectionDocument::load(path, &document, &error))
    {
        QMessageBox::warning(this, tr("Open Collection"), error);
        return;
    }
    const SResolvedCollection resolved = CCollectionDocument::resolve(document, activeMapRoots());
    if(!resolved.isValid())
    {
        QMessageBox::warning(this, tr("Open Collection"),
            tr("These collection references could not be resolved:\n\n%1")
                .arg(resolved.errors.join('\n')));
        return;
    }
    const QString profileId = activeResourceProfile().profileId;
    if(!document.profileId.isEmpty()
       && document.profileId.compare(profileId, Qt::CaseInsensitive) != 0
       && QMessageBox::warning(this, tr("Collection profile"),
            tr("This collection was saved for profile '%1', while '%2' is active. "
               "Opening it will not switch resources automatically. Continue?")
                .arg(document.profileId, profileId),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if(!isExitAllowed())
        return;
    if(!beginWorkspaceLoad(tr("collection %1").arg(QFileInfo(path).fileName())))
        return;
    QProgressDialog progress(tr("Loading collection..."), QString(), 0,
                             resolved.mobPaths.size() + 3, this);
    progress.setWindowTitle(tr("Loading map workspace"));
    configureBlockingProgress(&progress);
    progress.setValue(0);
    progress.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_undoStack->clear();
    m_pView->unloadMob("");
    // isExitAllowed() already collected the dirty-document decision.
    m_pView->unloadLand(false);
    progress.setLabelText(tr("Loading landscape..."));
    progress.setValue(1);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    setWorkspaceIdentity("collection", QFileInfo(path).absoluteFilePath(),
                         document.name.isEmpty()
                            ? QFileInfo(path).completeBaseName() : document.name,
                         document.mapZoneId);
    m_pView->loadLandscape(QFileInfo(resolved.mprPath));
    int progressValue = 2;
    for(const QString& mobPath : resolved.mobPaths)
    {
        progress.setLabelText(tr("Loading %1...").arg(QFileInfo(mobPath).fileName()));
        progress.setValue(progressValue++);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QFileInfo mob(mobPath);
        m_pView->loadMob(mob);
        rememberRecentFile(mob);
    }
    rememberRecentFile(QFileInfo(resolved.mprPath));
    refreshMapTestContext();
    progress.setValue(resolved.mobPaths.size() + 3);
    progress.close();
    endWorkspaceLoad();
    rebuildWindowTitle();
    ei::log(eLogInfo, QString("Opened collection path:%1 map:%2 mpr:%3 mobs:%4")
        .arg(path, document.mapZoneId, resolved.mprPath)
        .arg(resolved.mobPaths.size()));
}

void MainWindow::newCollection()
{
    SEnduranceCollection initial;
    initial.profileId = activeResourceProfile().profileId;
    initial.mapZoneId = m_mapTestEntry.zoneId;
    initial.name = m_mapTestEntry.zoneId.isEmpty() ? tr("New collection")
                                                   : m_mapTestEntry.zoneId;
    if(CLandscape::getInstance()->isMprLoad())
        initial.mprReference = CLandscape::getInstance()->filePath().absoluteFilePath();
    if(m_pView)
        for(CMob* mob : m_pView->mobs())
            if(mob) initial.mobReferences.append(mob->filePath().absoluteFilePath());
    createCollectionFromTemplate(initial, initial.name, true);
}

void MainWindow::setWorkspaceIdentity(const QString& kind, const QString& identifier,
                                      const QString& displayName, const QString& mapZoneId,
                                      bool syncView)
{
    m_workspaceIdentityKind = kind;
    m_workspaceDisplayName = displayName;
    m_openedMapZoneId = mapZoneId;
    m_currentCollectionPath = kind.compare("collection", Qt::CaseInsensitive) == 0
        ? QFileInfo(identifier).absoluteFilePath() : QString();
    if(syncView && m_pView)
        m_pView->setSessionWorkspaceIdentity(kind, identifier, displayName, mapZoneId);
    rebuildWindowTitle();
}

void MainWindow::loadLastSession()
{
    if(!m_pView)
        return;
    m_pView->loadSession();
    if(!CLandscape::getInstance()->isMprLoad())
        return;
    setWorkspaceIdentity(m_pView->sessionWorkspaceKind(),
                         m_pView->sessionWorkspaceIdentifier(),
                         m_pView->sessionWorkspaceDisplayName(),
                         m_pView->sessionWorkspaceMapZoneId(), false);
    refreshMapTestContext();
    ei::log(eLogInfo, QString("Restored session workspace kind:%1 identity:%2 map:%3")
        .arg(m_workspaceIdentityKind,
             m_pView->sessionWorkspaceIdentifier(), m_openedMapZoneId));
}

void MainWindow::createCollectionFromTemplate(
    const SEnduranceCollection& initial, const QString& suggestedBaseName,
    bool promptToLoad)
{
    CCollectionEditorDialog dialog(initial, m_mapCatalog.entries, this);
    for(;;)
    {
        if(dialog.exec() != QDialog::Accepted)
            return;
        QDir().mkpath(CCollectionDocument::defaultDirectory());
        QString suggested = suggestedBaseName.trimmed().isEmpty()
            ? dialog.collection().name : suggestedBaseName;
        suggested.replace(QRegularExpression("[^A-Za-z0-9_.-]+"), "-");
        const QString path = QFileDialog::getSaveFileName(this, tr("Save Collection"),
            QDir(CCollectionDocument::defaultDirectory()).filePath(suggested + ".collection"),
            tr("Endurance Collection (*.collection)"));
        if(path.isEmpty())
        {
            // The editor dialog retains the user's values. Returning to it is
            // less destructive than discarding the draft because the native
            // Save As dialog was cancelled.
            dialog.show();
            dialog.raise();
            dialog.activateWindow();
            continue;
        }
        QString outputPath = path;
        if(!outputPath.endsWith(".collection", Qt::CaseInsensitive))
            outputPath += ".collection";
        SEnduranceCollection value = dialog.collection();
        value.mprReference = CCollectionDocument::portableReference(
            value.mprReference, outputPath, activeMapRoots());
        for(QString& mob : value.mobReferences)
            mob = CCollectionDocument::portableReference(mob, outputPath, activeMapRoots());
        QString error;
        if(!CCollectionDocument::save(outputPath, value, &error))
        {
            QMessageBox::warning(this, tr("Save Collection"), error);
            continue;
        }
        refreshMapsBrowser();
        statusBar()->showMessage(tr("Collection saved: %1").arg(outputPath), 5000);
        if(promptToLoad && QMessageBox::question(this, tr("New Collection"),
            tr("The collection was created successfully.\n\nLoad it now?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
            openCollection(QFileInfo(outputPath).absoluteFilePath());
        return;
    }
}

void MainWindow::editCollection()
{
    QDir().mkpath(CCollectionDocument::defaultDirectory());
    const QString path = QFileDialog::getOpenFileName(this, tr("Edit Collection"),
        m_currentCollectionPath.isEmpty() ? CCollectionDocument::defaultDirectory()
                                          : m_currentCollectionPath,
        tr("Endurance Collection (*.collection)"));
    if(path.isEmpty()) return;
    editCollection(path);
}

void MainWindow::editCollection(const QString& path)
{
    SEnduranceCollection value;
    QString error;
    if(!CCollectionDocument::load(path, &value, &error))
    {
        QMessageBox::warning(this, tr("Edit Collection"), error);
        return;
    }
    const SEnduranceCollection original = value;
    const SResolvedCollection resolved = CCollectionDocument::resolve(value, activeMapRoots());
    if(!resolved.mprPath.isEmpty()) value.mprReference = resolved.mprPath;
    if(resolved.mobPaths.size() == value.mobReferences.size())
        value.mobReferences = resolved.mobPaths;
    CCollectionEditorDialog dialog(value, m_mapCatalog.entries, this);
    if(dialog.exec() != QDialog::Accepted) return;
    value = dialog.collection();
    value.mprReference = CCollectionDocument::portableReference(
        value.mprReference, path, activeMapRoots());
    for(QString& mob : value.mobReferences)
        mob = CCollectionDocument::portableReference(mob, path, activeMapRoots());
    const bool changed = !CCollectionDocument::equivalent(original, value);
    if(!CCollectionDocument::save(path, value, &error))
        QMessageBox::warning(this, tr("Edit Collection"), error);
    else
    {
        refreshMapsBrowser();
        const bool editsLoadedCollection = !m_currentCollectionPath.isEmpty()
            && QFileInfo(m_currentCollectionPath).absoluteFilePath().compare(
                QFileInfo(path).absoluteFilePath(), Qt::CaseInsensitive) == 0;
        if(changed && editsLoadedCollection)
        {
            const QMessageBox::StandardButton reload = QMessageBox::question(
                this, tr("Collection updated"),
                tr("This collection is currently loaded. Reload it now to apply the changed MPR, MOB set, and map identity?\n\n"
                   "If any loaded document has unsaved edits, Endurance will ask whether to save them before replacing the workspace."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if(reload == QMessageBox::Yes)
                openCollection(QFileInfo(path).absoluteFilePath());
        }
    }
}

void MainWindow::duplicateMapAsCollection(const QString& zoneId,
                                          const QString& mprPath,
                                          const QString& mobPath)
{
    if(!QFileInfo::exists(mprPath) || !QFileInfo::exists(mobPath))
    {
        QMessageBox::warning(this, tr("Duplicate as Collection"),
            tr("The map entry is incomplete and cannot seed a collection."));
        return;
    }
    SEnduranceCollection initial;
    initial.name = zoneId + tr(" Copy");
    initial.profileId = activeResourceProfile().profileId;
    initial.mapZoneId = zoneId;
    initial.mprReference = QFileInfo(mprPath).absoluteFilePath();
    initial.mobReferences.append(QFileInfo(mobPath).absoluteFilePath());
    createCollectionFromTemplate(initial, zoneId + "-copy", true);
}

void MainWindow::duplicateCollection(const QString& path)
{
    SEnduranceCollection value;
    QString error;
    if(!CCollectionDocument::load(path, &value, &error))
    {
        QMessageBox::warning(this, tr("Duplicate Collection"), error);
        return;
    }
    const SResolvedCollection resolved = CCollectionDocument::resolve(value, activeMapRoots());
    if(!resolved.isValid())
    {
        QMessageBox::warning(this, tr("Duplicate Collection"),
            tr("These collection references could not be resolved:\n\n%1")
                .arg(resolved.errors.join('\n')));
        return;
    }
    value.name += tr(" Copy");
    value.mprReference = resolved.mprPath;
    value.mobReferences = resolved.mobPaths;
    value.sourcePath.clear();
    createCollectionFromTemplate(value,
        QFileInfo(path).completeBaseName() + "-copy", true);
}

void MainWindow::saveCollectionAs(const QString& path)
{
    SEnduranceCollection value;
    QString error;
    if(!CCollectionDocument::load(path, &value, &error))
    {
        QMessageBox::warning(this, tr("Save Collection As"), error);
        return;
    }
    const SResolvedCollection resolved = CCollectionDocument::resolve(value, activeMapRoots());
    if(!resolved.isValid())
    {
        QMessageBox::warning(this, tr("Save Collection As"),
            tr("These collection references could not be resolved:\n\n%1")
                .arg(resolved.errors.join('\n')));
        return;
    }
    QString destination = QFileDialog::getSaveFileName(this, tr("Save Collection As"),
        QDir(CCollectionDocument::defaultDirectory()).filePath(
            QFileInfo(path).completeBaseName() + "-copy.collection"),
        tr("Endurance Collection (*.collection)"));
    if(destination.isEmpty())
        return;
    if(!destination.endsWith(".collection", Qt::CaseInsensitive))
        destination += ".collection";
    value.name = QFileInfo(destination).completeBaseName();
    value.mprReference = CCollectionDocument::portableReference(
        resolved.mprPath, destination, activeMapRoots());
    value.mobReferences.clear();
    for(const QString& mobPath : resolved.mobPaths)
        value.mobReferences.append(CCollectionDocument::portableReference(
            mobPath, destination, activeMapRoots()));
    value.sourcePath.clear();
    if(!CCollectionDocument::save(destination, value, &error))
        QMessageBox::warning(this, tr("Save Collection As"), error);
    else
    {
        refreshMapsBrowser();
        statusBar()->showMessage(tr("Collection saved as: %1").arg(destination), 5000);
    }
}

void MainWindow::deleteCollection(const QString& path)
{
    const QFileInfo file(path);
    if(!file.exists() || !file.isFile())
    {
        refreshMapsBrowser();
        return;
    }
    if(QMessageBox::warning(this, tr("Delete Collection"),
        tr("Delete collection '%1'?\n\n%2\n\nMap files referenced by it will not be deleted.")
            .arg(file.completeBaseName(), QDir::toNativeSeparators(file.absoluteFilePath())),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    if(!QFile::remove(file.absoluteFilePath()))
    {
        QMessageBox::warning(this, tr("Delete Collection"),
            tr("Could not delete the collection file."));
        return;
    }
    if(QFileInfo(m_currentCollectionPath).absoluteFilePath().compare(
           file.absoluteFilePath(), Qt::CaseInsensitive) == 0)
        setWorkspaceIdentity(QString(), QString(), QString(), QString());
    refreshMapsBrowser();
    statusBar()->showMessage(tr("Collection deleted: %1").arg(file.fileName()), 5000);
}

void MainWindow::saveCollection(bool saveAs)
{
    if(!m_pView || !CLandscape::getInstance()->isMprLoad() || m_pView->mobs().isEmpty())
    {
        QMessageBox::warning(this, tr("Save Collection"),
            tr("Load one MPR and at least one MOB first."));
        return;
    }
    QString path = saveAs ? QString() : m_currentCollectionPath;
    if(path.isEmpty())
    {
        QDir().mkpath(CCollectionDocument::defaultDirectory());
        path = QFileDialog::getSaveFileName(this, tr("Save Collection As"),
            CCollectionDocument::defaultDirectory(), tr("Endurance Collection (*.collection)"));
    }
    if(path.isEmpty()) return;
    if(!path.endsWith(".collection", Qt::CaseInsensitive)) path += ".collection";
    SEnduranceCollection value;
    value.name = QFileInfo(path).completeBaseName();
    value.profileId = activeResourceProfile().profileId;
    value.mapZoneId = m_mapTestEntry.zoneId;
    value.mprReference = CCollectionDocument::portableReference(
        CLandscape::getInstance()->filePath().absoluteFilePath(), path, activeMapRoots());
    for(CMob* mob : m_pView->mobs())
        if(mob) value.mobReferences.append(CCollectionDocument::portableReference(
            mob->filePath().absoluteFilePath(), path, activeMapRoots()));
    QString error;
    if(!CCollectionDocument::save(path, value, &error))
        QMessageBox::warning(this, tr("Save Collection"), error);
    else
    {
        setWorkspaceIdentity("collection", QFileInfo(path).absoluteFilePath(),
                             value.name.isEmpty()
                                ? QFileInfo(path).completeBaseName() : value.name,
                             value.mapZoneId);
        refreshMapsBrowser();
        statusBar()->showMessage(tr("Collection saved: %1").arg(path), 5000);
    }
}

void MainWindow::openMapPair(const QString& mprPath, const QString& mobPath,
                             const QString& zoneId)
{
    QElapsedTimer workspaceLoadTimer;
    workspaceLoadTimer.start();
    if(m_workspaceLoadInProgress)
    {
        statusBar()->showMessage(tr("Finish loading the current workspace first."), 4000);
        return;
    }
    if(!QFileInfo::exists(mprPath) || !QFileInfo::exists(mobPath))
    {
        QMessageBox::warning(this, tr("Open map"),
            tr("The map pair for %1 is incomplete. Refresh the active game/mod profile.").arg(zoneId));
        refreshMapsBrowser();
        return;
    }
    if(!isExitAllowed())
        return;
    if(!beginWorkspaceLoad(tr("map %1").arg(zoneId)))
        return;
    QProgressDialog progress(tr("Loading map..."), QString(), 0, 3, this);
    progress.setWindowTitle(tr("Loading map workspace"));
    configureBlockingProgress(&progress);
    progress.setValue(0);
    progress.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_undoStack->clear();
    m_pView->unloadMob("");
    // isExitAllowed() already collected the dirty-document decision.
    m_pView->unloadLand(false);
    progress.setLabelText(tr("Loading landscape..."));
    progress.setValue(1);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    setWorkspaceIdentity("map", zoneId, zoneId, zoneId);
    m_pView->loadLandscape(QFileInfo(mprPath));
    progress.setLabelText(tr("Loading %1...").arg(QFileInfo(mobPath).fileName()));
    progress.setValue(2);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QFileInfo mobFile(mobPath);
    m_pView->loadMob(mobFile);
    rememberRecentFile(QFileInfo(mprPath));
    rememberRecentFile(QFileInfo(mobPath));
    // The landscape title update can run before the matching MOB has finished
    // loading. Re-resolve the map identity from the completed pair so family
    // validation and hot reload cannot retain the previous map's primary MOB,
    // entrances, or custom-spawn context.
    refreshMapTestContext();
    ei::log(eLogInfo, QString("Opened profile map zone:%1 mpr:%2 mob:%3")
        .arg(zoneId, mprPath, mobPath));
    progress.setValue(3);
    progress.close();
    endWorkspaceLoad();
    ei::log(eLogInfo, QString("Workspace load complete kind:map identity:%1 elapsedMs:%2")
        .arg(zoneId).arg(workspaceLoadTimer.elapsed()));
    rebuildWindowTitle();
    QTimer::singleShot(0, this, [this]()
    {
        if(!m_validEntrances.isEmpty())
        {
            m_entranceCombo->setCurrentIndex(0);
            updateEntranceOverlays();
            moveCameraToActiveSpawn();
        }
    });
}

void MainWindow::connectUiButtons()
{
    CButtonConnector::getInstance()->attach(m_pView);
    CButtonConnector::getInstance()->addButton(EButtonOpSelect, m_ui->selectButton);
    CButtonConnector::getInstance()->addButton(EButtonOpMove, m_ui->moveButton);
    CButtonConnector::getInstance()->addButton(EButtonOpRotate, m_ui->rotateButton);
    CButtonConnector::getInstance()->addButton(EButtonOpScale, m_ui->scaleButton);
    // Landscape Tools is a persistent dock/mode toggle, not an exclusive
    // transform operation. Its checked state mirrors dock visibility.
    connect(m_pView, &CView::selectionStateChanged,
            this, &MainWindow::updateOperationAvailability);
    updateOperationAvailability(false);
}

void MainWindow::updateOperationAvailability(bool hasSelection)
{
    const bool hasMob = m_pView && m_pView->hasActiveMob();
    // Selection notifications can arrive while a batched tree/viewport update is
    // still settling.  The view is the source of truth for toolbar and shortcut
    // availability, so never leave the controls disabled because of a stale
    // notification argument.
    hasSelection = hasMob && m_pView->hasEditableSelection();
    const bool hasMpr = CLandscape::getInstance()->isMprLoad();
    const bool hasMovableSelection = (hasMob && hasSelection)
        || (m_customSpawnSelected && m_customSpawn.valid && m_customSpawn.custom
            && m_customSpawnContextKey == currentMapContextKey());
    const struct { EButtonOp type; QToolButton* button; bool enabled; } states[] = {
        {EButtonOpSelect, m_ui->selectButton, hasMob},
        {EButtonOpMove, m_ui->moveButton, hasMovableSelection},
        {EButtonOpRotate, m_ui->rotateButton, hasMob && hasSelection},
        {EButtonOpScale, m_ui->scaleButton, hasMob && hasSelection},
        {EButtonOpTilebrush, m_ui->tileBrushButton,
            hasMpr && m_pView->isLandscapeVisible()}
    };
    for(const auto& state : states)
    {
        state.button->setEnabled(state.enabled);
        if(QAction* action = m_operationActions.value(state.type, nullptr))
            action->setEnabled(state.enabled);
    }
    if(m_convertWorldObjectToLeverAction)
        m_convertWorldObjectToLeverAction->setEnabled(
            m_pView && m_pView->canConvertSelectedWorldObjectToLever());
}

void MainWindow::updateLoadedMapPanel()
{
    if(!m_loadedMapTree || !m_pView)
        return;
    const QSignalBlocker blocker(m_loadedMapTree);
    m_loadedMapTree->clear();

    QTreeWidgetItem* root = new QTreeWidgetItem(m_loadedMapTree);
    const bool hasMpr = CLandscape::getInstance()->isMprLoad();
    if(hasMpr)
    {
        root->setText(0, CLandscape::getInstance()->filePath().fileName()
                      + (CLandscape::getInstance()->isDirty() ? " *" : ""));
        root->setText(2, tr("MPR"));
        root->setToolTip(0, CLandscape::getInstance()->filePath().absoluteFilePath());
        root->setFlags(root->flags() | Qt::ItemIsUserCheckable);
        root->setCheckState(1, m_pView->isLandscapeVisible()
                            ? Qt::Checked : Qt::Unchecked);
        root->setData(0, kLoadedMapKindRole, kLoadedMapMpr);
    }
    else
    {
        root->setText(0, m_pView->mobs().isEmpty()
                      ? tr("No map loaded") : tr("No MPR (neutral grid)"));
        root->setText(2, tr("Workspace"));
    }
    root->setExpanded(true);

    QTreeWidgetItem* activeItem = nullptr;
    for(CMob* mob : m_pView->mobs())
    {
        if(!mob)
            continue;
        QTreeWidgetItem* item = new QTreeWidgetItem(root);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(1, m_pView->isMobVisible(mob)
                            ? Qt::Checked : Qt::Unchecked);
        item->setText(0, mob->filePath().fileName() + (mob->isDirty() ? " *" : ""));
        QSet<CNode*> selectedNodes;
        for(CNode* node : mob->nodes())
            if(node && node->nodeState() == ENodeState::eSelect) selectedNodes.insert(node);
        for(CNode* node : mob->logicNodes())
            if(node && node->nodeState() == ENodeState::eSelect) selectedNodes.insert(node);
        QStringList states;
        states.append(mob == m_pView->currentMob() ? tr("Active") : tr("Locked"));
        if(!selectedNodes.isEmpty())
            states.append(tr("Selected: %1").arg(selectedNodes.size()));
        if(!m_pView->isMobVisible(mob))
            states.append(tr("Hidden"));
        item->setText(2, states.join(", "));
        item->setToolTip(0, mob->filePath().absoluteFilePath()
            + (selectedNodes.isEmpty() ? QString()
               : tr("\n%1 selected object(s)").arg(selectedNodes.size())));
        item->setData(0, kLoadedMapKindRole, kLoadedMapMob);
        item->setData(0, kLoadedMapPathRole, mob->filePath().absoluteFilePath());
        if(mob == m_pView->currentMob())
        {
            activeItem = item;
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        }
        if(!selectedNodes.isEmpty())
        {
            const QColor background = mob == m_pView->currentMob()
                ? QColor(20, 75, 88) : QColor(92, 64, 12);
            for(int column = 0; column < 3; ++column)
            {
                item->setBackground(column, background);
                item->setForeground(column, QColor(Qt::white));
            }
        }
    }
    if(activeItem)
        m_loadedMapTree->setCurrentItem(activeItem, 0,
            QItemSelectionModel::ClearAndSelect);
    m_loadedMapTree->resizeColumnToContents(1);
    updateOperationAvailability();
}

void MainWindow::updateOutlinerSelectionPath()
{
    if(!m_outlinerSelectionPath || !m_objectPropertiesHeader
       || !m_ui || !m_ui->treeWidget)
        return;
    const auto showNavigation = [this](bool show)
    {
        const bool visible = show
            && CScene::getInstance()->getMode() == eEditModeLogic;
        m_behaviorNavigationBar->setVisible(visible);
    };
    const auto setPathText = [this](const QString& text)
    {
        static_cast<CElidedPathLabel*>(m_outlinerSelectionPath)->setFullText(text);
    };

    const bool customSelected = m_customSpawnSelected && m_customSpawn.valid
        && m_customSpawn.custom
        && m_customSpawnContextKey == currentMapContextKey();
    if(customSelected)
    {
        const QString coordinates = tr("Play from Here — X: %1  Y: %2  Z: %3")
            .arg(m_customSpawnPosition.x(), 0, 'f', 2)
            .arg(m_customSpawnPosition.y(), 0, 'f', 2)
            .arg(m_customSpawnPosition.z(), 0, 'f', 2);
        m_outlinerSelectionPath->setStyleSheet(
            "QLabel { background: #29424a; color: white; padding: 5px 7px; "
            "border-left: 3px solid #32c8e6; }");
        setPathText(coordinates);
        m_outlinerSelectionPath->setToolTip(coordinates);
        m_outlinerSelectionPath->show();
        showNavigation(false);
        m_objectPropertiesHeader->show();
        return;
    }

    const QList<CNode*> selectedNodes = m_pView
        ? m_pView->selectionSnapshot() : QList<CNode*>();
    QTreeWidgetItem* item = m_ui->treeWidget->currentItem();
    const bool virtualLogicSelection = item
        && item->data(0, Qt::UserRole + 10).isValid()
        && !dynamic_cast<CTreeObject*>(item);
    if(selectedNodes.size() > 1)
    {
        bool includesLockedObject = false;
        for(CNode* node : selectedNodes)
            if(CMob* owner = m_pView->ownerMob(node))
                includesLockedObject = includesLockedObject
                    || owner != m_pView->currentMob();
        const QString summary = virtualLogicSelection
            ? tr("%1 — %2 units selected")
                .arg(item->text(0)).arg(selectedNodes.size())
            : tr("%1 objects selected").arg(selectedNodes.size());
        m_outlinerSelectionPath->setStyleSheet(includesLockedObject
            ? "QLabel { background: #5c400c; color: white; padding: 5px 7px; border-left: 3px solid #ff8c1a; }"
            : "QLabel { background: #29424a; color: white; padding: 5px 7px; border-left: 3px solid #32c8e6; }");
        setPathText(summary);
        m_outlinerSelectionPath->setToolTip(summary);
        m_outlinerSelectionPath->show();
        showNavigation(false);
        m_objectPropertiesHeader->show();
        return;
    }
    if(!item || !item->parent())
    {
        showNavigation(false);
        m_outlinerSelectionPath->hide();
        m_objectPropertiesHeader->hide();
        return;
    }

    QStringList path;
    const bool behaviorSelection = item->data(
        0, CTreeView::BehaviorItemRole).toBool();
    for(QTreeWidgetItem* part = item; part; part = part->parent())
    {
        path.prepend(part->text(0));
        // Behavior breadcrumbs contain only the selected point, its logical
        // parents, and the owning unit/trap. MOB/type categories add no useful
        // selection context and consume the space reserved for the leaf.
        if(behaviorSelection && !part->data(
               0, CTreeView::BehaviorItemRole).toBool())
            break;
    }

    const QString hierarchy = path.join(QString::fromUtf8(" \u203a "));
    bool locked = false;
    if(CTreeObject* objectItem = dynamic_cast<CTreeObject*>(item))
        if(CMob* owner = m_pView ? m_pView->ownerMob(objectItem->node()) : nullptr)
            locked = owner != m_pView->currentMob();
    m_outlinerSelectionPath->setStyleSheet(locked
        ? "QLabel { background: #5c400c; color: white; padding: 5px 7px; border-left: 3px solid #ff8c1a; }"
        : "QLabel { background: #29424a; color: white; padding: 5px 7px; border-left: 3px solid #32c8e6; }");
    setPathText(hierarchy);
    m_outlinerSelectionPath->setToolTip(virtualLogicSelection
        ? tr("Selected Behavior setup: %1").arg(hierarchy)
        : tr("Selected object: %1").arg(hierarchy));
    m_outlinerSelectionPath->show();
    showNavigation(behaviorSelection && !virtualLogicSelection);
    m_objectPropertiesHeader->show();
}

void MainWindow::updateDocumentActionAvailability()
{
    const bool idle = !m_workspaceLoadInProgress;
    const bool hasActiveMob = m_pView && m_pView->hasActiveMob();
    const bool hasAnyMob = m_pView && !m_pView->mobs().isEmpty();
    const bool hasMpr = CLandscape::getInstance()->isMprLoad();
    const bool dirtyMpr = hasMpr && CLandscape::getInstance()->isDirty();
    m_ui->actionOpen->setEnabled(idle);
    m_ui->actionNew_MPR->setEnabled(idle);
    m_ui->actionNew_MOB->setEnabled(idle);
    m_ui->actionOpen_MPR->setEnabled(idle);
    m_ui->actionOpen_MOB->setEnabled(idle);
    m_ui->actionOpen_recent->setEnabled(idle);
    if(m_recentFilesMenu) m_recentFilesMenu->setEnabled(idle);
    if(m_mapsBrowser) m_mapsBrowser->setEnabled(idle);
    m_ui->actionSave->setEnabled(idle && hasActiveMob);
    m_ui->actionSave_as->setEnabled(idle && hasActiveMob);
    m_ui->actionSave_all_MOB_s->setEnabled(idle && hasAnyMob);
    m_ui->actionSave_landscape_MPR->setEnabled(idle && dirtyMpr);
    m_ui->actionSave_landscape_MPR_as->setEnabled(idle && hasMpr);
    m_ui->actionMap_parameters->setEnabled(idle);
    m_ui->actionClose_all->setEnabled(idle && (hasAnyMob || hasMpr));
}

bool MainWindow::beginWorkspaceLoad(const QString& description)
{
    if(m_workspaceLoadInProgress)
        return false;
    m_workspaceLoadInProgress = true;
    if(m_pView)
        m_pView->setWorkspaceLoadInProgress(true);
    ei::log(eLogInfo, QString("Workspace load started: %1").arg(description));
    updateDocumentActionAvailability();
    updateMapTestAction();
    return true;
}

void MainWindow::endWorkspaceLoad()
{
    if(!m_workspaceLoadInProgress)
        return;
    m_workspaceLoadInProgress = false;
    if(m_pView)
        m_pView->setWorkspaceLoadInProgress(false);
    ei::log(eLogInfo, "Workspace load finished");
    updateDocumentActionAvailability();
    updateMapTestAction();
    refreshViewportMinimapSources();
}

void MainWindow::showLandscapeTools()
{
    if(!m_landscapeToolsDock)
        return;
    if(m_landscapeToolsDock->isVisible())
    {
        m_landscapeToolsDock->raise();
        if(m_pView)
            m_pView->onLandscapeToolsShown();
        return;
    }
    m_landscapeToolsDock->show();
    m_landscapeToolsDock->raise();
    if(m_pView)
        m_pView->onLandscapeToolsShown();
}

void MainWindow::hideLandscapeTools()
{
    if(m_landscapeToolsDock && m_landscapeToolsDock->isVisible())
        m_landscapeToolsDock->hide();
}

void MainWindow::toggleLandscapeTools()
{
    if(!m_landscapeToolsDock)
        return;
    if(m_landscapeToolsDock->isVisible())
        hideLandscapeTools();
    else
        showLandscapeTools();
}

void MainWindow::handleEscapeShortcut()
{
    if(!m_pView)
        return;

    if(m_pickSpawnPending)
    {
        if(m_movingCustomSpawn)
        {
            m_pickSpawnPending = false;
            m_movingCustomSpawn = false;
            m_customSpawnSelected = true;
            m_pView->unsetCursor();
            m_pView->setTestSpawnMarker(m_customSpawnPosition, m_customSpawn.adjusted);
            m_pView->setTestSpawnMarkerSelected(true);
            m_pView->setLandscapeInteractionSuspended(false);
            updateCustomSpawnControls();
            updateEntranceOverlays();
            updateOperationAvailability();
            updateMapTestAction();
        }
        else if(m_playFromHereButton)
            m_playFromHereButton->setChecked(false);
        m_pView->setFocus();
        return;
    }

    // A transient transform must be cancelled before unrelated persistent UI
    // state (selected spawn marker or Landscape Tools visibility) consumes Esc.
    if(m_pView->cancelPendingOperation())
    {
        CButtonConnector::getInstance()->pressButton(EButtonOpSelect);
        m_pView->setFocus();
        return;
    }

    if(m_customSpawnSelected)
    {
        m_customSpawnSelected = false;
        m_pView->setTestSpawnMarkerSelected(false);
        m_pView->setLandscapeInteractionSuspended(false);
        updateCustomSpawnControls();
        updateEntranceOverlays();
        updateOperationAvailability();
        updateOutlinerSelectionPath();
        m_pView->setFocus();
        return;
    }

    if(m_landscapeToolsDock && m_landscapeToolsDock->isVisible())
    {
        hideLandscapeTools();
        m_pView->setFocus();
        return;
    }

    m_pView->clearObjectSelection();
    CButtonConnector::getInstance()->pressButton(EButtonOpSelect);
    m_pView->setFocus();
}

void MainWindow::saveUiState()
{
    if(m_uiStateSaved || !m_ui)
        return;
    if(m_landscapeToolsDock)
        hideLandscapeTools();
    QSettings uiSettings;
    ei::saveFloatingDockWindowState(m_messageLogDock);
    ei::saveFloatingDockWindowState(m_validationDock);
    uiSettings.setValue("ui/mainWindowGeometry", saveGeometry());
    uiSettings.setValue("ui/mainWindowState", saveState());
    uiSettings.setValue("ui/workspaceSplitter", m_ui->workspaceSplitter->saveState());
    uiSettings.setValue("ui/outlinerSplitter", m_ui->outlinerSplitter->saveState());
    saveMainDockExtents(uiSettings);
    uiSettings.sync();
    m_uiStateSaved = true;
}

void MainWindow::saveMainDockExtents(QSettings& settings) const
{
    const QString prefix = QStringLiteral("ui/mainDockExtentsV1/");
    auto saveDock = [&settings, &prefix](const QString& name, QDockWidget* dock) {
        if(!dock || dock->isFloating()) return;
        const QSize size = dock->size();
        if(size.width() > 0) settings.setValue(prefix + name + "/width", size.width());
        if(size.height() > 0) settings.setValue(prefix + name + "/height", size.height());
    };
    saveDock(QStringLiteral("maps"), m_mapsDock);
    saveDock(QStringLiteral("loadedMap"), m_loadedMapDock);
    saveDock(QStringLiteral("outliner"), m_outlinerDock);
    saveDock(QStringLiteral("properties"), m_propertiesDock);
    saveDock(QStringLiteral("messageLog"), m_messageLogDock);
    ei::log(eLogInfo, QStringLiteral(
            "MAIN_DOCK_EXTENTS saved maps:%1 loaded:%2 outliner:%3 properties:%4")
            .arg(m_mapsDock ? m_mapsDock->width() : 0)
            .arg(m_loadedMapDock ? m_loadedMapDock->width() : 0)
            .arg(m_outlinerDock ? m_outlinerDock->width() : 0)
            .arg(m_propertiesDock ? m_propertiesDock->width() : 0));
}

void MainWindow::restoreMainDockExtents()
{
    QSettings settings;
    const QString prefix = QStringLiteral("ui/mainDockExtentsV1/");
    const int mapsWidth = settings.value(prefix + "maps/width", 0).toInt();
    const int loadedWidth = settings.value(prefix + "loadedMap/width", 0).toInt();
    const int outlinerWidth = settings.value(prefix + "outliner/width", 0).toInt();
    const int propertiesWidth = settings.value(prefix + "properties/width", 0).toInt();
    const int rightWidth = qMax(loadedWidth, qMax(outlinerWidth, propertiesWidth));
    if(mapsWidth > 0 && m_mapsDock && !m_mapsDock->isFloating())
        resizeDocks(QList<QDockWidget*>() << m_mapsDock,
                    QList<int>() << mapsWidth, Qt::Horizontal);
    if(rightWidth > 0 && m_loadedMapDock && !m_loadedMapDock->isFloating())
        resizeDocks(QList<QDockWidget*>() << m_loadedMapDock,
                    QList<int>() << rightWidth, Qt::Horizontal);

    QList<QDockWidget*> rightDocks;
    QList<int> rightHeights;
    auto addHeight = [&settings, &prefix, &rightDocks, &rightHeights](
            const QString& name, QDockWidget* dock) {
        const int height = settings.value(prefix + name + "/height", 0).toInt();
        if(height > 0 && dock && !dock->isFloating()) {
            rightDocks.append(dock);
            rightHeights.append(height);
        }
    };
    addHeight(QStringLiteral("loadedMap"), m_loadedMapDock);
    addHeight(QStringLiteral("outliner"), m_outlinerDock);
    addHeight(QStringLiteral("properties"), m_propertiesDock);
    if(!rightDocks.isEmpty()) resizeDocks(rightDocks, rightHeights, Qt::Vertical);

    const int logHeight = settings.value(prefix + "messageLog/height", 0).toInt();
    if(logHeight > 0 && m_messageLogDock && !m_messageLogDock->isFloating())
        resizeDocks(QList<QDockWidget*>() << m_messageLogDock,
                    QList<int>() << logHeight, Qt::Vertical);
    QStringList heightValues;
    for(int value : rightHeights) heightValues.append(QString::number(value));
    ei::log(eLogInfo, QStringLiteral(
            "MAIN_DOCK_EXTENTS restored maps:%1 right:%2 heights:%3")
            .arg(mapsWidth).arg(rightWidth)
            .arg(heightValues.join(',')));
}

void MainWindow::closeHotReloadProgress()
{
    if(!m_hotReloadProgress)
        return;
    m_hotReloadProgress->close();
    m_hotReloadProgress->deleteLater();
    m_hotReloadProgress = nullptr;
}

bool MainWindow::isExitAllowed()
{
    if(!isDatabaseExitAllowed())return false;
    QStringList dirtyMobs;
    if(m_pView)
        for(CMob* mob : m_pView->mobs())
            if(mob && mob->isDirty())
                dirtyMobs.append(mob->filePath().absoluteFilePath());
    const bool dirtyLandscape = CLandscape::getInstance()->isMprLoad()
        && CLandscape::getInstance()->isDirty();
    if(!dirtyMobs.isEmpty() || dirtyLandscape)
        ei::log(eLogWarning, QString("Document-close preflight detected dirty state mobs:%1 landscape:%2 pendingOperation:%3")
                .arg(dirtyMobs.join('|'))
                .arg(dirtyLandscape ? CLandscape::getInstance()->filePath().absoluteFilePath()
                                    : QString("none"))
                .arg(m_pView && m_pView->prepareDocumentTransition() ? "cancelled" : "none"));
    else if(m_pView)
        m_pView->prepareDocumentTransition();
    return m_pView->onExit();
}

bool MainWindow::isDatabaseExitAllowed()
{
    if(!m_databaseDocument)return true;
    QString diagnostic;
    if(!m_databaseDocument->hasUnappliedChanges(&diagnostic))return true;
    QMessageBox box(QMessageBox::Warning,tr("Unapplied database changes"),tr("The active database has unapplied changes. Apply them before deciding whether to save maps, so saved maps cannot reference database rows that were never published."),QMessageBox::NoButton,this);
    QPushButton* apply=box.addButton(tr("Apply Database"),QMessageBox::AcceptRole);
    QPushButton* discard=box.addButton(tr("Exit Without Applying"),QMessageBox::DestructiveRole);
    QPushButton* cancel=box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(apply);box.setEscapeButton(cancel);box.exec();
    if(box.clickedButton()==cancel)return false;
    if(box.clickedButton()==discard)return true;
    CBackupManager backups;const SBackupResult backup=backups.snapshotBeforeSave(QFileInfo(m_databaseDocument->sourceArchivePath()));
    if(!backup.success&&!backup.skipped){QMessageBox::critical(this,tr("Database apply failed"),backup.error);return false;}
    if(!m_databaseDocument->applyToSource(&diagnostic)){QMessageBox::critical(this,tr("Database apply failed"),diagnostic);return false;}
    return true;
}

void MainWindow::closeAll()
{
    if(isExitAllowed())
    {
        m_undoStack->clear();
        m_pView->unloadMob("");
        // isExitAllowed() already obtained the user's save/discard decision.
        // Do not ask for the same dirty MPR decision again during unload.
        m_pView->unloadLand(false);
        setWorkspaceIdentity(QString(), QString(), QString(), QString());
        updateDocumentActionAvailability();
    }
}

void MainWindow::showOutliner(bool bShow)
{
    if(bShow)
    {
        m_outlinerDock->show();
        m_propertiesDock->show();
    }
    else
    {
        m_outlinerDock->hide();
        m_propertiesDock->hide();
    }
}

void MainWindow::on_actionExit_triggered()
{
    close();
}

void MainWindow::on_actionOpen_triggered()
{
    COptString* opt = dynamic_cast<COptString*>(m_settings->opt(eOptSetGeneral, "lastVisitedFolder"));
    Q_ASSERT(opt);

    QFileInfo fileName = QFileDialog::getOpenFileName(this, "Open MPR or MOB", opt->value(),
                                                       tr("Evil Islands maps (*.mpr *.mob);;MPR (*.mpr);;MOB (*.mob)"));

    if(!fileName.path().isEmpty())
        opt->setValue(fileName.dir().path());

    if(fileName.fileName().toLower().endsWith(".mpr"))
    {
        if(replaceLoadedLandscape(fileName))
            rememberRecentFile(fileName);
    }
    else if(fileName.fileName().toLower().endsWith(".mob"))
    {
        m_pView->prepareDocumentTransition();
        if(loadMobWithProgress(fileName))
            rememberRecentFile(fileName);
    }
}

void MainWindow::showGameAndModSetup()
{
    COptString* configuredProfile = dynamic_cast<COptString*>(
        m_settings->opt(eOptSetResource, "resourceProfile"));
    CResourceSetupDialog dialog(m_settings->configuredGameRoot(),
        configuredProfile ? configuredProfile->value() : QString(), this);
    centerDialogOnPreferredScreen(&dialog, this);
    QTimer::singleShot(0, &dialog, [this, &dialog]()
    {
        centerDialogOnPreferredScreen(&dialog, this);
        dialog.raise();
        dialog.activateWindow();
    });
    if(dialog.exec() != QDialog::Accepted)
        return;
    const SResourceProfileEvaluation profile = dialog.evaluation();
    const SResourceProfileEvaluation previous = activeResourceProfile();
    auto samePaths = [](const QStringList& left, const QStringList& right)
    {
        if(left.size() != right.size()) return false;
        for(int index = 0; index < left.size(); ++index)
            if(QDir::cleanPath(left.at(index)).compare(QDir::cleanPath(right.at(index)),
                                                      Qt::CaseInsensitive) != 0)
                return false;
        return true;
    };
    const bool resourceConfigurationChanged =
        QDir::cleanPath(previous.gameRoot).compare(QDir::cleanPath(profile.gameRoot),
                                                   Qt::CaseInsensitive) != 0
        || QDir::cleanPath(previous.modRoot).compare(QDir::cleanPath(profile.modRoot),
                                                   Qt::CaseInsensitive) != 0
        || previous.profileId.compare(profile.profileId, Qt::CaseInsensitive) != 0
        || !samePaths(previous.resourceDirectories, profile.resourceDirectories)
        || !samePaths(previous.mapDirectories, profile.mapDirectories);
    if(resourceConfigurationChanged && m_pView
       && (CLandscape::getInstance()->isMprLoad() || !m_pView->mobs().isEmpty()))
    {
        if(!isExitAllowed())
            return;
        m_undoStack->clear();
        m_pView->unloadMob("");
        // isExitAllowed() already collected the dirty-document decision.
        m_pView->unloadLand(false);
        setWorkspaceIdentity(QString(), QString(), QString(), QString());
        ei::log(eLogInfo, QString("Unloaded documents before resource reinitialization profile:%1 -> %2 root:%3 -> %4")
            .arg(previous.profileId, profile.profileId,
                 previous.gameRoot, profile.gameRoot));
    }
    m_settings->applyResourceProfile(profile);
    if(m_pView)
        m_pView->refreshAfterResourceSetup();
    refreshMapsBrowser();
    updateWindowTitle(eTitleTypeData::eTitleTypeDataMpr, m_sWindowTitle.mpr);
    QMessageBox confirmation(QMessageBox::Information, tr("Game and mod setup"),
        tr("%1 is ready. Endurance found %2 RES archives across %3 ordered resource folders.")
            .arg(profile.profileName).arg(profile.archiveCount)
            .arg(profile.resourceDirectories.size()), QMessageBox::Ok, this);
    centerDialogOnPreferredScreen(&confirmation, this);
    QTimer::singleShot(0, &confirmation, [this, &confirmation]()
    {
        centerDialogOnPreferredScreen(&confirmation, this);
        confirmation.raise();
        confirmation.activateWindow();
    });
    confirmation.exec();
}

void MainWindow::rememberRecentFile(const QFileInfo& fileName)
{
    if(!fileName.exists())
        return;
    COptStringList* option = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetGeneral, "recentFiles"));
    if(!option)
        return;

    const QString path = fileName.absoluteFilePath();
    QVector<QString> files = option->value();
    for(int i = files.size() - 1; i >= 0; --i)
    {
        if(QString::compare(files.at(i), path, Qt::CaseInsensitive) == 0)
            files.remove(i);
    }
    files.prepend(path);
    while(files.size() > 12)
        files.removeLast();
    option->setValue(files);
    rebuildRecentFilesMenu();
}

void MainWindow::rebuildRecentFilesMenu()
{
    if(!m_recentFilesMenu)
        return;
    m_recentFilesMenu->clear();
    COptStringList* option = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetGeneral, "recentFiles"));
    QVector<QString> files = option ? option->value() : QVector<QString>();
    if(files.isEmpty())
    {
        QAction* empty = m_recentFilesMenu->addAction(tr("No recent files"));
        empty->setEnabled(false);
        return;
    }

    for(const QString& path : files)
    {
        QFileInfo file(path);
        QAction* action = m_recentFilesMenu->addAction(QString("%1  —  %2")
                .arg(file.fileName(), QDir::toNativeSeparators(file.absolutePath())));
        action->setToolTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path]()
        {
            QFileInfo recent(path);
            if(!recent.exists())
            {
                COptStringList* recentOption = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetGeneral, "recentFiles"));
                if(recentOption)
                {
                    QVector<QString> remaining = recentOption->value();
                    for(int i = remaining.size() - 1; i >= 0; --i)
                        if(QString::compare(remaining.at(i), path, Qt::CaseInsensitive) == 0)
                            remaining.remove(i);
                    recentOption->setValue(remaining);
                }
                rebuildRecentFilesMenu();
                QMessageBox::warning(this, tr("Open recent file"), tr("The file no longer exists:\n%1").arg(path));
                return;
            }
            openDocument(recent);
        });
    }
    m_recentFilesMenu->addSeparator();
    QAction* clear = m_recentFilesMenu->addAction(tr("Clear Recent Files"));
    connect(clear, &QAction::triggered, this, [this]()
    {
        if(COptStringList* option = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetGeneral, "recentFiles")))
            option->setValue({});
        rebuildRecentFilesMenu();
    });
}

bool MainWindow::openDocument(const QFileInfo& fileName)
{
    if(m_workspaceLoadInProgress)
        return false;
    const QString suffix = fileName.suffix().toLower();
    if(!fileName.exists() || (suffix != "mpr" && suffix != "mob"))
    {
        ei::log(eLogWarning, "Cannot open document: " + fileName.absoluteFilePath());
        return false;
    }

    COptString* opt = dynamic_cast<COptString*>(m_settings->opt(eOptSetGeneral, "lastVisitedFolder"));
    if(opt)
        opt->setValue(fileName.dir().path());
    m_pView->prepareDocumentTransition();
    if(suffix == "mpr")
    {
        if(!replaceLoadedLandscape(fileName))
            return false;
    }
    else
    {
        QFileInfo mobFile(fileName);
        if(!loadMobWithProgress(mobFile))
            return false;
    }
    rememberRecentFile(fileName);
    return true;
}

bool MainWindow::replaceLoadedLandscape(const QFileInfo& fileName)
{
    CLandscape* landscape = CLandscape::getInstance();
    if(landscape->isMprLoad())
    {
        const QFileInfo current = landscape->filePath();
        const QString currentCanonical = current.canonicalFilePath().isEmpty()
            ? current.absoluteFilePath() : current.canonicalFilePath();
        const QString requestedCanonical = fileName.canonicalFilePath().isEmpty()
            ? fileName.absoluteFilePath() : fileName.canonicalFilePath();
        if(QDir::cleanPath(currentCanonical).compare(QDir::cleanPath(requestedCanonical),
                                                     Qt::CaseInsensitive) == 0)
        {
            statusBar()->showMessage(tr("%1 is already loaded.").arg(fileName.fileName()), 4000);
            return true;
        }

        if(!isExitAllowed())
            return false;
        m_undoStack->clear();
        m_pView->unloadMob("");
        m_pView->unloadLand(false);
    }

    setWorkspaceIdentity(QString(), QString(), QString(), QString());
    m_pView->prepareDocumentTransition();
    m_undoStack->clear();
    if(!loadLandscapeWithProgress(fileName))
        return false;
    updateDocumentActionAvailability();
    return true;
}

bool MainWindow::loadMobWithProgress(QFileInfo fileName)
{
    if(!beginWorkspaceLoad(tr("MOB %1").arg(fileName.fileName())))
        return false;
    QProgressDialog progress(tr("Loading MOB: %1").arg(fileName.fileName()),
                             QString(), 0, 0, this);
    progress.setWindowTitle(tr("Loading MOB"));
    configureBlockingProgress(&progress);
    progress.setMinimumDuration(250);
    progress.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const bool loaded = m_pView->loadMob(fileName);
    progress.close();
    endWorkspaceLoad();
    updateDocumentActionAvailability();
    return loaded;
}

bool MainWindow::loadLandscapeWithProgress(const QFileInfo& fileName)
{
    if(!beginWorkspaceLoad(tr("MPR %1").arg(fileName.fileName())))
        return false;
    QProgressDialog progress(tr("Loading MPR: %1").arg(fileName.fileName()),
                             QString(), 0, 0, this);
    progress.setWindowTitle(tr("Loading MPR"));
    configureBlockingProgress(&progress);
    progress.setMinimumDuration(250);
    progress.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_pView->loadLandscape(fileName);
    progress.close();
    endWorkspaceLoad();
    return CLandscape::getInstance()->isMprLoad();
}

void MainWindow::configureAutosaveTimer()
{
    if(!m_autosaveTimer || !m_settings)
        return;
    const COptBool* enabled = dynamic_cast<const COptBool*>(m_settings->opt("autosaveEnabled"));
    const COptInt* interval = dynamic_cast<const COptInt*>(m_settings->opt("autosaveIntervalMinutes"));
    configureAutosaveTimer(enabled && enabled->value(), interval ? interval->value() : 5);
}

void MainWindow::configureAutosaveTimer(bool enabled, int intervalMinutes)
{
    if(!m_autosaveTimer)
        return;
    if(!enabled)
    {
        m_autosaveTimer->stop();
        if(m_autosaveCountdownTimer) m_autosaveCountdownTimer->stop();
        if(m_autosaveNotification) m_autosaveNotification->hide();
        ei::log(eLogInfo, "Periodic recovery autosave disabled from applied settings");
        return;
    }
    const int minutes = qBound(1, intervalMinutes, 120);
    m_autosaveTimer->start(minutes * 60 * 1000);
    ei::log(eLogInfo, QString("Periodic recovery autosave scheduled intervalMinutes:%1 active:%2 remainingMs:%3 timerIntervalMs:%4")
            .arg(minutes).arg(m_autosaveTimer->isActive()).arg(m_autosaveTimer->remainingTime())
            .arg(m_autosaveTimer->interval()));
}

void MainWindow::beginAutosaveCountdown()
{
    if(m_shutdownInProgress)
        return;
    if(!m_pView || m_pView->hasPendingOperation())
    {
        ei::log(eLogInfo, "Recovery autosave countdown deferred while an edit operation is pending");
        return;
    }
    bool dirty = false;
    for(CMob* mob : m_pView->mobs())
        dirty = dirty || (mob && mob->isDirty());
    dirty = dirty || (CLandscape::getInstance()->isMprLoad()
                      && CLandscape::getInstance()->isDirty());
    if(!dirty)
    {
        ei::log(eLogInfo, "Recovery autosave tick skipped because no document is dirty");
        return;
    }
    if(m_autosaveCountdownTimer && m_autosaveCountdownTimer->isActive())
        return;

    if(!m_autosaveNotification)
    {
        m_autosaveNotification = new QFrame(this);
        m_autosaveNotification->setObjectName("autosaveNotification");
        m_autosaveNotification->setFrameShape(QFrame::StyledPanel);
        m_autosaveNotification->setAttribute(Qt::WA_ShowWithoutActivating);
        m_autosaveNotification->setStyleSheet(
            "QFrame#autosaveNotification { background: #292929; border: 1px solid #808080; border-radius: 4px; }"
            "QLabel { color: #eeeeee; } QPushButton { min-width: 88px; }");
        QHBoxLayout* layout = new QHBoxLayout(m_autosaveNotification);
        layout->setContentsMargins(12, 10, 12, 10);
        m_autosaveCountdownLabel = new QLabel(m_autosaveNotification);
        m_cancelAutosaveButton = new QPushButton(
            tr("Cancel Autosave"), m_autosaveNotification);
        layout->addWidget(m_autosaveCountdownLabel, 1);
        layout->addWidget(m_cancelAutosaveButton);
        m_autosaveCountdownTimer = new QTimer(m_autosaveNotification);
        m_autosaveCountdownTimer->setInterval(1000);
        connect(m_autosaveCountdownTimer, &QTimer::timeout, this, [this]()
        {
            --m_autosaveCountdownSeconds;
            if(m_autosaveCountdownSeconds <= 0)
            {
                m_autosaveCountdownTimer->stop();
                m_autosaveCountdownLabel->setText(tr("Autosaving..."));
                m_cancelAutosaveButton->setEnabled(false);
                m_autosaveNotification->adjustSize();
                positionAutosaveNotification();
                m_autosaveNotification->repaint();
                performAutosave();
                m_autosaveNotification->hide();
                m_cancelAutosaveButton->setEnabled(true);
                return;
            }
            m_autosaveCountdownLabel->setText(
                tr("Recovery autosave in %1 seconds...").arg(m_autosaveCountdownSeconds));
        });
        connect(m_cancelAutosaveButton, &QPushButton::clicked, this, [this]()
        {
            m_autosaveCountdownTimer->stop();
            m_autosaveNotification->hide();
            ei::log(eLogInfo, "Recovery autosave occurrence skipped by user");
            statusBar()->showMessage(tr("This recovery autosave was skipped."), 3000);
        });
    }
    m_autosaveCountdownSeconds = 15;
    m_cancelAutosaveButton->setEnabled(true);
    m_autosaveCountdownLabel->setText(
        tr("Recovery autosave in %1 seconds...").arg(m_autosaveCountdownSeconds));
    m_autosaveNotification->adjustSize();
    positionAutosaveNotification();
    m_autosaveNotification->show();
    m_autosaveNotification->raise();
    m_autosaveCountdownTimer->start();
    ei::log(eLogInfo, "Recovery autosave countdown started seconds:15");
}

void MainWindow::performAutosave()
{
    if(m_shutdownInProgress)
        return;
    ei::log(eLogInfo, QString("Recovery autosave tick active:%1 intervalMs:%2")
            .arg(m_autosaveTimer && m_autosaveTimer->isActive())
            .arg(m_autosaveTimer ? m_autosaveTimer->interval() : -1));
    if(!m_pView || m_pView->hasPendingOperation())
    {
        ei::log(eLogInfo, "Recovery autosave deferred while an edit operation is pending");
        return;
    }
    int written = 0;
    int failed = 0;
    CBackupManager manager;
    for(CMob* mob : m_pView->mobs())
    {
        if(!mob || !mob->isDirty())
            continue;
        QByteArray candidate;
        CValidationReport report;
        QString saveClass;
        if(!mob->buildValidatedCandidate(&candidate, &report, &saveClass))
        {
            ++failed;
            ei::log(eLogWarning, QString("Recovery autosave skipped unsafe MOB path:%1 findings:%2")
                .arg(mob->filePath().absoluteFilePath(), report.toText().trimmed()));
            continue;
        }
        const SBackupResult result = manager.snapshotAutosave(mob->filePath(), candidate);
        if(result.success)
            written += result.skipped ? 0 : 1;
        else
        {
            ++failed;
            ei::log(eLogWarning, QString("Recovery autosave failed path:%1 error:%2")
                .arg(mob->filePath().absoluteFilePath(), result.error));
        }
    }
    CLandscape* landscape = CLandscape::getInstance();
    if(landscape->isMprLoad() && landscape->isDirty())
    {
        QByteArray candidate;
        CValidationReport report;
        if(!m_pView->buildValidatedLandscapeCandidate(&candidate, &report))
        {
            ++failed;
            ei::log(eLogWarning, QString("Recovery autosave skipped unsafe MPR path:%1 findings:%2")
                .arg(landscape->filePath().absoluteFilePath(),
                     report.toText().trimmed()));
        }
        else
        {
            const SBackupResult result = manager.snapshotAutosave(
                landscape->filePath(), candidate);
            if(result.success)
            {
                written += result.skipped ? 0 : 1;
                ei::log(eLogInfo, QString("Recovery autosave MPR path:%1 sha256:%2 skipped:%3")
                    .arg(landscape->filePath().absoluteFilePath(),
                         result.sha256).arg(result.skipped));
            }
            else
            {
                ++failed;
                ei::log(eLogWarning, QString("Recovery autosave failed MPR path:%1 error:%2")
                    .arg(landscape->filePath().absoluteFilePath(), result.error));
            }
        }
    }
    if(written > 0)
        statusBar()->showMessage(tr("Recovery autosave created %1 new generation(s).").arg(written), 4000);
    ei::log(failed > 0 ? eLogWarning : eLogInfo,
            QString("Recovery autosave pass complete written:%1 failed:%2").arg(written).arg(failed));
}

void MainWindow::showRecoverableAutosaves()
{
    if(m_recoverableAutosaveNoticeCount <= 0
       || m_recoverableAutosaveNoticeKeys.isEmpty())
        return;
    ei::log(eLogInfo, QString("Recoverable editor autosaves available count:%1")
            .arg(m_recoverableAutosaveNoticeCount));
    showTransientResultNotification(
        tr("%1 recoverable editor autosave generation(s) are available.\n"
           "Their source maps are unchanged; open the backup folder to inspect or copy them.")
            .arg(m_recoverableAutosaveNoticeCount),
        false, true, tr("Open Backup Folder"),
        ETransientNotificationAction::OpenBackupFolder, 7000);
    QString acknowledgeError;
    if(!CBackupManager().acknowledgeRecoveries(
           m_recoverableAutosaveNoticeKeys, &acknowledgeError))
        ei::log(eLogWarning, "Could not acknowledge recovery notification: "
                + acknowledgeError);
    else
    {
        m_recoverableAutosaveNoticeCount = 0;
        m_recoverableAutosaveNoticeKeys.clear();
    }
}

void MainWindow::on_actionOpen_MPR_triggered()
{
    COptString* opt = dynamic_cast<COptString*>(m_settings->opt(eOptSetGeneral, "lastVisitedFolder"));
    Q_ASSERT(opt);
    QFileInfo fileName = QFileDialog::getOpenFileName(this, "Open MPR", opt->value(), tr("MPR (*.mpr)"));
    if(fileName.fileName().isEmpty())
        return;
    opt->setValue(fileName.dir().path());
    if(replaceLoadedLandscape(fileName))
        rememberRecentFile(fileName);
}

void MainWindow::on_actionNew_MPR_triggered()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Create New MPR"));
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog.resize(760, 820);
    QVBoxLayout* root = new QVBoxLayout(&dialog);
    QLabel* explanation = new QLabel(tr(
        "A new landscape reuses the exact atlas, material, and animation metadata from a proven template MPR. "
        "Endurance creates fresh MP/SEC members and does not invent a MOB or map.txt registration. "
        "If you change the namespace, matching atlas textures must already exist in the active resource profile."), &dialog);
    explanation->setWordWrap(true);
    root->addWidget(explanation);
    QFormLayout* form = new QFormLayout;
    root->addLayout(form);

    QLineEdit* templatePath = new QLineEdit(&dialog);
    templatePath->setToolTip(tr(
        "A proven MPR whose atlas namespace, tile metadata, liquid materials, animations, and other MP settings will be copied. The template itself is never modified."));
    QPushButton* browseTemplate = new QPushButton(tr("Browse..."), &dialog);
    QWidget* templateRow = new QWidget(&dialog);
    QHBoxLayout* templateLayout = new QHBoxLayout(templateRow);
    templateLayout->setContentsMargins(0, 0, 0, 0);
    templateLayout->addWidget(templatePath, 1);
    templateLayout->addWidget(browseTemplate);
    form->addRow(tr("Terrain template MPR"), templateRow);

    QLineEdit* nameSpace = new QLineEdit(&dialog);
    nameSpace->setToolTip(tr(
        "Internal MP/SEC resource-name stem for the new archive. Keep the template namespace unless matching atlas textures already exist under a different namespace."));
    form->addRow(tr("MPR namespace"), nameSpace);
    QSpinBox* sectorsX = new QSpinBox(&dialog);
    QSpinBox* sectorsY = new QSpinBox(&dialog);
    sectorsX->setRange(1, 32);
    sectorsY->setRange(1, 32);
    sectorsX->setValue(4);
    sectorsY->setValue(4);
    sectorsX->setToolTip(tr(
        "Horizontal map size. One sector contains 16 x 16 terrain tiles (33 x 33 shared vertices). Maximum: 32 sectors."));
    sectorsY->setToolTip(tr(
        "Vertical map size. One sector contains 16 x 16 terrain tiles (33 x 33 shared vertices). Maximum: 32 sectors."));
    form->addRow(tr("Width (16-tile sectors)"), sectorsX);
    form->addRow(tr("Height (16-tile sectors)"), sectorsY);
    QDoubleSpinBox* maxZ = new QDoubleSpinBox(&dialog);
    maxZ->setRange(0.001, 1000000.0);
    maxZ->setDecimals(3);
    maxZ->setValue(100.0);
    maxZ->setToolTip(tr(
        "World-space height represented by raw height 65535. Raw 0 is world height 0; raw 32768 is approximately half of maxZ. Increase maxZ for taller elevation differences without changing the 0-65535 raw precision."));
    form->addRow(tr("Vertical scale (maxZ)"), maxZ);
    QSpinBox* flatRaw = new QSpinBox(&dialog);
    flatRaw->setRange(0, 65535);
    flatRaw->setValue(32768);
    flatRaw->setToolTip(tr(
        "Initial terrain height when no heightmap is selected. World height = raw height / 65535 x maxZ. A selected heightmap replaces this value across the terrain."));
    form->addRow(tr("Flat raw height"), flatRaw);
    QSpinBox* atlasSlot = new QSpinBox(&dialog);
    atlasSlot->setRange(0, 16383);
    atlasSlot->setToolTip(tr(
        "Existing terrain tile in the template atlas namespace. Endurance stores this packed atlas slot on every initial land tile; it does not create atlas imagery."));
    form->addRow(tr("Initial atlas slot"), atlasSlot);
    QComboBox* rotation = new QComboBox(&dialog);
    rotation->addItems({tr("0 degrees"), tr("90 degrees"),
                        tr("180 degrees"), tr("270 degrees")});
    rotation->setToolTip(tr(
        "Rotation applied to the selected initial land atlas tile."));
    form->addRow(tr("Initial tile rotation"), rotation);
    QCheckBox* closeEdges = new QCheckBox(tr("Taper the outside edge down to raw height 0"), &dialog);
    closeEdges->setChecked(true);
    closeEdges->setToolTip(tr(
        "Gradually lowers the outer terrain band to raw 0 after heightmap import, creating a closed map edge. This deliberately replaces heightmap values inside that band."));
    form->addRow(QString(), closeEdges);
    QSpinBox* taperTiles = new QSpinBox(&dialog);
    taperTiles->setRange(1, 16);
    taperTiles->setValue(2);
    taperTiles->setToolTip(tr(
        "Width of the terrain edge band affected by the smooth taper."));
    form->addRow(tr("Edge taper width (tiles)"), taperTiles);

    QLineEdit* heightmapPath = new QLineEdit(&dialog);
    heightmapPath->setObjectName(QStringLiteral("newMprHeightmapPath"));
    heightmapPath->setPlaceholderText(tr("Optional; leave blank for a flat map"));
    heightmapPath->setToolTip(tr(
        "Optional grayscale image stretched across the complete map. Black pixels map to the black raw endpoint and white pixels to the white raw endpoint; either endpoint may be higher."));
    QPushButton* browseHeightmap = new QPushButton(tr("Browse..."), &dialog);
    QPushButton* clearHeightmap = new QPushButton(tr("Clear"), &dialog);
    clearHeightmap->setObjectName(QStringLiteral("newMprHeightmapClear"));
    QWidget* heightmapRow = new QWidget(&dialog);
    QHBoxLayout* heightmapLayout = new QHBoxLayout(heightmapRow);
    heightmapLayout->setContentsMargins(0, 0, 0, 0);
    heightmapLayout->addWidget(heightmapPath, 1);
    heightmapLayout->addWidget(browseHeightmap);
    heightmapLayout->addWidget(clearHeightmap);
    form->addRow(tr("Heightmap"), heightmapRow);
    QSpinBox* heightmapMinimum = new QSpinBox(&dialog);
    QSpinBox* heightmapMaximum = new QSpinBox(&dialog);
    heightmapMinimum->setRange(0, 65535);
    heightmapMaximum->setRange(0, 65535);
    heightmapMaximum->setValue(65535);
    heightmapMinimum->setToolTip(tr(
        "Raw terrain height assigned to black pixels. It may be higher than the white endpoint to invert the heightmap."));
    heightmapMaximum->setToolTip(tr(
        "Raw terrain height assigned to white pixels. It may be lower than the black endpoint to invert the heightmap."));
    form->addRow(tr("Heightmap black raw height"), heightmapMinimum);
    form->addRow(tr("Heightmap white raw height"), heightmapMaximum);
    QCheckBox* flipHeightmap = new QCheckBox(tr("Flip heightmap vertically"), &dialog);
    flipHeightmap->setToolTip(tr(
        "Mirrors the source image north-to-south before it is stretched onto the map."));
    form->addRow(QString(), flipHeightmap);

    QGroupBox* overlayGroup = new QGroupBox(tr("Initial overlay plane"), &dialog);
    QFormLayout* overlayForm = new QFormLayout(overlayGroup);
    QCheckBox* createOverlay = new QCheckBox(
        tr("Create a full overlay plane (for water, lava, or other surfaces)"),
        overlayGroup);
    createOverlay->setObjectName(QStringLiteral("newMprCreateOverlay"));
    createOverlay->setToolTip(tr(
        "Creates an active overlay tile over the entire new map. Leave disabled for terrain-only maps."));
    overlayForm->addRow(QString(), createOverlay);
    QSpinBox* overlayRawHeight = new QSpinBox(overlayGroup);
    overlayRawHeight->setRange(0, 65535);
    overlayRawHeight->setValue(32768);
    overlayRawHeight->setToolTip(tr(
        "Constant raw height of the overlay plane. World height = raw height / 65535 x maxZ. This plane does not use the terrain heightmap."));
    overlayForm->addRow(tr("Plane raw height"), overlayRawHeight);
    QSpinBox* overlayAtlasSlot = new QSpinBox(overlayGroup);
    overlayAtlasSlot->setRange(0, 16383);
    overlayAtlasSlot->setToolTip(tr(
        "Existing atlas tile used by every initial overlay cell, such as a water or lava tile."));
    overlayForm->addRow(tr("Overlay atlas slot"), overlayAtlasSlot);
    QComboBox* overlayRotation = new QComboBox(overlayGroup);
    overlayRotation->addItems({tr("0 degrees"), tr("90 degrees"),
                               tr("180 degrees"), tr("270 degrees")});
    overlayRotation->setToolTip(tr(
        "Rotation applied to the selected initial overlay atlas tile."));
    overlayForm->addRow(tr("Overlay tile rotation"), overlayRotation);
    QSpinBox* overlayMaterial = new QSpinBox(overlayGroup);
    overlayMaterial->setRange(0, 0);
    overlayMaterial->setToolTip(tr(
        "Existing liquid/overlay material from the template MP. This controls material properties; the atlas slot controls the visible tile texture."));
    overlayForm->addRow(tr("Liquid material index"), overlayMaterial);
    QCheckBox* taperOverlay = new QCheckBox(
        tr("Taper overlay edges down to raw height 0"), overlayGroup);
    taperOverlay->setToolTip(tr(
        "Gradually lowers only the overlay plane around the outside edge. The land taper remains independent."));
    overlayForm->addRow(QString(), taperOverlay);
    QSpinBox* overlayTaperTiles = new QSpinBox(overlayGroup);
    overlayTaperTiles->setRange(1, 16);
    overlayTaperTiles->setValue(2);
    overlayTaperTiles->setToolTip(tr(
        "Width of the overlay edge band affected by the smooth taper."));
    overlayForm->addRow(tr("Overlay taper width (tiles)"), overlayTaperTiles);
    root->addWidget(overlayGroup);
    for(QWidget* control : {static_cast<QWidget*>(overlayRawHeight),
                            static_cast<QWidget*>(overlayAtlasSlot),
                            static_cast<QWidget*>(overlayRotation),
                            static_cast<QWidget*>(overlayMaterial),
                            static_cast<QWidget*>(taperOverlay),
                            static_cast<QWidget*>(overlayTaperTiles)})
        control->setEnabled(false);

    QLabel* previewStatus = new QLabel(&dialog);
    previewStatus->setObjectName(QStringLiteral("newMprHeightmapStatus"));
    previewStatus->setWordWrap(true);
    CNewMprCanvasPreview* canvasPreview = new CNewMprCanvasPreview(&dialog);
    canvasPreview->setObjectName(QStringLiteral("newMprCanvasPreview"));
    root->addWidget(previewStatus);
    root->addWidget(canvasPreview, 1);

    const QFileInfo currentTemplate = CLandscape::getInstance()->isMprLoad()
        ? CLandscape::getInstance()->filePath() : QFileInfo();
    if(currentTemplate.exists())
    {
        templatePath->setText(currentTemplate.absoluteFilePath());
        CMprDocument current;
        CValidationReport report;
        if(current.load(currentTemplate, &report))
        {
            nameSpace->setText(current.mapInfo().innerNamespace);
            maxZ->setValue(current.mapInfo().maxZ);
            const quint32 atlasCount = qMin<quint32>(256, current.mapInfo().textureCount);
            atlasSlot->setMaximum(qMax(0, int(atlasCount * 64) - 1));
            overlayAtlasSlot->setMaximum(qMax(0, int(atlasCount * 64) - 1));
            overlayMaterial->setMaximum(qMax(0, int(current.mapInfo().materialCount) - 1));
        }
    }
    connect(browseTemplate, &QPushButton::clicked, &dialog, [&]() {
        const QString path = QFileDialog::getOpenFileName(&dialog,
            tr("Choose Terrain Template MPR"), templatePath->text(), tr("MPR (*.mpr)"));
        if(path.isEmpty())
            return;
        templatePath->setText(path);
        CMprDocument candidateTemplate;
        CValidationReport report;
        if(candidateTemplate.load(QFileInfo(path), &report))
        {
            nameSpace->setText(candidateTemplate.mapInfo().innerNamespace);
            maxZ->setValue(candidateTemplate.mapInfo().maxZ);
            const quint32 atlasCount = qMin<quint32>(
                256, candidateTemplate.mapInfo().textureCount);
            atlasSlot->setMaximum(qMax(0, int(atlasCount * 64) - 1));
            overlayAtlasSlot->setMaximum(qMax(0, int(atlasCount * 64) - 1));
            overlayMaterial->setMaximum(qMax(0,
                int(candidateTemplate.mapInfo().materialCount) - 1));
        }
    });
    connect(browseHeightmap, &QPushButton::clicked, &dialog, [&]() {
        const QString path = QFileDialog::getOpenFileName(&dialog,
            tr("Choose Grayscale Heightmap"), heightmapPath->text(),
            tr("Heightmaps (*.png *.bmp *.tif *.tiff);;Images (*)"));
        if(!path.isEmpty())
            heightmapPath->setText(path);
    });
    connect(clearHeightmap, &QPushButton::clicked, heightmapPath, &QLineEdit::clear);
    connect(closeEdges, &QCheckBox::toggled, taperTiles, &QSpinBox::setEnabled);
    connect(createOverlay, &QCheckBox::toggled, &dialog, [&](bool enabled) {
        overlayRawHeight->setEnabled(enabled);
        overlayAtlasSlot->setEnabled(enabled);
        overlayRotation->setEnabled(enabled);
        overlayMaterial->setEnabled(enabled);
        taperOverlay->setEnabled(enabled);
        overlayTaperTiles->setEnabled(enabled && taperOverlay->isChecked());
    });
    connect(taperOverlay, &QCheckBox::toggled, &dialog, [&](bool enabled) {
        overlayTaperTiles->setEnabled(createOverlay->isChecked() && enabled);
    });

    QImage previewHeightmap;
    const auto refreshPreview = [&]() {
        const QString path = heightmapPath->text().trimmed();
        flatRaw->setEnabled(path.isEmpty());
        if(path.isEmpty())
        {
            previewHeightmap = QImage();
            previewStatus->setText(tr(
                "Flat terrain preview: raw %1 = %2 world units. Choose a heightmap to preview its stretch and orientation.")
                .arg(flatRaw->value())
                .arg(double(flatRaw->value()) * maxZ->value() / 65535.0,
                     0, 'f', 3));
            previewStatus->setStyleSheet(QString());
        }
        else
        {
            const SNewMprHeightmap loaded = loadNewMprHeightmap(path);
            previewHeightmap = loaded.image;
            if(previewHeightmap.isNull())
            {
                previewStatus->setText(loaded.error);
                previewStatus->setStyleSheet(QStringLiteral("color: #ff8080;"));
            }
            else
            {
                previewStatus->setText(tr(
                    "%1 x %2 image stretched to the full terrain canvas. Black: raw %3 = %4 world units. White: raw %5 = %6 world units.%7")
                    .arg(previewHeightmap.width()).arg(previewHeightmap.height())
                    .arg(heightmapMinimum->value())
                    .arg(double(heightmapMinimum->value()) * maxZ->value()
                         / 65535.0, 0, 'f', 3)
                    .arg(heightmapMaximum->value())
                    .arg(double(heightmapMaximum->value()) * maxZ->value()
                         / 65535.0, 0, 'f', 3)
                    .arg(heightmapMinimum->value() > heightmapMaximum->value()
                         ? tr(" Elevation is inverted (black is higher).")
                         : QString()));
                previewStatus->setStyleSheet(QString());
            }
        }
        canvasPreview->setCanvas(sectorsX->value(), sectorsY->value(),
            previewHeightmap, flipHeightmap->isChecked(), createOverlay->isChecked(),
            closeEdges->isChecked(), taperOverlay->isChecked());
    };
    connect(heightmapPath, &QLineEdit::textChanged, &dialog,
            [&](const QString&) { refreshPreview(); });
    connect(sectorsX, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [&](int) { refreshPreview(); });
    connect(sectorsY, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [&](int) { refreshPreview(); });
    connect(flipHeightmap, &QCheckBox::toggled, &dialog,
            [&](bool) { refreshPreview(); });
    connect(maxZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &dialog,
            [&](double) { refreshPreview(); });
    connect(flatRaw, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [&](int) { refreshPreview(); });
    connect(heightmapMinimum, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [&](int) { refreshPreview(); });
    connect(heightmapMaximum, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [&](int) { refreshPreview(); });
    connect(closeEdges, &QCheckBox::toggled, &dialog,
            [&](bool) { refreshPreview(); });
    connect(createOverlay, &QCheckBox::toggled, &dialog,
            [&](bool) { refreshPreview(); });
    connect(taperOverlay, &QCheckBox::toggled, &dialog,
            [&](bool) { refreshPreview(); });
    refreshPreview();

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create and Open..."));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked,
            &dialog, [&]() {
        CMprDocument terrainTemplate;
        CValidationReport report;
        const QFileInfo templateFile(templatePath->text());
        if(!terrainTemplate.load(templateFile, &report) || report.hasErrors())
        {
            QMessageBox::warning(&dialog, tr("Create New MPR"),
                tr("The terrain template could not be validated.\n\n%1")
                    .arg(report.toText()));
            return;
        }
        SMprCreationRequest request;
        request.innerNamespace = nameSpace->text().trimmed();
        request.sectorCountX = quint32(sectorsX->value());
        request.sectorCountY = quint32(sectorsY->value());
        request.maxZ = float(maxZ->value());
        request.flatRawHeight = quint16(flatRaw->value());
        SMprPackedTile tile;
        tile.atlasIndex = quint8(atlasSlot->value() / 64);
        tile.tileIndex = quint8(atlasSlot->value() % 64);
        tile.rotation = quint8(rotation->currentIndex());
        request.initialPackedTile = tile.encode();
        request.closeEdges = closeEdges->isChecked();
        request.edgeTaperTiles = taperTiles->value();

        const QString selectedHeightmap = heightmapPath->text().trimmed();
        if(!selectedHeightmap.isEmpty())
        {
            const SNewMprHeightmap loaded = loadNewMprHeightmap(selectedHeightmap);
            if(loaded.image.isNull())
            {
                ei::log(eLogWarning, QString("New MPR heightmap decode refused path:%1 error:%2")
                    .arg(selectedHeightmap, loaded.error));
                QMessageBox::warning(&dialog, tr("Create New MPR"), loaded.error);
                heightmapPath->setFocus();
                return;
            }
            request.heightmapWidth = loaded.image.width();
            request.heightmapHeight = loaded.image.height();
            request.heightmapMinimumRaw = float(heightmapMinimum->value());
            request.heightmapMaximumRaw = float(heightmapMaximum->value());
            request.heightmapSamples.reserve(loaded.image.width() * loaded.image.height());
            for(int outputY = 0; outputY < loaded.image.height(); ++outputY)
            {
                const int imageY = flipHeightmap->isChecked()
                    ? loaded.image.height() - 1 - outputY : outputY;
                for(int x = 0; x < loaded.image.width(); ++x)
                    request.heightmapSamples.append(
                        float(qGray(loaded.image.pixel(x, imageY))) / 255.0f);
            }
        }
        request.createOverlayPlane = createOverlay->isChecked();
        request.overlayRawHeight = quint16(overlayRawHeight->value());
        SMprPackedTile overlayTile;
        overlayTile.atlasIndex = quint8(overlayAtlasSlot->value() / 64);
        overlayTile.tileIndex = quint8(overlayAtlasSlot->value() % 64);
        overlayTile.rotation = quint8(overlayRotation->currentIndex());
        request.overlayPackedTile = overlayTile.encode();
        request.overlayMaterialIndex = qint16(overlayMaterial->value());
        request.taperOverlayEdges = taperOverlay->isChecked();
        request.overlayEdgeTaperTiles = overlayTaperTiles->value();

        QByteArray candidate;
        report = CValidationReport();
        if(!terrainTemplate.buildNewCanvas(request, &candidate, &report))
        {
            ei::log(eLogWarning, "New MPR generation refused: " + report.toText());
            QMessageBox::warning(&dialog, tr("Create New MPR"),
                tr("The new landscape could not be generated.\n\n%1")
                    .arg(report.toText()));
            return;
        }
        COptString* opt = dynamic_cast<COptString*>(
            m_settings->opt(eOptSetGeneral, "lastVisitedFolder"));
        const QString startFolder = opt ? opt->value() : templateFile.absolutePath();
        QString destinationPath = QFileDialog::getSaveFileName(&dialog,
            tr("Save New MPR"),
            QDir(startFolder).filePath(request.innerNamespace + QStringLiteral(".mpr")),
            tr("MPR (*.mpr)"));
        if(destinationPath.isEmpty())
            return;
        if(!destinationPath.endsWith(".mpr", Qt::CaseInsensitive))
            destinationPath += QStringLiteral(".mpr");
        const QFileInfo destination(destinationPath);
        if(destination.exists()
           && QMessageBox::question(&dialog, tr("Replace MPR"),
                tr("%1 already exists. Back it up and replace it?")
                    .arg(destination.fileName())) != QMessageBox::Yes)
            return;
        SMprSaveCommitRequest commit;
        commit.destination = destination;
        const SMprSaveCommitResult result = commitGeneratedMpr(candidate, commit);
        if(!result.success)
        {
            QMessageBox::critical(&dialog, tr("Create New MPR"),
                tr("The generated MPR was not committed.\n\n%1").arg(result.error));
            return;
        }
        if(opt)
            opt->setValue(destination.absolutePath());
        rememberRecentFile(destination);
        if(!replaceLoadedLandscape(destination))
        {
            QMessageBox::information(&dialog, tr("New MPR Created"),
                tr("The validated MPR was created at:\n%1\n\nIt was not opened because the current document transition was cancelled.")
                    .arg(destination.absoluteFilePath()));
            dialog.accept();
            return;
        }
        statusBar()->showMessage(tr("Created and opened %1")
            .arg(destination.fileName()), 5000);
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::on_actionNew_MOB_triggered()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Create New MOB"));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.resize(650, 560);
    QVBoxLayout* root = new QVBoxLayout(&dialog);
    QLabel* introduction = new QLabel(tr(
        "Create an empty, runtime-valid MOB. Objects, embedded AI graph, VSS, and "
        "editor-only directory data are never copied. The normal script and "
        "selected map metadata can be copied explicitly."), &dialog);
    introduction->setWordWrap(true);
    root->addWidget(introduction);

    QFormLayout* form = new QFormLayout;
    QComboBox* role = new QComboBox(&dialog);
    role->addItem(tr("Primary map MOB (SC_OBJECT_DB_FILE)"));
    role->addItem(tr("Supplemental / AutoZone MOB (PR_OBJECT_DB_FILE)"));
    role->setCurrentIndex(1);
    role->setToolTip(tr(
        "Primary MOBs own world settings and normally anchor a map. Supplemental "
        "MOBs are the usual choice for AutoZones and optional map states."));
    form->addRow(tr("MOB role"), role);

    QComboBox* templateMob = new QComboBox(&dialog);
    templateMob->addItem(tr("Do not copy from a loaded MOB"), qulonglong(0));
    if(m_pView)
    {
        for(CMob* mob : m_pView->mobs())
        {
            if(mob)
                templateMob->addItem(mob->filePath().fileName(),
                                     qulonglong(reinterpret_cast<quintptr>(mob)));
        }
    }
    templateMob->setToolTip(tr(
        "Only explicitly selected fields are copied. Objects, graphs, VSS, and "
        "editor metadata are always omitted."));
    form->addRow(tr("Copy settings from"), templateMob);

    QCheckBox* copyRanges = new QCheckBox(tr("Copy the source MOB's active ID ranges"), &dialog);
    QCheckBox* copyScript = new QCheckBox(tr("Copy normal script (SS_TEXT)"), &dialog);
    QCheckBox* copyDiplomacy = new QCheckBox(tr("Copy diplomacy table and player names"), &dialog);
    QCheckBox* copyWorld = new QCheckBox(tr("Copy primary world settings"), &dialog);
    for(QCheckBox* box : {copyRanges, copyScript, copyDiplomacy, copyWorld})
        box->setEnabled(false);
    form->addRow(QString(), copyRanges);
    form->addRow(QString(), copyScript);
    form->addRow(QString(), copyDiplomacy);
    form->addRow(QString(), copyWorld);

    QRegularExpressionValidator* unsignedValidator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{1,10}")), &dialog);
    QLineEdit* rangeMinimum = new QLineEdit(QStringLiteral("1"), &dialog);
    QLineEdit* rangeMaximum = new QLineEdit(QStringLiteral("999999"), &dialog);
    rangeMinimum->setValidator(unsignedValidator);
    rangeMaximum->setValidator(unsignedValidator);
    rangeMinimum->setToolTip(tr(
        "First object ID reserved for this MOB. Verify that it does not overlap "
        "another MOB used by the same map state."));
    rangeMaximum->setToolTip(tr(
        "Last object ID reserved for this MOB. The range is required even for an "
        "initially empty MOB so future object creation is deterministic."));
    form->addRow(tr("ID range minimum"), rangeMinimum);
    form->addRow(tr("ID range maximum"), rangeMaximum);

    QCheckBox* createDiplomacy = new QCheckBox(
        tr("Create a default 32-player diplomacy table"), &dialog);
    createDiplomacy->setToolTip(tr(
        "Creates the standard 32 by 32 zero-valued table with Player-0 through "
        "Player-31 names. It contains no hostile relations. Leave disabled to "
        "omit diplomacy metadata."));
    form->addRow(QString(), createDiplomacy);
    root->addLayout(form);

    QGroupBox* worldGroup = new QGroupBox(tr("Initial primary world settings"), &dialog);
    QFormLayout* worldForm = new QFormLayout(worldGroup);
    QDoubleSpinBox* windX = new QDoubleSpinBox(worldGroup);
    QDoubleSpinBox* windY = new QDoubleSpinBox(worldGroup);
    QDoubleSpinBox* windZ = new QDoubleSpinBox(worldGroup);
    QDoubleSpinBox* windStrength = new QDoubleSpinBox(worldGroup);
    QDoubleSpinBox* time = new QDoubleSpinBox(worldGroup);
    QDoubleSpinBox* ambient = new QDoubleSpinBox(worldGroup);
    QDoubleSpinBox* sunlight = new QDoubleSpinBox(worldGroup);
    for(QDoubleSpinBox* value : {windX, windY, windZ, windStrength,
                                 time, ambient, sunlight})
    {
        value->setRange(-1000000.0, 1000000.0);
        value->setDecimals(4);
    }
    time->setValue(12.0);
    time->setToolTip(tr("Initial game time stored in WORLD_SET."));
    ambient->setToolTip(tr("Initial ambient-light scalar stored in WORLD_SET."));
    sunlight->setToolTip(tr("Initial sun-light scalar stored in WORLD_SET."));
    worldForm->addRow(tr("Wind direction X"), windX);
    worldForm->addRow(tr("Wind direction Y"), windY);
    worldForm->addRow(tr("Wind direction Z"), windZ);
    worldForm->addRow(tr("Wind strength"), windStrength);
    worldForm->addRow(tr("Time"), time);
    worldForm->addRow(tr("Ambient"), ambient);
    worldForm->addRow(tr("Sun light"), sunlight);
    root->addWidget(worldGroup);

    QCheckBox* openParameters = new QCheckBox(
        tr("Open MOB Parameters after creation"), &dialog);
    openParameters->setChecked(true);
    root->addWidget(openParameters);

    const auto selectedTemplate = [&]() -> CMob* {
        return reinterpret_cast<CMob*>(quintptr(templateMob->currentData().toULongLong()));
    };
    const auto refreshControls = [&]() {
        const bool hasTemplate = selectedTemplate() != nullptr;
        const bool primary = role->currentIndex() == 0;
        copyRanges->setEnabled(hasTemplate);
        copyScript->setEnabled(hasTemplate);
        copyDiplomacy->setEnabled(hasTemplate);
        copyWorld->setEnabled(hasTemplate && primary
                              && !selectedTemplate()->isQuestMob());
        if(!copyWorld->isEnabled())
            copyWorld->setChecked(false);
        worldGroup->setEnabled(primary && !copyWorld->isChecked());
        rangeMinimum->setEnabled(!copyRanges->isChecked());
        rangeMaximum->setEnabled(!copyRanges->isChecked());
        createDiplomacy->setEnabled(!copyDiplomacy->isChecked());
    };
    connect(templateMob, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, [&](int) { refreshControls(); });
    connect(role, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, [&](int) { refreshControls(); });
    connect(copyRanges, &QCheckBox::toggled, &dialog,
            [&](bool) { refreshControls(); });
    connect(copyDiplomacy, &QCheckBox::toggled, &dialog,
            [&](bool) { refreshControls(); });
    connect(copyWorld, &QCheckBox::toggled, &dialog,
            [&](bool) { refreshControls(); });
    refreshControls();

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create and Open..."));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked,
            &dialog, [&]() {
        const bool supplemental = role->currentIndex() == 1;
        CMob* source = selectedTemplate();
        const SMobParameterState sourceState = source
            ? source->parameterState() : SMobParameterState();
        QVector<SRange> ranges;
        if(copyRanges->isChecked() && source)
            ranges = sourceState.questMob ? sourceState.secondaryRanges
                                          : sourceState.mainRanges;
        else
        {
            bool minimumOk = false;
            bool maximumOk = false;
            const qulonglong minimum = rangeMinimum->text().toULongLong(&minimumOk);
            const qulonglong maximum = rangeMaximum->text().toULongLong(&maximumOk);
            if(!minimumOk || !maximumOk || minimum == 0 || minimum > 0xFFFFFFFFull
               || maximum > 0xFFFFFFFFull || minimum > maximum)
            {
                QMessageBox::warning(&dialog, tr("Create New MOB"), tr(
                    "Enter an ID range between 1 and 4294967295, with the minimum "
                    "not greater than the maximum."));
                rangeMinimum->setFocus();
                return;
            }
            ranges.append(SRange(uint(minimum), uint(maximum)));
        }
        if(ranges.isEmpty())
        {
            QMessageBox::warning(&dialog, tr("Create New MOB"), tr(
                "The selected source has no active ID range to copy. Enter a new "
                "range instead."));
            copyRanges->setChecked(false);
            return;
        }

        CMob generated;
        generated.setQuestMob(supplemental);
        generated.ensureObjectSection();
        generated.setRanges(!supplemental, ranges);
        if(copyScript->isChecked() && source)
            generated.setScript(sourceState.script);
        if(copyDiplomacy->isChecked() && source)
        {
            generated.setDiplomacyField(sourceState.diplomacy);
            generated.setDiplomacyNames(sourceState.diplomacyNames);
        }
        else if(createDiplomacy->isChecked())
            generated.generateDiplomacyTable();
        if(!supplemental)
        {
            CWorldSet world;
            if(copyWorld->isChecked() && source)
                world = sourceState.worldSet;
            else
            {
                world.setData(eWsTypeWindDir, QStringLiteral("(%1,%2,%3)")
                    .arg(windX->value(), 0, 'g', 9)
                    .arg(windY->value(), 0, 'g', 9)
                    .arg(windZ->value(), 0, 'g', 9));
                world.setData(eWsTypeWindStr, QString::number(windStrength->value(), 'g', 9));
                world.setData(eWsTypeTime, QString::number(time->value(), 'g', 9));
                world.setData(eWsTypeAmbient, QString::number(ambient->value(), 'g', 9));
                world.setData(eWsTypeSunLight, QString::number(sunlight->value(), 'g', 9));
            }
            generated.setWorldSet(world);
        }

        QByteArray candidate;
        generated.serializeMob(candidate, false);
        CMobDocument candidateDocument;
        CValidationReport candidateReport;
        if(!candidateDocument.loadBytes(candidate, QStringLiteral("new.mob"),
                                         &candidateReport)
           || candidateReport.hasErrors()
           || candidateDocument.isQuestMob() != supplemental
           || !candidateDocument.objects().isEmpty()
           || !candidateDocument.hasSection(0xB000)
           || candidateDocument.hasEmbeddedAiGraph()
           || candidateDocument.hasSection(0x1E00))
        {
            QMessageBox::critical(&dialog, tr("Create New MOB"), tr(
                "The generated MOB failed its fresh validation and was not written.\n\n%1")
                .arg(candidateReport.toText()));
            return;
        }

        COptString* opt = dynamic_cast<COptString*>(
            m_settings->opt(eOptSetGeneral, "lastVisitedFolder"));
        const QString startFolder = opt ? opt->value() : QDir::currentPath();
        QString suggestedName = supplemental
            ? QStringLiteral("autozone-new.mob") : QStringLiteral("zone-new.mob");
        QString destinationPath = QFileDialog::getSaveFileName(&dialog,
            tr("Save New MOB"), QDir(startFolder).filePath(suggestedName),
            tr("MOB (*.mob)"));
        if(destinationPath.isEmpty())
            return;
        if(!destinationPath.endsWith(QStringLiteral(".mob"), Qt::CaseInsensitive))
            destinationPath += QStringLiteral(".mob");
        const QFileInfo destination(destinationPath);
        if(m_pView)
        {
            for(CMob* loaded : m_pView->mobs())
            {
                if(loaded && loaded->filePath().absoluteFilePath().compare(
                       destination.absoluteFilePath(), Qt::CaseInsensitive) == 0)
                {
                    QMessageBox::warning(&dialog, tr("Create New MOB"), tr(
                        "%1 is currently loaded. Unload it before replacing it with "
                        "a newly generated MOB.").arg(destination.fileName()));
                    return;
                }
            }
        }
        if(destination.exists()
           && QMessageBox::question(&dialog, tr("Replace MOB"),
                tr("%1 already exists. Back it up and replace it?")
                    .arg(destination.fileName())) != QMessageBox::Yes)
            return;

        SMobSaveCommitRequest commit;
        commit.destination = destination;
        const SMobSaveCommitResult result = commitGeneratedMob(candidate, commit);
        if(!result.success)
        {
            QMessageBox::critical(&dialog, tr("Create New MOB"), tr(
                "The generated MOB was not committed.\n\n%1").arg(result.error));
            return;
        }
        if(opt)
            opt->setValue(destination.absolutePath());
        if(!loadMobWithProgress(destination))
        {
            QMessageBox::information(&dialog, tr("New MOB Created"), tr(
                "The validated MOB was created at:\n%1\n\nIt could not be opened automatically.")
                .arg(destination.absoluteFilePath()));
            dialog.accept();
            return;
        }
        rememberRecentFile(destination);
        if(openParameters->isChecked())
            m_pView->openActiveMobEditParams();
        statusBar()->showMessage(tr("Created and opened %1")
            .arg(destination.fileName()), 5000);
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::on_actionOpen_MOB_triggered()
{
    COptString* opt = dynamic_cast<COptString*>(m_settings->opt(eOptSetGeneral, "lastVisitedFolder"));
    Q_ASSERT(opt);
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Open one or more MOB files", opt->value(), tr("MOB (*.mob)"));
    m_pView->prepareDocumentTransition();
    for(const QString& path : paths)
    {
        QFileInfo fileName(path);
        opt->setValue(fileName.dir().path());
        if(loadMobWithProgress(fileName))
            rememberRecentFile(fileName);
    }
}

void MainWindow::on_actionSettings_triggered()
{
}

void MainWindow::on_actionSave_as_triggered()
{
    notifyFamilyPreflight(tr("Save As"));
    m_pView->saveMobAs();
}


void MainWindow::on_actionSelect_All_triggered()
{
    m_selector->selectAll();
}

void MainWindow::on_actionSelect_by_triggered()
{
    m_selector->show();
    m_selector->activateWindow();
}

void MainWindow::on_actionShow_undo_redo_triggered()
{
    m_historyDock->show();
    m_historyDock->raise();
    m_historyDock->activateWindow();
}

void MainWindow::on_actionClose_all_triggered()
{
    closeAll();
}

void MainWindow::on_actionSave_triggered()
{
    notifyFamilyPreflight(tr("Save"));
    m_pView->saveActiveMob();
}

void MainWindow::on_action_Mob_parameters_triggered()
{
    if(m_pView && m_pView->hasActiveMob())
        m_pView->openActiveMobEditParams();
}


void MainWindow::on_actionUndo_triggered()
{
    if(m_pView->prepareHistoryNavigation())
        runUndoToIndex(m_undoStack->index() - 1);
    else
        keepFloatingHistoryRaised();
}

void MainWindow::on_selectButton_clicked()
{
    qDebug() << "hello Select";
    CButtonConnector::getInstance()->clickButton(EButtonOpSelect);
}

void MainWindow::on_moveButton_clicked()
{
    qDebug() << "TODO: insert gizmo to Move";
    CButtonConnector::getInstance()->clickButton(EButtonOpMove);
}

void MainWindow::on_actionRedo_triggered()
{
    if(m_pView->prepareHistoryNavigation())
        runUndoToIndex(m_undoStack->index() + 1);
    else
        keepFloatingHistoryRaised();
}

void MainWindow::on_actionOpen_MOB_Script_triggered()
{
    if(m_pView && m_pView->hasActiveMob())
        m_pView->openActiveMobScript();
}

void MainWindow::on_actionDuplicate_selected_objects_triggered()
{
    if(m_pView)
        m_pView->duplicateSelectedObjects();
}


void MainWindow::on_actionCreate_new_object_triggered()
{
    if(!m_pView || !m_pView->hasActiveMob())
    {
        statusBar()->showMessage(tr("Open or activate a MOB before creating an object."), 4000);
        return;
    }
    m_createDialog->show();
    m_createDialog->activateWindow();
}

void MainWindow::updateWindowTitle(eTitleTypeData type, QString data)
{
    switch (type) {
        case eTitleTypeData::eTitleTypeDataActiveMob:
    {
        m_sWindowTitle.activeMob = data;
        m_ui->action_Mob_parameters->setEnabled(!data.isEmpty());
        m_ui->actionOpen_MOB_Script->setEnabled(!data.isEmpty());
        m_externalScriptAction->setEnabled(!data.isEmpty());
        m_ui->actionCreate_new_object->setEnabled(!data.isEmpty());
        m_ui->actionSave->setEnabled(!data.isEmpty());
        m_ui->actionSave_as->setEnabled(!data.isEmpty());
        m_ui->actionSave_all_MOB_s->setEnabled(!data.isEmpty());
        updateOperationAvailability(m_pView && m_pView->nSelectedNodes() > 0);
        break;
    }
    case eTitleTypeData::eTitleTypeDataMpr:
    {
        m_sWindowTitle.mpr = data;
        break;
    }
    case eTitleTypeData::eTitleTypeDataMprDirtyFlag:
    {
        m_sWindowTitle.mprDirty = !data.isEmpty();
        break;
    }
    case eTitleTypeData::eTitleTypeDataDirtyFlag:
    {
        m_sWindowTitle.durty = !data.isEmpty();
        break;
    }
    default:
        Q_ASSERT(false);
        break;
    }
    rebuildWindowTitle();
    updateDocumentActionAvailability();
    if(type == eTitleTypeData::eTitleTypeDataMpr
       || type == eTitleTypeData::eTitleTypeDataMprDirtyFlag)
        updateLoadedMapPanel();
    updateMapTestAction();
    if(type == eTitleTypeData::eTitleTypeDataMpr)
        refreshMapTestContext();
}

void MainWindow::rebuildWindowTitle()
{
    QString title = QString("EI Mapper: Endurance Edition %1 [%2]")
        .arg(QCoreApplication::applicationVersion(), activeProfileDisplayName());
    if(m_workspaceIdentityKind.compare("collection", Qt::CaseInsensitive) == 0)
    {
        const QString fileName = QFileInfo(m_currentCollectionPath).fileName();
        const QString identity = m_workspaceDisplayName.isEmpty() ? fileName
            : (fileName.isEmpty() ? m_workspaceDisplayName
                                  : QString("%1 — %2").arg(m_workspaceDisplayName, fileName));
        title += QString(" Collection: (%1)").arg(identity);
    }
    else if(m_workspaceIdentityKind.compare("map", Qt::CaseInsensitive) == 0)
        title += QString(" Map: (%1)").arg(m_workspaceDisplayName);
    if(!m_sWindowTitle.mpr.isEmpty())
    {
        title += QString(" MPR: (%1)").arg(m_sWindowTitle.mpr);
        if(m_sWindowTitle.mprDirty)
            title += "*";
    }

    if(!m_sWindowTitle.activeMob.isEmpty())
    {
        title += QString(" Active MOB: (%1)").arg(m_sWindowTitle.activeMob);
        if(m_sWindowTitle.durty) //show durty flag only if mob is loaded. if it will be possible to edit mpr, move flag outside this block
            title += "*";
    }
    setWindowTitle(title);
}


void MainWindow::on_actionSave_all_MOB_s_triggered()
{
    notifyFamilyPreflight(tr("Save All MOBs"));
    m_pView->saveAllMob();
}


void MainWindow::on_rotateButton_clicked()
{
    CButtonConnector::getInstance()->clickButton(EButtonOpRotate);
}


void MainWindow::on_scaleButton_clicked()
{
    CButtonConnector::getInstance()->clickButton(EButtonOpScale);
}


void MainWindow::on_actionReset_cam_position_triggered()
{
    m_pView->resetCamPosition();
}


void MainWindow::on_actionChange_mod_e_triggered()
{
    CScene::getInstance()->changeMode(CScene::getInstance()->getMode() == eEditModeObjects ? eEditModeLogic : eEditModeObjects);
}


void MainWindow::on_actionCopy_IDs_to_clipboard_triggered()
{
    m_pView->copySelectedIDsToClipboard();
}


void MainWindow::on_actionOpen_2_triggered()
{
    m_settings->onShow(eOptSetGeneral);
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    positionMapTestResultNotification();
    positionAutosaveNotification();
    positionViewportMinimap();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if(m_mapTestLauncher && m_mapTestLauncher->isRunning())
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(this,
            tr("JMV test is running"),
            tr("Stop the game process launched by Endurance, clean up its temporary files, and exit?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if(answer != QMessageBox::Yes)
        {
            e->ignore();
            return;
        }
        m_closeAfterTestStop = true;
        m_mapTestLauncher->stop();
        e->ignore();
        return;
    }
    if(!isExitAllowed())
    {
        e->ignore();
        return;
    }

    // Persist ordinary dock visibility before secondary-window cleanup closes
    // docks. Landscape Tools is intentionally normalized to closed by
    // saveUiState(); editing mode never resumes implicitly next session.
    saveUiState();
    // Autosave owns callbacks which query the live document/view state. Stop
    // both its long-period timer and any active countdown before the viewport
    // is destroyed; a queued timeout after view teardown used to crash during
    // otherwise clean shutdown.
    m_shutdownInProgress = true;
    if(m_autosaveTimer)
        m_autosaveTimer->stop();
    if(m_autosaveCountdownTimer)
        m_autosaveCountdownTimer->stop();
    if(m_validationTimer)
        m_validationTimer->stop();
    if(m_mapTestResultTimer)
        m_mapTestResultTimer->stop();
    if(m_cameraSpeedPersistTimer)
        m_cameraSpeedPersistTimer->stop();
    if(m_autosaveNotification)
        m_autosaveNotification->hide();
    // The process-wide button connector outlives MainWindow and otherwise
    // retains a dangling viewport pointer while tool buttons are destroyed.
    CButtonConnector::getInstance()->attach(nullptr);
    // Floating tab groups have their native owner cleared so Windows gives
    // them taskbar entries. Close those private hosts explicitly before the
    // generic top-level sweep; no editor-owned diagnostic may outlive main.
    ei::closeFloatingDockHost(m_messageLogDock);
    ei::closeFloatingDockHost(m_validationDock);
    ei::closeSecondaryWindows(this);
    // Remove the editor from the desktop before synchronous scene teardown.
    // Windows otherwise presents a still-visible window as unresponsive.
    hide();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    // The minimap is parented to the viewport while its cadence timers belong
    // to MainWindow. Stop and disconnect them before deleting the viewport so
    // no queued timeout can observe the already-destroyed child overlay during
    // the remainder of shutdown. QPointer clears the child reference itself.
    if(m_minimapRefreshTimer)
        m_minimapRefreshTimer->stop();
    if(m_minimapCameraTimer)
        m_minimapCameraTimer->stop();
    if(m_viewportMinimap)
        m_viewportMinimap->disconnect(this);
    if(m_pView)
    {
        // A long GL teardown gives already-posted viewport/UI callbacks ample
        // time to become stale. Drain them and sever view-to-window delivery
        // before the pointer is cleared.
        QObject::disconnect(m_pView, nullptr, this, nullptr);
        QCoreApplication::removePostedEvents(m_pView);
    }
    // Destroy the QGLWidget while the main window and its native child surface
    // are still alive. Waiting for MainWindow::~MainWindow() is too late on
    // Windows: Qt has already released the surface even though CView::isValid()
    // may still report true, and GPU-resource cleanup then dereferences a null
    // QOpenGLContext surface.
    ei::setCrashContext("shutdown/delete-live-view");
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    ei::log(eLogInfo, "Shutdown live view deletion started");
    m_pView->prepareForApplicationShutdown();
    delete m_pView;
    m_pView = nullptr;
    m_viewportMinimap = nullptr;
    ei::log(eLogInfo, QString("Shutdown live view deletion complete elapsedMs:%1")
            .arg(shutdownTimer.elapsed()));
    ei::setCrashContext("shutdown/view-deleted");
    if(m_backupSessionStarted)
    {
        QString backupSessionError;
        if(!CBackupManager().finishEditorSession(&backupSessionError))
            ei::log(eLogWarning, "Could not mark editor backup session clean: "
                    + backupSessionError);
        else
            ei::log(eLogInfo, "Editor backup session marked clean");
    }
    // No deferred MainWindow callback may run after closeEvent returns with a
    // null viewport. Child destruction is synchronous from this point.
    QCoreApplication::removePostedEvents(this);
    e->accept();
}

void MainWindow::on_actionOpen_recent_triggered()
{
    loadLastSession();
}


void MainWindow::on_actionReset_logic_paths_triggered()
{
    m_pView->resetUnitLogicPaths();
}


void MainWindow::on_actionRandomize_parameter_triggered()
{
    m_randomizeForm->show();
    m_randomizeForm->activateWindow();
}


void MainWindow::on_actionReset_selected_IDs_triggered()
{
    m_pView->resetSelectedId();
}


void MainWindow::on_action_About_triggered()
{
    QMessageBox about(this);
    about.setWindowTitle(tr("About"));
    about.setIconPixmap(qApp->windowIcon().pixmap(96, 96));
    about.setText(QString("EI Mapper: Endurance Edition %1\n\n"
                          "A private, independently versioned derivative of EI Mapper by konstvest.\n"
                          "Upstream: https://github.com/konstvest/ei_maper\n"
                          "License: GNU GPL-3.0")
                      .arg(QCoreApplication::applicationVersion()));
    about.exec();
}


void MainWindow::on_actionSave_landscape_MPR_as_triggered()
{
    m_pView->saveLandAs();
}


void MainWindow::on_actionMap_parameters_triggered()
{
    m_pView->openMapParameters();
}


void MainWindow::on_tileBrushButton_clicked()
{
    if(QAction* action = m_operationActions.value(EButtonOpTilebrush, nullptr))
        action->trigger();
}


void MainWindow::on_actionSave_landscape_MPR_triggered()
{
    m_pView->saveLand();
}

