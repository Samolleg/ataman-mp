#include "model_gallery.h"
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

namespace {
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
        else { painter->setPen(option.palette.color(QPalette::Mid)); painter->drawText(imageRect,Qt::AlignCenter,QStringLiteral("…")); }
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
    ThumbnailProvider provider,QWidget* parent):QDialog(parent),m_models(models),m_textures(textures),
    m_catalog(catalog),m_scan(scan),m_profile(profile),m_provider(std::move(provider))
{
    setObjectName("modelGallery"); setWindowTitle(tr("Model gallery")); resize(960,720);
    setMinimumSize(600,440); setWindowFlag(Qt::WindowContextHelpButtonHint,false);
    m_models.removeDuplicates(); m_models.sort(Qt::CaseInsensitive);
    auto* layout=new QVBoxLayout(this);
    m_search=new QLineEdit(this); m_search->setObjectName("modelGallerySearch");
    m_search->setPlaceholderText(tr("Search by model ID or Russian name")); layout->addWidget(m_search);
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
    auto* textureRow=new QHBoxLayout; textureRow->addWidget(new QLabel(tr("Primary texture:"),this));
    m_texture=new QComboBox(this); m_texture->setObjectName("modelGalleryTexture"); m_texture->setEditable(true);
    m_texture->setInsertPolicy(QComboBox::NoInsert); m_texture->setMinimumContentsLength(20);
    m_texture->completer()->setFilterMode(Qt::MatchContains); m_texture->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    textureRow->addWidget(m_texture,1);
    m_saveTexture=new QPushButton(tr("Bind texture"),this); m_saveTexture->setObjectName("modelGalleryBindTexture"); textureRow->addWidget(m_saveTexture); layout->addLayout(textureRow);
    m_status=new QLabel(this); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    auto* buttons=new QHBoxLayout; buttons->addStretch();
    m_choose=new QPushButton(tr("Choose model"),this); m_choose->setObjectName("modelGalleryChoose"); m_choose->setDefault(true); buttons->addWidget(m_choose);
    auto* cancel=new QPushButton(tr("Cancel"),this); buttons->addWidget(cancel); layout->addLayout(buttons);
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(m_choose,&QPushButton::clicked,this,[this]{ if(!selectedModel().isEmpty()) accept(); });
    connect(m_list,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem*){ accept(); });
    connect(m_list,&QListWidget::currentItemChanged,this,[this]{selectionChanged();});
    connect(m_search,&QLineEdit::textChanged,this,[this]{filter();});
    connect(m_saveName,&QPushButton::clicked,this,&CModelGallery::saveName);
    connect(m_saveTexture,&QPushButton::clicked,this,&CModelGallery::saveTexture);
    connect(m_texture,QOverload<int>::of(&QComboBox::activated),this,[this]{ if(m_list->currentItem()) {m_list->currentItem()->setIcon({}); m_thumbnailTimer.start();} });
    connect(m_list->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{m_thumbnailTimer.start();});
    connect(&m_thumbnailTimer,&QTimer::timeout,this,&CModelGallery::loadVisibleThumbnail);
    m_thumbnailTimer.setInterval(40); m_thumbnailTimer.setSingleShot(true);
    connect(&m_thumbnailWatcher,&QFutureWatcher<QImage>::finished,this,&CModelGallery::finishThumbnail);
    filter();
}
CModelGallery::~CModelGallery() { m_thumbnailTimer.stop(); m_thumbnailWatcher.waitForFinished(); }
void CModelGallery::resizeEvent(QResizeEvent* event) { QDialog::resizeEvent(event); m_thumbnailTimer.start(); }
QString CModelGallery::selectedModel() const { return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString(); }
QString CModelGallery::selectedTexture() const { return m_texture->currentText().trimmed(); }
QString CModelGallery::cacheKey(const QString& model,const QString& texture) const { return model.toLower()+'|'+texture.toLower(); }
QString CModelGallery::textureForModel(const QString& model) const
{
    if(model==selectedModel() && m_textures.contains(selectedTexture(),Qt::CaseInsensitive)) return selectedTexture();
    const auto choices=m_catalog->textureChoices(m_profile,model,m_textures,*m_scan);
    return choices.isEmpty() ? QString() : choices.first().name;
}
void CModelGallery::setCurrentModel(const QString& model)
{
    for(int i=0;i<m_list->count();++i)
        if(ei::TechnicalModelCatalog::modelKey(m_list->item(i)->data(Qt::UserRole).toString())==ei::TechnicalModelCatalog::modelKey(model)) {m_list->setCurrentRow(i);m_list->scrollToItem(m_list->item(i));break;}
}
void CModelGallery::filter()
{
    const QString previous=selectedModel(); const QSignalBlocker block(m_list);
    m_list->clear();
    for(const auto& model:m_models)
    {
        const QString russian=m_catalog->russianName(model);
        if(!ei::TechnicalModelCatalog::matches(model,russian,m_search->text())) continue;
        auto* item=new QListWidgetItem(m_list); item->setData(Qt::UserRole,model);
        item->setData(Qt::UserRole+1,russian.isEmpty()?tr("Russian name not set"):russian);
        item->setToolTip(model+"\n"+russian); item->setSizeHint(QSize(208,192));
        if(model==previous) m_list->setCurrentItem(item);
    }
    if(!m_list->currentItem() && m_list->count()) m_list->setCurrentRow(0);
    selectionChanged(); m_thumbnailTimer.start();
}
void CModelGallery::selectionChanged()
{
    const QString model=selectedModel(); m_name->setText(m_catalog->russianName(model));
    m_choose->setEnabled(!model.isEmpty());
    m_saveName->setEnabled(!model.isEmpty() && m_catalog->writable());
    m_saveTexture->setEnabled(!model.isEmpty() && m_catalog->writable());
    const QSignalBlocker block(m_texture); m_texture->clear();
    const auto exact=m_catalog->textureChoices(m_profile,model,m_textures,*m_scan);
    QStringList names;
    for(const auto& choice:exact) names.append(choice.name);
    for(const auto& name:m_textures) if(!names.contains(name,Qt::CaseInsensitive)) names.append(name);
    m_texture->addItems(names); if(exact.isEmpty()) m_texture->setCurrentIndex(-1);
    m_status->setText(tr("%1 model(s). Names and texture bindings: %2").arg(m_list->count()).arg(m_catalog->path()));
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
    if(saved) filter(); reportSave(saved,error);
}
void CModelGallery::saveTexture()
{
    const QString model=selectedModel(),texture=selectedTexture();
    if(model.isEmpty() || !m_textures.contains(texture,Qt::CaseInsensitive)) return;
    m_catalog->bindTexture(m_profile,model,texture); QString error;
    reportSave(m_catalog->save(&error),error);
    if(m_list->currentItem()) m_list->currentItem()->setIcon(QIcon());
    m_thumbnailTimer.start();
}
void CModelGallery::loadVisibleThumbnail()
{
    if(!isVisible() || m_thumbnailWatcher.isRunning() || !m_provider) return;
    for(int i=0;i<m_list->count();++i)
    {
        auto* item=m_list->item(i);
        if(!m_list->visualItemRect(item).intersects(m_list->viewport()->rect())) continue;
        const QString model=item->data(Qt::UserRole).toString(),texture=textureForModel(model),key=cacheKey(model,texture);
        if(const QImage* cached=m_thumbnails.object(key))
        { if(!cached->isNull()) item->setIcon(QPixmap::fromImage(*cached)); else item->setToolTip(model+"\n"+tr("Preview unavailable")); continue; }
        m_loadingKey=key; m_loadingModel=model;
        const auto provider=m_provider;
        m_thumbnailWatcher.setFuture(QtConcurrent::run([provider,model,texture]{return provider(model,texture);}));
        return;
    }
}
void CModelGallery::finishThumbnail()
{
    const QImage image=m_thumbnailWatcher.result(); m_thumbnails.insert(m_loadingKey,new QImage(image));
    for(int i=0;i<m_list->count();++i)
    {
        auto* item=m_list->item(i); const QString model=item->data(Qt::UserRole).toString();
        if(model==m_loadingModel && cacheKey(model,textureForModel(model))==m_loadingKey && !image.isNull()) item->setIcon(QPixmap::fromImage(image));
    }
    m_thumbnailTimer.start();
}
