#include <QPointer>
#include "window_geometry.h"
#include "createobjectform.h"
#include "ui_createobjectform.h"
#include "objects\lever.h"
#include "objects\light.h"
#include "objects\magictrap.h"
#include "objects\particle.h"
#include "objects\sound.h"
#include "objects\torch.h"
#include "objects\worldobj.h"
#include "objects\unit.h"
#include "resourcemanager.h"
#include "landscape.h"
#include "log.h"
#include "preview.h"
#include "undo.h"
#include "layout_components/tree_view.h"
#include <QJsonArray>
#include <QMessageBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QCheckBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QSet>
#include "endurance_document.h"
#include "database_document.h"
#include "item_spell_constructor_dialog.h"
#include "settings.h"
#include <QCompleter>
#include <QStandardItemModel>
#include <QPushButton>
#include <QCoreApplication>
#include <QDir>
#include <QStyle>
#include <QSplitter>
#include <QHeaderView>
#include <QSettings>
#include <QHideEvent>
#include <QScopedValueRollback>
#include "model_gallery.h"
#include "model_gallery_thumbnail.h"
#include "resource_catalog.h"
#include "resource_name.h"
#include "game_text_catalog.h"
#include <QPainter>

CCreateObjectForm::CCreateObjectForm(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CCreateObjectForm)
  ,m_pNode(nullptr)
  ,m_pView(nullptr)
  ,m_pPreview(nullptr)
  ,m_pUndoStack(nullptr)
{
    ui->setupUi(this);
    initViewWidget();
    QObject::connect(this, SIGNAL(sendNewBbbox(CBox)), m_pPreview, SLOT(refreshCam(CBox)));
    m_tableManager.reset(new CTableManager(ui->tableParameters));
    QObject::connect(m_tableManager.get(), SIGNAL(onUpdateProperty(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
    QObject::connect(m_tableManager.get(), SIGNAL(onUpdateProperties(QList<QSharedPointer<IPropertyBase>>)),
                     this, SLOT(onPropertiesChange(QList<QSharedPointer<IPropertyBase>>)));

    setWindowTitle(tr("Create object"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);


    m_objType[eWorldObject] = tr("World object");
    m_objType[eUnit] = tr("Unit");
    m_objType[eTorch] = tr("Torch");
    m_objType[eMagicTrap] = tr("Magic trap");
    m_objType[eLever] = tr("Lever");
    m_objType[eLight] = tr("Light source");
    m_objType[eSound] = tr("Sound source");
    m_objType[eParticle] = tr("Particle source");
    ui->comboObjectType->initComboItem(tr("<choose object type>"), m_objType.values());

    QObject::connect(ui->comboObjectType, SIGNAL(currentIndexChangedOut(QString&)), this, SLOT(onObjectChoose(QString&)));

    m_databaseCategory = new QComboBox(this);
    m_databaseCategory->setObjectName("databaseCategory");
    m_databaseCategory->addItems({tr("Creatures"), tr("Items"), tr("Usable objects / levers"),
                                  tr("Technical objects")});
    m_itemMaterial = new QComboBox(this);
    m_itemMaterial->setObjectName("databaseItemMaterial");
    m_itemMaterial->setToolTip(tr("Object material"));
    m_itemModel = new QComboBox(this);
    m_itemModel->setObjectName("databaseItemModel");
    m_itemModel->setToolTip(QString::fromUtf8("Вариант модели предмета из каталога"));
    connect(m_itemModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { selectDatabaseEntry(); });
    m_databaseSearch = new QLineEdit(this);
    m_databaseSearch->setObjectName("databaseSearch");
    m_databaseSearch->setPlaceholderText(tr("Search by name or model"));
    m_databaseList = new QListWidget(this);
    m_databaseList->setObjectName("databaseObjects");
    m_databaseDetails = new QLabel(this);
    m_databaseDetails->setWordWrap(true);
    m_databaseDetails->setTextFormat(Qt::PlainText);
    m_advanced = new QCheckBox(tr("Advanced properties"), this);
    ui->verticalLayout_2->insertWidget(0, m_databaseCategory);
    ui->verticalLayout_2->insertWidget(1, m_databaseSearch);
    ui->verticalLayout_2->insertWidget(2, m_databaseList, 1);
    ui->verticalLayout_2->insertWidget(3, m_databaseDetails);
    ui->verticalLayout_2->insertWidget(4, m_itemMaterial);
    ui->verticalLayout_2->insertWidget(5, m_advanced);
    ui->verticalLayout_2->insertWidget(5, m_itemModel);
    m_technicalTextureLabel = new QLabel(tr("Primary texture for this model"), this);
    m_technicalTexture = new QComboBox(this);
    m_technicalTexture->setObjectName("technicalObjectTexture");
    m_technicalTexture->setEditable(true);
    m_technicalTexture->setInsertPolicy(QComboBox::NoInsert);
    m_technicalTexture->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_technicalTexture->setMinimumContentsLength(25);
    m_technicalTexture->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    m_technicalTexture->completer()->setFilterMode(Qt::MatchContains);
    m_technicalTextureStatus = new QLabel(this);
    m_technicalTextureStatus->setObjectName("technicalTextureCatalogStatus");
    m_technicalTextureStatus->setWordWrap(true);
    ui->verticalLayout_2->insertWidget(6, m_technicalTextureLabel);
    ui->verticalLayout_2->insertWidget(7, m_technicalTexture);
    ui->verticalLayout_2->insertWidget(8, m_technicalTextureStatus);
    m_modelGalleryButton = new QPushButton(tr("Choose model from gallery…"), this);
    m_modelGalleryButton->setObjectName("technicalModelGallery");
    m_modelGalleryButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    ui->verticalLayout_2->insertWidget(6, m_modelGalleryButton);
    connect(m_modelGalleryButton, &QPushButton::clicked, this, &CCreateObjectForm::openModelGallery);
    auto* assemblyButton=new QPushButton(QString::fromUtf8("Сборки…"),this);
    m_assemblyButton=assemblyButton;
    assemblyButton->setObjectName("createAssemblyCatalog");
    assemblyButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    ui->verticalLayout_2->insertWidget(6,assemblyButton);
    connect(assemblyButton,&QPushButton::clicked,this,[this]{if(m_pView){hide();m_pView->openAssemblyCatalog();}});
    // The technical entry points follow object type, with texture controls below.
    ui->verticalLayout_2->removeItem(ui->horizontalLayout_2);
    ui->verticalLayout_2->insertLayout(6,ui->horizontalLayout_2);
    ui->verticalLayout_2->removeWidget(m_modelGalleryButton);
    ui->verticalLayout_2->insertWidget(7,m_modelGalleryButton);
    ui->verticalLayout_2->removeWidget(assemblyButton);
    ui->verticalLayout_2->insertWidget(8,assemblyButton);
    ui->labelObjectType->setText(QString::fromUtf8("Выбор объекта:"));
    for(auto* button:{m_modelGalleryButton,assemblyButton}) {
        QPixmap icon(48,48);icon.fill(Qt::transparent);
        QPainter p(&icon);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#c5deed"),2));p.setBrush(QColor("#477f98"));
        if(button==assemblyButton) {p.drawRect(3,26,16,16);p.drawRect(29,26,16,16);p.drawRect(16,3,16,16);p.drawLine(11,26,24,19);p.drawLine(37,26,24,19);}
        else {for(int y:{4,26})for(int x:{4,26})p.drawRoundedRect(x,y,17,17,2,2);}
        p.end();button->setIcon(QIcon(icon));button->setIconSize(QSize(24,24));
        button->setMinimumHeight(32);button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    }
    connect(m_technicalTexture, QOverload<int>::of(&QComboBox::activated),
            this, [this](int) { applyTechnicalTextureChoice(); });
    connect(m_technicalTexture->lineEdit(), &QLineEdit::editingFinished,
            this, [this] { applyTechnicalTextureChoice(); });

    connect(m_itemMaterial, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this] { selectDatabaseEntry(); });
    connect(m_databaseCategory, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { filterDatabase(); });
    connect(m_databaseSearch, &QLineEdit::textChanged, this, [this] { filterDatabase(); });
    connect(m_databaseList, &QListWidget::currentRowChanged, this, [this] { selectDatabaseEntry(); });
    connect(m_advanced, &QCheckBox::toggled, ui->tableParameters, &QWidget::setVisible);
    // Both panes expand; the divider is a real mouse handle rather than a layout gap.
    ui->creationSplitter->setChildrenCollapsible(false);
    ui->creationSplitter->setStyleSheet("QSplitter::handle { background:palette(mid); } QSplitter::handle:hover { background:palette(highlight); }");
    ui->creationProperties->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    ui->creationSplitter->setStretchFactor(0,1);
    ui->creationSplitter->setStretchFactor(1,1);
    ui->creationProperties->setMinimumWidth(qMax(440,fontMetrics().horizontalAdvance(QString(32,'x'))));
    ui->creationPreview->setMinimumWidth(180);
    ui->creationSplitter->setSizes({460,360});
    ui->creationSplitter->handle(1)->setToolTip(QString::fromUtf8("Потяните, чтобы изменить ширину свойств и предпросмотра"));
    ui->tableParameters->installEventFilter(this);
    ui->tableParameters->viewport()->installEventFilter(this);
    m_parameterColumnWidth=qMax(0,QSettings().value("ui/windows/createObject/parameterWidth",0).toInt());
    connect(ui->tableParameters->horizontalHeader(),&QHeaderView::sectionResized,this,
            [this](int section,int,int width) {
        if(section==0 && m_layoutSeen && !m_updatingTable && !m_adjustingColumns) {
            m_parameterColumnWidth=width;
            QTimer::singleShot(0,this,[this]{adjustPropertyColumns();});
        }
    });
    ei::rememberWindowGeometry(this, QStringLiteral("createObject"));
    resize(860, 600);
}

CCreateObjectForm::~CCreateObjectForm()
{
    saveInnerLayout();
    m_pPreview->attachNode(nullptr, false);
    delete m_pNode;
    //delete m_pPreview; // it will remove as children of Form (inherited behaviour from parent widget)
    delete ui;
}

void CCreateObjectForm::attach(CView *pView, QUndoStack *pStack)
{
     m_pView = pView;
     m_pUndoStack = pStack;
     connect(m_pView, &QObject::destroyed, this, [this] {
         m_pView = nullptr;
     });
}

void CCreateObjectForm::initViewWidget()
{
    m_pPreview = new CPreview(this);
    m_pPreview->setObjectName(QString::fromUtf8("openGLWidget"));

    ui->horizontalLayout_4->addWidget(m_pPreview);
}

void CCreateObjectForm::updateTable()
{
    QScopedValueRollback<bool> updating(m_updatingTable,true);
    m_tableManager->reset();
    if(!m_pNode)
        return;
    QList<QSharedPointer<IPropertyBase>> aProp;
    m_pNode->collectParams(aProp, m_pNode->nodeType());
    //filter parameters
    util::removeProp(aProp, eObjParam_NID);
    util::removeProp(aProp, eObjParam_POSITION);
    util::removeProp(aProp, eObjParam_POSITION_X);
    util::removeProp(aProp, eObjParam_POSITION_Y);
    util::removeProp(aProp, eObjParam_POSITION_Z);
    util::removeProp(aProp, eObjParam_ROTATION);
    util::removeProp(aProp, eObjParam_ROTATION_X);
    util::removeProp(aProp, eObjParam_ROTATION_Y);
    util::removeProp(aProp, eObjParam_ROTATION_Z);
    util::removeProp(aProp, eObjParam_COMPLECTION);

    auto type = m_pNode->nodeType();
    switch (type)
    {
    case ENodeType::eTorch:
    {
        util::removeProp(aProp, eObjParam_PLAYER);
        break;
    }
    case ENodeType::eLight:
    case ENodeType::eSound:
    case ENodeType::eParticle:
    {
        util::removeProp(aProp, eObjParam_PRIM_TXTR);
        break;
    }
    case ENodeType::eMagicTrap:
    {
        util::removeProp(aProp, eObjParam_PRIM_TXTR);
        break;
    }
    default:
        break;
    }

    m_tableManager->setNewData(aProp);
    for(int row=0;row<ui->tableParameters->rowCount();++row)
        if(auto* combo=qobject_cast<CComboDynItem*>(ui->tableParameters->cellWidget(row,1)))
            if(combo->value()->type()==eObjParam_TEMPLATE) combo->setModelNames(m_modelCatalog);
    adjustPropertyColumns();
    refreshTechnicalTextureChoices();
    m_pPreview->updateGL();
}

CPreview *CCreateObjectForm::viewWidget()
{
    return m_pPreview;
}

void CCreateObjectForm::closeEvent(QCloseEvent *event)
{ //ignore close event bcs id does something wrong and closing app has assert when CPreview destroying
    event->ignore();
    hide();
}

void CCreateObjectForm::onObjectChoose(QString& object)
{
    if(m_databaseMode) return;
    // The first combo entry is localized display text, not an object type.
    // Validate against the populated type map instead of comparing its label.
    if(!m_objType.values().contains(object))
        return;

    ENodeType objType = m_objType.key(object);

    m_pPreview->attachNode(nullptr, false);
    if (m_pNode)
        delete m_pNode;

    switch (objType)
    {
    case ENodeType::eUnit:
    {
        m_pNode = new CUnit();
        break;
    }
    case ENodeType::eTorch:
    {
        m_pNode = new CTorch();
        break;
    }
    case ENodeType::eMagicTrap:
    {
        m_pNode = new CMagicTrap();
        break;
    }
    case ENodeType::eLever:
    {
        m_pNode = new CLever();
        break;
    }
    case ENodeType::eLight:
    {
        m_pNode = new CLight();
        break;
    }
    case ENodeType::eSound:
    {
        m_pNode = new CSound();
        break;
    }
    case ENodeType::eParticle:
    {
        m_pNode = new CParticle();
        break;
    }
    case ENodeType::eWorldObject:
    {
        m_pNode = new CWorldObj();
        break;
    }
    default:
    {
        Q_ASSERT("unknown node type" && false);
        break;
    }
    }

    if(nullptr == m_pNode)
    {
        Q_ASSERT("Failed to create new node" && false);
    }
    QList<CNode*> arrNode = m_pView->selectedNodes();
    if(arrNode.size() == 1)
    {
        //copy data from selected node
        auto pSourceNode = arrNode.first();
        QList<QSharedPointer<IPropertyBase>> aProp;
        ENodeType type = pSourceNode->nodeType();
        pSourceNode->collectParams(aProp, type);
        for(const auto& param: aProp)
        {
            const EObjParam& tp = param->type();
            if(tp == eObjParam_POSITION_X
                    || tp == eObjParam_POSITION_Y
                    || tp == eObjParam_POSITION_Z)
                continue;
            if(objType == eMagicTrap && param->type() == eObjParam_NAME)
                continue;
//            switch (param.first) {
//            case eObjParam_ROTATION:
//            case eObjParam_POSITION:
//            {
//                continue;
//            }
//            default:
//                break;
//            }
            m_pNode->applyParam(param);
        }
    }
    m_pPreview->attachNode(m_pNode);
    m_autoPickExactTexture = m_pNode->textureName().trimmed().isEmpty();
    updateTable();
    ui->buttonCreate->setEnabled(m_pNode && m_pView && m_pView->currentMob());
}

void CCreateObjectForm::onParamChange(const QSharedPointer<IPropertyBase> prop)
{
    if(!m_pNode || prop.isNull())
        return;
    const bool modelChanged = prop->type() == eObjParam_TEMPLATE
        && m_pNode->modelName() != prop->toString();
    m_pNode->applyParam(prop);
    if(modelChanged && !m_databaseMode)
    {
        // An old texture belongs to the old model; a new model needs its own
        // observed texture or an explicit fallback choice.
        m_pNode->applyParam(QSharedPointer<IPropertyBase>(
            new propStr(eObjParam_PRIM_TXTR, QString())));
        m_autoPickExactTexture = true;
    }
    else if(prop->type() == eObjParam_PRIM_TXTR)
        m_autoPickExactTexture = false;
    if(modelChanged)
    {
        emit sendNewBbbox(m_pNode->getBBox());
    }
    updateTable();
}

void CCreateObjectForm::onPropertiesChange(
    const QList<QSharedPointer<IPropertyBase>>& properties)
{
    if(!m_pNode || properties.isEmpty())
        return;
    const QString previousModel = m_pNode->modelName();
    bool modelChanged = false;
    bool textureChanged = false;
    for(const QSharedPointer<IPropertyBase>& property : properties)
    {
        if(property.isNull())
            continue;
        m_pNode->applyParam(property);
        modelChanged = modelChanged || (property->type() == eObjParam_TEMPLATE
            && previousModel != property->toString());
        textureChanged = textureChanged || property->type() == eObjParam_PRIM_TXTR;
        if(property->type() == eObjParam_PRIM_TXTR)
            m_autoPickExactTexture = false;
    }
    if(modelChanged)
    {
        if(!textureChanged && !m_databaseMode)
        {
            m_pNode->applyParam(QSharedPointer<IPropertyBase>(
                new propStr(eObjParam_PRIM_TXTR, QString())));
            m_autoPickExactTexture = true;
        }
        emit sendNewBbbox(m_pNode->getBBox());
    }
    updateTable();
}

void CCreateObjectForm::on_buttonCancel_clicked()
{
    hide();
}


void CCreateObjectForm::on_buttonCreate_clicked()
{
    Q_ASSERT(m_pView);
    if(m_databaseMode)
    {
        if(!m_pNode || !m_pView->currentMob()) return;
        if(m_databaseGeneration != ei::ProfileDatabaseContext::instance().generation())
        {
            refreshDatabase();
            return;
        }
        m_pPreview->makeCurrent();
        if(!CTextureList::getInstance()->hasTexture(m_pNode->textureName(), m_pNode->modelName()))
        {
            m_databaseDetails->setText(tr("Choose an available primary texture in Advanced properties before placing."));
            m_advanced->setChecked(true);
            return;
        }
        QString diagnostic;
        if(!m_pView->beginDatabasePlacement(m_pNode, &diagnostic))
        {
            m_databaseDetails->setText(diagnostic);
            return;
        }
        hide();
        m_pView->setFocus();
        m_pView->updateGL();
        return;
    }
    if(!m_pNode)
        return;
    if (nullptr == m_pView->currentMob())
    {
        ei::log(eLogWarning, "cant create pre-defined object without active mob file");
        return;
    }
    auto pMob = m_pView->currentMob();
    const ENodeType type = m_pNode->nodeType();
    m_pPreview->makeCurrent();
    if((type == eWorldObject || type == eUnit || type == eTorch || type == eLever)
        && !m_pNode->modelName().trimmed().isEmpty()
        && !CTextureList::getInstance()->hasTexture(
            m_pNode->textureName(), m_pNode->modelName()))
    {
        QMessageBox::warning(this, tr("Choose a texture"),
            tr("Choose an installed primary texture for this model before creating the object."));
        return;
    }
    CNode* rawTemplate = nullptr;
    const QList<CNode*> selected = m_pView->selectedNodes();
    if(selected.size() == 1 && selected.front()->nodeType() == type &&
       selected.front()->hasRawObjectProvenance())
        rawTemplate = selected.front();
    if(rawTemplate && !pMob->nodes().contains(rawTemplate))
        rawTemplate = nullptr;
    auto pos = QWidget::mapFromGlobal(QCursor::pos());
    auto posOnLand = m_pView->getTerrainPos(pos.x(), pos.y());
    switch (type)
    {
    case eWorldObject:
    case eUnit:
    case eTorch:
    case eMagicTrap:
    case eLever:
    {
        posOnLand.setZ(0.0f);
        break;
    }
    default:
        break;
    }

    QVector3D posBackup(0,0,0);
    m_pNode->updatePos(posOnLand);
    CLandscape::getInstance()->projectPosition(m_pNode);

    QJsonObject nodeData = m_pNode->toJson();
    if(!rawTemplate)
    {
        CValidationReport report;
        if(!CMob::prepareFreshTechnicalNode(m_pNode,&nodeData,&report))
        {
            QMessageBox::warning(this,tr("Не удалось создать объект"),
                tr("Проверьте обязательные поля объекта.\n\n%1").arg(report.toText()));
            return;
        }
    }
    else
    {
        QJsonObject provenance;
        provenance.insert("sourceDocumentSha256",
            QString::fromLatin1(rawTemplate->rawSourceDocumentSha256().toHex()));
        provenance.insert("sourceObjectPath", rawTemplate->rawSourceObjectPath());
        provenance.insert("sourceSubtreeSha256",
            QString::fromLatin1(rawTemplate->rawSourceSubtreeSha256().toHex()));
        nodeData.insert("_Endurance raw provenance", provenance);
    }
    if(type==eWorldObject || type==eLever) {
        QString diagnostic;
        const bool placed=m_pView->beginTechnicalPlacement(nodeData,&diagnostic);
        m_pNode->setPos(posBackup);m_pNode->setDrawPosition(posBackup);
        if(!placed) {QMessageBox::warning(this,tr("Не удалось создать объект"),diagnostic);return;}
        hide();m_pView->setFocus();m_pView->updateGL();return;
    }
    pMob->clearSelect();
    CCreateNodeCommand* pUndo = new CCreateNodeCommand(m_pView, nodeData);
    QObject::connect(pUndo, SIGNAL(addNodeSignal(CNode*)), m_pView->objectTree(), SLOT(addNodeToTree(CNode*)));
    QObject::connect(pUndo, SIGNAL(undo_addNodeSignal(uint)), m_pView->objectTree(), SLOT(onNodeDelete(uint)));
    m_pUndoStack->push(pUndo);
    //restore node position after projection. it is temporary solution for correct drawing object in preview window
    m_pNode->setPos(posBackup);
    m_pNode->setDrawPosition(posBackup);
    if(type == eUnit)
    {
        dynamic_cast<CUnit*>(m_pNode)->resetLogic();
    }

    m_pView->changeOperation(EButtonOpMove);
    hide();
}

void CCreateObjectForm::showEvent(QShowEvent *pEvent)
{
    if(!m_layoutSeen) {
        const auto state=QSettings().value("ui/windows/createObject/splitterState").toByteArray();
        if(!state.isEmpty())ui->creationSplitter->restoreState(state);
        ui->creationSplitter->setChildrenCollapsible(false);
        m_layoutSeen=true;
    }
    if(m_pView && m_pView->isDatabasePlacementPending())
        m_pView->cancelPendingOperation();
    refreshDatabase();
    QString text = ui->comboObjectType->currentText();
    if(m_objType.values().contains(text))
    {
        onObjectChoose(text);
    }
    QWidget::showEvent(pEvent);
    adjustPropertyColumns();
}

void CCreateObjectForm::saveInnerLayout()
{
    if(!m_layoutSeen)return;
    QSettings settings;
    settings.setValue("ui/windows/createObject/splitterState",ui->creationSplitter->saveState());
    settings.setValue("ui/windows/createObject/parameterWidth",m_parameterColumnWidth);
    settings.sync();
}

void CCreateObjectForm::hideEvent(QHideEvent* event)
{
    saveInnerLayout();
    QDialog::hideEvent(event);
}

bool CCreateObjectForm::eventFilter(QObject* watched,QEvent* event)
{
    if((watched==ui->tableParameters || watched==ui->tableParameters->viewport()) && event->type()==QEvent::Resize)
        QTimer::singleShot(0,this,[this]{adjustPropertyColumns();});
    return QDialog::eventFilter(watched,event);
}

void CCreateObjectForm::adjustPropertyColumns()
{
    auto* table=ui->tableParameters;
    const int available=table->viewport()->width();
    if(available<=0)return;
    // Auto-sizing a long parameter label must never consume the value editor.
    const int minimumValue=qMax(140,fontMetrics().horizontalAdvance(QString(16,'x'))+24);
    const int desired=m_parameterColumnWidth>0?m_parameterColumnWidth:available/2;
    QScopedValueRollback<bool> adjusting(m_adjustingColumns,true);
    table->setColumnWidth(0,qBound(80,desired,qMax(80,available-minimumValue)));
    // Stretch-last-section otherwise retains its earlier wide minimum when
    // the splitter/window shrinks and creates a horizontal scrollbar.
    table->setColumnWidth(1,minimumValue);
}

void CCreateObjectForm::refreshDatabase()
{
    QStringList resourceRoots,mapRoots;
    if(m_pView && m_pView->settings()) {
        if(auto* option=dynamic_cast<COptStringList*>(m_pView->settings()->opt(eOptSetResource,"resourceRoots")))resourceRoots=option->value().toList();
        if(auto* option=dynamic_cast<COptStringList*>(m_pView->settings()->opt(eOptSetResource,"mapRoots")))mapRoots=option->value().toList();
    }
    QString token=QString::number(ei::ProfileDatabaseContext::instance().generation())+resourceRoots.join('|')+mapRoots.join('|');
    const auto activeDatabase = ei::ProfileDatabaseContext::instance().current();
    const QString weaponsPath = ei::MapWeaponCatalog::pathForDatabase(activeDatabase ? activeDatabase->sourceArchivePath : QString());
    const QFileInfo weaponsFile(weaponsPath);
    token += QString("|%1:%2:%3").arg(weaponsPath).arg(weaponsFile.size()).arg(weaponsFile.lastModified().toMSecsSinceEpoch());
    for(const auto& root:resourceRoots) {
        const QFileInfo file(QDir(root).filePath("texts.res"));
        token+=QString("|%1:%2").arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch());
    }
    if(token==m_databaseRefreshToken) {
        const auto database=ei::ProfileDatabaseContext::instance().current();
        m_modelCatalog.load(ei::TechnicalModelCatalog::pathForDatabase(database?database->sourceArchivePath:QString()),&m_modelCatalogError);
        filterDatabase();return;
    }
    m_databaseRefreshToken=token;
    const auto snapshot = ei::ProfileDatabaseContext::instance().current();
    m_databaseGeneration = ei::ProfileDatabaseContext::instance().generation();
    m_databaseEntries = snapshot && snapshot->usable()
        ? ei::databaseMapCatalog(*snapshot) : QVector<ei::SDatabaseMapEntry>();
    const auto document = snapshot ? ei::DatabaseDocument::fromSnapshot(snapshot)
                                   : QSharedPointer<ei::DatabaseDocument>();
    m_itemCatalog = document ? ei::ItemSpellConstructorCatalog::fromDocument(*document)
                            : ei::ItemSpellConstructorCatalog();
    const auto texts=ei::loadGameTextCatalog(resourceRoots);
    ei::applyDatabaseMapTexts(&m_databaseEntries,texts.entries);
    m_itemCatalog.applyGameTexts(texts.entries);
    m_weaponCatalog.load(weaponsPath, m_itemCatalog);
    if(m_weaponCatalog.present()) {
        QVector<ei::SDatabaseMapEntry> filtered;
        for(auto entry : m_databaseEntries) {
            if(entry.category == ei::EDatabaseMapCategory::Item) {
                const auto models = m_weaponCatalog.models(entry.name);
                if(models.isEmpty()) continue;
                QStringList names;
                for(const auto& model : models) names.append(model.model);
                entry.model = names.join(" / ");
            }
            filtered.append(entry);
        }
        m_databaseEntries = filtered;
    }
    // Read the existing database-side catalog without importing or saving it.
    m_modelCatalogProfile=ei::TechnicalModelCatalog::profileKey(mapRoots);
    m_modelCatalog.load(ei::TechnicalModelCatalog::pathForDatabase(snapshot?snapshot->sourceArchivePath:QString()),&m_modelCatalogError);
    filterDatabase();
}

void CCreateObjectForm::filterDatabase()
{
    m_databaseMode = m_databaseCategory->currentIndex() != 3;
    setWindowTitle(m_databaseMode ? tr("Add from database") : tr("Create object"));
    ui->labelObjectType->setVisible(!m_databaseMode);
    ui->comboObjectType->setVisible(!m_databaseMode);
    m_databaseSearch->setVisible(m_databaseMode);
    m_databaseList->setVisible(m_databaseMode);
    m_databaseDetails->setVisible(m_databaseMode);
    m_advanced->setVisible(m_databaseMode);
    m_itemMaterial->setVisible(m_databaseMode && m_databaseCategory->currentIndex() == 1);
    m_itemModel->setVisible(m_databaseMode && m_databaseCategory->currentIndex() == 1 && m_weaponCatalog.present());
    const bool technicalMode = !m_databaseMode;
    m_technicalTextureLabel->setVisible(technicalMode);
    m_technicalTexture->setVisible(technicalMode);
    m_technicalTextureStatus->setVisible(technicalMode);
    m_modelGalleryButton->setVisible(technicalMode);
    m_assemblyButton->setVisible(technicalMode);
    if(technicalMode && m_pView) {
        const auto database=ei::ProfileDatabaseContext::instance().current();
        m_modelCatalog.load(ei::TechnicalModelCatalog::pathForDatabase(database?database->sourceArchivePath:QString()),&m_modelCatalogError);
    }
    ui->tableParameters->setVisible(!m_databaseMode || m_advanced->isChecked());
    ui->buttonCreate->setText(m_databaseMode ? tr("Place on map") : tr("Create"));
    if(!m_databaseMode)
    {
        m_pPreview->attachNode(nullptr, false);
        delete m_pNode; m_pNode = nullptr;
        QString type = ui->comboObjectType->currentText();
        onObjectChoose(type);
        ui->buttonCreate->setEnabled(m_pNode != nullptr);
        return;
    }
    const QSignalBlocker block(m_databaseList);
    m_databaseList->clear();
    const auto category = static_cast<ei::EDatabaseMapCategory>(m_databaseCategory->currentIndex());
    const QString query = m_databaseSearch->text().trimmed();
    for(int index = 0; index < m_databaseEntries.size(); ++index)
    {
        const auto& entry = m_databaseEntries.at(index);
        const QString searchable = entry.name + " " + entry.label + " " + entry.model;
        if(entry.category != category || !searchable.contains(query, Qt::CaseInsensitive)) continue;
        const QString code=entry.category==ei::EDatabaseMapCategory::Lever?entry.model:entry.name;
        auto* item = new QListWidgetItem(entry.label == entry.name ? code
            : code + " - " + entry.label, m_databaseList);
        item->setData(Qt::UserRole, index);
        item->setData(Qt::UserRole+1,entry.name);
        item->setToolTip(entry.model + "\n" + (entry.labelFromTexts
            ?QString::fromUtf8("Название из texts.res: ")+entry.label
            :QString::fromUtf8("Название в texts.res не найдено. Показано описание базы, если оно есть."))
            +"\n"+entry.unavailableReason);
    }
    if(m_databaseList->count()) m_databaseList->setCurrentRow(0);
    selectDatabaseEntry();
}

void CCreateObjectForm::refreshTechnicalTextureChoices()
{
    if(!m_technicalTexture || m_databaseMode)
        return;
    const bool supportsTexture = m_pNode &&
        (m_pNode->nodeType() == eWorldObject || m_pNode->nodeType() == eUnit
         || m_pNode->nodeType() == eTorch || m_pNode->nodeType() == eLever);
    m_technicalTextureLabel->setVisible(supportsTexture);
    m_technicalTexture->setVisible(supportsTexture);
    m_technicalTextureStatus->setVisible(supportsTexture);
    m_modelGalleryButton->setVisible(!m_databaseMode);
    m_modelGalleryButton->setEnabled(supportsTexture);
    if(!supportsTexture)
        return;

    const QString model = m_pNode->modelName().trimmed();
    const QString current = m_pNode->textureName().trimmed();
    QStringList available = CTextureList::getInstance()->textureList();
    available.removeDuplicates();
    available.sort(Qt::CaseInsensitive);
    const auto exact = m_modelCatalog.textureChoices(m_modelCatalogProfile, model, available, m_technicalTextures);
    const QString russian = m_modelCatalog.russianName(model);
    m_modelGalleryButton->setText(QString::fromUtf8("Выбрать модель из витрины…"));
    m_modelGalleryButton->setToolTip(russian);
    const QSignalBlocker blocker(m_technicalTexture);
    m_rebuildingTextureChoices = true;
    auto* prepared=new QStandardItemModel;
    auto add=[prepared](const QString& text,const QVariant& value=QVariant()) {
        auto* item=new QStandardItem(text);item->setData(value,Qt::UserRole);prepared->appendRow(item);return item;
    };
    QSet<QString> exactNames;
    if(!exact.isEmpty()) {
        add(tr("Textures bound to this model"))->setEnabled(false);
        for(const auto& choice:exact) {add(choice.name,choice.name);exactNames.insert(choice.name.toLower());}
    }
    add(tr("All installed textures"))->setEnabled(false);
    for(const auto& name:available)if(!exactNames.contains(name.toLower()))add(name,name);
    QPointer<QAbstractItemModel> previous=m_technicalTexture->model();
    m_technicalTexture->setModel(prepared);prepared->setParent(m_technicalTexture);
    if(previous && previous->parent()==m_technicalTexture)delete previous;

    int selection = -1;
    for(int index = 0; index < m_technicalTexture->count(); ++index)
    {
        if(m_technicalTexture->itemData(index).toString().compare(current, Qt::CaseInsensitive) == 0
            && !current.isEmpty())
        {
            selection = index;
            break;
        }
    }
    if(m_autoPickExactTexture && !m_technicalTextures.hasPendingFiles()
        && !exact.isEmpty() &&
        (current.isEmpty() || !exactNames.contains(current.toLower())))
    {
        m_pNode->applyParam(QSharedPointer<IPropertyBase>(
            new propStr(eObjParam_PRIM_TXTR, exact.first().name)));
        selection = 1;
        m_autoPickExactTexture = false;
    }
    m_technicalTexture->setCurrentIndex(selection);
    if(selection < 0)
        m_technicalTexture->setEditText(current);
    m_rebuildingTextureChoices = false;

    QString status;
    if(available.isEmpty())
        status = tr("No textures are available in the active profile. Configure its resources first.");
    else if(model.isEmpty())
        status = tr("Choose a model to see textures used by it.");
    else if(exact.isEmpty())
        status = tr("No installed texture was found paired with this model. Choose from all installed textures.");
    else
        status = tr("%1 texture binding(s) available from the model catalog.").arg(exact.size());
    if(m_technicalTextures.hasPendingFiles())
        status += tr(" Scanning maps: %1 of %2 complete.")
            .arg(m_technicalTextures.sourceFileCount() - m_technicalTextures.pendingFileCount())
            .arg(m_technicalTextures.sourceFileCount());
    if(m_technicalTextures.failedFileCount())
        status += tr(" %1 MOB file(s) could not be read.").arg(m_technicalTextures.failedFileCount());
    if(!m_modelCatalogError.isEmpty()) status += "\n" + m_modelCatalogError;
    m_technicalTextureStatus->setText(status);
}

void CCreateObjectForm::applyTechnicalTextureChoice()
{
    if(m_rebuildingTextureChoices || m_databaseMode || !m_pNode)
        return;
    const QString input = m_technicalTexture->currentText().trimmed();
    QString selected;
    for(int index = 0; index < m_technicalTexture->count(); ++index)
        if(m_technicalTexture->itemData(index).toString().compare(input, Qt::CaseInsensitive) == 0)
        {
            selected = m_technicalTexture->itemData(index).toString();
            break;
        }
    if(selected.isEmpty())
    {
        m_technicalTexture->setEditText(m_pNode->textureName());
        return;
    }
    m_autoPickExactTexture = false;
    if(selected.compare(m_pNode->textureName(), Qt::CaseInsensitive) != 0)
    {
        m_pNode->applyParam(QSharedPointer<IPropertyBase>(
            new propStr(eObjParam_PRIM_TXTR, selected)));
        updateTable();
    }
}

void CCreateObjectForm::openModelGallery()
{
    if(m_databaseMode || !m_pNode || !m_pView) return;
    m_modelCatalogError.clear();
    const auto database = ei::ProfileDatabaseContext::instance().current();
    m_modelCatalog.load(ei::TechnicalModelCatalog::pathForDatabase(database?database->sourceArchivePath:QString()),&m_modelCatalogError);
    const auto generation=ei::ProfileDatabaseContext::instance().generation();
    const QStringList models=CObjectList::getInstance()->figureList();
    const QStringList sources=configuredResourceSourcePaths(m_pView->settings());
    auto provider = CModelGallery::resourceThumbnails(sources);
    QStringList textures = CTextureList::getInstance()->textureList(); textures.sort(Qt::CaseInsensitive);
    CModelGallery gallery(models,&m_modelCatalog,m_modelCatalogProfile,textures,&m_technicalTextures,provider,this,
        m_pNode->modelName(),m_pNode->textureName(),CModelGallery::resourceThumbnailNamespace(sources),CModelConnectionEditor::resourceScenes(sources));
    gallery.enableMultiSelection();
    if(!m_modelCatalogError.isEmpty()) gallery.showCatalogError(m_modelCatalogError);
    if(gallery.exec() == QDialog::Accepted && !gallery.selectedModel().isEmpty())
    {
        if(generation!=ei::ProfileDatabaseContext::instance().generation()) {refreshDatabase();return;}
        const auto selected=gallery.selectedEntries();
        if(selected.size()>1) {
            QString error;
            if(m_pView->beginGalleryPlacement(selected,&error)) {hide();m_pView->setFocus();}
            else QMessageBox::warning(this,tr("Не удалось создать объекты"),error);
            return;
        }
        onPropertiesChange({QSharedPointer<IPropertyBase>(new propStr(eObjParam_TEMPLATE,gallery.selectedModel())),
            QSharedPointer<IPropertyBase>(new propStr(eObjParam_PRIM_TXTR,gallery.selectedTexture()))});
    }
    refreshTechnicalTextureChoices();
}

void CCreateObjectForm::selectDatabaseEntry()
{
    if(!m_databaseMode) return;
    m_pPreview->makeCurrent();
    m_pPreview->attachNode(nullptr, false);
    delete m_pNode; m_pNode = nullptr;
    ui->buttonCreate->setEnabled(false);
    auto* selected = m_databaseList->currentItem();
    if(!selected)
    {
        m_databaseDetails->setText(m_databaseEntries.isEmpty()
            ? tr("No active database. Configure the game or mod profile first.")
            : tr("No matching objects."));
        if(m_databaseCategory->currentIndex() == 1 && m_weaponCatalog.present()
           && !m_weaponCatalog.diagnostic().isEmpty())
            m_databaseDetails->setText(m_weaponCatalog.diagnostic());
        const QSignalBlocker materialBlock(m_itemMaterial), modelBlock(m_itemModel);
        m_itemMaterial->clear(); m_itemModel->clear();
        updateTable(); return;
    }
    auto entry = m_databaseEntries.at(selected->data(Qt::UserRole).toInt());
    if(entry.category == ei::EDatabaseMapCategory::Item)
    {
        QString itemDiagnostic;
        const auto* item = m_itemCatalog.findItem(entry.name);
        m_itemMaterial->setEnabled(item != nullptr);
        if(item)
        {
            const QSignalBlocker block(m_itemMaterial);
            const QString previous = m_itemMaterial->currentData().toString();
            m_itemMaterial->clear();
            int firstAvailable = -1;
            for(const auto& material : m_itemCatalog.compatibleMaterials(*item)) {
                const QString texture = ei::MapWeaponCatalog::textureName(*item, &material, m_itemCatalog.isJmvProfile());
                const bool available = !m_weaponCatalog.present()
                    || CTextureList::getInstance()->textureList().contains(texture);
                m_itemMaterial->addItem(QString::fromUtf8("Материал: ") + material.displayName
                    + (available ? QString() : QString::fromUtf8(" (нет текстуры)")), material.name);
                const int row = m_itemMaterial->count() - 1;
                m_itemMaterial->setItemData(row, texture, Qt::ToolTipRole);
                if(available && firstAvailable < 0) firstAvailable = row;
                if(!available) if(auto* choices = qobject_cast<QStandardItemModel*>(m_itemMaterial->model()))
                    choices->item(row)->setEnabled(false);
            }
            const int previousIndex = m_itemMaterial->findData(previous);
            const bool previousAvailable = previousIndex >= 0
                && (m_itemMaterial->model()->flags(m_itemMaterial->model()->index(previousIndex, 0)) & Qt::ItemIsEnabled);
            m_itemMaterial->setCurrentIndex(previousAvailable ? previousIndex : firstAvailable);
            m_itemMaterial->setEnabled(m_itemMaterial->count() > 0);
            const auto* material = m_itemCatalog.findMaterial(m_itemMaterial->currentData().toString());
            if(m_weaponCatalog.present()) {
                const QSignalBlocker modelBlock(m_itemModel);
                const QString previousModel = m_itemModel->currentData().toString();
                m_itemModel->clear();
                for(const auto& model : m_weaponCatalog.models(item->name))
                    m_itemModel->addItem(QString::fromUtf8("Модель: ") + model.model, model.model);
                const int modelIndex = m_itemModel->findData(previousModel);
                if(modelIndex >= 0) m_itemModel->setCurrentIndex(modelIndex);
                m_itemModel->setEnabled(m_itemModel->count() > 1);
                m_pNode = m_weaponCatalog.create(*item, material, m_itemModel->currentData().toString(),
                    m_itemCatalog.isJmvProfile(), &itemDiagnostic);
                if(m_itemMaterial->currentIndex() < 0)
                    itemDiagnostic = QString::fromUtf8("Для материалов этого предмета нет текстур в активном профиле. Добавьте соответствующие текстуры в ресурсы профиля.");
            } else {
                m_pNode = ei::ItemSpellConstructorDialog::buildItemPreview(m_itemCatalog, *item,
                    material, ei::ProfileDatabaseContext::instance().current(), true, &itemDiagnostic);
            }
            m_pPreview->attachNode(m_pNode);
        }
        else
        {
            const QSignalBlocker block(m_itemMaterial);
            m_itemMaterial->clear();
        }
        m_databaseDetails->setText(tr("Place as an ordinary object. You can convert it to a lever and assign an interaction script afterwards."));
        if(m_pNode && m_weaponCatalog.present())
            m_databaseDetails->setText(m_itemCatalog.itemDisplayName(*item,
                m_itemCatalog.findMaterial(m_itemMaterial->currentData().toString()))
                + QString::fromUtf8("\nТекстура: ") + m_pNode->textureName()
                + QString::fromUtf8("\nРазместите объект на карте. Esc — отмена."));
        if(!m_pNode)
            m_databaseDetails->setText(itemDiagnostic.isEmpty()
                ? tr("No model or material texture is available for placing this item.") : itemDiagnostic);
        ui->buttonCreate->setEnabled(m_pNode && m_pView && m_pView->currentMob());
        updateTable();
        return;
    }
    if(!entry.unavailableReason.isEmpty())
    {
        m_databaseDetails->setText(entry.unavailableReason);
        updateTable(); return;
    }
    if(entry.category == ei::EDatabaseMapCategory::Lever)
    {
        const auto bindings=m_modelCatalog.textureChoices(m_modelCatalogProfile,entry.model,
            CTextureList::getInstance()->textureList(),m_technicalTextures);
        if(!bindings.isEmpty())entry.primaryTexture=bindings.first().name;
        // Use only an unambiguous installed texture match; do not borrow a map object.
        QStringList matches;
        for(QString texture : CTextureList::getInstance()->textureList())
        {
            if(texture.endsWith(".mmp", Qt::CaseInsensitive)) texture.chop(4);
            if(texture.compare(entry.model, Qt::CaseInsensitive) == 0)
            { matches = QStringList{texture}; break; }
            if(texture.startsWith(entry.model, Qt::CaseInsensitive)) matches.append(texture);
        }
        matches.removeDuplicates();
        if(entry.primaryTexture.isEmpty() && matches.size() == 1) entry.primaryTexture = matches.first();
    }
    bool hasModel = false;
    for(QString model : CObjectList::getInstance()->figureList())
    {
        if(model.endsWith(".mod", Qt::CaseInsensitive) || model.endsWith(".fig", Qt::CaseInsensitive))
            model.chop(4);
        if(model.compare(entry.model, Qt::CaseInsensitive) == 0) { hasModel = true; break; }
    }
    if(!hasModel)
    {
        m_databaseDetails->setText(tr("Model %1 is not available in the active profile.").arg(entry.model));
        updateTable(); return;
    }
    QJsonObject defaults;
    if(entry.category == ei::EDatabaseMapCategory::Creature) { CUnit unit; defaults = unit.toJson(); }
    else { CLever lever; defaults = lever.toJson(); }
    QJsonObject data;
    if(!ei::applyDatabaseMapEntry(entry, defaults, &data)) { updateTable(); return; }
    m_pNode = entry.category == ei::EDatabaseMapCategory::Creature
        ? static_cast<CNode*>(new CUnit(data)) : static_cast<CNode*>(new CLever(data));
    m_pNode->loadFigure();
    m_pNode->loadTexture();
    m_pPreview->attachNode(m_pNode);
    m_databaseDetails->setText(tr("%1\nModel: %2\nClick Place on map, then click a position. Esc cancels.")
        .arg(entry.name, entry.model));
    if(entry.primaryTexture.isEmpty())
        m_databaseDetails->setText(tr("Model: %1\nChoose a primary texture in Advanced properties before placing.").arg(entry.model));
    ui->buttonCreate->setEnabled(m_pView && m_pView->currentMob());
    updateTable();
}

