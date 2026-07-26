#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QCheckBox>
#include <QKeySequenceEdit>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>

#include "settings.h"
#include "ui_settings.h"
#include "main_window.h"
#include "utils.h"
#include "log.h"
#include "resource_profile.h"
#include "profile_database.h"

CSettings::CSettings(QWidget *parent) :
    QWidget(parent, Qt::Window)
    ,ui(new Ui::CSettings)
    ,m_fileOpt("endurance-options.json")
  ,m_version(1.0)
{
    ui->setupUi(this);
    const QStringList cameraWheelGestures = {
        "Ctrl+Wheel", "Alt+Wheel", "Shift+Wheel", "Meta+Wheel"
    };
    for(int index = 0; index < cameraWheelGestures.size(); ++index)
        ui->cameraSpeedWheelGesture->setItemData(index, cameraWheelGestures.at(index));

    // Keep the General tab in a predictable top-to-bottom order. The legacy
    // UI owns the first six rows plus the inactive polling row and spacer;
    // move those two before adding Endurance's dynamic controls.
    ui->gridLayout->removeItem(ui->horizontalLayout_8);
    ui->gridLayout->addLayout(ui->horizontalLayout_8, 8, 0);
    ui->gridLayout->removeItem(ui->verticalSpacer_2);
    ui->gridLayout->addItem(ui->verticalSpacer_2, 13, 0, 1, 2);

    QLabel* uiScaleLabel = new QLabel(tr("UI scale"), ui->tabGeneral);
    uiScaleLabel->setToolTip(tr(
        "Scales editor fonts, menus, dialogs, docks, property tables, and viewport text labels. "
        "Applied immediately. Default: 100%."));
    QSlider* uiScale = new QSlider(Qt::Horizontal, ui->tabGeneral);
    uiScale->setObjectName("uiScalePercent");
    uiScale->setRange(75, 200);
    uiScale->setSingleStep(5);
    uiScale->setPageStep(25);
    uiScale->setTickInterval(25);
    uiScale->setToolTip(uiScaleLabel->toolTip());
    QLabel* uiScaleValue = new QLabel(QStringLiteral("100%"), ui->tabGeneral);
    uiScaleValue->setObjectName("uiScalePercentValue");
    uiScaleValue->setMinimumWidth(48);
    uiScaleValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QHBoxLayout* uiScaleControls = new QHBoxLayout;
    uiScaleControls->addWidget(uiScale, 1);
    uiScaleControls->addWidget(uiScaleValue);
    ui->gridLayout->addWidget(uiScaleLabel, 6, 0);
    ui->gridLayout->addLayout(uiScaleControls, 6, 1);
    connect(uiScale, &QSlider::valueChanged, this, [uiScaleValue](int value)
    {
        uiScaleValue->setText(QStringLiteral("%1%").arg(value));
    });

    QLabel* editorLabel = new QLabel(tr("External script editor"), ui->tabGeneral);
    editorLabel->setToolTip(tr("Executable used to edit managed MOB scripts. Clear this field to use the Windows .lua file association."));
    QLineEdit* editorPath = new QLineEdit(ui->tabGeneral);
    editorPath->setObjectName("externalEditorPath");
    editorPath->setPlaceholderText(tr("Windows file association"));
    editorPath->setToolTip(editorLabel->toolTip());
    QPushButton* browseEditor = new QPushButton(tr("Browse..."), ui->tabGeneral);
    QPushButton* clearEditor = new QPushButton(tr("Clear"), ui->tabGeneral);
    QHBoxLayout* editorControls = new QHBoxLayout;
    editorControls->addWidget(editorPath, 1);
    editorControls->addWidget(browseEditor);
    editorControls->addWidget(clearEditor);
    ui->gridLayout->addWidget(editorLabel, 7, 0);
    ui->gridLayout->addLayout(editorControls, 7, 1);
    QCheckBox* openLastSession = new QCheckBox(tr("Open last session on startup"),
                                               ui->tabGeneral);
    openLastSession->setObjectName("openLastSessionOnStartup");
    openLastSession->setToolTip(tr(
        "Automatically invokes File > Open Last Session after startup. "
        "Disabled by default; missing files are reported and skipped by the existing session loader."));
    ui->gridLayout->addWidget(openLastSession, 9, 0, 1, 2);
    QCheckBox* autosaveEnabled = new QCheckBox(tr("Periodic recovery autosave"),
                                               ui->tabGeneral);
    autosaveEnabled->setObjectName("autosaveEnabled");
    autosaveEnabled->setToolTip(tr(
        "Writes validated, versioned recovery copies under the Endurance backup folder. "
        "It never replaces the open MOB/MPR or marks it saved."));
    ui->gridLayout->addWidget(autosaveEnabled, 10, 0, 1, 2);
    QLabel* autosaveIntervalLabel = new QLabel(tr("Autosave interval (minutes)"),
                                                ui->tabGeneral);
    autosaveIntervalLabel->setToolTip(tr(
        "How often Endurance snapshots dirty documents. Identical document states are deduplicated."));
    QLineEdit* autosaveInterval = new QLineEdit(ui->tabGeneral);
    autosaveInterval->setObjectName("autosaveIntervalMinutes");
    autosaveInterval->setValidator(new QIntValidator(1, 120, autosaveInterval));
    autosaveInterval->setToolTip(autosaveIntervalLabel->toolTip());
    ui->gridLayout->addWidget(autosaveIntervalLabel, 11, 0);
    ui->gridLayout->addWidget(autosaveInterval, 11, 1);
    QPushButton* autosaveNow = new QPushButton(tr("Autosave Now"), ui->tabGeneral);
    autosaveNow->setToolTip(tr("Immediately create recovery generations for all dirty documents."));
    QPushButton* openBackupFolder = new QPushButton(tr("Open Backup Folder"), ui->tabGeneral);
    openBackupFolder->setToolTip(tr("Open the Endurance folder containing automatic and pre-save backups."));
    QHBoxLayout* autosaveActions = new QHBoxLayout;
    autosaveActions->addWidget(autosaveNow);
    autosaveActions->addWidget(openBackupFolder);
    autosaveActions->addStretch(1);
    ui->gridLayout->addLayout(autosaveActions, 12, 0, 1, 2);
    connect(autosaveNow, &QPushButton::clicked, this, &CSettings::autosaveRequested);
    connect(openBackupFolder, &QPushButton::clicked, this, &CSettings::backupFolderRequested);
    connect(browseEditor, &QPushButton::clicked, this, [this, editorPath]()
    {
        const QString selected = QFileDialog::getOpenFileName(this,
            tr("Select external script editor"), editorPath->text(),
            tr("Applications (*.exe);;All files (*)"));
        if(!selected.isEmpty())
            editorPath->setText(QDir::toNativeSeparators(selected));
    });
    connect(clearEditor, &QPushButton::clicked, editorPath, &QLineEdit::clear);
    setWindowModality(Qt::WindowModal);
    ui->rangeIncrement->setValidator(new QIntValidator(1, 1000000, this));
    ui->undoHistoryLimit->setValidator(new QIntValidator(1, 1000000, this));
    ui->renderFpsCap->setValidator(new QIntValidator(0, 1000, this));
    ui->renderShadowDistance->setValidator(new QIntValidator(1, 1024, this));
    initOptions();
    readOptions();
    const COptString* savedProfile = dynamic_cast<const COptString*>(opt(eOptSetResource, "resourceProfile"));
    const QString savedRoot = configuredGameRoot();
    if(savedProfile && !savedProfile->value().isEmpty() && !savedRoot.isEmpty())
    {
        for(const SResourceProfile& candidate : loadResourceProfiles())
        {
            if(candidate.id.compare(savedProfile->value(), Qt::CaseInsensitive) != 0)
                continue;
            const SResourceProfileEvaluation evaluation = evaluateResourceProfile(savedRoot, candidate);
            if(evaluation.valid)
                applyResourceProfile(evaluation);
            break;
        }
    }
}

CSettings::~CSettings()
{
    delete ui;
    saveOptions(); //todo:what reason for saving options here? last folder path?
}

COpt* CSettings::opt(QString name)
{
    for(auto& cat : m_aOptCategory)
        for (auto& opt: cat)
        {
            if (opt->name() == name)
                return opt.get();
        }
    return nullptr;
}

COpt* CSettings::opt(EOptSet optSet, QString& name)
{
    for(const auto& opt: m_aOptCategory[optSet])
        if(opt->name() == name)
            return opt.get();

    return nullptr;
}

COpt* CSettings::opt(EOptSet optSet, const char* name)
{
    QString optName(name);
    return opt(optSet, optName);
}

void CSettings::onShow(EOptSet optSet)
{
    // Reopen from the last applied model. This also discards slider,
    // checkbox, combo, and key-binding edits abandoned through the window X.
    updateOptUi();
    show();
    switch (optSet) {
    case eOptSetGeneral:
        ui->tabWidget->setCurrentIndex(ui->tabWidget->indexOf(ui->tabGeneral));
        break;
    case eOptSetRender:
        ui->tabWidget->setCurrentIndex(ui->tabWidget->indexOf(ui->tabRender));
        break;
    case eOptSetKeyBinding:
        ui->tabWidget->setCurrentIndex(ui->tabWidget->indexOf(ui->tabKeyBindings));
        break;
    case eOptSetResource:
        ui->tabWidget->setCurrentIndex(ui->tabWidget->indexOf(ui->tabResource));
        break;
    default:
        Q_ASSERT("incorrect option set" && false);
        break;
    }
}

bool CSettings::onClose()
{
    return close();
}

void CSettings::closeEvent(QCloseEvent* e)
{
    // Closing never applies pending controls. Restore the applied model so a
    // later reopen cannot expose abandoned values, then persist that model.
    updateOptUi();
    saveOptions();
    QWidget::closeEvent(e);
}

bool CSettings::close()
{
    return QWidget::close();
}

void CSettings::on_buttonCancel_clicked()
{
    updateOptUi();
    close();
}

void CSettings::resetOptions()
{
    if(m_fileOpt.exists())
        m_fileOpt.remove();
}

bool CSettings::hasConfiguredResources() const
{
    for(EOptSet set : {eOptSetGeneral, eOptSetResource})
    {
        for(const auto& option : m_aOptCategory.value(set))
        {
            if(option->name() != "resourceRoots" &&
               option->name() != "figPaths" && option->name() != "texPaths")
                continue;
            const COptStringList* paths = dynamic_cast<const COptStringList*>(option.get());
            if(paths && !paths->value().isEmpty())
                return true;
        }
    }
    return false;
}

QString CSettings::configuredGameRoot() const
{
    for(const auto& option : m_aOptCategory.value(eOptSetGeneral))
    {
        if(option->name() == "gameRoot")
        {
            const COptString* root = dynamic_cast<const COptString*>(option.get());
            return root ? root->value() : QString();
        }
    }
    return QString();
}

void CSettings::applyResourceProfile(const SResourceProfileEvaluation& profile)
{
    if(!profile.valid)
        return;
    ei::SProfileDatabaseLoadRequest databaseRequest;
    const SProfileProviderBinding databaseBinding = profile.providers.value("database");
    databaseRequest.providerId = databaseBinding.id;
    databaseRequest.providerInterfaceVersion = databaseBinding.interfaceVersion;
    databaseRequest.profileId = profile.profileId;
    databaseRequest.runtimeManifestId = profile.runtimeManifestId;
    databaseRequest.resourceDirectories = profile.resourceDirectories;
    static ei::ProfileDatabaseService databaseService;
    const QSharedPointer<const ei::SProfileDatabaseSnapshot> database =
        databaseService.snapshot(databaseRequest);
    if(COptString* root = dynamic_cast<COptString*>(opt(eOptSetGeneral, "gameRoot")))
        root->setValue(profile.gameRoot);
    if(COptString* id = dynamic_cast<COptString*>(opt(eOptSetResource, "resourceProfile")))
        id->setValue(profile.profileId);
    if(COptStringList* roots = dynamic_cast<COptStringList*>(opt(eOptSetResource, "resourceRoots")))
        roots->setValue(profile.resourceDirectories.toVector());
    if(COptStringList* maps = dynamic_cast<COptStringList*>(opt(eOptSetResource, "mapRoots")))
        maps->setValue(profile.mapDirectories.toVector());
    if(!profile.mapDirectories.isEmpty())
    {
        if(COptString* lastFolder = dynamic_cast<COptString*>(opt(eOptSetGeneral, "lastVisitedFolder")))
            lastFolder->setValue(profile.mapDirectories.first());
    }
    for(const char* legacyName : {"figPaths", "texPaths"})
        if(COptStringList* legacy = dynamic_cast<COptStringList*>(opt(eOptSetResource, legacyName)))
            legacy->setValue(QVector<QString>());
    // One pointer swap publishes the new profile-scoped database to every
    // dropdown and resolver after all profile options have been replaced.
    QMap<QString, QString> providerBindings;
    for(auto it = profile.providers.constBegin(); it != profile.providers.constEnd(); ++it)
        providerBindings.insert(it.key(), it->id);
    ei::ProfileDatabaseContext::instance().activate(database, providerBindings);
    updateOptUi();
    saveOptions();
    ei::log(eLogInfo, QString("Applied resource profile id:%1 gameRoot:%2 resourceDirectories:%3 archives:%4")
            .arg(profile.profileId, profile.gameRoot)
            .arg(profile.resourceDirectories.size()).arg(profile.archiveCount));
    for(const SRuntimeComponentEvaluation& component : profile.runtimeComponents)
    {
        const ELogMessageType level = component.exists
            && (!component.enforceHash || component.hashAccepted) ? eLogInfo : eLogWarning;
        ei::log(level, QString("Runtime component id:%1 strict:%2 exists:%3 accepted:%4 sha256:%5 path:%6")
            .arg(component.id).arg(component.enforceHash).arg(component.exists)
            .arg(component.hashAccepted).arg(component.sha256, component.path));
    }
    for(const ei::SDatabaseDiagnostic& diagnostic : database->diagnostics)
        ei::log(eLogWarning,
                QString("Profile database [%1] %2").arg(diagnostic.code, diagnostic.message));
}

void CSettings::updateOptUi()
{
    for (auto& cat: m_aOptCategory)
    {
        for(const auto& opt: cat)
        {
            if (COptString* pOpt = dynamic_cast<COptString*>(opt.get()))
            {
                QComboBox* combo = ui->tabWidget->findChild<QComboBox*>(pOpt->name());
                if(combo)
                {
                    const int index = combo->findData(pOpt->value());
                    combo->setCurrentIndex(index >= 0 ? index : 0);
                    continue;
                }
                QKeySequenceEdit* keyEdit = ui->tabWidget->findChild<QKeySequenceEdit*>(pOpt->name());
                if(keyEdit)
                {
                    keyEdit->setKeySequence(QKeySequence(pOpt->value()));
                    continue;
                }
                //find label, text edit
                QLineEdit* pLineEdit = ui->tabWidget->findChild<QLineEdit*>(pOpt->name());
                if (pLineEdit)
                {
                    pLineEdit->setText(pOpt->value());
                }
            }
            else if(COptBool* pOpt = dynamic_cast<COptBool*>(opt.get()))
            {
                // find checkbox
                QCheckBox* pCheckBox = ui->tabWidget->findChild<QCheckBox*>(pOpt->name());
                if (pCheckBox)
                    pCheckBox->setChecked(pOpt->value());
            }
            else if(COptDouble* pOpt = dynamic_cast<COptDouble*>(opt.get()))
            {
                //todo
                Q_UNUSED(pOpt);
            }
            else if(COptInt* pOpt = dynamic_cast<COptInt*>(opt.get()))
            {
                QSlider* pSlider = ui->tabWidget->findChild<QSlider*>(pOpt->name());
                if (pSlider)
                    pSlider->setValue(pOpt->value());

                QLineEdit* pLine = ui->tabWidget->findChild<QLineEdit*>(pOpt->name());
                if (pLine)
                    pLine->setText(QString::number(pOpt->value()));
            }
            else if(COptStringList* pOpt = dynamic_cast<COptStringList*>(opt.get()))
            {
                QListWidget* pListWidget = ui->tabWidget->findChild<QListWidget*>(pOpt->name());
                if (pListWidget)
                {
                    pListWidget->clear();
                    for(auto& str : pOpt->value())
                        pListWidget->addItem(str);
                }
            }
        }
    }
}

void CSettings::updateOptFromUi()
{
    for (auto& cat: m_aOptCategory)
    {
        for(const auto& opt: cat)
        {
            if (COptString* pOpt = dynamic_cast<COptString*>(opt.get()))
            {
                QComboBox* combo = ui->tabWidget->findChild<QComboBox*>(pOpt->name());
                if(combo)
                {
                    pOpt->setValue(combo->currentData().toString());
                    continue;
                }
                QKeySequenceEdit* keyEdit = ui->tabWidget->findChild<QKeySequenceEdit*>(pOpt->name());
                if(keyEdit)
                {
                    pOpt->setValue(keyEdit->keySequence().toString(QKeySequence::PortableText));
                    continue;
                }
                //find label, text edit
                QLineEdit* pLineEdit = ui->tabWidget->findChild<QLineEdit*>(pOpt->name());
                if (pLineEdit)
                {
                    pOpt->setValue(pLineEdit->text());
                }
            }
            else if(COptBool* pOpt = dynamic_cast<COptBool*>(opt.get()))
            {
                // find checkbox
                QCheckBox* pCheckBox = ui->tabWidget->findChild<QCheckBox*>(pOpt->name());
                if (pCheckBox)
                    pOpt->setValue(pCheckBox->isChecked());
            }
            else if(COptDouble* pOpt = dynamic_cast<COptDouble*>(opt.get()))
            {
                //todo
                Q_UNUSED(pOpt);
            }
            else if(COptInt* pOpt = dynamic_cast<COptInt*>(opt.get()))
            {
                QSlider* pSlider = ui->tabWidget->findChild<QSlider*>(pOpt->name());
                if (pSlider)
                    pOpt->setValue(pSlider->value());

                QLineEdit* pLine = ui->tabWidget->findChild<QLineEdit*>(pOpt->name());
                if (pLine)
                    pOpt->setValue(pLine->text().toInt());
            }
            else if (COptStringList* pOpt = dynamic_cast<COptStringList*>(opt.get()))
            {
                QListWidget* pListWidget = ui->tabWidget->findChild<QListWidget*>(pOpt->name());
                if(pListWidget)
                {
                    //QStringList list;
                    QVector<QString> list;
                    for(int i(0); i<pListWidget->count();++i)
                        list.append(pListWidget->item(i)->text());

                    pOpt->setValue(list);
                }
            }
        }
    }
    CLogger::getInstance()->setLogLevel(ELogMessageType(dynamic_cast<COptInt*>(opt("logLevel"))->value()));
}

void CSettings::initOptions()
{
    QVector<QSharedPointer<COpt>> aOpt;
    //init default options for strings
    aOpt.append(QSharedPointer<COpt>(new COptStringList("figPaths", {})));
    aOpt.append(QSharedPointer<COpt>(new COptStringList("texPaths", {})));
    aOpt.append(QSharedPointer<COpt>(new COptStringList("resourceRoots", {})));
    aOpt.append(QSharedPointer<COpt>(new COptStringList("mapRoots", {})));
    aOpt.append(QSharedPointer<COpt>(new COptString("resourceProfile", "")));
    aOpt.append(QSharedPointer<COpt>(new COptString("gameRoot", "")));
    aOpt.append(QSharedPointer<COpt>(new COptString("lastVisitedFolder", "")));
    aOpt.append(QSharedPointer<COpt>(new COptString("externalEditorPath", "")));
    aOpt.append(QSharedPointer<COpt>(new COptStringList("recentFiles", {})));

    //init default options for bools
    aOpt.append(QSharedPointer<COpt>(new COptBool("drawLogic", false)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("drawWater", true)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("freeCamera", false)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("dipEditSymmetric", true)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("logLevel", 2)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("drawHelp", false)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("frustumCulling", true)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("mapTestGodmode", false)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("openLastSessionOnStartup", false)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("autosaveEnabled", true)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("animateUnits", true)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("renderPerformanceCounters", false)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("renderVSync", true)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("environmentLighting", true)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("drawSkybox", true)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("renderFog", true)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("renderSceneLights", true)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("renderTextureMipmapping", true)));
    aOpt.append(QSharedPointer<COpt>(new COptBool("overrideWorldTime", false)));


    //init default options for digits
    aOpt.append(QSharedPointer<COpt>(new COptDouble("version", m_version)));
    aOpt.append(QSharedPointer<COpt>(new COptDouble("cameraSpeedMultiplier", 1.0)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("mouseSenseX", 25)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("mouseSenseY", 25)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("rangeIncrement", 999)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("landCheckTime", 0)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("undoHistoryLimit", 512)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("uiScalePercent", 100)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("mapTestWarningVersion", 0)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("autosaveIntervalMinutes", 5)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("renderFpsCap", 0)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("renderShadowQuality", 2)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("renderShadowDistance", 320)));
    aOpt.append(QSharedPointer<COpt>(new COptInt("renderTimeOfDay", 1200)));
    // 0 = authored texture/unlit, 1 = compatibility simple shading,
    // 2 = resource-driven environment preview.
    aOpt.append(QSharedPointer<COpt>(new COptInt("renderLightingMode", 2)));

    const QMap<QString, QString> shortcutDefaults = {
        {"shortcutOpen", "Ctrl+O"},
        {"shortcutOpenMob", "Alt+Shift+O"},
        {"shortcutOpenMpr", "Ctrl+Shift+O"},
        {"shortcutSaveActiveMob", "Ctrl+S"},
        {"shortcutSaveAllMobs", "Alt+Shift+S"},
        {"shortcutUndo", "Ctrl+Z"},
        {"shortcutRedo", "Ctrl+Y"},
        {"shortcutHistory", "Ctrl+L"},
        {"shortcutCreateObject", "Ctrl+N"},
        {"shortcutDuplicateObjects", "Ctrl+D"},
        {"shortcutMobParameters", "Ctrl+M"},
        {"shortcutMobScript", "Ctrl+Shift+M"},
        {"shortcutExternalMobScript", "Ctrl+Alt+M"},
        {"shortcutMapsBrowser", "Ctrl+Space"},
        {"shortcutTestMap", "Ctrl+F5"},
        {"shortcutStopMapTest", "Shift+Esc"},
        {"shortcutResetCamera", "Ctrl+R"},
        {"shortcutResetLogicPaths", "Ctrl+P"},
        {"shortcutSwitchMode", "Ctrl+Tab"},
        {"shortcutSelectTool", "Esc"},
        {"shortcutMoveTool", "G"},
        {"shortcutRotateTool", "R"},
        {"shortcutScaleTool", "T"},
        {"shortcutTileBrushTool", "L"}
    };
    for(auto it = shortcutDefaults.constBegin(); it != shortcutDefaults.constEnd(); ++it)
        m_aOptCategory[eOptSetKeyBinding].append(
            QSharedPointer<COpt>(new COptString(it.key(), it.value())));
    m_aOptCategory[eOptSetKeyBinding].append(QSharedPointer<COpt>(
        new COptString("cameraSpeedWheelGesture", "Ctrl+Wheel")));

    //split options into different category
    QFile inputFile(":/optSet.txt");
    QStringList aOptCategoryList;
    if (inputFile.open(QIODevice::ReadOnly))
    {
       QTextStream in(&inputFile);
       while (!in.atEnd())
       {
          QString line = in.readLine();
          aOptCategoryList.append(line);
       }
       inputFile.close();
    }

    EOptSet set = eOptSetGeneral;
    for (auto& line : aOptCategoryList)
    {
        if (line.isEmpty())
            continue;

        if (line.contains("Option set name"))
        {
            QString section = line.split(":").last();
            section.remove(QRegExp("[\\n\\t\\r]"));
            if (section == "General")
                set = eOptSetGeneral;
            else if (section == "Render")
                set = eOptSetRender;
            else if (section == "Resource")
                set = eOptSetResource;
            continue;
        }

        QString optName(line);
        optName.remove(QRegExp("[\\n\\t\\r]"));
        for (auto& opt : aOpt)
        {
            if (opt.get()->name() == optName)
            {
                m_aOptCategory[set].append(opt);
                break;
            }
        }

    }
}

void CSettings::updatePathOpt(const char* name, EOptSet optSet)
{
    COptString* val = dynamic_cast<COptString*>(opt(optSet, name));
    QLineEdit* le = ui->tabWidget->findChild<QLineEdit*>(name);
    if(le && val)
    {
        val->setValue(le->text());
    }
    COptBool* valB = dynamic_cast<COptBool*>(opt(optSet, name));
    QCheckBox* chB = ui->tabWidget->findChild<QCheckBox*>(name);
    if(chB && valB)
    {
        valB->setValue(chB->isChecked());
    }
}

void CSettings::readOptions()
{
    if(!m_fileOpt.exists())
    {
        updateOptUi();
        return;
    }

    QString json;
    if(!m_fileOpt.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Q_ASSERT("can't open option file" && false);
        return;
    }
    json = m_fileOpt.readAll();
    m_fileOpt.close();


    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    QJsonObject aOptObject = doc.object();
    auto version = aOptObject["Version"].toVariant().toFloat();
    if(version != m_version)
    {
        ei::log(eLogWarning, "option file version does not match. skip reading options");
        return;
    }


    auto readOptionTab = [&aOptObject, this](QString tabName, EOptSet optSet)
    {
        QJsonValue val = aOptObject.value(tabName);
        if (!val.isObject())
            return;

        QJsonObject obj =  val.toObject(); // General
        for(auto& opt : m_aOptCategory[optSet])
        {
            QJsonValue jVal = obj.value(opt.get()->name());
            if (jVal == QJsonValue::Null || jVal.isUndefined())
                continue;

            if (COptString* pOpt = dynamic_cast<COptString*>(opt.get()))
            {
                pOpt->setValue(jVal.toString());
            }
            else if(COptBool* pOpt = dynamic_cast<COptBool*>(opt.get()))
            {
                pOpt->setValue(jVal.toBool());
            }
            else if(COptDouble* pOpt = dynamic_cast<COptDouble*>(opt.get()))
            {
                pOpt->setValue(jVal.toDouble());
            }
            else if(COptInt* pOpt = dynamic_cast<COptInt*>(opt.get()))
            {
                pOpt->setValue(jVal.toInt());
            }
            else if(COptStringList* pOpt = dynamic_cast<COptStringList*>(opt.get()))
            {
                QJsonArray arr = jVal.toArray();
                //QStringList list;
                QVector<QString> list;
                for(auto elem : arr)
                    list.append(elem.toString());

                pOpt->setValue(list);
            }
        }

    };

    readOptionTab("General", eOptSetGeneral);
    readOptionTab("Render", eOptSetRender);
    readOptionTab("Resource", eOptSetResource);
    readOptionTab("KeyBinding", eOptSetKeyBinding);

    updateOptUi();
}

void CSettings::saveOptions()
{
    QJsonObject aOpt;

    auto optToObj = [&aOpt, this](QString tabName, EOptSet optSet)
    {
        QJsonObject tabObj;
        for(auto& opt : m_aOptCategory[optSet])
        {

            if (COptString* pOpt = dynamic_cast<COptString*>(opt.get()))
            {
                tabObj.insert(opt.get()->name(), pOpt->value());
            }
            else if(COptBool* pOpt = dynamic_cast<COptBool*>(opt.get()))
            {
                tabObj.insert(opt.get()->name(), pOpt->value());
            }
            else if(COptDouble* pOpt = dynamic_cast<COptDouble*>(opt.get()))
            {
                tabObj.insert(opt.get()->name(), pOpt->value());
            }
            else if(COptInt* pOpt = dynamic_cast<COptInt*>(opt.get()))
            {
                tabObj.insert(opt.get()->name(), pOpt->value());
            }
            if (COptStringList* pOpt = dynamic_cast<COptStringList*>(opt.get()))
            {
                QJsonArray arr;
                for(auto& str : pOpt->value())
                    arr.append(str);
                tabObj.insert(opt.get()->name(), arr);
            }
        }
        aOpt.insert(tabName, tabObj);
    };

    optToObj("General", eOptSetGeneral);
    optToObj("Render", eOptSetRender);
    optToObj("Resource", eOptSetResource);
    optToObj("KeyBinding", eOptSetKeyBinding);
    aOpt.insert("Version", QString::number(m_version));

    QJsonDocument doc(aOpt);
    if (!m_fileOpt.open(QIODevice::WriteOnly)) {
        Q_ASSERT("Couldn't open option file." && false);
        //todo: suggest choosing a folder to save
        return;
    }
    m_fileOpt.write(doc.toJson());
    m_fileOpt.close();
}

void CSettings::on_buttonApply_clicked()
{
    applyUiOptions();
}

void CSettings::on_buttonSaveClose_clicked()
{
    applyUiOptions();
    close();
}

void CSettings::applyUiOptions()
{
    updateOptFromUi();
    const COptBool* autosaveEnabled = dynamic_cast<const COptBool*>(
        opt(eOptSetGeneral, "autosaveEnabled"));
    const COptInt* autosaveInterval = dynamic_cast<const COptInt*>(
        opt(eOptSetGeneral, "autosaveIntervalMinutes"));
    const COptInt* uiScale = dynamic_cast<const COptInt*>(
        opt(eOptSetGeneral, "uiScalePercent"));
    emit autosaveSettingsApplied(autosaveEnabled && autosaveEnabled->value(),
                                 qBound(1, autosaveInterval ? autosaveInterval->value() : 5, 120));
    emit uiScaleApplied(qBound(75, uiScale ? uiScale->value() : 100, 200));
    emit shortcutsChanged();
    emit settingsApplied();
    emit renderSettingsChanged();
    saveOptions();
}

void CSettings::persistOptions()
{
    saveOptions();
}

void CSettings::on_figPathAdd_clicked()
{
    COptString* option = dynamic_cast<COptString*>(opt(eOptSetGeneral, "lastVisitedFolder"));
    const QString directory = QFileDialog::getExistingDirectory(this, "Choose legacy figure resource directory", option->value());
    if(!directory.isEmpty())
    {
        ui->figPaths->addItem(directory);
        option->setValue(directory);
    }
}


void CSettings::on_texPathAdd_clicked()
{
    COptString* option = dynamic_cast<COptString*>(opt(eOptSetGeneral, "lastVisitedFolder"));
    const QString directory = QFileDialog::getExistingDirectory(this, "Choose legacy texture resource directory", option->value());
    if(!directory.isEmpty())
    {
        ui->texPaths->addItem(directory);
        option->setValue(directory);
    }
}


void CSettings::on_figPathRemove_clicked()
{
    int index = ui->figPaths->currentRow();
    if(index >=0)
        ui->figPaths->takeItem(index);
}


void CSettings::on_texPathRemove_clicked()
{
    int index = ui->texPaths->currentRow();
    if(index >=0)
        ui->texPaths->takeItem(index);
}


void CSettings::on_texPathUp_clicked()
{
    int index = ui->texPaths->currentRow();
    if(index > 0)
    {
        auto pItem = ui->texPaths->takeItem(index);
        ui->texPaths->insertItem(index-1, pItem);
        ui->texPaths->setCurrentItem(pItem);
    }
}


void CSettings::on_texpathDown_clicked()
{
    int index = ui->texPaths->currentRow();
    if(index < ui->texPaths->count()-1)
    {
        auto pItem = ui->texPaths->takeItem(index);
        ui->texPaths->insertItem(index+1, pItem);
        ui->texPaths->setCurrentItem(pItem);
    }
}


void CSettings::on_figPathUp_clicked()
{
    int index = ui->figPaths->currentRow();
    if(index > 0)
    {
        auto pItem = ui->figPaths->takeItem(index);
        ui->figPaths->insertItem(index-1, pItem);
        ui->figPaths->setCurrentItem(pItem);
    }
}


void CSettings::on_figPathDown_clicked()
{
    int index = ui->figPaths->currentRow();
    if(index < ui->figPaths->count()-1)
    {
        auto pItem = ui->figPaths->takeItem(index);
        ui->figPaths->insertItem(index+1, pItem);
        ui->figPaths->setCurrentItem(pItem);
    }
}

