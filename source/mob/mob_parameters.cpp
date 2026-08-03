#include <QTableWidget>
#include <QMessageBox>
#include <QTextBlock>
#include <QMenu>
#include <QCloseEvent>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QCheckBox>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QSettings>
#include <QVBoxLayout>

#include "mob_parameters.h"
#include "ui_mob_parameters.h"
#include "range_dialog.h"
#include "utils.h"
#include "view.h"
#include "settings.h"
#include "undo.h"
#include "layout_components\connectors_ui.h"

CMobParameters::CMobParameters(QWidget* parent, CMob* pMob, CView* pView):
    QWidget(parent, Qt::Window)
  ,ui(new Ui::CMobParameters)
  ,m_pCurMob(pMob)
  ,m_pView(pView)
  ,m_pDiplomacyWindow(nullptr)
  ,m_pSymmetricDiplomacyCheck(nullptr)
   ,m_isDurty(false)
  ,m_closeAccepted(false)
  ,m_pShortcut(nullptr)
{
    ui->setupUi(this);
    ui->verticalLayout_2->removeWidget(ui->diplomacyButton);
    ui->verticalLayout_3->insertWidget(1, ui->diplomacyButton);
    ui->diplomacyButton->setText(tr("Diplomacy..."));
    ui->diplomacyButton->setToolTip(tr(
        "Edit or initialize this MOB's 32-player diplomacy table. Magic-trap "
        "hostility is derived from this table at runtime; the trap's stored "
        "diplomacy value is not sufficient."));
    QMenu* diplomacyMenu = new QMenu(ui->diplomacyButton);
    diplomacyMenu->addAction(tr("Edit table"), this,
                             SLOT(on_diplomacyButton_clicked()));
    diplomacyMenu->addAction(tr("Copy from loaded MOB..."), this,
                             SLOT(copyDiplomacyFromLoadedMob()));
    diplomacyMenu->addAction(tr("Create default 32-player table"), this,
                             SLOT(createNeutralDiplomacyTable()));
    ui->diplomacyButton->setMenu(diplomacyMenu);
    initLineEdit();

    m_pUndoStack.reset(new QUndoStack(this));
    ui->undoViewMob->setStack(m_pUndoStack.get());

    generateTitle();
    ui->listRanges->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_pDiplomacyWindow = new QWidget(this, Qt::Window);
    m_pDiplomacyWindow->setObjectName(QStringLiteral("diplomacyWindow"));
    m_pDiplomacyWindow->setWindowTitle(tr("MOB Diplomacy"));
    m_pDiplomacyWindow->setAttribute(Qt::WA_QuitOnClose, false);
    m_pDiplomacyWindow->installEventFilter(this);
    QVBoxLayout* diplomacyLayout = new QVBoxLayout(m_pDiplomacyWindow);
    QLabel* legend = new QLabel(tr(
        "0 (Ally): friendly. 1 (Neutral): neither friendly nor hostile.\n"
        "2 (War): hostile. Double-click any cell to cycle its value."),
        m_pDiplomacyWindow);
    legend->setWordWrap(true);
    diplomacyLayout->addWidget(legend);
    m_pSymmetricDiplomacyCheck = new QCheckBox(
        tr("Apply changes in both directions"), m_pDiplomacyWindow);
    m_pSymmetricDiplomacyCheck->setToolTip(tr(
        "Checked: changing A → B also changes B → A.\n"
        "Unchecked: edit only the selected row-to-column relationship. "
        "Directional relationships are supported."));
    bool symmetric = true;
    if(m_pView && m_pView->settings())
        if(COptBool* option = dynamic_cast<COptBool*>(
               m_pView->settings()->opt("dipEditSymmetric")))
            symmetric = option->value();
    m_pSymmetricDiplomacyCheck->setChecked(symmetric);
    connect(m_pSymmetricDiplomacyCheck, &QCheckBox::toggled,
            this, [this](bool checked)
    {
        if(!m_pView || !m_pView->settings())
            return;
        if(COptBool* option = dynamic_cast<COptBool*>(
               m_pView->settings()->opt("dipEditSymmetric")))
        {
            option->setValue(checked);
            m_pView->settings()->persistOptions();
        }
    });
    diplomacyLayout->addWidget(m_pSymmetricDiplomacyCheck);
    QLabel* directionNotice = new QLabel(tr(
        "Rows are the acting faction; columns are the faction being treated. "
        "Self-relations and one-way relations are supported, but validation "
        "warns so unusual values can be confirmed."), m_pDiplomacyWindow);
    directionNotice->setWordWrap(true);
    diplomacyLayout->addWidget(directionNotice);
    m_pTable.reset(new QTableWidget(32, 32, m_pDiplomacyWindow));
    m_pTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    diplomacyLayout->addWidget(m_pTable.get(), 1);
    QObject::connect(m_pTable.get(), SIGNAL(cellDoubleClicked(int,int)), this, SLOT(tableItemClicked(int,int)));
    QObject::connect(m_pTable.get(), SIGNAL(currentCellChanged(int,int,int,int)),
                     this, SLOT(highlightDiplomacySelection(int,int,int,int)));
    m_pShortcut = new QShortcut(QKeySequence(Qt::CTRL + eKey_S), this, SLOT(onSaveShortcut()));
    //m_aCell.clear();
    m_aCell.resize(32*32);
    for (int i(0); i < 32; ++i)
    {
        for (int j(0); j < 32; ++j)
        {
            m_aCell[i*32+j].reset(new QTableWidgetItem());
            // low performance
            m_pTable->setItem(i, j, m_aCell[i*32+j].get());
        }
    }
    //connect(m_pTable.get(), SIGNAL(cellClicked(int, int)), this, SLOT(tableItemClicked(int, int)));
    // Script editing is document-level and intentionally lives in its own window.
    ui->scriptLabel->hide();
    ui->pushButtonOpenExtEditor->hide();
    ui->plainTextEdit->hide();
    ui->listRanges->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listRanges, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    updateWindow();
    m_initialState = m_pCurMob->parameterState();
}

CMobParameters::~CMobParameters()
{
    m_aCell.clear();
    m_pTable.clear();
    delete m_pShortcut;
}

void CMobParameters::reset()
{
    paramLine(eWsTypeWindDir)->clear();
    paramLine(eWsTypeWindStr)->clear();
    paramLine(eWsTypeAmbient)->clear();
    paramLine(eWsTypeTime)->clear();
    paramLine(eWsTypeSunLight)->clear();
    ui->listRanges->clear();
    ui->isPrimaryBox->setChecked(false);
    m_pCurMob = nullptr;
}

void CMobParameters::test()
{
    //ui->diplomacyButton->click();
    show();
}

void CMobParameters::execWsChanges(EWsType paramType, QString &value)
{
    auto pCommand = new CChangeWorldSetCommand(m_pCurMob, paramType, value);
    QObject::connect(pCommand, SIGNAL(changeWsSignal()), this, SLOT(updateMobParamsOnly()));
    m_pUndoStack->push(pCommand);
}

void CMobParameters::setNewRange(SRange& arrRanges, int index)
{
    auto pRangeCommand = new CChangeRangeCommand(m_pCurMob, index, arrRanges);
    QObject::connect(pRangeCommand, SIGNAL(changeRangeSignal()), this, SLOT(updateRangeList()));
    m_pUndoStack->push(pRangeCommand);
}

void CMobParameters::initLineEdit()
{
    for(int i(0); i<eWsTypeCount; ++i)
    {
        auto pParam = new CParamLineEdit(this, (EWsType)i);
        pParam->setMinimumSize(QSize(0, 0));
        ui->formLayout_2->setWidget(1+i, QFormLayout::FieldRole, pParam); // 0 row - label environment
        m_arrMobParam.append(pParam);
    }
}

CParamLineEdit *CMobParameters::paramLine(EWsType param)
{
    CParamLineEdit* pEdit = nullptr;
    foreach(pEdit, m_arrMobParam)
    {
        if(pEdit->isParam(param))
            return pEdit;
    }
    Q_ASSERT("cant find suitable line edit" && false);
    return pEdit;
}

const QVector<SRange> &CMobParameters::activeRanges()
{
    return m_pCurMob->ranges(!m_pCurMob->isQuestMob());
}

void CMobParameters::setDurty(bool isDurty)
{
    m_isDurty = isDurty;
    generateTitle();
}

void CMobParameters::generateTitle()
{
    QString title("Mob Parameters: " + m_pCurMob->filePath().absoluteFilePath());
    if (m_isDurty)
        title += " *";

    setWindowTitle(title);
}

void colorizeCell(QTableWidgetItem* pCell, int value)
{
    switch (value)
    {
    case 0: // if diplomacy table - friend. if scripts - neutral
        pCell->setBackground(Qt::darkGreen);
        break;
    case 1: // if diplomacy table - neutral. if scripts - friend
        pCell->setBackground(Qt::darkYellow);
        break;
    case 2:
        pCell->setBackground(Qt::darkRed);
        break;
    }
}

QString diplomacyValueText(uint value)
{
    return QString::number(value);
}

QString diplomacyValueToolTip(uint value)
{
    switch(value)
    {
    case 0: return QObject::tr(
        "Ally: the row faction treats the column faction as friendly.");
    case 1: return QObject::tr(
        "Neutral: the row faction treats the column faction as neither friendly nor hostile.");
    case 2: return QObject::tr(
        "War: the row faction treats the column faction as hostile.");
    default: return QObject::tr(
        "Unknown stored diplomacy value %1; Endurance preserves it.").arg(value);
    }
}

void setDiplomacyCellPresentation(QTableWidgetItem* cell, uint value)
{
    if(!cell)
        return;
    cell->setText(diplomacyValueText(value));
    cell->setToolTip(diplomacyValueToolTip(value));
}

void CMobParameters::updateWindow()
{
    Q_ASSERT(m_pCurMob);


    QVector<SRange> range;
    if(m_pCurMob->isQuestMob())
    { // collect only sec rangers for quest mob
        range = m_pCurMob->ranges(false);
        ui->baseMobParamWidget->hide();
    }
    else
    { // collect data for base mob

        CWorldSet worldSet = m_pCurMob->worldSet();
        for(int i(0); i<eWsTypeCount; ++i)
        {
            paramLine((EWsType)i)->setText(worldSet.data((EWsType)i));
            paramLine((EWsType)i)->saveBackupValue();
        }

        range = m_pCurMob->ranges(true);

        ui->baseMobParamWidget->show();
    }

    QString rangeText;
    ui->listRanges->clear();
    const SRange& activeRange = m_pCurMob->activeRange();
    for (const auto& r : range)
    {
        rangeText = QString::number(r.minRange) + "-" + QString::number(r.maxRange);
        if(r == activeRange)
        {
            ui->listRanges->addItem(new QListWidgetItem(*CIconManager::getInstance()->icon("arrow_right"), rangeText));
        }
        else
            ui->listRanges->addItem(rangeText);
//            auto pItem = ui->listRanges->item(ui->listRanges->count()-1);
//            pItem->setFlags(pItem->flags() | Qt::ItemIsEditable);
    }

    //diplomacy
    QStringList dipName = m_pCurMob->diplomacyNames();
    QVector<QVector<uint>>& dipTable = m_pCurMob->diplomacyField();
    int val;
    if(!dipName.isEmpty() && !dipTable.isEmpty())
    {
        Q_ASSERT(dipName.size() == 32);
        for (int i(0); i < dipTable.size(); ++i)
        {
            for (int j(0); j < dipTable[i].size(); ++j)
            {
                auto pCell = m_aCell[i*32 + j];
                val = dipTable[i][j];
                setDiplomacyCellPresentation(pCell.get(), uint(val));
                colorizeCell(pCell.get(), val);
                pCell->setTextColor(Qt::white);
                pCell->setFlags(pCell->flags() & ~Qt::ItemIsEditable);
            }
        }
    }
    ui->isPrimaryBox->setChecked(m_pCurMob->isQuestMob());
}


void CMobParameters::on_diplomacyButton_clicked()
{
    if(!m_pCurMob)
    {
        return;
    }

    QStringList dipName = m_pCurMob->diplomacyNames();
    QVector<QVector<uint>> dipTable = m_pCurMob->diplomacyField();
    if(dipName.isEmpty() || dipTable.isEmpty())
    {
        QMessageBox::information(this, tr("No diplomacy table"), tr(
            "This MOB has no diplomacy table. Use the arrow menu to copy the "
            "intended table from a loaded source MOB, or create a neutral table."));
        return;
    }

    //setup view

    HorizontalHeaderView* headerView = new HorizontalHeaderView(Qt::Horizontal); //todo: remove memory leak!

    //QHeaderView* headerView = new QHeaderView(Qt::Horizontal);

    m_pTable->setHorizontalHeader(headerView); //set our custom headerview
    m_pTable->setToolTip(tr(
        "0 (Ally): friendly. 1 (Neutral): neither friendly nor hostile.\n"
        "2 (War): hostile. Double-click any cell to cycle its value."));
    m_pTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_pTable->setVerticalHeaderLabels(dipName);
    m_pTable->setHorizontalHeaderLabels(dipName);
    m_pTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_pTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pTable->resizeColumnsToContents();
    m_pTable->resizeRowsToContents();
    m_pDiplomacyWindow->show();
    restoreDiplomacyWindowSize();
    m_pDiplomacyWindow->raise();
    m_pDiplomacyWindow->activateWindow();
    //todo: disable mob parameter widget, connect to table close signal to enable main window again
}

void CMobParameters::createNeutralDiplomacyTable()
{
    if(!m_pCurMob)
        return;
    if(QMessageBox::question(this, tr("Create diplomacy table"), tr(
        "Create the default zero-valued 32-player diplomacy table?\n\n"
        "It contains no hostile relations, so it does not make magic traps "
        "hostile. For a copied "
        "trap, copying diplomacy from its source MOB is usually correct."))
       != QMessageBox::Yes)
        return;

    QList<QString> names;
    QVector<QVector<uint>> table(32, QVector<uint>(32, 0));
    for(int index = 0; index < 32; ++index)
        names.append(QString("Player-%1").arg(index));
    auto* command = new CReplaceDiplomacyTableCommand(
        m_pCurMob, names, table, tr("Created default diplomacy table"));
    connect(command, SIGNAL(diplomacyTableReplaced()),
            this, SLOT(updateDiplomacyTable()));
    m_pUndoStack->push(command);
}

void CMobParameters::copyDiplomacyFromLoadedMob()
{
    if(!m_pCurMob || !m_pView)
        return;
    QVector<CMob*> sources;
    QStringList labels;
    for(CMob* mob : m_pView->mobs())
    {
        if(!mob || mob == m_pCurMob || mob->diplomacyNames().size() != 32
           || mob->diplomacyField().size() != 32)
            continue;
        sources.append(mob);
        labels.append(QString("%1 — %2")
            .arg(mob->mobName(), mob->filePath().absoluteFilePath()));
    }
    if(sources.isEmpty())
    {
        QMessageBox::information(this, tr("No diplomacy source"), tr(
            "No other loaded MOB has a complete diplomacy table. Load the "
            "primary/source MOB, then try again."));
        return;
    }
    bool accepted = false;
    const QString choice = QInputDialog::getItem(
        this, tr("Copy diplomacy table"), tr("Source MOB:"), labels, 0, false,
        &accepted);
    if(!accepted)
        return;
    const int index = labels.indexOf(choice);
    if(index < 0 || index >= sources.size())
        return;
    CMob* source = sources.at(index);
    auto* command = new CReplaceDiplomacyTableCommand(
        m_pCurMob, source->diplomacyNames(), source->diplomacyField(),
        tr("Copied diplomacy from %1").arg(source->mobName()));
    connect(command, SIGNAL(diplomacyTableReplaced()),
            this, SLOT(updateDiplomacyTable()));
    m_pUndoStack->push(command);
}

void CMobParameters::tableItemClicked(int r, int c)
{
    auto pCommand = new CChangeDiplomacyTableItem(
        m_pCurMob, r, c, m_pSymmetricDiplomacyCheck->isChecked());
    QObject::connect(pCommand, SIGNAL(changeDipGroup(int,int)), this, SLOT(updateDiplomacyTable(int,int)));
    m_pUndoStack->push(pCommand);
}

void CMobParameters::highlightDiplomacySelection(
    int currentRow, int currentColumn, int previousRow, int previousColumn)
{
    Q_UNUSED(previousRow)
    Q_UNUSED(previousColumn)
    if(currentRow < 0 || currentColumn < 0 || !m_pTable->selectionModel())
        return;
    QItemSelectionModel* selection = m_pTable->selectionModel();
    selection->select(m_pTable->model()->index(currentRow, 0),
                      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    selection->select(m_pTable->model()->index(0, currentColumn),
                      QItemSelectionModel::Select | QItemSelectionModel::Columns);
}

QSize CMobParameters::diplomacyContentSize() const
{
    const QMargins margins = m_pDiplomacyWindow->layout()->contentsMargins();
    int width = 0;
    int height = margins.top() + margins.bottom();
    const int count = m_pDiplomacyWindow->layout()->count();
    for(int index = 0; index < count; ++index)
    {
        QLayoutItem* item = m_pDiplomacyWindow->layout()->itemAt(index);
        QWidget* widget = item ? item->widget() : nullptr;
        if(!widget)
            continue;
        QSize itemSize = widget->sizeHint();
        if(widget == m_pTable.get())
        {
            itemSize.setWidth(m_pTable->verticalHeader()->width()
                + m_pTable->horizontalHeader()->length()
                + m_pTable->frameWidth() * 2 + 4);
            itemSize.setHeight(m_pTable->horizontalHeader()->height()
                + m_pTable->verticalHeader()->length()
                + m_pTable->frameWidth() * 2 + 4);
        }
        width = qMax(width, itemSize.width());
        height += itemSize.height();
    }
    if(count > 1)
        height += m_pDiplomacyWindow->layout()->spacing() * (count - 1);
    return QSize(width + margins.left() + margins.right(), height);
}

void CMobParameters::restoreDiplomacyWindowSize()
{
    static const char kSizeKey[] = "ui/diplomacyWindowSizeV2";
    QSettings settings;
    const QSize saved = settings.value(kSizeKey).toSize();
    if(saved.isValid())
    {
        m_pDiplomacyWindow->resize(saved);
        return;
    }

    m_pDiplomacyWindow->ensurePolished();
    m_pDiplomacyWindow->layout()->activate();
    const QSize wanted = diplomacyContentSize();
    QScreen* screen = QGuiApplication::screenAt(m_pDiplomacyWindow->frameGeometry().center());
    if(!screen)
        screen = QGuiApplication::primaryScreen();
    const QSize available = screen ? screen->availableGeometry().size() : QSize(1280, 800);
    m_pDiplomacyWindow->resize(
        qMin(wanted.width(), qRound(available.width() * 0.9)),
        qMin(wanted.height(), qRound(available.height() * 0.9)));
}

bool CMobParameters::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == m_pDiplomacyWindow && event->type() == QEvent::Close)
        QSettings().setValue("ui/diplomacyWindowSizeV2", m_pDiplomacyWindow->size());
    return QWidget::eventFilter(watched, event);
}

void CMobParameters::on_pushCancel_clicked()
{
    close();
}

void CMobParameters::applyParameterChanges()
{
    SMobParameterState finalState = m_pCurMob->parameterState();
    // A script may have changed through the independent editor while this
    // window was open. Never fold that change into this parameters command.
    m_initialState.script = finalState.script;
    m_pUndoStack->clear();
    m_pView->commitMobParameterEdit(m_pCurMob, m_initialState, finalState);
}

bool CMobParameters::resolveCloseRequest()
{
    if(!m_pUndoStack->isClean())
    {
        const QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Closing", "Parameters has local changes.\nDo you want to Apply them?",
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if(reply == QMessageBox::Yes)
            applyParameterChanges();
        else if(reply == QMessageBox::No)
        {
            while(m_pUndoStack->canUndo())
                m_pUndoStack->undo();
        }
        else if(reply == QMessageBox::Cancel)
            return false;
    }
    return true;
}

void CMobParameters::on_pushApply_clicked()
{
    applyParameterChanges();
    emit editFinishedSignal(this);
    m_closeAccepted = true;
    close();
}

void CMobParameters::closeEvent(QCloseEvent* event)
{
    if(m_closeAccepted)
    {
        event->accept();
        return;
    }

    if(!resolveCloseRequest())
    {
        event->ignore();
        return;
    }

    emit editFinishedSignal(this);
    m_closeAccepted = true;
    event->accept();
}

#if 0 // Legacy script highlighter moved to mob/script_highlighter.cpp.
Highlighter::Highlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    HighlightingRule rule;

    keywordFormat.setForeground(QBrush(QColor(22, 67, 233)));
    keywordFormat.setFontWeight(QFont::Bold);
    QStringList keywordPatterns;
    keywordPatterns << "\\bGlobalVars\\b" << "\\bDeclareScript\\b" << "\\bScript\\b"
                    << "\\bWorldScript\\b" << "\\bif\\b" << "\\bthen\\b"
                    << "\\bNULL\\b";
    foreach (const QString &pattern, keywordPatterns) {
        rule.pattern = QRegExp(pattern);
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    //
//    classFormat.setFontWeight(QFont::Bold);
//    classFormat.setForeground(Qt::green);
//    rule.pattern = QRegExp("\\bQ[A-Za-z]+\\b");
//    rule.format = classFormat;
//    highlightingRules.append(rule);

    quotationFormat.setForeground(Qt::darkYellow);
    rule.pattern = QRegExp("\".*\"");
    rule.format = quotationFormat;
    highlightingRules.append(rule);

    functionFormat.setForeground(QBrush(QColor(196, 62, 229)));
    functionFormat.setFontWeight(QFont::Bold);
    rule.pattern = QRegExp("\\b(ActivateTrap|Add|AddLoot|AddMob|AddObject|AddRectToArea|AddRoundToArea|AddUnitToParty|AddUnitToServer|AddUnitUnderControl|AlarmPosX|AlarmPosY|AlarmTime|Any|AttachParticles|AttachParticleSource|Attack|BlockUnit|Cast|CastSpellPoint|CastSpellUnit|ConsoleFloat|ConsoleString|CopyItems|CopyLoot|CopyStats|Crawl|CreateFX|CreateFXSource|CreateLightning|CreateParticleSource|CreateParty|CreatePointLight|CreateRandomizeFXSource|DeleteArea|DeleteFXSource|DeleteLightning|DeleteParticleSource|DeletePointLight|DistanceUnitPoint|DistanceUnitUnit|Div|EnableLever|EraseQuestItem|Every|FixItems|FixWorldTime|Follow|For|ForIf|GetAIClass|GetBSZValue|GetDiplomacy|GetFutureX|GetFutureY|GetLeader|GetLeverState|GetLootItemsCount|GetMercsNumber|GetMoney|GetObject|GetObjectByID|GetObjectByName|GetObjectID|GetPlayer|GetPlayerUnits|GetUnitOfPlayer|GetWorldTime|GetX|GetY|GetZ|GetZValue|GiveDexterity|GiveIntelligence|GiveItem|GiveMoney|GiveQuestItem|GiveSkill|GiveStrength|GiveUnitQuestItem|GiveUnitSpell|GodMode|GroupAdd|GroupCross|GroupHas|GroupSee|GroupSize|GroupSub|GSDelVar|GSGetVar|GSSetVar|GSSetVarMax|Guard|HaveItem|HideObject|HP|Idle|InflictDamage|InvokeAlarm|IsAlarm|IsAlive|IsCameraPlaying|IsDead|IsEnemy|IsEqual|IsEqualString|IsGreater|IsInArea|IsInSquare|IsLess|IsNight|IsPlayerInDanger|IsPlayerInSafety|IsUnitBlocked|IsUnitInWater|IsUnitVisible|KillScript|KillUnit|LeaveToZone|Lie|Mana|MaxHP|MaxMana|MoveParticleSource|MovePointLight|MoveToObject|MoveToPoint|Mul|Not|PlayAnimation|PlayCamera|PlayerSee|PlayFX|PlayMovie|QFinish|QObjArea|QObjGetItem|QObjKillGroup|QObjKillUnit|QObjSeeObject|QObjSeeUnit|QObjUse|QStart|QuestComplete|Random|RecalcMercBriefings|RedeployParty|RemoveObject|RemoveObjectFromServer|RemoveParty|RemoveQuestItem|RemoveUnitFromControl|RemoveUnitFromParty|RemoveUnitFromServer|ResetTarget|Rest|RotateTo|Run|RunWorldTime|SendEvent|SendStringEvent|Sentry|SetCameraOrientation|SetCameraPosition|SetCP|SetCPFast|SetCurrentParty|SetDiplomacy|SetDirectionToObject|SetEnemy|SetParticleSourceSize|SetPlayer|SetPlayerAggression|SetScience|SetSpellAggression|SetSunLight|SetWaterLevel|SetWind|ShowBitmap|ShowCredits|Sleep|SleepUntil|SleepUntilIdle|Stand|StartAnimation|Sub|SwitchLeverState|SwitchLeverStateEx|UMAg|UMAggression|UMClear|UMCorpseWatcher|UMFear|UMFollow|UMGuard|UMGuardEx|UMPatrol|UMPatrolAddPoint|UMPatrolAddPointLook|UMPatrolClear|UMPlayer|UMRevenge|UMSentry|Walk|CreateRandomizedFXSource|PlayMusic|SetBackGroundColor|UMStandard|UMSuspection|UnitInSquare|UnitSee|WaitEndAnimation|WaitSegment|WasLooted|GetPlayerOfUnit|GetUnitExp|GetArmor|GetObjectStringID|FloatToStr|StrToFloat|StrCat|StrLen|Sin|Cos|Pow|DistancePointPoint|NearestUnit|UnitsNearPoint)(?!.*\")\\b");
    rule.format = functionFormat;
    highlightingRules.append(rule);
//    QStringList funcPatterns;
//    keywordPatterns << "\\bGlobalVars\\b" << "\\bDeclareScript\\b" << "\\bScript\\b"
//                    << "\\bWorldScript\\b" << "\\bif\\b" << "\\bthen\\b"
//                    << "\\bNULL\\b";
//    foreach (const QString &pattern, funcPatterns) {
//        rule.pattern = QRegExp(pattern);
//        rule.format = functionFormat;
//        highlightingRules.append(rule);
//    }

//    customFunctionFormat.setFontItalic(true);
//    customFunctionFormat.setForeground(Qt::magenta);
//    rule.pattern = QRegExp("\\b[A-Za-z0-9_]+(?=\\()");
//    rule.format = customFunctionFormat;
//    highlightingRules.append(rule);

    //
    singleLineCommentFormat.setForeground(Qt::lightGray);
    rule.pattern = QRegExp("//[^\n]*");
    rule.format = singleLineCommentFormat;
    highlightingRules.append(rule);

    multiLineCommentFormat.setForeground(Qt::lightGray);

    commentStartExpression = QRegExp("/\\*");
    commentEndExpression = QRegExp("\\*/");

    typesFormat.setFontWeight(QFont::Bold);
    typesFormat.setForeground(QBrush(QColor(80, 232, 209)));
    rule.pattern = QRegExp("(object|float|group|string)((?=,|\\s+|\\))|$)");
    rule.format = typesFormat;
    highlightingRules.append(rule);

    numberFormat.setFontWeight(QFont::Bold);
    numberFormat.setForeground(QBrush(QColor(224, 217, 26)));
    //rule.pattern = QRegExp("(?<!_)[0-9.]+"); //look behind didnt work with QRegExp. Use QRegularExpression instead. rewrite highlight block
    rule.pattern = QRegExp("\\-?\\b[\\d.]+\\b(?![a-zA-Z ]*\")");
    rule.format = numberFormat;
    highlightingRules.append(rule);
}


void Highlighter::highlightBlock(const QString &text)
{
    foreach (const HighlightingRule &rule, highlightingRules) {
            QRegExp expression(rule.pattern);
            int index = expression.indexIn(text);
            while (index >= 0) {
                int length = expression.matchedLength();
                setFormat(index, length, rule.format);
                index = expression.indexIn(text, index + length);
            }
        }
    //
    setCurrentBlockState(0);
    //
    int startIndex = 0;
     if (previousBlockState() != 1)
         startIndex = commentStartExpression.indexIn(text);
     //
     while (startIndex >= 0) {
          int endIndex = commentEndExpression.indexIn(text, startIndex);
          int commentLength;
          if (endIndex == -1) {
              setCurrentBlockState(1);
              commentLength = text.length() - startIndex;
          } else {
              commentLength = endIndex - startIndex
                              + commentEndExpression.matchedLength();
          }
          setFormat(startIndex, commentLength, multiLineCommentFormat);
          startIndex = commentStartExpression.indexIn(text, startIndex + commentLength);
      }
}


#endif

void CMobParameters::on_isPrimaryBox_clicked()
{
    if(nullptr == m_pCurMob)
        return;

    //m_pView->switchToQuestMob(m_pCurMob, !ui->isPrimaryBox->isChecked());
    auto pCommand = new CSwitchToQuestMobCommand(m_pCurMob);
    QObject::connect(pCommand, SIGNAL(switchQuestMobSignal()), this, SLOT(updateMobParamsOnly()));
    m_pUndoStack->push(pCommand);

    //auto iii = m_pUndoStack->count();
    //convertIdRange();
    //m_pCurMob->setQuestMob(ui->isPrimaryBox->isChecked());
    //updateWindow();
}

void CMobParameters::on_button_minusRanges_clicked()
{
    int index = ui->listRanges->currentRow();
    if(index < 0)
        return;

    SRange range;
    auto pRangeCommand = new CChangeRangeCommand(m_pCurMob, index, range);
    QObject::connect(pRangeCommand, SIGNAL(changeRangeSignal()), this, SLOT(updateRangeList()));
    m_pUndoStack->push(pRangeCommand);
}


void CMobParameters::on_button_plusRanges_clicked()
{
    CRangeDialog* rangeD = new CRangeDialog(this);
    rangeD->setAttribute(Qt::WA_DeleteOnClose);
    rangeD->initRanges(activeRanges(), ui->listRanges->count());
    rangeD->exec();
}

void CMobParameters::onMobUnload(CMob *pMob)
{
    if(pMob == m_pCurMob)
        close();
}


CParamLineEdit::CParamLineEdit(QWidget *pParent, EWsType param):
    QLineEdit(pParent)
  ,m_mobParam(param)
{
    m_pParent = dynamic_cast<CMobParameters*>(pParent);
    QObject::connect(this, SIGNAL(editingFinished()), this, SLOT(editingFinishedOverried()));
}

void CParamLineEdit::saveBackupValue()
{
    m_storedValue = text();
}

void CParamLineEdit::editingFinishedOverried()
{
    auto enteredText = text();
    if(enteredText == m_storedValue)
        return;

    if(isValidValue(m_mobParam, enteredText))
    {
        m_storedValue = enteredText;
        m_pParent->execWsChanges(m_mobParam, enteredText);
    }
    else
    { // return old value
        //todo: show user warn message
        this->blockSignals(true);
        setText(m_storedValue);
        this->blockSignals(false);
    }

}

bool CParamLineEdit::isValidValue(EWsType paramType, const QString& str)
{
    bool bRes = false;
    if(paramType == eWsTypeWindDir)
    {// vector3d. checked value: (111.66,94.46,-0.25) ( 0.21, 0.13, 1.20)
        QRegExp rx("\\((\\s*-?\\d+(\\.\\d+)?\\,){2}(\\s*-?\\d+(\\.\\d+)?)\\)");
        bRes = rx.exactMatch(str);
    }
    else
    { //float;
        QRegExp rx("^\\s*-?\\d+(\\.\\d+)?");
        bRes = rx.exactMatch(str);
    }

    return bRes;
}

void CMobParameters::on_listRanges_itemDoubleClicked(QListWidgetItem *item)
{
    Q_UNUSED(item);
    auto rangeD = new CRangeDialog(this);
    rangeD->setAttribute(Qt::WA_DeleteOnClose);
    rangeD->initRanges(activeRanges(), ui->listRanges->currentRow());
    rangeD->exec();
}

void CMobParameters::on_pushButtonOpenExtEditor_clicked()
{
    if(m_pView && m_pCurMob)
        m_pView->openExternalMobScript(m_pCurMob);
}

void CMobParameters::updateMobParamsOnly()
{
    Q_ASSERT(m_pCurMob);

    QVector<SRange> arrRange;
    if(m_pCurMob->isQuestMob())
    { // collect only sec rangers for quest mob
        arrRange = m_pCurMob->ranges(false);
        ui->baseMobParamWidget->hide();
    }
    else
    { // collect data for base mob
        CWorldSet worldSet = m_pCurMob->worldSet();
        for(int i(0); i<eWsTypeCount; ++i)
        {
            paramLine((EWsType)i)->setText(worldSet.data((EWsType)i));
            paramLine((EWsType)i)->saveBackupValue();
        }
        ui->baseMobParamWidget->show();
    }

    updateRangeList();

    ui->isPrimaryBox->setChecked(m_pCurMob->isQuestMob());
    setDurty(true);
}

void CMobParameters::updateDiplomacyTable()
{
    //diplomacy
    QStringList dipName = m_pCurMob->diplomacyNames();
    const QVector<QVector<uint>>& dipTable = m_pCurMob->diplomacyField();
    if(!dipName.isEmpty() && !dipTable.isEmpty())
    {
        Q_ASSERT(dipName.size() == 32);
        for (int i(0); i < dipTable.size(); ++i)
        {
            for (int j(0); j < dipTable[i].size(); ++j)
            {
                auto pCell = m_aCell[i*32 + j];
                setDiplomacyCellPresentation(pCell.get(), dipTable[i][j]);
                colorizeCell(pCell.get(), int(dipTable[i][j]));
                //pCell->setFlags(pCell->flags() & ~Qt::ItemIsEditable);
            }
        }
    }
}

void CMobParameters::updateDiplomacyTable(int row, int column)
{
    //diplomacy
    QStringList dipName = m_pCurMob->diplomacyNames();
    const QVector<QVector<uint>>& dipTable = m_pCurMob->diplomacyField();
    if(!dipName.isEmpty() && !dipTable.isEmpty())
    {
        Q_ASSERT(dipName.size() == 32);
        auto pCell = m_aCell[row*32 + column];
        int value = dipTable[row][column];
        setDiplomacyCellPresentation(pCell.get(), uint(value));
        colorizeCell(pCell.get(), value);
    }
}


void CMobParameters::on_pushCheck_clicked()
{

}

void CMobParameters::onSaveShortcut()
{
    m_pUndoStack->clear();
    setDurty(false);
}

void CMobParameters::showContextMenu(QPoint pos)
{
    // Handle global position
        QPoint globalPos = ui->listRanges->mapToGlobal(pos);

        // Create menu and insert some actions
        QMenu myMenu;
        myMenu.addAction("mark as range for auto-generate IDs", this, SLOT(markRange()));
        //myMenu.addAction("Erase",  this, SLOT(eraseItem()));

        // Show context menu at handling position
        myMenu.exec(globalPos);
}

void CMobParameters::markRange()
{
    m_pCurMob->setActiveRange(ui->listRanges->currentRow());
    // If multiple selection is on, we need to erase all selected items
        for (int i = 0; i < ui->listRanges->selectedItems().size(); ++i)
        {
            qDebug() << ui->listRanges->item(ui->listRanges->currentRow())->text();
            // Get curent item on selected row
            //QListWidgetItem *item = ui->listRanges->takeItem(ui->listRanges->currentRow());
            // And remove it
            //delete item;
        }
        updateRangeList();
}

void CMobParameters::updateRangeList()
{
    ui->listRanges->clear();
    QVector<SRange> arrRange = m_pCurMob->ranges(!m_pCurMob->isQuestMob());
    QString rangeText;
    ui->listRanges->clear();
    const SRange& activeRange = m_pCurMob->activeRange();
    for (const auto& r : arrRange)
    {
        rangeText = QString::number(r.minRange) + "-" + QString::number(r.maxRange);
        if(r == activeRange)
            ui->listRanges->addItem(new QListWidgetItem(*CIconManager::getInstance()->icon("arrow_right"), rangeText));
        else
            ui->listRanges->addItem(rangeText);
    }
}

