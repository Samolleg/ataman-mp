#include "assembly_gallery.h"
#include "model_catalog_icons.h"
#include "window_geometry.h"
#include "resource_catalog.h"
#include "resource_name.h"
#include "resourcemanager.h"
#include "model_gallery_thumbnail.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QSettings>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QTabBar>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QQuaternion>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStyle>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
namespace {
QString key(const ei::ObjectAssembly& assembly) {
    // Hash only visual inputs, never megabytes of full raw MOB templates on scroll.
    QJsonArray visuals;
    for(const auto& p:assembly.parts){auto wo=ei::assemblyWorldObject(p.object);auto base=wo.value("Base object").toObject();visuals.append(QJsonObject{
        {"model",wo.value("Model name")},{"texture",wo.value("Primary texture")},{"parts",wo.value("Object parts")},
        {"rotation",base.value("Rotation")},{"offset",QJsonArray{p.offset.x(),p.offset.y(),p.offset.z()}}});}
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(visuals).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
QStringList categories(){return {QString::fromUtf8("Дома"),QString::fromUtf8("Архитектура"),QString::fromUtf8("Ворота"),QString::fromUtf8("Утварь"),QString::fromUtf8("Деревья"),QString::fromUtf8("Кусты"),QString::fromUtf8("Рычаги и сундуки"),QString::fromUtf8("Декор")};}
QStringList regions(){return {QString::fromUtf8("Гипат"),QString::fromUtf8("Ингос"),QString::fromUtf8("Суслангер")};}
class TileDelegate : public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p,const QStyleOptionViewItem& o,const QModelIndex& i) const override {
        p->save();const bool selected=o.state&QStyle::State_Selected;
        p->fillRect(o.rect.adjusted(2,2,-2,-2),selected?o.palette.highlight():o.palette.alternateBase());
        auto area=o.rect.adjusted(10,8,-10,-60);auto icon=qvariant_cast<QIcon>(i.data(Qt::DecorationRole));
        p->setPen(selected?o.palette.highlightedText().color():o.palette.text().color());
        if(icon.isNull())p->drawText(area,Qt::AlignCenter,QString::fromUtf8("Предпросмотр…"));else icon.paint(p,area);
        QRect title(o.rect.x()+10,o.rect.bottom()-51,o.rect.width()-20,24);
        p->drawText(title,Qt::AlignCenter,o.fontMetrics.elidedText(i.data(Qt::DisplayRole).toString(),Qt::ElideRight,title.width()));
        p->drawText(title.translated(0,23),Qt::AlignCenter,QString::fromUtf8("Деталей: %1").arg(i.data(Qt::UserRole+1).toInt()));p->restore();
    }
};
struct Sources {CResourceCatalog catalog;bool indexed=false;QStringList paths;QCache<QString,ei::SResourceModelScene> scenes{16*1024};QCache<QString,QImage> textures{32*1024};};
auto renderer(const QStringList& paths)->std::function<QImage(const ei::ObjectAssembly&,const std::shared_ptr<std::atomic_bool>&)> {
    auto source=std::make_shared<Sources>();source->paths=paths;
    return [source](const ei::ObjectAssembly& assembly,const std::shared_ptr<std::atomic_bool>& cancel) {
        if(!source->indexed){source->catalog.build(source->paths.toVector());source->indexed=true;}
        auto read=[&](const QString& logical){auto providers=source->catalog.providers(logical);if(providers.isEmpty() || providers.first().size>64*1024*1024)return QByteArray();auto* archive=source->catalog.archive(providers.first().archivePriority);return archive?archive->readMember(providers.first().memberIndex):QByteArray();};
        QVector<ei::SGalleryTriangle> triangles;QVector<QImage> textures;
        for(const auto& part:assembly.parts) {
            if(cancel->load())return QImage();const auto wo=ei::assemblyWorldObject(part.object);const auto base=wo.value("Base object").toObject();
            const auto model=ei::TechnicalModelCatalog::modelKey(wo.value("Model name").toString());if(model.isEmpty())continue;
            ei::SResourceModelScene scene;
            if(auto* cached=source->scenes.object(model))scene=*cached;
            else {
                auto mod=read(model+".mod");scene=mod.isEmpty()?ei::buildFig8Scene(read(model+".fig")):ei::buildModScene(mod,read(model+".bon"));
                qint64 bytes=sizeof(scene)+qint64(scene.parts.size())*2048;for(const auto& p:scene.parts)bytes+=qint64(p.triangles.capacity())*sizeof(ei::SResourceModelTriangle);
                source->scenes.insert(model,new ei::SResourceModelScene(scene),int(qMin<qint64>(2147483647,(bytes+1023)/1024)));
            }
            const QString texture=wo.value("Primary texture").toString();QImage image;
            for(const auto& candidate:ei::textureResourceCandidates(texture,model)) {
                if(auto* cached=source->textures.object(candidate.toCaseFolded())){image=*cached;break;}
                auto bytes=read(candidate);if(bytes.isEmpty())continue;image=CTextureList::prepareStaticTexture(bytes).image;
                source->textures.insert(candidate.toCaseFolded(),new QImage(image),qMax(1,int((qint64(image.bytesPerLine())*image.height()+1023)/1024)));break;
            }
            const int textureIndex=textures.size();textures.append(image);
            const auto r=base.value("Rotation").toArray();const QQuaternion rotation(float(r[3].toDouble()),float(r[0].toDouble()),float(r[1].toDouble()),float(r[2].toDouble()));
            auto visible=ei::defaultVisibleModelParts(scene,model);const auto authored=wo.value("Object parts").toArray();
            if(!authored.isEmpty()){visible.clear();for(const auto& v:authored)visible.insert(v.toString().toLower());}
            float minU=0,maxU=0,minV=0,maxV=0;for(const auto& p:scene.parts)for(const auto& t:p.triangles)for(const auto& v:t.vertices){minU=qMin(minU,v.uv.x());maxU=qMax(maxU,v.uv.x());minV=qMin(minV,v.uv.y());maxV=qMax(maxV,v.uv.y());}
            for(const auto& p:scene.parts)if(visible.contains(p.name.toLower()))for(const auto& triangle:p.triangles) {
                if(cancel->load() || triangles.size()>=60000)return QImage();
                ei::SGalleryTriangle entry;entry.triangle=triangle;entry.textureIndex=textureIndex;
                entry.uvScale=ei::textureUvScaleForBounds(texture,minU,maxU,minV,maxV);
                for(auto& v:entry.triangle.vertices)v.position=part.offset+rotation.rotatedVector(v.position);
                triangles.append(entry);
            }
        }
        if(cancel->load())return QImage();return ei::renderGalleryTriangles(triangles,textures,QSize(240,156),cancel.get());
    };
}
}
CAssemblyGallery::CAssemblyGallery(ei::TechnicalModelCatalog* catalog,const QStringList& resources,QWidget* parent,Capture capture):QDialog(parent),m_catalog(catalog),m_capture(capture) {
    setObjectName("assemblyGallery");setWindowTitle(QString::fromUtf8("Каталог — готовые сборки"));resize(1180,790);setMinimumSize(700,480);setWindowFlag(Qt::WindowContextHelpButtonHint,false);
    ei::rememberWindowGeometry(this,"assemblyGallery");m_cancel=std::make_shared<std::atomic_bool>(false);m_renderer=renderer(resources);
    auto* root=new QVBoxLayout(this);m_search=new QLineEdit(this);m_search->setObjectName("assemblySearch");m_search->setPlaceholderText(QString::fromUtf8("Поиск по названию сборки, моделям, русским названиям и текстурам"));root->addWidget(m_search);
    m_categories=new QTabBar(this);m_categories->setObjectName("assemblyCategories");m_categories->setExpanding(false);m_categories->addTab(QString::fromUtf8("Все сборки"));
    const auto ids=ei::TechnicalModelCatalog::categoryIds();const auto labels=categories();for(int i=0;i<ids.size();++i){int index=m_categories->addTab(ei::modelCatalogCategoryIcon(i),labels[i]);m_categories->setTabData(index,ids[i]);}root->addWidget(m_categories);
    m_regions=new QTabBar(this);m_regions->setObjectName("assemblyRegions");m_regions->setExpanding(false);m_regions->addTab(QString::fromUtf8("Все острова"));const auto islands=ei::TechnicalModelCatalog::regionIds();const auto names=regions();for(int i=0;i<islands.size();++i){int index=m_regions->addTab(names[i]);m_regions->setTabData(index,islands[i]);}root->addWidget(m_regions);
    auto* split=new QSplitter(this);split->setObjectName("assemblySplitter");split->setChildrenCollapsible(false);
    m_tiles=new QListWidget(split);m_tiles->setObjectName("assemblyTiles");m_tiles->setViewMode(QListView::IconMode);m_tiles->setResizeMode(QListView::Adjust);m_tiles->setMovement(QListView::Static);
    m_tiles->setGridSize(QSize(264,220));m_tiles->setIconSize(QSize(240,156));m_tiles->setUniformItemSizes(true);m_tiles->setItemDelegate(new TileDelegate(m_tiles));
    m_parts=new QListWidget(split);m_parts->setObjectName("assemblyParts");m_parts->setMinimumWidth(180);split->setSizes({830,260});
    split->restoreState(QSettings().value("windows/assemblyGallery/splitter").toByteArray());
    connect(split,&QSplitter::splitterMoved,this,[split]{QSettings().setValue("windows/assemblyGallery/splitter",split->saveState());});root->addWidget(split,1);
    auto* row=new QHBoxLayout;m_name=new QLineEdit(this);m_name->setObjectName("assemblyName");row->addWidget(new QLabel(QString::fromUtf8("Название:"),this));row->addWidget(m_name,1);
    m_save=new QPushButton(QString::fromUtf8("Сохранить описание"),this);m_save->setObjectName("assemblySave");row->addWidget(m_save);root->addLayout(row);
    row=new QHBoxLayout;m_category=new QComboBox(this);m_category->setObjectName("assemblyCategory");m_category->addItem(QString::fromUtf8("Без категории"),QString());for(int i=0;i<ids.size();++i)m_category->addItem(ei::modelCatalogCategoryIcon(i),labels[i],ids[i]);
    m_region=new QComboBox(this);m_region->setObjectName("assemblyRegion");m_region->addItem(QString::fromUtf8("Не выбран"),QString());for(int i=0;i<islands.size();++i)m_region->addItem(names[i],islands[i]);
    row->addWidget(new QLabel(QString::fromUtf8("Категория:"),this));row->addWidget(m_category,1);row->addWidget(new QLabel(QString::fromUtf8("Остров:"),this));row->addWidget(m_region,1);root->addLayout(row);
    m_status=new QLabel(this);m_status->setWordWrap(true);m_status->setTextFormat(Qt::PlainText);root->addWidget(m_status);
    auto* help=new QLabel(QString::fromUtf8("Выберите сборку → Разместить → ЛКМ на карте. Esc — отмена. Выбор одной детали выбирает сборку целиком. «Разобрать» — редактировать детали; «Заменить состав» — обновить запись из текущего выделения. После расстановки сохраните MOB для сохранения группировки."),this);help->setWordWrap(true);root->addWidget(help);
    row=new QHBoxLayout;m_replace=new QPushButton(QString::fromUtf8("Заменить состав из выделения"),this);m_replace->setObjectName("assemblyReplace");m_replace->setEnabled(bool(m_capture));row->addWidget(m_replace);
    m_remove=new QPushButton(QString::fromUtf8("Удалить из каталога"),this);m_remove->setObjectName("assemblyRemove");row->addWidget(m_remove);row->addStretch();m_place=new QPushButton(QString::fromUtf8("Разместить сборку"),this);m_place->setObjectName("assemblyPlace");row->addWidget(m_place);
    auto* close=new QPushButton(QString::fromUtf8("Закрыть"),this);row->addWidget(close);root->addLayout(row);
    connect(close,&QPushButton::clicked,this,&QDialog::reject);connect(m_place,&QPushButton::clicked,this,[this]{if(m_placementAvailable && !selectedAssembly().id.isEmpty())accept();});
    connect(m_save,&QPushButton::clicked,this,&CAssemblyGallery::save);connect(m_remove,&QPushButton::clicked,this,&CAssemblyGallery::remove);connect(m_replace,&QPushButton::clicked,this,&CAssemblyGallery::replaceComposition);
    connect(m_search,&QLineEdit::textChanged,this,[this]{rebuild();});connect(m_categories,&QTabBar::currentChanged,this,[this]{rebuild();});connect(m_regions,&QTabBar::currentChanged,this,[this]{rebuild();});connect(m_tiles,&QListWidget::currentRowChanged,this,[this]{select();});
    connect(m_tiles,&QListWidget::itemDoubleClicked,this,[this]{m_place->click();});connect(m_tiles->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{m_timer.start();});
    m_timer.setSingleShot(true);m_timer.setInterval(0);connect(&m_timer,&QTimer::timeout,this,&CAssemblyGallery::loadVisible);connect(&m_watcher,&QFutureWatcher<QImage>::finished,this,&CAssemblyGallery::finish);rebuild();
}
CAssemblyGallery::~CAssemblyGallery(){m_timer.stop();m_cancel->store(true);}
void CAssemblyGallery::resizeEvent(QResizeEvent* event){QDialog::resizeEvent(event);m_timer.start();}
ei::ObjectAssembly CAssemblyGallery::selectedAssembly() const {auto* item=m_tiles->currentItem();if(item)for(const auto& a:m_entries)if(a.id==item->data(Qt::UserRole).toString())return a;return {};}
void CAssemblyGallery::setPlacementAvailable(bool value){m_placementAvailable=value;select();}
void CAssemblyGallery::rebuild(const QString& preferred) {
    const QString selected=preferred.isEmpty()?selectedAssembly().id:preferred;QString error;
    if(m_loadedRevision!=m_catalog->revision()){m_entries=m_catalog->assemblies(&error);m_loadedRevision=m_catalog->revision();}
    std::sort(m_entries.begin(),m_entries.end(),[](const ei::ObjectAssembly& a,const ei::ObjectAssembly& b){return QString::localeAwareCompare(a.name,b.name)<0;});
    m_tiles->clear();const QString category=m_categories->tabData(m_categories->currentIndex()).toString(),region=m_regions->tabData(m_regions->currentIndex()).toString();
    for(const auto& a:m_entries) {
        if((!category.isEmpty() && a.category!=category) || (!region.isEmpty() && a.region!=region))continue;
        QString searchable=a.name;for(const auto& part:a.parts){const auto wo=ei::assemblyWorldObject(part.object);const auto model=wo.value("Model name").toString();searchable+=' '+model+' '+m_catalog->russianName(model)+' '+wo.value("Primary texture").toString();}
        if(!ei::TechnicalModelCatalog::matches(searchable,{},m_search->text()))continue;
        auto* item=new QListWidgetItem(a.name,m_tiles);item->setSizeHint(QSize(264,220));item->setData(Qt::UserRole,a.id);item->setData(Qt::UserRole+1,a.parts.size());item->setToolTip(searchable);
        if(auto* image=m_images.object(key(a)))item->setIcon(QPixmap::fromImage(*image));
        if(a.id==selected)m_tiles->setCurrentItem(item);
    }
    m_status->setText(error.isEmpty()?QString::fromUtf8("Сборок: %1. Каталог: %2").arg(m_tiles->count()).arg(m_catalog->path()):error);
    if(!m_tiles->currentItem() && m_tiles->count())m_tiles->setCurrentRow(0);select();m_timer.start();
}
void CAssemblyGallery::select() {
    auto a=selectedAssembly();const bool exists=!a.id.isEmpty();m_place->setEnabled(exists && m_placementAvailable);m_save->setEnabled(exists);m_remove->setEnabled(exists);m_replace->setEnabled(exists && bool(m_capture));
    m_name->setText(a.name);m_category->setCurrentIndex(qMax(0,m_category->findData(a.category)));m_region->setCurrentIndex(qMax(0,m_region->findData(a.region)));m_parts->clear();
    for(const auto& p:a.parts){auto wo=ei::assemblyWorldObject(p.object);auto model=wo.value("Model name").toString();const auto alias=m_catalog->russianName(model);auto* item=new QListWidgetItem(model+(alias.isEmpty()?QString():" — "+alias)+"\n"+wo.value("Primary texture").toString(),m_parts);item->setToolTip(QString("X: %1 Y: %2 Z: %3").arg(p.offset.x()).arg(p.offset.y()).arg(p.offset.z()));}
}
void CAssemblyGallery::save() {
    auto a=selectedAssembly();if(a.id.isEmpty())return;a.name=m_name->text().trimmed();a.category=m_category->currentData().toString();a.region=a.category.isEmpty()?QString():m_region->currentData().toString();
    auto previous=*m_catalog;QString error;if(!m_catalog->setAssembly(a,&error) || !m_catalog->save(&error)){*m_catalog=previous;m_status->setText(error);return;}rebuild(a.id);
}
void CAssemblyGallery::remove() {
    auto a=selectedAssembly();if(a.id.isEmpty())return;
    if(QMessageBox::question(this,QString::fromUtf8("Удалить сборку из каталога"),a.name+QString::fromUtf8("? Уже размещённые объекты останутся на карте."))!=QMessageBox::Yes)return;
    auto previous=*m_catalog;QString error;if(!m_catalog->removeAssembly(a.id) || !m_catalog->save(&error)){*m_catalog=previous;m_status->setText(error);return;}rebuild();
}
void CAssemblyGallery::replaceComposition() {
    auto old=selectedAssembly();if(!m_capture || old.id.isEmpty())return;
    if(QMessageBox::question(this,QString::fromUtf8("Заменить состав"),QString::fromUtf8("Заменить детали «%1» текущим выделением? Существующие экземпляры останутся прежними.").arg(old.name))!=QMessageBox::Yes)return;
    ei::ObjectAssembly a;QString error;if(!m_capture(old.name,&a,&error)){m_status->setText(error);return;}
    a.id=old.id;a.category=old.category;a.region=old.region;a.extra=old.extra;auto previous=*m_catalog;
    if(!m_catalog->setAssembly(a,&error) || !m_catalog->save(&error)){*m_catalog=previous;m_status->setText(error);return;}rebuild(a.id);
}
void CAssemblyGallery::loadVisible() {
    if(m_jobActive)return;
    const QRect visible=m_tiles->viewport()->rect();for(int i=0;i<m_tiles->count();++i){auto* item=m_tiles->item(i);if(!m_tiles->visualItemRect(item).intersects(visible))continue;
        for(const auto& a:m_entries)if(a.id==item->data(Qt::UserRole).toString()) {
            const auto cacheKey=key(a);if(m_images.object(cacheKey))continue;m_loadingId=a.id;m_loadingKey=cacheKey;m_jobActive=true;
            auto draw=m_renderer;auto cancel=m_cancel;m_watcher.setFuture(QtConcurrent::run([draw,a,cancel]{return draw(a,cancel);}));return;
        }
    }
}
void CAssemblyGallery::finish() {
    const auto image=m_watcher.result();m_jobActive=false;m_images.insert(m_loadingKey,new QImage(image),qMax(1,int((qint64(image.bytesPerLine())*image.height()+1023)/1024)));
    for(int i=0;i<m_tiles->count();++i){auto* item=m_tiles->item(i);if(item->data(Qt::UserRole).toString()!=m_loadingId)continue;
        for(const auto& a:m_entries)if(a.id==m_loadingId && key(a)==m_loadingKey){if(!image.isNull())item->setIcon(QPixmap::fromImage(image));else {item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));item->setToolTip(item->toolTip()+QString::fromUtf8("\nПредпросмотр недоступен или превышает 60000 треугольников. Состав сохранён полностью."));}}
    }m_timer.start();
}
