#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include "window_geometry.h"
#include "ui_theme.h"
#include "item_spell_constructor_dialog.h"

#include "database_document.h"
#include "database_map_catalog.h"
#include "game_text_catalog.h"
#include "objects/worldobj.h"
#include "runtime/providers/profile_visual_resolver_support.h"
#include "runtime_providers.h"
#include "property.h"
#include "preview.h"
#include "view.h"
#include "resourcemanager.h"
#include <QApplication>
#include <QClipboard>
#include "verified_clipboard.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonArray>
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
#include <QSizePolicy>
#include <algorithm>

namespace {
class CConstructorPreviewObject final : public CWorldObj
{
    QJsonObject m_nativePreview;
public:
    QJsonObject independentPreviewSource() const override { return m_nativePreview; }
    void applyResolvedItem(const QString& modelName,const QString& primaryTexture,
                           const QStringList& parts,
                           const QMap<QString,QString>& partTextures,
                           const QStringList& bodyTextureLayers,
                           const QMap<QString,QVector3D>& emissions)
    {
        QStringList uniqueTextures=partTextures.values();uniqueTextures.removeDuplicates();
        m_nativePreview={{"model",modelName},{"texture",uniqueTextures.size()==1?uniqueTextures.first():primaryTexture},{"parts",QJsonArray::fromStringList(parts)},
            {"requiresAppearance",!bodyTextureLayers.isEmpty()||uniqueTextures.size()>1}};
        applyTransientVisual(modelName,primaryTexture,parts,QVector3D(1,1,1),
                             partTextures,QString(),bodyTextureLayers,emissions);
    }
    void applyStandaloneItem(ei::CFigure* figure,const QString& modelName,
                             const QStringList& textureCandidates,
                             bool preserveAuthoredUvs,
                             bool normalizeAuthoredAtlasCell)
    {
        m_nativePreview={{"model",modelName},{"texture",textureCandidates.value(0)},{"standalone",true}};
        applyTransientStandaloneVisual(figure,modelName,textureCandidates,
                                       preserveAuthoredUvs,normalizeAuthoredAtlasCell);
    }
};
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
                             const QString& key, QListWidget* list,
                             const QPixmap& icon=QPixmap())
{
    auto* item = new QListWidgetItem(title + QStringLiteral("\n") + subtitle, list);
    item->setData(Qt::UserRole, key);
    if(!icon.isNull())item->setIcon(QIcon(icon));
    return item;
}
QPixmap runePixmap(const ei::ItemSpellConstructorCatalog& catalog,
                   const ei::SConstructorRune& rune)
{
    for(const QString& texture:catalog.runeTextureCandidates(rune))
    {
        const QImage image=ei::ItemSpellConstructorCatalog::orientPreviewImage(
            CTextureList::getInstance()->textureImage(texture,QString()));
        if(!image.isNull())return QPixmap::fromImage(image).scaled(
            40,40,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    }
    return {};
}
QPixmap spellPixmap(const ei::ItemSpellConstructorCatalog& catalog,
                    const ei::SConstructorSpell& spell,const QStringList& runes,int side)
{
    QImage source;for(const QString& candidate:catalog.spellTextureCandidates(spell))
    {source=ei::ItemSpellConstructorCatalog::orientPreviewImage(CTextureList::getInstance()->textureImage(candidate,QString()));if(!source.isNull())break;}
    if(source.isNull())return {};
    QPixmap result(side,side);result.fill(QColor(24,24,28));QPainter painter(&result);
    const int margin=qMax(4,side/32);const QPixmap icon=QPixmap::fromImage(source).scaled(side-margin*2,side-margin*2,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    painter.drawPixmap((side-icon.width())/2,(side-icon.height())/2,icon);
    const int runeSide=qMax(24,side/5),gap=qMax(2,side/80),columns=4;
    const int startY=side-gap-((runes.size()+columns-1)/columns)*(runeSide+gap);
    for(int i=0;i<runes.size();++i)
    {
        const QRect square(gap+(i%columns)*(runeSide+gap),startY+(i/columns)*(runeSide+gap),runeSide,runeSide);
        QPixmap runeIcon;if(const auto* rune=catalog.findRune(runes.at(i)))runeIcon=runePixmap(catalog,*rune);
        painter.setPen(QPen(QColor(230,200,95),1));painter.setBrush(QColor(20,20,24,220));painter.drawRect(square);
        if(!runeIcon.isNull())painter.drawPixmap(square.adjusted(2,2,-2,-2),runeIcon);
        else{painter.setPen(Qt::white);painter.drawText(square,Qt::AlignCenter,runes.at(i));}
    }
    return result;
}
}

namespace ei {
ItemSpellConstructorDialog::ItemSpellConstructorDialog(
    const QSharedPointer<DatabaseDocument>& document, const QStringList& resourceRoots,
    CView* view, QWidget* parent)
    : QDialog(nullptr, Qt::Window | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint |
                      Qt::WindowCloseButtonHint),
      m_catalog(document && !(view && view->property("rebuild64Selected").toBool()) ? ItemSpellConstructorCatalog::fromDocument(*document)
                         : ItemSpellConstructorCatalog()),
      m_databaseSnapshot(document && !(view && view->property("rebuild64Selected").toBool()) ? document->currentSnapshot() : QSharedPointer<const SProfileDatabaseSnapshot>()),
      m_providerId(document ? document->providerId() : QString()),
      m_profileId(document ? document->profileId() : QString()),
      m_view(view)
{
    Q_UNUSED(parent);
    setObjectName(QStringLiteral("itemSpellConstructorDialog"));
    setWindowTitle(tr("Item / Spell Constructor"));
    setWindowIcon(QApplication::windowIcon());
    // Keep this reusable top-level window alive. WA_DeleteOnClose posts a
    // deferred delete; reopening before that event ran could resurrect the
    // closing OpenGL child and then destroy it while visible.
    setAttribute(Qt::WA_DeleteOnClose, false);
    ei::rememberWindowGeometry(this, QStringLiteral("itemSpellConstructor"));
    resize(1100, 720);
    setMinimumSize(780, 520);
    const bool native = view && view->property("rebuild64Selected").toBool();
    if(!native){const SGameTextCatalog texts = loadGameTextCatalog(resourceRoots);m_catalog.applyGameTexts(texts.entries);}

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
    m_material = new QComboBox(itemTab);m_material->setObjectName(QStringLiteral("constructorMaterial"));
    m_itemSpell = new QLineEdit(itemTab); m_itemSpell->setPlaceholderText(tr("Optional spell code"));
    auto* useSpell = new QPushButton(tr("Use spell from Spell tab"), itemTab);
    auto* openSpell = new QPushButton(tr("Open in Spell tab"), itemTab);openSpell->setObjectName(QStringLiteral("constructorOpenInsertedSpell"));
    auto* spellRow = new QHBoxLayout; spellRow->addWidget(m_itemSpell, 1); spellRow->addWidget(useSpell);spellRow->addWidget(openSpell);
    itemForm->addRow(tr("Compatible material:"), m_material);
    itemForm->addRow(tr("Inserted spell:"), spellRow);
    itemRight->addLayout(itemForm);
    m_itemDetails = new QLabel(itemTab); m_itemDetails->setWordWrap(true); m_itemDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    itemRight->addWidget(m_itemDetails);
    if(m_view)
    {
        m_previewObject = new CConstructorPreviewObject;
        // The constructor is a separate top-level window, so explicitly share
        // the main viewport's figures, textures, and shader resources.
        auto* previewControls=new QHBoxLayout;previewControls->addStretch();
        auto* resetCamera=new QPushButton(tr("Reset camera"),itemTab);
        resetCamera->setObjectName(QStringLiteral("constructorResetCamera"));
        previewControls->addWidget(resetCamera);itemRight->addLayout(previewControls);
        m_itemPreview = new CPreview(itemTab, m_view); m_itemPreview->attachView(m_view);
        m_itemPreview->setMinimumHeight(220);itemRight->addWidget(m_itemPreview,2);
        m_itemSpellBadge=new QLabel(m_itemPreview);m_itemSpellBadge->setFixedSize(144,144);m_itemSpellBadge->setAlignment(Qt::AlignCenter);m_itemSpellBadge->setFrameStyle(QFrame::StyledPanel|QFrame::Sunken);m_itemSpellBadge->move(8,8);m_itemSpellBadge->hide();m_itemSpellBadge->raise();
        connect(resetCamera,&QPushButton::clicked,this,&ItemSpellConstructorDialog::resetItemCamera);
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
    m_availableRunes = new QListWidget(spellTab);m_availableRunes->setIconSize(QSize(40,40)); runeColumn->addWidget(m_availableRunes, 1);
    auto* runeButtons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add →"), spellTab); auto* remove = new QPushButton(tr("Remove"), spellTab);
    auto* up = new QPushButton(tr("↑"), spellTab); auto* down = new QPushButton(tr("↓"), spellTab);
    runeButtons->addWidget(add); runeButtons->addWidget(remove); runeButtons->addWidget(up); runeButtons->addWidget(down);
    runeColumn->addLayout(runeButtons);
    runeColumn->addWidget(new QLabel(tr("Runes in spell (maximum 8)"), spellTab));
    m_selectedRunes = new QListWidget(spellTab);m_selectedRunes->setIconSize(QSize(40,40)); m_selectedRunes->setObjectName(QStringLiteral("constructorSelectedRunes"));
    runeColumn->addWidget(m_selectedRunes, 1);
    spellRoot->addLayout(runeColumn, 2);

    auto* spellRight = new QVBoxLayout;
    m_spellIcon = new QLabel(spellTab); m_spellIcon->setAlignment(Qt::AlignCenter);
    m_spellIcon->setFixedSize(240,240);m_spellIcon->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);m_spellIcon->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    spellRight->addWidget(m_spellIcon,0,Qt::AlignHCenter);
    m_spellDetails = new QLabel(spellTab); m_spellDetails->setWordWrap(true); m_spellDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    spellRight->addWidget(m_spellDetails);
    spellRoot->addLayout(spellRight, 2);
    m_tabs->addTab(spellTab, tr("Spells"));

    m_validation = new QLabel(this); m_validation->setWordWrap(true); m_validation->setObjectName(QStringLiteral("constructorValidation"));
    root->addWidget(m_validation);

    connect(m_itemFilter,&QLineEdit::textChanged,this,&ItemSpellConstructorDialog::refreshItemFilter);
    connect(m_itemCategory,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](){refreshItemFilterChoices();refreshItemFilter();});
    connect(m_itemType,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](){refreshArmorLayerAvailability();refreshItemFilter();});
    connect(m_armorLayer,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&ItemSpellConstructorDialog::refreshItemFilter);
    connect(m_itemList,&QListWidget::currentRowChanged,this,&ItemSpellConstructorDialog::selectItem);
    connect(m_material,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&ItemSpellConstructorDialog::selectMaterial);
    connect(m_itemSpell,&QLineEdit::textChanged,this,[this](){updateOutput();updateItemPreview();});
    connect(useSpell,&QPushButton::clicked,this,&ItemSpellConstructorDialog::useSpellInItem);
    connect(openSpell,&QPushButton::clicked,this,&ItemSpellConstructorDialog::openItemSpellInTab);
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
    if(native) {
        m_nativeLoading=true;m_tabs->setEnabled(false);m_validation->setText(tr("Rebuild64: подготовка каталога просмотра…"));
        struct Value {ItemSpellConstructorCatalog catalog;QSharedPointer<const SProfileDatabaseSnapshot> snapshot;};
        auto* watcher=new QFutureWatcher<Value>(this);const auto detached=document?QSharedPointer<DatabaseDocument>::create(*document):QSharedPointer<DatabaseDocument>();
        connect(watcher,&QFutureWatcher<Value>::finished,this,[this,watcher]{
            const auto value=watcher->result();m_catalog=value.catalog;m_databaseSnapshot=value.snapshot;watcher->deleteLater();m_nativeLoading=false;m_tabs->setEnabled(true);
            refreshItemFilterChoices();refreshItemFilter();refreshSpellFilter();updateOutput();
            if(!m_pendingCode.isEmpty()){const auto code=m_pendingCode;m_pendingCode.clear();setCode(code);}
        });
        watcher->setFuture(QtConcurrent::run([detached,resourceRoots]{
            Value value;value.snapshot=detached?detached->currentSnapshot():QSharedPointer<const SProfileDatabaseSnapshot>();value.catalog=detached?ItemSpellConstructorCatalog::fromDocument(*detached):ItemSpellConstructorCatalog();
            value.catalog.applyGameTexts(loadGameTextCatalog(resourceRoots).entries);return value;
        }));
    } else {refreshItemFilterChoices();refreshItemFilter();refreshSpellFilter();updateOutput();}
}

ItemSpellConstructorDialog::~ItemSpellConstructorDialog()
{
    if(m_itemPreview)
    {
        m_itemPreview->makeCurrent();
        m_itemPreview->attachNode(nullptr);
    }
    delete m_previewObject;
    m_previewObject=nullptr;
    if(m_itemPreview)m_itemPreview->doneCurrent();
}
QString ItemSpellConstructorDialog::code() const { return m_codeEdit->text().trimmed(); }
void ItemSpellConstructorDialog::setCode(const QString& value) { if(m_nativeLoading){m_pendingCode=value;return;}m_codeEdit->setText(value); importCode(); }

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
    QVector<const SConstructorRune*> visibleRunes;for(const auto&rune:m_catalog.runes){if(type!=-999&&rune.type!=type)continue;if(!runeQuery.isEmpty()&&!searchable({rune.name,rune.displayName,rune.code,rune.tag,runeTypeName(rune.type)}).contains(runeQuery))continue;visibleRunes.append(&rune);}std::sort(visibleRunes.begin(),visibleRunes.end(),[](const SConstructorRune*left,const SConstructorRune*right){return ItemSpellConstructorCatalog::naturalCodeLess(left->code,right->code);});for(const auto*rune:visibleRunes){const QString identity=rune->tag.isEmpty()?rune->code:tr("%1 • %2").arg(rune->tag,rune->code);catalogItem(rune->displayName,tr("%1 • %2").arg(identity,runeTypeName(rune->type)),rune->code,m_availableRunes,runePixmap(m_catalog,*rune));}
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

void ItemSpellConstructorDialog::resetItemCamera()
{
    if(!m_itemPreview)return;
    const auto* item=currentItem();
    if(item&&item->category==QStringLiteral("Weapon"))
        m_itemPreview->resetView(QVector3D(-25.0f,0.0f,45.0f));
    else
        m_itemPreview->resetView();
    m_itemPreviewInitialized=true;
    m_itemPreviewCategory=item?item->category:QString();
}

void ItemSpellConstructorDialog::addRune(){if(!m_availableRunes->currentItem()||m_selectedRunes->count()>=8)return;const QString code=m_availableRunes->currentItem()->data(Qt::UserRole).toString();if(const auto*rune=m_catalog.findRune(code))catalogItem(rune->displayName,tr("%1 • %2").arg(rune->code,runeTypeName(rune->type)),rune->code,m_selectedRunes,runePixmap(m_catalog,*rune));updateSpellPreview();updateOutput();}
void ItemSpellConstructorDialog::removeRune(){delete m_selectedRunes->takeItem(m_selectedRunes->currentRow());updateSpellPreview();updateOutput();}
void ItemSpellConstructorDialog::moveRuneUp(){const int row=m_selectedRunes->currentRow();if(row<=0)return;auto*item=m_selectedRunes->takeItem(row);m_selectedRunes->insertItem(row-1,item);m_selectedRunes->setCurrentRow(row-1);updateOutput();}
void ItemSpellConstructorDialog::moveRuneDown(){const int row=m_selectedRunes->currentRow();if(row<0||row+1>=m_selectedRunes->count())return;auto*item=m_selectedRunes->takeItem(row);m_selectedRunes->insertItem(row+1,item);m_selectedRunes->setCurrentRow(row+1);updateOutput();}
void ItemSpellConstructorDialog::useSpellInItem(){if(const auto*spell=currentSpell()){m_itemSpell->setText(ItemSpellConstructorCatalog::formatSpellCode(spell->code,selectedRuneCodes()));m_tabs->setCurrentIndex(0);}}
void ItemSpellConstructorDialog::openItemSpellInTab()
{
    const auto parsed=ItemSpellConstructorCatalog::parseSpellCode(m_itemSpell->text());if(!parsed.valid){showImportDiagnostic(parsed.diagnostics);return;}
    const auto*spell=m_catalog.findSpell(parsed.prototypeCode);if(!spell){showImportDiagnostic({tr("Unknown spell prototype '%1'.").arg(parsed.prototypeCode)});return;}
    m_tabs->setCurrentIndex(1);for(int i=0;i<m_spellList->count();++i)if(m_spellList->item(i)->data(Qt::UserRole).toString().compare(spell->code,Qt::CaseInsensitive)==0){m_spellList->setCurrentRow(i);break;}
    m_selectedRunes->clear();for(const QString&code:parsed.runeCodes)if(const auto*rune=m_catalog.findRune(code))catalogItem(rune->displayName,tr("%1 • %2").arg(rune->code,runeTypeName(rune->type)),rune->code,m_selectedRunes,runePixmap(m_catalog,*rune));updateOutput();
}

CWorldObj* ItemSpellConstructorDialog::buildItemPreview(
    const ItemSpellConstructorCatalog& catalog, const SConstructorItem& item,
    const SConstructorMaterial* material,
    const QSharedPointer<const SProfileDatabaseSnapshot>& snapshot, bool worldObject,
    QString* diagnostic)
{
    if(diagnostic) diagnostic->clear();
    auto mapObject = [&](const QString& model, const QString& texture, const QStringList& parts) -> CWorldObj* {
        const QString mapTexture = databaseItemMapTextureName(texture);
        const auto* textureList = CTextureList::getInstance();
        bool exactMatch = false;
        for(const QString& available : textureList->textureList())
            if(available.compare(mapTexture, Qt::CaseInsensitive) == 0)
            { exactMatch = true; break; }
        if(!exactMatch || !CTextureList::getInstance()->hasTexture(mapTexture, model))
        {
            if(diagnostic) *diagnostic = tr("Map texture %1.mmp is missing or cannot be loaded. Add the converted texture to the active profile resources.").arg(mapTexture);
            return nullptr;
        }
        CWorldObj defaults;
        SDatabaseMapEntry entry;
        entry.category = EDatabaseMapCategory::Item;
        entry.name = ItemSpellConstructorCatalog::formatItemCode(
            item.name, material ? material->name : QString());
        entry.model = model; entry.primaryTexture = mapTexture;
        QJsonObject data;
        if(!applyDatabaseMapEntry(entry, defaults.toJson(), &data)) return nullptr;
        data.insert("Object parts", QJsonArray::fromStringList(parts));
        auto* object = new CWorldObj(data);
        object->loadFigure(); object->loadTexture();
        return object;
    };
    if(catalog.isJmvProfile() && item.category != QStringLiteral("Quick Item")
       && material && snapshot)
    {
        const QString model=catalog.previewModelName();
        SEquipmentLayoutRequest request;
        request.profileId=snapshot->profileId;
        request.runtimeManifestId=snapshot->runtimeManifestId;
        request.prototype=item.name;
        request.model=model;
        request.assemblyParts=CObjectList::getInstance()->figurePartNames(model);
        request.dataSource=EUnitVisualDataSource::ProfileDatabase;
        if(ei::CFigure* assembly=CObjectList::getInstance()->getFigure(model))
        {
            QMap<QString,QVector3D> ignoredOffsets;
            QVector3D ignoredComplexion(1,1,1);
            assembly->getAssemblyHierarchy(request.assemblyParents,ignoredOffsets,
                                           ignoredComplexion);
        }
        const QString reference=ItemSpellConstructorCatalog::formatItemCode(
            item.name,material->name);
        if(item.category==QStringLiteral("Weapon"))request.weapons<<reference;
        else request.armor<<reference;
        QJsonObject resolved;
        const SRuntimeProviderResult resolution=resolveEquipmentLayout(
            snapshot->providerId,true,SJmvEquipmentGates(),snapshot,request,&resolved);
        QStringList components;
        for(const QJsonValue& value:resolved.value("components").toArray())
            components<<value.toString();
        QMap<QString,QString> textures;
        const QJsonObject textureObject=resolved.value("componentTextures").toObject();
        for(auto it=textureObject.constBegin();it!=textureObject.constEnd();++it)
            textures.insert(it.key(),it.value().toString());
        const QString primary=catalog.previewTextureName();
        const QJsonArray bodyLayers=resolved.value("bodyTextureLayers").toArray();
        QStringList bodyTextureLayers;
        for(const QJsonValue& value:bodyLayers)bodyTextureLayers<<value.toString();
        QMap<QString,QVector3D> emissions;
        for(const QJsonValue& value:resolved.value("emissiveParts").toArray())
            emissions.insert(value.toString().toLower(),QVector3D(1,1,1));
        if(resolution.success&&!components.isEmpty())
        {
            if(worldObject)
            {
                QSet<QString> uniqueTextures;
                for(const QString& part : components)
                    uniqueTextures.insert(textures.value(part, primary));
                if(uniqueTextures.size() == 1 && bodyTextureLayers.isEmpty())
                    return mapObject(model, *uniqueTextures.constBegin(), components);
                // Only persist a visual expressible by ordinary map fields.
                // Otherwise try the standalone inventory figure below.
            }
            else
            {
                auto* preview = new CConstructorPreviewObject;
                preview->applyResolvedItem(model,primary,components,textures,bodyTextureLayers,emissions);
                return preview;
            }
        }
    }
    const QString figure = ItemSpellConstructorCatalog::itemFigureName(item);
    const QStringList textures = catalog.itemTextureCandidates(item,material);
    if(figure.isEmpty() || textures.isEmpty()) return nullptr;
    ei::CFigure* raw = CObjectList::getInstance()->getFigureResource(figure+QStringLiteral(".fig"));
    if(!raw) return nullptr;
    if(worldObject)
    {
        for(const QString& texture : textures)
            if(auto* object = mapObject(figure, texture, {})) return object;
        return nullptr;
    }
    auto* preview = new CConstructorPreviewObject;
    preview->applyStandaloneItem(raw,figure,textures,
        ItemSpellConstructorCatalog::preservesStandaloneTextureUvs(item),
        ItemSpellConstructorCatalog::normalizesStandaloneTextureAtlasCell(item));
    return preview;
}

void ItemSpellConstructorDialog::updateItemPreview()
{
    const auto* item=currentItem();
    if(!item || !m_itemPreview) return;
    const bool resetForCategory=ItemSpellConstructorCatalog::previewCategoryNeedsCameraReset(
        m_itemPreviewCategory,item->category);
    m_itemPreview->makeCurrent();
    m_itemPreview->attachNode(nullptr,false);
    delete m_previewObject;
    m_previewObject=buildItemPreview(m_catalog,*item,currentMaterial(),m_databaseSnapshot);
    m_itemPreview->attachNode(m_previewObject,false);
    if(m_previewObject && (!m_itemPreviewInitialized || resetForCategory)) resetItemCamera();
    m_itemPreview->updateGL();
    if(m_itemSpellBadge){const auto parsed=ItemSpellConstructorCatalog::parseSpellCode(m_itemSpell->text());const auto*spell=parsed.valid?m_catalog.findSpell(parsed.prototypeCode):nullptr;if(spell){m_itemSpellBadge->setPixmap(spellPixmap(m_catalog,*spell,parsed.runeCodes,140));m_itemSpellBadge->show();m_itemSpellBadge->raise();}else m_itemSpellBadge->hide();}
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
        m_spellIcon->setText(QString());m_spellIcon->setPixmap(spellPixmap(m_catalog,*spell,selectedRuneCodes(),240));
    }
    const auto report=m_catalog.spellReport(*spell,selectedRuneCodes());
    QStringList verifiedLines;
    for(const QString& code:selectedRuneCodes())
        if(const auto* rune=m_catalog.findRune(code))
            verifiedLines<<tr("%1 - %2 (%3)").arg(
                rune->code,rune->displayName,runeTypeName(rune->type));
    const QString effect=report.effectBonus==0.0
        ?QString::number(report.effect,'g',8)
        :tr("%1 + %2 = %3").arg(QString::number(report.effectBase,'g',8),
                                 QString::number(report.effectBonus,'g',8),
                                 QString::number(report.effect,'g',8));
    m_spellDetails->setText(tr(
        "%1\nCode: %2\nSchool: %3 | %4\n"
        "Energy: %5 (%6 per second) | difficulty: %7\n"
        "Effect: %8 | duration: %9 s (%10 segments)\n"
        "Range: %11 m | area: %12 m² (radius %13 m) | targets: %14\n\n"
        "Runes:\n%15")
        .arg(spell->displayName,spell->code).arg(spell->school).arg(spell->subtype)
        .arg(report.spellMana,0,'g',8).arg(report.spellManaPerSecond,0,'g',8).arg(report.spellComplexity)
        .arg(effect).arg(report.durationSegments/15.0,0,'g',8)
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
        tr("Material family: %1 | base energy: %2 | maximum spell difficulty: %3 | crafting material required: %4 components")
            .arg(item->materialType).arg(item->mana,0,'g',8)
            .arg(item->slotCapacity).arg(item->components)};
    if((item->category==QStringLiteral("Weapon")||item->category==QStringLiteral("Armor"))&&currentMaterial())
        detail<<tr("Preview: standalone inventory figure with its material texture, matching the game's item preview path.");
    QStringList warnings;
    const bool materialRequired=!item->materialType.trimmed().isEmpty()
        && item->materialType.compare(QStringLiteral("None"),Qt::CaseInsensitive)!=0;
    if(materialRequired&&!currentMaterial())
        warnings<<tr("This item requires a supported %1 material.").arg(item->materialType);
    const SParsedSpellCode parsed=ItemSpellConstructorCatalog::parseSpellCode(spellCode);
    const auto capacity=m_catalog.limits(*item,currentMaterial(),nullptr,{});
    detail<<tr("Constructed item: energy %1 | maximum spell difficulty %2 | energy regeneration %3 per second")
               .arg(capacity.itemMana,0,'g',8).arg(capacity.itemSlots)
               .arg(capacity.itemManaRegenerationPerSecond,0,'g',8);
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
            const auto marked=[](const QString&value,bool exceeded){return exceeded?QStringLiteral("<span style='color:%1'><b>%2</b></span>").arg(ei::uiTextColor(ei::UiTextTone::Error).name(),value):value;};
            detail<<tr("Inserted spell: energy %1 (%2 per second) | difficulty %3")
                        .arg(marked(QString::number(report.spellMana,'g',8),report.spellMana>report.itemMana),QString::number(report.spellManaPerSecond,'g',8),marked(QString::number(report.spellComplexity),report.spellComplexity>report.itemSlots));
        }
    }
    m_itemDetails->setTextFormat(Qt::RichText);m_itemDetails->setText(detail.join(QStringLiteral("<br>")));
    m_validation->setText(warnings.isEmpty()
        ?tr("Code is structurally valid. Compatibility findings are advisory.")
        :tr("Advisory validation:\n• %1").arg(warnings.join("\n• ")));
}

void ItemSpellConstructorDialog::importCode()
{
    const QString value=m_codeEdit->text().trimmed();if(value.contains('{')&&!value.contains('[')){const auto parsed=ItemSpellConstructorCatalog::parseSpellCode(value);if(!parsed.valid){showImportDiagnostic(parsed.diagnostics);return;}const auto*spell=m_catalog.findSpell(parsed.prototypeCode);if(!spell){showImportDiagnostic({tr("Unknown spell prototype '%1'.").arg(parsed.prototypeCode)});return;}m_tabs->setCurrentIndex(1);for(int i=0;i<m_spellList->count();++i)if(m_spellList->item(i)->data(Qt::UserRole).toString().compare(spell->code,Qt::CaseInsensitive)==0)m_spellList->setCurrentRow(i);m_selectedRunes->clear();for(const QString&code:parsed.runeCodes){if(const auto*rune=m_catalog.findRune(code))catalogItem(rune->displayName,tr("%1 • %2").arg(rune->code,runeTypeName(rune->type)),rune->code,m_selectedRunes,runePixmap(m_catalog,*rune));else catalogItem(tr("Unknown rune"),code,code,m_selectedRunes);}updateOutput();return;}
    const auto parsed=ItemSpellConstructorCatalog::parseItemCode(value);if(!parsed.valid){showImportDiagnostic(parsed.diagnostics);return;}const auto*item=m_catalog.findItem(parsed.prototypeName);if(!item){showImportDiagnostic({tr("Unknown item prototype '%1'.").arg(parsed.prototypeName)});return;}m_tabs->setCurrentIndex(0);m_itemFilter->clear();m_itemCategory->setCurrentIndex(0);m_itemType->setCurrentIndex(0);m_armorLayer->setCurrentIndex(0);refreshItemFilterChoices();refreshItemFilter();for(int i=0;i<m_itemList->count();++i)if(m_itemList->item(i)->data(Qt::UserRole).toString().compare(item->name,Qt::CaseInsensitive)==0){m_itemList->setCurrentRow(i);break;}rebuildMaterialChoices(parsed.materialName);if(!parsed.materialName.isEmpty()&&m_material->currentData().toString().compare(parsed.materialName,Qt::CaseInsensitive)!=0)showImportDiagnostic({tr("Material '%1' is not compatible with %2 (%3 required).").arg(parsed.materialName,item->name,item->materialType)});m_itemSpell->setText(parsed.spell.valid?ItemSpellConstructorCatalog::formatSpellCode(parsed.spell.prototypeCode,parsed.spell.runeCodes):parsed.bracketPayload);updateOutput();
}
void ItemSpellConstructorDialog::copyCode(){ei::copyTextToClipboard(code(),this);}
void ItemSpellConstructorDialog::showImportDiagnostic(const QStringList&messages){QMessageBox::warning(this,tr("Code could not be imported"),messages.join('\n'));}
}
