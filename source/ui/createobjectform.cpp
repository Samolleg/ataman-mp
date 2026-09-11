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
#include "endurance_document.h"
#include "database_document.h"
#include "item_spell_constructor_dialog.h"

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
                                  tr("Technical object (from selected template)")});
    m_itemMaterial = new QComboBox(this);
    m_itemMaterial->setObjectName("databaseItemMaterial");
    m_itemMaterial->setToolTip(tr("Object material"));
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
    connect(m_itemMaterial, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this] { selectDatabaseEntry(); });
    connect(m_databaseCategory, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { filterDatabase(); });
    connect(m_databaseSearch, &QLineEdit::textChanged, this, [this] { filterDatabase(); });
    connect(m_databaseList, &QListWidget::currentRowChanged, this, [this] { selectDatabaseEntry(); });
    connect(m_advanced, &QCheckBox::toggled, ui->tableParameters, &QWidget::setVisible);
    resize(860, 600);
}

CCreateObjectForm::~CCreateObjectForm()
{
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
    updateTable();
    ui->buttonCreate->setEnabled(m_pNode && m_pView && m_pView->currentMob());
}

void CCreateObjectForm::onParamChange(const QSharedPointer<IPropertyBase> prop)
{
    if(!m_pNode || prop.isNull())
        return;
    m_pNode->applyParam(prop);
    if(prop->type() == eObjParam_TEMPLATE)
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
    bool modelChanged = false;
    for(const QSharedPointer<IPropertyBase>& property : properties)
    {
        if(property.isNull())
            continue;
        m_pNode->applyParam(property);
        modelChanged = modelChanged || property->type() == eObjParam_TEMPLATE;
    }
    if(modelChanged)
        emit sendNewBbbox(m_pNode->getBBox());
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
    CNode* rawTemplate = nullptr;
    const QList<CNode*> selected = m_pView->selectedNodes();
    if(selected.size() == 1 && selected.front()->nodeType() == type &&
       selected.front()->hasRawObjectProvenance())
        rawTemplate = selected.front();
    if(!rawTemplate)
    {
        QMessageBox::information(this, tr("Select a creation template"),
            tr("To preserve complete MOB data, select one existing object of the same type "
            "before opening Create Object. Endurance will use its complete raw record as "
            "the template and apply the values entered here. You can also copy and paste "
            "an existing object."));
        return;
    }
    pMob->clearSelect();
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
    QJsonObject provenance;
    provenance.insert("sourceDocumentSha256",
        QString::fromLatin1(rawTemplate->rawSourceDocumentSha256().toHex()));
    provenance.insert("sourceObjectPath", rawTemplate->rawSourceObjectPath());
    provenance.insert("sourceSubtreeSha256",
        QString::fromLatin1(rawTemplate->rawSourceSubtreeSha256().toHex()));
    nodeData.insert("_Endurance raw provenance", provenance);
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
    if(m_pView && m_pView->isDatabasePlacementPending())
        m_pView->cancelPendingOperation();
    refreshDatabase();
    QString text = ui->comboObjectType->currentText();
    if(m_objType.values().contains(text))
    {
        onObjectChoose(text);
    }
    QWidget::showEvent(pEvent);
}

void CCreateObjectForm::refreshDatabase()
{
    const auto snapshot = ei::ProfileDatabaseContext::instance().current();
    m_databaseGeneration = ei::ProfileDatabaseContext::instance().generation();
    m_databaseEntries = snapshot && snapshot->usable()
        ? ei::databaseMapCatalog(*snapshot) : QVector<ei::SDatabaseMapEntry>();
    const auto document = snapshot ? ei::DatabaseDocument::fromSnapshot(snapshot)
                                   : QSharedPointer<ei::DatabaseDocument>();
    m_itemCatalog = document ? ei::ItemSpellConstructorCatalog::fromDocument(*document)
                            : ei::ItemSpellConstructorCatalog();
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
        auto* item = new QListWidgetItem(entry.label == entry.name ? entry.name
            : entry.label + " (" + entry.name + ")", m_databaseList);
        item->setData(Qt::UserRole, index);
        item->setToolTip(entry.model + "\n" + entry.unavailableReason);
    }
    if(m_databaseList->count()) m_databaseList->setCurrentRow(0);
    selectDatabaseEntry();
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
        updateTable(); return;
    }
    auto entry = m_databaseEntries.at(selected->data(Qt::UserRole).toInt());
    if(entry.category == ei::EDatabaseMapCategory::Item)
    {
        const auto* item = m_itemCatalog.findItem(entry.name);
        m_itemMaterial->setEnabled(item != nullptr);
        if(item)
        {
            const QSignalBlocker block(m_itemMaterial);
            const QString previous = m_itemMaterial->currentData().toString();
            m_itemMaterial->clear();
            for(const auto& material : m_itemCatalog.compatibleMaterials(*item))
                m_itemMaterial->addItem(material.displayName, material.name);
            const int previousIndex = m_itemMaterial->findData(previous);
            if(previousIndex >= 0) m_itemMaterial->setCurrentIndex(previousIndex);
            m_itemMaterial->setEnabled(m_itemMaterial->count() > 0);
            m_pNode = ei::ItemSpellConstructorDialog::buildItemPreview(m_itemCatalog, *item,
                m_itemCatalog.findMaterial(m_itemMaterial->currentData().toString()),
                ei::ProfileDatabaseContext::instance().current(), true);
            m_pPreview->attachNode(m_pNode);
        }
        else
        {
            const QSignalBlocker block(m_itemMaterial);
            m_itemMaterial->clear();
        }
        m_databaseDetails->setText(tr("Place as an ordinary object. You can convert it to a lever and assign an interaction script afterwards."));
        if(!m_pNode)
            m_databaseDetails->setText(tr("No model or material texture is available for placing this item."));
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
        if(matches.size() == 1) entry.primaryTexture = matches.first();
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

