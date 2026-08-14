#include "item_spell_constructor_dialog.h"

#include "database_document.h"
#include "game_text_catalog.h"
#include "objects/worldobj.h"
#include "objects/unit.h"
#include "property.h"
#include "preview.h"
#include "view.h"
#include "resourcemanager.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QSet>
#include <QPixmap>
#include <QTextCursor>
#include <QPainter>
#include <algorithm>

namespace {
QString searchable(const QStringList& values) { return values.join(QLatin1Char(' ')).toCaseFolded(); }
QString runeTypeName(int type)
{
    static const QStringList names = {"Range", "Targets", "Area", "Effect", "Duration",
                                      "Energy", "Filter", "Item"};
    return type >= 0 && type < names.size() ? names.at(type) : QStringLiteral("Type %1").arg(type);
}
QString weaponOrArmorLayer(const ei::SConstructorItem& item)
{
    const QString type = item.type.trimmed().toCaseFolded();
    if(item.category != QStringLiteral("Armor")) return QString();
    return type == "helm" || type == "plate" || type == "leggings"
        ? QStringLiteral("Upper / heavy") : QStringLiteral("Lower / light");
}
QListWidgetItem* catalogItem(const QString& title, const QString& subtitle,
                             const QString& key, QListWidget* list)
{
    auto* item = new QListWidgetItem(title + QStringLiteral("\n") + subtitle, list);
    item->setData(Qt::UserRole, key);
    return item;
}
}

namespace ei {
ItemSpellConstructorDialog::ItemSpellConstructorDialog(
    const QSharedPointer<DatabaseDocument>& document, const QStringList& resourceRoots,
    CView* view, QWidget* parent)
    : QDialog(nullptr, Qt::Window | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint |
                      Qt::WindowCloseButtonHint),
      m_catalog(document ? ItemSpellConstructorCatalog::fromDocument(*document)
                         : ItemSpellConstructorCatalog()),
      m_view(view)
{
    Q_UNUSED(parent);
    setObjectName(QStringLiteral("itemSpellConstructorDialog"));
    setWindowTitle(tr("Item / Spell Constructor"));
    setWindowIcon(QApplication::windowIcon());
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1100, 720);
    setMinimumSize(780, 520);
    const SGameTextCatalog texts = loadGameTextCatalog(resourceRoots);
    m_catalog.applyGameTexts(texts.entries);

    auto* root = new QVBoxLayout(this);
    auto* codeRow = new QHBoxLayout;
    codeRow->addWidget(new QLabel(tr("Item or spell code:"), this));
    m_codeEdit = new QLineEdit(this);
    m_codeEdit->setObjectName(QStringLiteral("constructorCodeEdit"));
    m_codeEdit->setPlaceholderText(tr("Paste an item code such as armor.material[spell{rune;rune}]"));
    codeRow->addWidget(m_codeEdit, 1);
    auto* import = new QPushButton(tr("Import"), this);
    auto* copy = new QPushButton(tr("Copy"), this);
    codeRow->addWidget(import); codeRow->addWidget(copy);
    root->addLayout(codeRow);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("constructorTabs"));
    root->addWidget(m_tabs, 1);

    QWidget* itemTab = new QWidget(m_tabs);
    auto* itemRoot = new QHBoxLayout(itemTab);
    auto* itemLeft = new QVBoxLayout;
    auto* itemFilters = new QHBoxLayout;
    m_itemFilter = new QLineEdit(itemTab); m_itemFilter->setPlaceholderText(tr("Filter name or code…"));
    m_itemCategory = new QComboBox(itemTab);m_itemCategory->setObjectName(QStringLiteral("constructorItemCategory")); m_itemCategory->addItem(tr("All categories"), QString());
    m_itemCategory->addItem(tr("Weapons"), "Weapon"); m_itemCategory->addItem(tr("Armor"), "Armor");
    m_itemCategory->addItem(tr("Quick items"), "Quick Item");
    m_itemType = new QComboBox(itemTab); m_itemType->setObjectName(QStringLiteral("constructorItemType"));m_itemType->addItem(tr("All item types"), QString());
    m_armorLayer = new QComboBox(itemTab);m_armorLayer->setObjectName(QStringLiteral("constructorArmorLayer")); m_armorLayer->addItem(tr("All armor layers"), QString());
    m_armorLayer->addItem(tr("Upper / heavy armor"), "Upper / heavy");
    m_armorLayer->addItem(tr("Lower / light armor"), "Lower / light");
    itemFilters->addWidget(m_itemFilter, 1); itemFilters->addWidget(m_itemCategory);
    itemFilters->addWidget(m_itemType); itemFilters->addWidget(m_armorLayer);
    itemLeft->addLayout(itemFilters);
    m_itemList = new QListWidget(itemTab); m_itemList->setObjectName(QStringLiteral("constructorItemList"));
    itemLeft->addWidget(m_itemList, 1);
    itemRoot->addLayout(itemLeft, 3);

    auto* itemRight = new QVBoxLayout;
    auto* itemForm = new QFormLayout;
    m_material = new QComboBox(itemTab);
    m_itemSpell = new QLineEdit(itemTab); m_itemSpell->setPlaceholderText(tr("Optional spell code"));
    auto* useSpell = new QPushButton(tr("Use spell from Spell tab"), itemTab);
    auto* spellRow = new QHBoxLayout; spellRow->addWidget(m_itemSpell, 1); spellRow->addWidget(useSpell);
    itemForm->addRow(tr("Compatible material:"), m_material);
    itemForm->addRow(tr("Inserted spell:"), spellRow);
    itemRight->addLayout(itemForm);
    m_itemDetails = new QLabel(itemTab); m_itemDetails->setWordWrap(true); m_itemDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    itemRight->addWidget(m_itemDetails);
    m_itemIcon = new QLabel(itemTab); m_itemIcon->setAlignment(Qt::AlignCenter); m_itemIcon->setMinimumSize(220, 150);
    m_itemIcon->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    itemRight->addWidget(m_itemIcon, 1);
    if(m_view)
    {
        m_previewObject = new CWorldObj;
        m_previewUnit = new CUnit;
        // The constructor is a separate top-level window, so explicitly share
        // the main viewport's figures, textures, and shader resources.
        m_itemPreview = new CPreview(itemTab, m_view); m_itemPreview->attachView(m_view);
        m_itemPreview->setMinimumHeight(220); itemRight->addWidget(m_itemPreview, 2);
    }
    itemRoot->addLayout(itemRight, 2);
    m_tabs->addTab(itemTab, tr("Items & Quick Items"));

    QWidget* spellTab = new QWidget(m_tabs);
    auto* spellRoot = new QHBoxLayout(spellTab);
    auto* spellLeft = new QVBoxLayout;
    auto* spellFilters = new QHBoxLayout;
    m_spellFilter = new QLineEdit(spellTab); m_spellFilter->setPlaceholderText(tr("Filter spell name or code…"));
    m_spellSchool = new QComboBox(spellTab); m_spellSchool->setObjectName(QStringLiteral("constructorSpellSchool"));m_spellSchool->addItem(tr("All magic schools"), QString());
    QSet<QString> schools; for(const auto& spell : m_catalog.spells) schools.insert(spell.school);
    QStringList sortedSchools = schools.values();sortedSchools.sort(Qt::CaseInsensitive);
    for(const QString& school : sortedSchools) m_spellSchool->addItem(school, school);
    spellFilters->addWidget(m_spellFilter, 1); spellFilters->addWidget(m_spellSchool);
    spellLeft->addLayout(spellFilters);
    m_spellList = new QListWidget(spellTab); m_spellList->setObjectName(QStringLiteral("constructorSpellList"));
    spellLeft->addWidget(m_spellList, 1);
    spellRoot->addLayout(spellLeft, 2);

    auto* runeColumn = new QVBoxLayout;
    runeColumn->addWidget(new QLabel(tr("Available runes"), spellTab));
    auto* runeFilters = new QHBoxLayout;
    m_runeFilter = new QLineEdit(spellTab); m_runeFilter->setPlaceholderText(tr("Filter rune name or code…"));
    m_runeType = new QComboBox(spellTab); m_runeType->addItem(tr("All rune types"), -999);
    for(int type=0; type<8; ++type) m_runeType->addItem(runeTypeName(type), type);
    runeFilters->addWidget(m_runeFilter, 1); runeFilters->addWidget(m_runeType); runeColumn->addLayout(runeFilters);
    m_availableRunes = new QListWidget(spellTab); runeColumn->addWidget(m_availableRunes, 1);
    auto* runeButtons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add →"), spellTab); auto* remove = new QPushButton(tr("Remove"), spellTab);
    auto* up = new QPushButton(tr("↑"), spellTab); auto* down = new QPushButton(tr("↓"), spellTab);
    runeButtons->addWidget(add); runeButtons->addWidget(remove); runeButtons->addWidget(up); runeButtons->addWidget(down);
    runeColumn->addLayout(runeButtons);
    runeColumn->addWidget(new QLabel(tr("Runes in spell (maximum 8)"), spellTab));
    m_selectedRunes = new QListWidget(spellTab); m_selectedRunes->setObjectName(QStringLiteral("constructorSelectedRunes"));
    runeColumn->addWidget(m_selectedRunes, 1);
    spellRoot->addLayout(runeColumn, 2);

    auto* spellRight = new QVBoxLayout;
    m_spellIcon = new QLabel(spellTab); m_spellIcon->setAlignment(Qt::AlignCenter);
    m_spellIcon->setMinimumSize(240, 240); m_spellIcon->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    spellRight->addWidget(m_spellIcon, 1);
    m_spellDetails = new QLabel(spellTab); m_spellDetails->setWordWrap(true); m_spellDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    spellRight->addWidget(m_spellDetails);
    spellRoot->addLayout(spellRight, 2);
    m_tabs->addTab(spellTab, tr("Spells"));

    m_validation = new QLabel(this); m_validation->setWordWrap(true); m_validation->setObjectName(QStringLiteral("constructorValidation"));
    root->addWidget(m_validation);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Copy code and close"));
    root->addWidget(buttons);

    connect(m_itemFilter,&QLineEdit::textChanged,this,&ItemSpellConstructorDialog::refreshItemFilter);
    connect(m_itemCategory,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](){refreshItemFilterChoices();refreshItemFilter();});
    connect(m_itemType,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](){refreshArmorLayerAvailability();refreshItemFilter();});
    connect(m_armorLayer,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&ItemSpellConstructorDialog::refreshItemFilter);
    connect(m_itemList,&QListWidget::currentRowChanged,this,&ItemSpellConstructorDialog::selectItem);
    connect(m_material,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&ItemSpellConstructorDialog::selectMaterial);
    connect(m_itemSpell,&QLineEdit::textChanged,this,&ItemSpellConstructorDialog::updateOutput);
    connect(useSpell,&QPushButton::clicked,this,&ItemSpellConstructorDialog::useSpellInItem);
    connect(m_spellFilter,&QLineEdit::textChanged,this,&ItemSpellConstructorDialog::refreshSpellFilter);
    connect(m_spellSchool,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&ItemSpellConstructorDialog::refreshSpellFilter);
    connect(m_runeFilter,&QLineEdit::textChanged,this,&ItemSpellConstructorDialog::refreshSpellFilter);
    connect(m_runeType,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&ItemSpellConstructorDialog::refreshSpellFilter);
    connect(m_spellList,&QListWidget::currentRowChanged,this,&ItemSpellConstructorDialog::selectSpell);
    connect(add,&QPushButton::clicked,this,&ItemSpellConstructorDialog::addRune);
    connect(m_availableRunes,&QListWidget::itemDoubleClicked,this,
            [this](QListWidgetItem*){ addRune(); });
    connect(remove,&QPushButton::clicked,this,&ItemSpellConstructorDialog::removeRune);
    connect(m_selectedRunes,&QListWidget::itemDoubleClicked,this,
            [this](QListWidgetItem*){ removeRune(); });
    connect(up,&QPushButton::clicked,this,&ItemSpellConstructorDialog::moveRuneUp);
    connect(down,&QPushButton::clicked,this,&ItemSpellConstructorDialog::moveRuneDown);
    connect(import,&QPushButton::clicked,this,&ItemSpellConstructorDialog::importCode);
    connect(copy,&QPushButton::clicked,this,&ItemSpellConstructorDialog::copyCode);
    connect(m_tabs,&QTabWidget::currentChanged,this,&ItemSpellConstructorDialog::updateOutput);
    connect(buttons,&QDialogButtonBox::accepted,this,[this](){ copyCode(); accept(); });
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    refreshItemFilterChoices();refreshItemFilter(); refreshSpellFilter();
}

ItemSpellConstructorDialog::~ItemSpellConstructorDialog() { delete m_previewObject;delete m_previewUnit; }
QString ItemSpellConstructorDialog::code() const { return m_codeEdit->text().trimmed(); }
void ItemSpellConstructorDialog::setCode(const QString& value) { m_codeEdit->setText(value); importCode(); }

const SConstructorItem* ItemSpellConstructorDialog::currentItem() const
{ return m_itemList->currentItem() ? m_catalog.findItem(m_itemList->currentItem()->data(Qt::UserRole).toString()) : nullptr; }
const SConstructorMaterial* ItemSpellConstructorDialog::currentMaterial() const
{ return m_catalog.findMaterial(m_material->currentData().toString()); }
const SConstructorSpell* ItemSpellConstructorDialog::currentSpell() const
{ return m_spellList->currentItem() ? m_catalog.findSpell(m_spellList->currentItem()->data(Qt::UserRole).toString()) : nullptr; }
QStringList ItemSpellConstructorDialog::selectedRuneCodes() const
{ QStringList out; for(int i=0;i<m_selectedRunes->count();++i) out<<m_selectedRunes->item(i)->data(Qt::UserRole).toString(); return out; }

void ItemSpellConstructorDialog::refreshItemFilterChoices()
{
    const QString category=m_itemCategory->currentData().toString();
    const QString previousType=m_itemType->currentData().toString();
    QSet<QString> types;
    for(const auto& item:m_catalog.items)
        if(category.isEmpty()||item.category==category)types.insert(item.type);
    QStringList sorted=types.values();sorted.sort(Qt::CaseInsensitive);
    m_itemType->blockSignals(true);m_itemType->clear();m_itemType->addItem(tr("All item types"),QString());
    for(const QString& type:sorted)m_itemType->addItem(type,type);
    const int restored=m_itemType->findData(previousType);if(restored>=0)m_itemType->setCurrentIndex(restored);
    m_itemType->blockSignals(false);
    refreshArmorLayerAvailability();
}

void ItemSpellConstructorDialog::refreshArmorLayerAvailability()
{
    const QString category=m_itemCategory->currentData().toString();
    const QString type=m_itemType->currentData().toString();
    bool layerRelevant=category.isEmpty()||category==QStringLiteral("Armor");
    if(category.isEmpty()&&!type.isEmpty())
    {
        layerRelevant=false;
        for(const auto& item:m_catalog.items)
            if(item.category==QStringLiteral("Armor")
               &&item.type.compare(type,Qt::CaseInsensitive)==0)
            {layerRelevant=true;break;}
    }
    if(!layerRelevant)m_armorLayer->setCurrentIndex(0);
    m_armorLayer->setEnabled(layerRelevant);
    m_armorLayer->setToolTip(layerRelevant?QString():tr("Armor layers do not apply to this item category."));
}

void ItemSpellConstructorDialog::refreshItemFilter()
{
    const QString previous = m_itemList->currentItem() ? m_itemList->currentItem()->data(Qt::UserRole).toString() : QString();
    m_itemList->clear(); const QString query=m_itemFilter->text().trimmed().toCaseFolded();
    const QString category=m_itemCategory->currentData().toString(), type=m_itemType->currentData().toString(), layer=m_armorLayer->currentData().toString();
    for(const auto& item:m_catalog.items)
    {
        if(!category.isEmpty()&&item.category!=category)continue;
        if(!type.isEmpty()&&item.type.compare(type,Qt::CaseInsensitive)!=0)continue;
        if(!layer.isEmpty()&&weaponOrArmorLayer(item)!=layer)continue;
        if(!query.isEmpty()&&!searchable({item.name,item.displayName,item.category,item.type,item.materialType}).contains(query))continue;
        auto* row=catalogItem(item.displayName, tr("%1 • code: %2 • type: %3").arg(item.category,item.name,item.type),item.name,m_itemList);
        if(item.name.compare(previous,Qt::CaseInsensitive)==0)m_itemList->setCurrentItem(row);
    }
    if(m_itemList->currentRow()<0&&m_itemList->count())m_itemList->setCurrentRow(0);
}

void ItemSpellConstructorDialog::refreshSpellFilter()
{
    const QString previous=m_spellList->currentItem()?m_spellList->currentItem()->data(Qt::UserRole).toString():QString();
    m_spellList->clear();const QString query=m_spellFilter->text().trimmed().toCaseFolded();const QString school=m_spellSchool->currentData().toString();
    for(const auto& spell:m_catalog.spells){if(!school.isEmpty()&&spell.school.compare(school,Qt::CaseInsensitive)!=0)continue;if(!query.isEmpty()&&!searchable({spell.name,spell.displayName,spell.code,spell.subtype,spell.school}).contains(query))continue;auto*row=catalogItem(spell.displayName,tr("code: %1 • school: %2 • subtype: %3").arg(spell.code,spell.school,spell.subtype),spell.code,m_spellList);if(spell.code.compare(previous,Qt::CaseInsensitive)==0)m_spellList->setCurrentItem(row);}
    if(m_spellList->currentRow()<0&&m_spellList->count())m_spellList->setCurrentRow(0);
    m_availableRunes->clear();const QString runeQuery=m_runeFilter->text().trimmed().toCaseFolded();const int type=m_runeType->currentData().toInt();
    for(const auto&rune:m_catalog.runes){if(type!=-999&&rune.type!=type)continue;if(!runeQuery.isEmpty()&&!searchable({rune.name,rune.displayName,rune.code,runeTypeName(rune.type)}).contains(runeQuery))continue;catalogItem(rune.displayName,tr("%1 • %2").arg(rune.code,runeTypeName(rune.type)),rune.code,m_availableRunes);}
    if(m_availableRunes->count())m_availableRunes->setCurrentRow(0); updateOutput();
}

void ItemSpellConstructorDialog::rebuildMaterialChoices(const QString& selected)
{
    m_material->blockSignals(true);m_material->clear();
    if(const auto*item=currentItem())
    {
        const bool materialRequired=!item->materialType.trimmed().isEmpty()
            && item->materialType.compare(QStringLiteral("None"),Qt::CaseInsensitive)!=0;
        const auto materials=m_catalog.compatibleMaterials(*item);
        if(!materialRequired)m_material->addItem(tr("No material"),QString());
        for(const auto&material:materials)
        {
            const bool authoredException=material.type.compare(
                item->materialType,Qt::CaseInsensitive)!=0;
            m_material->addItem(authoredException
                ? tr("%1 (%2, authored compatibility)").arg(material.displayName,material.name)
                : tr("%1 (%2, code %3)").arg(material.displayName,material.name,material.code),
                material.name);
            if(material.name.compare(selected,Qt::CaseInsensitive)==0)
                m_material->setCurrentIndex(m_material->count()-1);
        }
        if(materialRequired&&materials.isEmpty())
        {
            m_material->addItem(tr("No supported %1 material exists").arg(item->materialType),QString());
            const QModelIndex index=m_material->model()->index(0,0);
            m_material->model()->setData(index,0,Qt::UserRole-1);
        }
    }
    m_material->blockSignals(false);
}
void ItemSpellConstructorDialog::selectItem(){rebuildMaterialChoices();const auto*item=currentItem();m_itemSpell->setText(item?item->builtInSpell:QString());updateItemPreview();updateOutput();}
void ItemSpellConstructorDialog::selectMaterial(){updateItemPreview();updateOutput();}
void ItemSpellConstructorDialog::selectSpell(){updateSpellPreview();updateOutput();}

void ItemSpellConstructorDialog::addRune(){if(!m_availableRunes->currentItem()||m_selectedRunes->count()>=8)return;const QString code=m_availableRunes->currentItem()->data(Qt::UserRole).toString();if(const auto*rune=m_catalog.findRune(code))catalogItem(rune->displayName,tr("%1 • %2").arg(rune->code,runeTypeName(rune->type)),rune->code,m_selectedRunes);updateSpellPreview();updateOutput();}
void ItemSpellConstructorDialog::removeRune(){delete m_selectedRunes->takeItem(m_selectedRunes->currentRow());updateSpellPreview();updateOutput();}
void ItemSpellConstructorDialog::moveRuneUp(){const int row=m_selectedRunes->currentRow();if(row<=0)return;auto*item=m_selectedRunes->takeItem(row);m_selectedRunes->insertItem(row-1,item);m_selectedRunes->setCurrentRow(row-1);updateOutput();}
void ItemSpellConstructorDialog::moveRuneDown(){const int row=m_selectedRunes->currentRow();if(row<0||row+1>=m_selectedRunes->count())return;auto*item=m_selectedRunes->takeItem(row);m_selectedRunes->insertItem(row+1,item);m_selectedRunes->setCurrentRow(row+1);updateOutput();}
void ItemSpellConstructorDialog::useSpellInItem(){if(const auto*spell=currentSpell()){m_itemSpell->setText(ItemSpellConstructorCatalog::formatSpellCode(spell->code,selectedRuneCodes()));m_tabs->setCurrentIndex(0);}}

void ItemSpellConstructorDialog::updateItemPreview()
{
    const auto* item=currentItem();
    if(!item){m_itemIcon->clear();return;}
    const QString figure=ItemSpellConstructorCatalog::itemFigureName(*item);
    const QString texture=ItemSpellConstructorCatalog::itemTextureName(*item,currentMaterial());
    const QImage image=ItemSpellConstructorCatalog::orientPreviewImage(
        CTextureList::getInstance()->textureImage(texture,figure));
    if(image.isNull())
    {
        m_itemIcon->setPixmap(QPixmap());
        m_itemIcon->setText(tr("No material preview texture for %1")
                            .arg(texture.isEmpty()?item->name:texture));
    }
    else
    {
        m_itemIcon->setText(QString());
        m_itemIcon->setPixmap(QPixmap::fromImage(image).scaled(
            m_itemIcon->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
    }
    if(m_itemPreview&&!figure.isEmpty()&&!texture.isEmpty())
    {
        if((item->category==QStringLiteral("Armor")||item->category==QStringLiteral("Weapon"))
           &&m_previewUnit)
        {
            const QString itemCode=ItemSpellConstructorCatalog::formatItemCode(
                item->name,currentMaterial()?currentMaterial()->name:QString(),m_itemSpell->text());
            m_previewUnit->applyParam(QSharedPointer<IPropertyBase>(new propStr(
                eObjParam_TEMPLATE,m_catalog.previewModelName())));
            m_previewUnit->applyParam(QSharedPointer<IPropertyBase>(new propStr(
                eObjParam_PRIM_TXTR,m_catalog.previewTextureName())));
            m_previewUnit->applyParam(QSharedPointer<IPropertyBase>(new propBool(
                eObjParam_UNIT_NEED_IMPORT,true)));
            m_previewUnit->applyParam(QSharedPointer<IPropertyBase>(new propStrAr(
                eObjParam_UNIT_ARMORS,item->category==QStringLiteral("Armor")
                    ?QStringList{itemCode}:QStringList())));
            m_previewUnit->applyParam(QSharedPointer<IPropertyBase>(new propStrAr(
                eObjParam_UNIT_WEAPONS,item->category==QStringLiteral("Weapon")
                    ?QStringList{itemCode}:QStringList())));
            m_itemPreview->makeCurrent();m_previewUnit->preloadVisualResources();
            m_itemPreview->attachNode(m_previewUnit);
        }
        else if(m_previewObject)
        {
            m_previewObject->applyParam(QSharedPointer<IPropertyBase>(new propStr(eObjParam_TEMPLATE,figure)));
            m_previewObject->applyParam(QSharedPointer<IPropertyBase>(new propStr(eObjParam_PRIM_TXTR,texture)));
            m_itemPreview->attachNode(m_previewObject);
        }
        m_itemPreview->updateGL();
    }
}

void ItemSpellConstructorDialog::updateSpellPreview()
{
    const auto* spell=currentSpell();
    if(!spell){m_spellIcon->clear();m_spellDetails->clear();return;}
    QString texture;QImage sourceImage;
    for(const QString& candidate:m_catalog.spellTextureCandidates(*spell))
    {
        const QImage candidateImage=CTextureList::getInstance()->textureImage(candidate,QString());
        if(!candidateImage.isNull()){texture=candidate;sourceImage=candidateImage;break;}
    }
    if(texture.isEmpty())texture=m_catalog.spellTextureName(*spell);
    const QImage image=ItemSpellConstructorCatalog::orientPreviewImage(sourceImage);
    if(image.isNull())
    {
        m_spellIcon->setPixmap(QPixmap());
        m_spellIcon->setText(tr("Spell icon %1 was not found in the active profile.").arg(texture));
    }
    else
    {
        QPixmap orientedPreview(240,240);orientedPreview.fill(QColor(24,24,28));
        QPainter painter(&orientedPreview);
        const QPixmap icon=QPixmap::fromImage(image).scaled(
            220,220,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        painter.drawPixmap((orientedPreview.width()-icon.width())/2,4,icon);
        const QStringList runes=selectedRuneCodes();
        const int chipWidth=54,chipHeight=24,gap=4;
        for(int i=0;i<runes.size();++i)
        {
            const int row=i/4,column=i%4;
            const QRect chip(6+column*(chipWidth+gap),184+row*(chipHeight+gap),
                             chipWidth,chipHeight);
            painter.setPen(QPen(QColor(230,200,95),1));
            painter.setBrush(QColor(20,20,24,220));painter.drawRoundedRect(chip,4,4);
            painter.setPen(Qt::white);painter.drawText(chip,Qt::AlignCenter,runes.at(i));
        }
        painter.end();m_spellIcon->setText(QString());m_spellIcon->setPixmap(orientedPreview);
    }
    const auto report=m_catalog.spellReport(*spell,selectedRuneCodes());
    QStringList verifiedLines;
    for(const QString& code:selectedRuneCodes())
        if(const auto* rune=m_catalog.findRune(code))
            verifiedLines<<tr("%1 - %2 (%3)").arg(
                rune->code,rune->displayName,runeTypeName(rune->type));
    m_spellDetails->setText(tr(
        "%1\nCode: %2\nSchool: %3 | %4\n"
        "Energy: %5 | item slots: %6 | difficulty: %7\n"
        "Effect: %8 | duration: %9 s (%10 segments)\n"
        "Range: %11 m | area: %12 m² (radius %13 m) | targets: %14\n\n"
        "Runes:\n%15")
        .arg(spell->displayName,spell->code).arg(spell->school).arg(spell->subtype)
        .arg(report.spellMana,0,'g',8).arg(report.spellSlots).arg(report.spellComplexity)
        .arg(report.effect,0,'g',8).arg(report.durationSegments/15.0,0,'g',8)
        .arg(report.durationSegments).arg(report.range,0,'g',8).arg(report.area,0,'g',8)
        .arg(report.areaRadius,0,'g',8).arg(report.targets).arg(verifiedLines.join('\n')));
}

void ItemSpellConstructorDialog::updateOutput()
{
    if(m_tabs->currentIndex()==1)
    {
        if(const auto* spell=currentSpell())
            m_codeEdit->setText(ItemSpellConstructorCatalog::formatSpellCode(
                spell->code,selectedRuneCodes()));
        m_validation->clear();updateSpellPreview();return;
    }
    const auto* item=currentItem();if(!item)return;
    const QString spellCode=m_itemSpell->text().trimmed();
    m_codeEdit->setText(ItemSpellConstructorCatalog::formatItemCode(
        item->name,m_material->currentData().toString(),spellCode));
    QStringList detail{
        tr("%1 | %2 | database code: %3").arg(
            m_catalog.itemDisplayName(*item,currentMaterial()),item->category,item->name),
        tr("Material family: %1 | base energy: %2 | base spell slots: %3 | crafting material required: %4 components")
            .arg(item->materialType).arg(item->mana,0,'g',8)
            .arg(item->slotCapacity).arg(item->components)};
    if((item->category==QStringLiteral("Weapon")||item->category==QStringLiteral("Armor"))&&currentMaterial())
        detail<<tr("Preview: the item is assembled through the normal unit appearance and equipment path; the image above is its material-colored inventory texture.");
    QStringList warnings;
    const bool materialRequired=!item->materialType.trimmed().isEmpty()
        && item->materialType.compare(QStringLiteral("None"),Qt::CaseInsensitive)!=0;
    if(materialRequired&&!currentMaterial())
        warnings<<tr("This item requires a supported %1 material.").arg(item->materialType);
    const SParsedSpellCode parsed=ItemSpellConstructorCatalog::parseSpellCode(spellCode);
    if(!spellCode.isEmpty())
    {
        warnings+=parsed.diagnostics;
        const auto* spell=parsed.valid?m_catalog.findSpell(parsed.prototypeCode):nullptr;
        if(parsed.valid&&!spell)
            warnings<<tr("Unknown spell prototype code '%1'.").arg(parsed.prototypeCode);
        if(spell)
        {
            const auto report=m_catalog.limits(*item,currentMaterial(),spell,parsed.runeCodes);
            warnings+=report.warnings;
            detail<<tr("Constructed item capacity: energy %1 | spell slots %2")
                        .arg(report.itemMana,0,'g',8).arg(report.itemSlots)
                  <<tr("Inserted spell: energy %1 | slots used %2 | caster difficulty %3")
                        .arg(report.spellMana,0,'g',8).arg(report.spellSlots)
                        .arg(report.spellComplexity);
        }
    }
    m_itemDetails->setText(detail.join('\n'));
    m_validation->setText(warnings.isEmpty()
        ?tr("Code is structurally valid. Compatibility findings are advisory.")
        :tr("Advisory validation:\n• %1").arg(warnings.join("\n• ")));
}

void ItemSpellConstructorDialog::importCode()
{
    const QString value=m_codeEdit->text().trimmed();if(value.contains('{')&&!value.contains('[')){const auto parsed=ItemSpellConstructorCatalog::parseSpellCode(value);if(!parsed.valid){showImportDiagnostic(parsed.diagnostics);return;}const auto*spell=m_catalog.findSpell(parsed.prototypeCode);if(!spell){showImportDiagnostic({tr("Unknown spell prototype '%1'.").arg(parsed.prototypeCode)});return;}m_tabs->setCurrentIndex(1);for(int i=0;i<m_spellList->count();++i)if(m_spellList->item(i)->data(Qt::UserRole).toString().compare(spell->code,Qt::CaseInsensitive)==0)m_spellList->setCurrentRow(i);m_selectedRunes->clear();for(const QString&code:parsed.runeCodes){if(const auto*rune=m_catalog.findRune(code))catalogItem(rune->displayName,tr("%1 • %2").arg(rune->code,runeTypeName(rune->type)),rune->code,m_selectedRunes);else catalogItem(tr("Unknown rune"),code,code,m_selectedRunes);}updateOutput();return;}
    const auto parsed=ItemSpellConstructorCatalog::parseItemCode(value);if(!parsed.valid){showImportDiagnostic(parsed.diagnostics);return;}const auto*item=m_catalog.findItem(parsed.prototypeName);if(!item){showImportDiagnostic({tr("Unknown item prototype '%1'.").arg(parsed.prototypeName)});return;}m_tabs->setCurrentIndex(0);for(int i=0;i<m_itemList->count();++i)if(m_itemList->item(i)->data(Qt::UserRole).toString().compare(item->name,Qt::CaseInsensitive)==0)m_itemList->setCurrentRow(i);rebuildMaterialChoices(parsed.materialName);if(!parsed.materialName.isEmpty()&&m_material->currentData().toString().compare(parsed.materialName,Qt::CaseInsensitive)!=0)showImportDiagnostic({tr("Material '%1' is not compatible with %2 (%3 required).").arg(parsed.materialName,item->name,item->materialType)});m_itemSpell->setText(parsed.spell.valid?ItemSpellConstructorCatalog::formatSpellCode(parsed.spell.prototypeCode,parsed.spell.runeCodes):parsed.bracketPayload);updateOutput();
}
void ItemSpellConstructorDialog::copyCode(){QApplication::clipboard()->setText(code());}
void ItemSpellConstructorDialog::showImportDiagnostic(const QStringList&messages){QMessageBox::warning(this,tr("Code could not be imported"),messages.join('\n'));}
}
