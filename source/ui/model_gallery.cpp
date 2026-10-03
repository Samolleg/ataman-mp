#include "window_geometry.h"
#include "model_catalog_icons.h"
#include "model_gallery_thumbnail.h"
#include "resource_catalog.h"
#include "resource_name.h"
#include "resourcemanager.h"
#include "model_gallery.h"
#include "view.h"
#include "createobjectform.h"
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCompleter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QtConcurrent/QtConcurrentRun>
#include <QSignalBlocker>
#include <QTabBar>
#include <QStyle>
#include <QPainterPath>
#include <QtMath>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

namespace {
QCache<QString,QImage>& sharedThumbnails()
{
    // Accessed only on the GUI thread. Cost is KiB, including failed results.
    static QCache<QString,QImage> cache(64*1024);
    return cache;
}
int imageCost(const QImage& image)
{
    return int(qMax<qint64>(1,(qint64(image.bytesPerLine())*image.height()+1023)/1024));
}
class ModelTileDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override
    {
        painter->save();
        painter->fillRect(option.rect.adjusted(2,2,-2,-2), option.state&QStyle::State_Selected
            ? option.palette.highlight() : option.palette.alternateBase());
        const QRect imageRect(option.rect.x()+10,option.rect.y()+8,option.rect.width()-20,132);
        const QIcon icon=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if(!icon.isNull()) icon.paint(painter,imageRect);
        else { painter->setPen(option.palette.color(option.state&QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Mid)); painter->drawText(imageRect,Qt::AlignCenter,QStringLiteral("…")); }
        painter->setPen(option.state&QStyle::State_Selected ? option.palette.highlightedText().color() : option.palette.text().color());
        const QRect title(option.rect.x()+10,option.rect.y()+146,option.rect.width()-20,20);
        painter->drawText(title,Qt::AlignCenter,option.fontMetrics.elidedText(index.data(Qt::UserRole).toString(),Qt::ElideMiddle,title.width()));
        painter->drawText(title.translated(0,22),Qt::AlignCenter,option.fontMetrics.elidedText(index.data(Qt::UserRole+1).toString(),Qt::ElideRight,title.width()));
        painter->restore();
    }
};
}
CModelGallery::CModelGallery(const QStringList& models,ei::TechnicalModelCatalog* catalog,
    const QString& profile,const QStringList& textures,const ei::TechnicalTextureCatalog* scan,
    ThumbnailProvider provider,QWidget* parent,const QString& initialModel,const QString& initialTexture,
    const QString& thumbnailNamespace,ModelSceneProvider sceneProvider):QDialog(parent),m_models(models),m_textures(textures),
    m_catalog(catalog),m_scan(scan),m_profile(profile),m_thumbnailNamespace(thumbnailNamespace),m_provider(std::move(provider))
{
    for(const auto& texture:m_textures) m_availableTextures.insert(texture.toCaseFolded());
    m_sceneProvider=std::move(sceneProvider);
    ei::rememberWindowGeometry(this, QStringLiteral("modelGallery"));
    setObjectName("modelGallery"); setWindowTitle(tr("Model gallery")); resize(1120,760);
    setMinimumSize(600,440); setWindowFlag(Qt::WindowContextHelpButtonHint,false);
    m_models.removeDuplicates(); m_models.sort(Qt::CaseInsensitive);
    auto* layout=new QVBoxLayout(this);
    CView* assemblyView=nullptr;
    for(QWidget* owner=parent;owner && !assemblyView;owner=owner->parentWidget()) {
        assemblyView=qobject_cast<CView*>(owner);if(!assemblyView)assemblyView=owner->findChild<CView*>();
    }
    if(assemblyView) {
        auto* assemblies=new QPushButton(QString::fromUtf8("Готовые сборки…"),this);assemblies->setObjectName("modelGalleryAssemblies");
        assemblies->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));layout->addWidget(assemblies);
        connect(assemblies,&QPushButton::clicked,this,[this,assemblyView]{
            if(auto* creation=qobject_cast<CCreateObjectForm*>(parentWidget()))creation->hide();
            reject();QTimer::singleShot(0,assemblyView,[assemblyView]{assemblyView->openAssemblyCatalog();});
        });
    }
    m_search=new QLineEdit(this); m_search->setObjectName("modelGallerySearch");
    m_search->setPlaceholderText(tr("Search by model ID or Russian name")); layout->addWidget(m_search);
    const QStringList categoryLabels{tr("Houses"),tr("Architecture"),tr("Gates"),tr("Utensils"),
        tr("Trees"),tr("Bushes"),tr("Levers and chests"),tr("Decor")};
    const QStringList regionLabels{tr("Gipat"),tr("Ingos"),tr("Suslanger")};
    m_categories=new QTabBar(this); m_categories->setObjectName("modelGalleryCategories");
    m_categories->setExpanding(false); m_categories->setUsesScrollButtons(true); m_categories->setIconSize(QSize(28,28));
    m_categories->addTab(style()->standardIcon(QStyle::SP_DirIcon),tr("All models"));
    m_categories->setTabToolTip(0,tr("All models, including those not yet assigned to a section"));
    const auto categories=ei::TechnicalModelCatalog::categoryIds();
    for(int i=0;i<categories.size();++i) {const int tab=m_categories->addTab(ei::modelCatalogCategoryIcon(i),categoryLabels[i]);m_categories->setTabData(tab,categories[i]);m_categories->setTabToolTip(tab,categoryLabels[i]);}
    layout->addWidget(m_categories);
    m_regions=new QTabBar(this); m_regions->setObjectName("modelGalleryRegions"); m_regions->setExpanding(false);
    const auto regions=ei::TechnicalModelCatalog::regionIds();
    for(int i=0;i<regions.size();++i) {const int tab=m_regions->addTab(regionLabels[i]);m_regions->setTabData(tab,regions[i]);}
    layout->addWidget(m_regions);
    m_list=new QListWidget(this); m_list->setObjectName("modelGalleryTiles");
    m_list->setViewMode(QListView::IconMode); m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static); m_list->setGridSize(QSize(216,198));
    m_list->setIconSize(QSize(192,132)); m_list->setSpacing(4); m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setItemDelegate(new ModelTileDelegate(m_list)); layout->addWidget(m_list,1);
    auto* nameRow=new QHBoxLayout;
    nameRow->addWidget(new QLabel(tr("Russian name:"),this));
    m_name=new QLineEdit(this); m_name->setObjectName("modelGalleryRussianName");
    m_name->setPlaceholderText(tr("Enter a recognizable name")); nameRow->addWidget(m_name,1);
    m_saveName=new QPushButton(tr("Save name"),this); m_saveName->setObjectName("modelGallerySaveName"); nameRow->addWidget(m_saveName); layout->addLayout(nameRow);
    auto* placementRow=new QHBoxLayout;
    placementRow->addWidget(new QLabel(tr("Section:"),this));
    m_categoryChoice=new QComboBox(this); m_categoryChoice->setObjectName("modelGalleryCategoryChoice");
    m_categoryChoice->addItem(tr("Unassigned"),QString());
    for(int i=0;i<categories.size();++i) m_categoryChoice->addItem(ei::modelCatalogCategoryIcon(i),categoryLabels[i],categories[i]);
    placementRow->addWidget(m_categoryChoice,1); placementRow->addWidget(new QLabel(tr("Island:"),this));
    m_regionChoice=new QComboBox(this); m_regionChoice->setObjectName("modelGalleryRegionChoice");
    m_regionChoice->addItem(tr("Not selected"),QString());
    for(int i=0;i<regions.size();++i) m_regionChoice->addItem(regionLabels[i],regions[i]);
    placementRow->addWidget(m_regionChoice,1);
    m_savePlacement=new QPushButton(tr("Save section"),this); m_savePlacement->setObjectName("modelGallerySavePlacement");
    placementRow->addWidget(m_savePlacement); layout->addLayout(placementRow);
    auto* textureRow=new QHBoxLayout; textureRow->addWidget(new QLabel(tr("Primary texture:"),this));
    m_texture=new QComboBox(this); m_texture->setObjectName("modelGalleryTexture"); m_texture->setEditable(true);
    m_texture->setInsertPolicy(QComboBox::NoInsert); m_texture->setMinimumContentsLength(20);
    m_texture->completer()->setFilterMode(Qt::MatchContains); m_texture->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    textureRow->addWidget(m_texture,1);
    m_saveTexture=new QPushButton(tr("Bind texture"),this); m_saveTexture->setObjectName("modelGalleryBindTexture"); textureRow->addWidget(m_saveTexture); layout->addLayout(textureRow);
    m_status=new QLabel(this); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    auto* buttons=new QHBoxLayout; buttons->addStretch();
    auto* connections=new QPushButton(QString::fromUtf8("Точки стыковки…"),this);
    connections->setObjectName("modelGalleryConnections");buttons->insertWidget(0,connections);
    connect(connections,&QPushButton::clicked,this,[this]{
        if(selectedModel().isEmpty())return;
        CModelConnectionEditor editor(m_catalog,selectedModel(),m_sceneProvider,this);
        if(editor.exec()==QDialog::Accepted)refreshCatalog();
    });
    m_choose=new QPushButton(tr("Choose model"),this); m_choose->setObjectName("modelGalleryChoose"); m_choose->setDefault(true); buttons->addWidget(m_choose);
    auto* cancel=new QPushButton(tr("Cancel"),this); buttons->addWidget(cancel); layout->addLayout(buttons);
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(m_texture,&QComboBox::currentTextChanged,this,[this]{
        m_choose->setEnabled(!selectedModel().isEmpty() && (!m_textureRequired || m_textures.contains(selectedTexture(),Qt::CaseInsensitive)));
        m_thumbnailTimer.start();
    });
    connect(m_choose,&QPushButton::clicked,this,[this]{ if(!selectedModel().isEmpty()) accept(); });
    connect(m_list,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem*){ accept(); });
    connect(m_list,&QListWidget::currentItemChanged,this,[this]{selectionChanged();});
    connect(m_search,&QLineEdit::textChanged,this,[this]{filter();});
    connect(m_categories,&QTabBar::currentChanged,this,[this]{filter();});
    connect(m_regions,&QTabBar::currentChanged,this,[this]{filter();});
    connect(m_savePlacement,&QPushButton::clicked,this,&CModelGallery::savePlacement);
    connect(m_categoryChoice,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int index){
        m_regionChoice->setEnabled(index>0 && m_catalog->writable() && !selectedModel().isEmpty());
        if(index==0) m_regionChoice->setCurrentIndex(0);
        else if(m_regionChoice->currentIndex()==0) m_regionChoice->setCurrentIndex(m_regions->currentIndex()+1);
    });
    connect(m_saveName,&QPushButton::clicked,this,&CModelGallery::saveName);
    connect(m_saveTexture,&QPushButton::clicked,this,&CModelGallery::saveTexture);
    connect(m_texture,QOverload<int>::of(&QComboBox::activated),this,[this]{m_chosenTextures.insert(selectedModel(),selectedTexture());m_thumbnailTimer.start();});
    connect(m_texture->lineEdit(),&QLineEdit::editingFinished,this,[this]{m_chosenTextures.insert(selectedModel(),selectedTexture());});
    connect(m_list->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{m_thumbnailTimer.start();});
    connect(&m_thumbnailTimer,&QTimer::timeout,this,&CModelGallery::loadVisibleThumbnail);
    // A queued GUI callback starts one CPU worker, without an artificial pause.
    m_thumbnailTimer.setInterval(0); m_thumbnailTimer.setSingleShot(true);
    connect(&m_thumbnailWatcher,&QFutureWatcher<QImage>::finished,this,&CModelGallery::finishThumbnail);
    filter(initialModel,initialTexture);
}
CModelGallery::~CModelGallery() { m_thumbnailTimer.stop(); }
void CModelGallery::resizeEvent(QResizeEvent* event) { QDialog::resizeEvent(event); m_thumbnailTimer.start(); }
QString CModelGallery::selectedModel() const { return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString(); }
QString CModelGallery::selectedTexture() const { return m_texture->currentText().trimmed(); }
void CModelGallery::enableMultiSelection()
{
    m_multiSelection=true;
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setToolTip(QString::fromUtf8("Ctrl — несколько моделей, Shift — диапазон. Выбранные объекты размещаются вместе; Esc отменяет добавление."));
    const auto refresh=[this]{m_choose->setText(QString::fromUtf8("Выбрать (%1)").arg(m_list->selectedItems().size()));};
    connect(m_list,&QListWidget::itemSelectionChanged,this,refresh);refresh();
}
QVector<QPair<QString,QString>> CModelGallery::selectedEntries() const
{
    QVector<QPair<QString,QString>> result;
    for(int i=0;i<m_list->count();++i)if(m_list->item(i)->isSelected()) {
        const QString model=m_list->item(i)->data(Qt::UserRole).toString();
        result.append(qMakePair(model,model==selectedModel()?selectedTexture():textureForModel(model)));
    }
    return result;
}
QString CModelGallery::cacheKey(const QString& model,const QString& texture) const { return model.toLower()+'|'+texture.toLower(); }
QVector<ei::STechnicalTextureChoice> CModelGallery::textureChoices(const QString& model) const
{
    if(m_catalogRevision!=m_catalog->revision() || m_scanRevision!=m_scan->revision()) {
        m_textureChoices.clear(); m_catalogRevision=m_catalog->revision(); m_scanRevision=m_scan->revision();
    }
    const QString key=ei::TechnicalModelCatalog::modelKey(model);
    auto found=m_textureChoices.constFind(key);
    if(found==m_textureChoices.constEnd()) {
        m_textureChoices.insert(key,m_catalog->textureChoices(m_profile,model,m_textures,*m_scan));
        found=m_textureChoices.constFind(key);
    }
    return found.value();
}
void CModelGallery::refreshCatalog() { m_textureChoices.clear(); filter(); }
QString CModelGallery::textureForModel(const QString& model) const
{
    if(model==selectedModel() && m_availableTextures.contains(selectedTexture().toCaseFolded())) return selectedTexture();
    if(m_chosenTextures.contains(model))return m_chosenTextures.value(model);
    const auto choices=textureChoices(model);
    return choices.isEmpty() ? QString() : choices.first().name;
}
void CModelGallery::setCurrentModel(const QString& model)
{
    for(int i=0;i<m_list->count();++i)
        if(ei::TechnicalModelCatalog::modelKey(m_list->item(i)->data(Qt::UserRole).toString())==ei::TechnicalModelCatalog::modelKey(model)) {m_list->setCurrentRow(i);m_list->scrollToItem(m_list->item(i));break;}
}
void CModelGallery::filter(const QString& preferredModel,const QString& preferredTexture)
{
    const QString previous=preferredModel.isEmpty()?selectedModel():preferredModel;
    const QString previousTexture=preferredModel.isEmpty()?selectedTexture():preferredTexture;
    const QSignalBlocker block(m_list);
    const QString category=m_categories->tabData(m_categories->currentIndex()).toString();
    const QString region=m_regions->tabData(m_regions->currentIndex()).toString();
    m_regions->setVisible(!category.isEmpty());
    m_list->clear();
    for(const auto& model:m_models)
    {
        const QString russian=m_catalog->russianName(model);
        if(!category.isEmpty() && (m_catalog->category(model)!=category || m_catalog->region(model)!=region)) continue;
        if(!ei::TechnicalModelCatalog::matches(model,russian,m_search->text())) continue;
        auto* item=new QListWidgetItem(m_list); item->setData(Qt::UserRole,model);
        item->setData(Qt::UserRole+1,russian);
        item->setToolTip(model+"\n"+russian); item->setSizeHint(QSize(208,192));
        if(ei::TechnicalModelCatalog::modelKey(model)==ei::TechnicalModelCatalog::modelKey(previous)) m_list->setCurrentItem(item);
    }
    if(!m_list->currentItem() && m_list->count()) m_list->setCurrentRow(0);
    selectionChanged();
    if(ei::TechnicalModelCatalog::modelKey(selectedModel())==ei::TechnicalModelCatalog::modelKey(previous)
        && m_availableTextures.contains(previousTexture.toCaseFolded())) m_texture->setCurrentText(previousTexture);
    if(m_list->currentItem()) m_list->scrollToItem(m_list->currentItem());
    m_thumbnailTimer.start();
}
void CModelGallery::selectionChanged()
{
    const QString model=selectedModel(); m_name->setText(m_catalog->russianName(model));
    m_choose->setEnabled(!model.isEmpty());
    m_saveName->setEnabled(!model.isEmpty() && m_catalog->writable());
    m_saveTexture->setEnabled(!model.isEmpty() && m_catalog->writable());
    m_savePlacement->setEnabled(!model.isEmpty() && m_catalog->writable());
    m_categoryChoice->setEnabled(m_savePlacement->isEnabled());
    {
        const QSignalBlocker categoryBlock(m_categoryChoice), regionBlock(m_regionChoice);
        m_categoryChoice->setCurrentIndex(qMax(0,m_categoryChoice->findData(m_catalog->category(model))));
        m_regionChoice->setCurrentIndex(qMax(0,m_regionChoice->findData(m_catalog->region(model))));
        m_regionChoice->setEnabled(m_savePlacement->isEnabled() && m_categoryChoice->currentIndex()>0);
    }
    const QSignalBlocker block(m_texture);
    const auto exact=textureChoices(model);
    // Populate off the view/completer: combo signal blocking does not suppress
    // model notifications and incremental insertion used to be quadratic.
    auto* prepared=new QStandardItemModel;
    auto add=[prepared](const QString& text,const QVariant& value=QVariant()) {
        auto* item=new QStandardItem(text); item->setData(value,Qt::UserRole); prepared->appendRow(item); return item;
    };
    auto header=[&](const QString& text) {
        auto* item=add(text);
        item->setFlags(Qt::NoItemFlags);
        QFont font=item->font(); font.setBold(true); item->setFont(font);
    };
    QSet<QString> names;
    header(exact.isEmpty() ? QString::fromUtf8("Текстуры модели — не найдены")
                          : QString::fromUtf8("Текстуры этой модели"));
    for(const auto& choice:exact) { names.insert(choice.name.toCaseFolded()); add(choice.name,choice.name); }
    auto* separator=add(QString()); separator->setFlags(Qt::NoItemFlags);
    separator->setData(QStringLiteral("separator"),Qt::AccessibleDescriptionRole);
    header(QString::fromUtf8("Другие текстуры профиля"));
    for(const auto& name:m_textures) if(!names.contains(name.toCaseFolded())) add(name,name);
    // Qt may destroy its default model inside setModel(). Track lifetime rather
    // than dereferencing a raw pointer after replacement.
    QPointer<QAbstractItemModel> previousModel=m_texture->model();
    m_texture->setModel(prepared); prepared->setParent(m_texture);
    if(previousModel && previousModel->parent()==m_texture) delete previousModel;
    m_texture->setCurrentIndex(exact.isEmpty() ? -1 : m_texture->findData(exact.first().name));
    if(m_chosenTextures.contains(model))m_texture->setCurrentText(m_chosenTextures.value(model));
    if(m_multiSelection)m_choose->setText(QString::fromUtf8("Выбрать (%1)").arg(m_list->selectedItems().size()));
    m_status->setText(tr("%1 model(s). Names and texture bindings: %2").arg(m_list->count()).arg(m_catalog->path()));
    m_choose->setEnabled(!model.isEmpty() && (!m_textureRequired || m_textures.contains(selectedTexture(),Qt::CaseInsensitive)));
    m_thumbnailTimer.start();
}
void CModelGallery::reportSave(bool ok,const QString& error)
{
    m_status->setText(ok ? tr("Saved to %1").arg(m_catalog->path()) : error);
}
void CModelGallery::saveName()
{
    const QString model=selectedModel(); if(model.isEmpty()) return;
    m_catalog->setRussianName(model,m_name->text()); QString error;
    const bool saved=m_catalog->save(&error);
    if(saved) filter();
    reportSave(saved,error);
}
void CModelGallery::saveTexture()
{
    const QString model=selectedModel(),texture=selectedTexture();
    if(model.isEmpty() || !m_textures.contains(texture,Qt::CaseInsensitive)) return;
    m_catalog->bindTexture(m_profile,model,texture); QString error;
    reportSave(m_catalog->save(&error),error);
    m_thumbnailTimer.start();
}
void CModelGallery::savePlacement()
{
    const QString model=selectedModel(),category=m_categoryChoice->currentData().toString(),region=m_regionChoice->currentData().toString();
    if(model.isEmpty()) return;
    if(!m_catalog->setPlacement(model,category,region)) {m_status->setText(tr("Choose a section and an island before saving."));return;}
    QString error; const bool saved=m_catalog->save(&error);
    if(saved)
    {
        const QSignalBlocker categoryBlock(m_categories),regionBlock(m_regions);
        m_categories->setCurrentIndex(category.isEmpty()?0:ei::TechnicalModelCatalog::categoryIds().indexOf(category)+1);
        m_regions->setCurrentIndex(qMax(0,ei::TechnicalModelCatalog::regionIds().indexOf(region)));
        filter();
    }
    reportSave(saved,error);
}
void CModelGallery::loadVisibleThumbnail()
{
    // isRunning() becomes false before the queued finished notification. Keep
    // ownership until that notification consumes the result and loading key.
    if(!isVisible() || m_thumbnailJobActive || !m_provider) return;
    for(int i=0;i<m_list->count();++i)
    {
        auto* item=m_list->item(i);
        if(!m_list->visualItemRect(item).intersects(m_list->viewport()->rect())) continue;
        const QString model=item->data(Qt::UserRole).toString(),texture=textureForModel(model),key=cacheKey(model,texture);
        if(item->data(Qt::UserRole+2).toString()==key) continue;
        const QImage* cached=m_thumbnails.object(key);
        if(!cached && !m_thumbnailNamespace.isEmpty()) cached=sharedThumbnails().object(m_thumbnailNamespace+'|'+key);
        if(cached) {
            if(item->data(Qt::UserRole+2).toString()!=key) {
                if(!cached->isNull()) item->setIcon(QPixmap::fromImage(*cached));
                else {item->setIcon(QIcon());item->setToolTip(model+"\n"+tr("Preview unavailable"));}
                item->setData(Qt::UserRole+2,key);
            }
            continue;
        }
        // A new texture must show loading rather than the previous texture.
        item->setIcon(QIcon()); item->setData(Qt::UserRole+2,QVariant());
        m_loadingKey=key; m_loadingModel=model;
        const auto provider=m_provider;
        m_thumbnailJobActive=true;
        m_thumbnailWatcher.setFuture(QtConcurrent::run([provider,model,texture]{return provider(model,texture);}));
        return;
    }
}
void CModelGallery::finishThumbnail()
{
    const QImage image=m_thumbnailWatcher.result(); m_thumbnails.insert(m_loadingKey,new QImage(image),imageCost(image));
    m_thumbnailJobActive=false;
    if(!m_thumbnailNamespace.isEmpty()) sharedThumbnails().insert(m_thumbnailNamespace+'|'+m_loadingKey,new QImage(image),imageCost(image));
    for(int i=0;i<m_list->count();++i)
    {
        auto* item=m_list->item(i); const QString model=item->data(Qt::UserRole).toString();
        if(model==m_loadingModel && cacheKey(model,textureForModel(model))==m_loadingKey) {
            if(!image.isNull()) item->setIcon(QPixmap::fromImage(image));
            else item->setToolTip(model+"\n"+tr("Preview unavailable"));
            item->setData(Qt::UserRole+2,m_loadingKey);
        }
    }
    m_thumbnailTimer.start();
}

QString CModelGallery::resourceThumbnailNamespace(const QStringList& sourcePaths)
{
    // Sample once per opening, rather than relying on the renderer's explicit
    // refresh generation. An externally replaced archive must get a new cache.
    QByteArray identity; QDataStream stream(&identity,QIODevice::WriteOnly);
    for(const auto& path:sourcePaths) {
        const QFileInfo info(path);
        stream << info.absoluteFilePath().toCaseFolded() << info.size()
               << info.lastModified().toMSecsSinceEpoch();
    }
    return QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex());
}
CModelGallery::ThumbnailProvider CModelGallery::resourceThumbnails(const QStringList& sourcePaths)
{
    struct PreviewSources {
        CResourceCatalog catalog;
        bool indexed=false;
        QVector<QString> paths;
        QMutex mutex;
        QCache<QString,ei::SResourceModelScene> scenes{16*1024};
        QCache<QString,QImage> textures{32*1024};
    };
    static QMutex registryMutex;
    static QCache<QString,QSharedPointer<PreviewSources>> registry(2);
    QSharedPointer<PreviewSources> sources;
    {
        QMutexLocker lock(&registryMutex);
        const QString identity=resourceThumbnailNamespace(sourcePaths);
        if(const auto* cached=registry.object(identity)) sources=*cached;
        else {
            sources.reset(new PreviewSources); sources->paths=sourcePaths.toVector();
            registry.insert(identity,new QSharedPointer<PreviewSources>(sources));
        }
    }
    return [sources](const QString& model, const QString& texture) -> QImage {
        // Immutable results can be shared across windows. Index and cache access
        // is serialized in workers; the GUI never waits on this mutex.
        QMutexLocker lock(&sources->mutex);
        if(!sources->indexed) { sources->catalog.build(sources->paths); sources->indexed = true; }
        const auto read = [sources](const QString& name) -> QByteArray {
            const auto entries = sources->catalog.providers(name);
              if(entries.isEmpty() || entries.first().size > 64 * 1024 * 1024) return {};
            const auto* archive = sources->catalog.archive(entries.first().archivePriority);
            return archive ? archive->readMember(entries.first().memberIndex) : QByteArray();
        };
        const QString stem = ei::TechnicalModelCatalog::modelKey(model);
        ei::SResourceModelScene scene;
        if(const auto* cached=sources->scenes.object(stem)) scene=*cached;
        else {
            const QByteArray payload=read(stem+".mod");
            scene=payload.isEmpty() ? ei::buildFig8Scene(read(stem+".fig")) : ei::buildModScene(payload,read(stem+".bon"));
            // Conservative allowance for part names and skeleton allocations,
            // plus the actual triangle capacity. Cache oversized scenes nowhere.
            qint64 bytes=sizeof(scene)+qint64(scene.parts.size())*2048;
            for(const auto& part:scene.parts) bytes+=qint64(part.triangles.capacity())*sizeof(ei::SResourceModelTriangle);
            sources->scenes.insert(stem,new ei::SResourceModelScene(scene),int(qMin<qint64>(2147483647,(bytes+1023)/1024)));
        }
        QImage image;
        if(!texture.isEmpty()) for(const QString& candidate : ei::textureResourceCandidates(texture, stem))
        {
            if(const auto* cached=sources->textures.object(candidate.toCaseFolded())) {image=*cached;break;}
            const QByteArray bytes = read(candidate);
            if(bytes.isEmpty()) continue;
            image = CTextureList::prepareStaticTexture(bytes).image;
            sources->textures.insert(candidate.toCaseFolded(),new QImage(image),imageCost(image)); break;
        }
        float minU=0,maxU=0,minV=0,maxV=0;
        for(const auto& part:scene.parts) for(const auto& triangle:part.triangles) for(const auto& vertex:triangle.vertices)
        { minU=qMin(minU,vertex.uv.x());maxU=qMax(maxU,vertex.uv.x());minV=qMin(minV,vertex.uv.y());maxV=qMax(maxV,vertex.uv.y()); }
        return ei::renderModelGalleryThumbnail(scene,image,stem,
            ei::textureUvScaleForBounds(texture,minU,maxU,minV,maxV));
    };
}

void CModelGallery::showCatalogError(const QString& error) { m_status->setText(error); }

void CModelGallery::requireTextureSelection()
{
    m_textureRequired = true;
    m_choose->setEnabled(!selectedModel().isEmpty() && m_textures.contains(selectedTexture(),Qt::CaseInsensitive));
}
void CModelGallery::accept()
{
    if(m_multiSelection) {
        const auto entries=selectedEntries();
        if(entries.isEmpty() || entries.size()>512) {showCatalogError(QString::fromUtf8("Выберите от 1 до 512 моделей."));return;}
        for(const auto& entry:entries)if(!m_availableTextures.contains(entry.second.toCaseFolded())) {
            showCatalogError(QString::fromUtf8("У модели %1 не выбрана текстура. Выберите её перед добавлением.").arg(entry.first));return;
        }
    }
    if(m_textureRequired && (selectedModel().isEmpty() || !m_textures.contains(selectedTexture(),Qt::CaseInsensitive))) {
        showCatalogError(QString::fromUtf8("Выберите установленную основную текстуру для этой модели."));
        return;
    }
    QDialog::accept();
}

void CModelGallery::setCurrentTexture(const QString& texture)
{
    if(m_availableTextures.contains(texture.toCaseFolded())) {m_texture->setCurrentText(texture);m_thumbnailTimer.start();}
}
