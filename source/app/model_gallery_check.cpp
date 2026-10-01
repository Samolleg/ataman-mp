#include "model_gallery_check.h"
#include "model_gallery.h"
#include <QTemporaryDir>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QApplication>
#include <QKeyEvent>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTabBar>
#include <QSet>
#include <QCompleter>
#include <QScrollBar>
#include <QFile>
#include <atomic>

bool runModelGalleryWidgetCheck(QString* diagnostic)
{
    const auto fail=[diagnostic](const QString& message){if(diagnostic)*diagnostic=message;return false;};
    QTemporaryDir temp; if(!temp.isValid()) return fail("temporary directory");
    ei::TechnicalModelCatalog catalog; QString error;
    if(!catalog.load(temp.filePath("model-catalog.json"),&error)) return fail(error);
    QStringList models={"house","bridge"};
    for(int i=0;i<250;++i) models.append(QString("model%1").arg(i));
    catalog.registerModels(models); catalog.setRussianName("house",QString::fromUtf8("Домик"));
    catalog.bindTexture("test","house","stone"); if(!catalog.save(&error)) return fail(error);
    ei::TechnicalTextureCatalog scan;
    std::atomic<int> previews{0};
    CModelGallery gallery(models,&catalog,"test",{"stone","wood"},&scan,
        [&previews](const QString&,const QString&){++previews;QImage image(192,132,QImage::Format_ARGB32);image.fill(Qt::red);return image;});
    gallery.setAttribute(Qt::WA_DontShowOnScreen); gallery.show();
    auto* list=gallery.findChild<QListWidget*>("modelGalleryTiles");
    auto* search=gallery.findChild<QLineEdit*>("modelGallerySearch");
    auto* name=gallery.findChild<QLineEdit*>("modelGalleryRussianName");
    auto* save=gallery.findChild<QPushButton*>("modelGallerySaveName");
    auto* bind=gallery.findChild<QPushButton*>("modelGalleryBindTexture");
    auto* texture=gallery.findChild<QComboBox*>("modelGalleryTexture");
    auto* choose=gallery.findChild<QPushButton*>("modelGalleryChoose");
    auto* categories=gallery.findChild<QTabBar*>("modelGalleryCategories");
    auto* regions=gallery.findChild<QTabBar*>("modelGalleryRegions");
    auto* categoryChoice=gallery.findChild<QComboBox*>("modelGalleryCategoryChoice");
    auto* regionChoice=gallery.findChild<QComboBox*>("modelGalleryRegionChoice");
    auto* savePlacement=gallery.findChild<QPushButton*>("modelGallerySavePlacement");
    if(!list||!search||!name||!save||!bind||!texture||!choose||!categories||!regions||!categoryChoice||!regionChoice||!savePlacement) return fail("production controls");
    if(categories->count()!=9 || regions->count()!=3 || list->count()!=models.size()) return fail("sections and legacy unassigned models");
    QSet<qint64> categoryIcons;
    for(int i=1;i<categories->count();++i) {
        if(categories->tabData(i).toString()!=ei::TechnicalModelCatalog::categoryIds()[i-1] || categories->tabIcon(i).isNull()) return fail("category/icon identity");
        categoryIcons.insert(categories->tabIcon(i).cacheKey());
        categories->setCurrentIndex(i);
        if(regions->isHidden()) return fail("nested island tabs");
        for(int r=0;r<3;++r) {regions->setCurrentIndex(r);if(list->count()!=0) return fail("unassigned model leaked into an island");}
    }
    if(categoryIcons.size()!=8) return fail("distinct category icons");
    categories->setCurrentIndex(0);
    for(int i=0;i<list->count();++i) if(list->item(i)->data(Qt::UserRole).toString()=="bridge" && !list->item(i)->data(Qt::UserRole+1).toString().isEmpty()) return fail("empty Russian subtitle placeholder");
    QElapsedTimer clock; clock.start();
    while(clock.elapsed()<800 && previews.load()<2) {QApplication::processEvents(QEventLoop::AllEvents,20);}
    if(previews.load()==0 || previews.load()>=models.size()) return fail("visible-only thumbnails");
    search->setText(QString::fromUtf8("ДОМИК house"));
    if(list->count()!=1 || gallery.selectedModel()!="house") return fail("Russian/technical search");
    name->setText(QString::fromUtf8("Каменный дом")); save->click();
    search->setText(QString::fromUtf8("КАМЕННЫЙ house"));
    if(list->count()!=1 || gallery.selectedModel()!="house") return fail("renamed search");
    texture->setCurrentText("wood"); bind->click();
    categoryChoice->setCurrentIndex(categoryChoice->findData("houses"));
    regionChoice->setCurrentIndex(regionChoice->findData("gipat")); savePlacement->click();
    if(categories->tabData(categories->currentIndex()).toString()!="houses" || regions->currentIndex()!=0 || list->count()!=1 || gallery.selectedModel()!="house") return fail("assign model to section/island");
    regions->setCurrentIndex(1); if(list->count()!=0) return fail("island isolation");
    regions->setCurrentIndex(0);
    regionChoice->setCurrentIndex(regionChoice->findData("ingos")); savePlacement->click();
    if(regions->currentIndex()!=1 || gallery.selectedModel()!="house") return fail("move model between islands");
    categoryChoice->setCurrentIndex(0); savePlacement->click();
    if(categories->currentIndex()!=0 || gallery.selectedModel()!="house" || !catalog.category("house").isEmpty()) return fail("unassign model");
    categoryChoice->setCurrentIndex(categoryChoice->findData("decor"));
    regionChoice->setCurrentIndex(regionChoice->findData("suslanger")); savePlacement->click();
    texture->setCurrentText("wood");
    const QString screenshot=qEnvironmentVariable("EI_MODEL_GALLERY_CHECK_SCREENSHOT");
    if(!screenshot.isEmpty() && !gallery.grab().save(screenshot)) return fail("gallery screenshot");
    choose->click();
    if(gallery.result()!=QDialog::Accepted || gallery.selectedModel()!="house" || gallery.selectedTexture()!="wood") return fail("model/texture choice");
    ei::TechnicalModelCatalog reopened;
    if(!reopened.load(catalog.path(),&error) || reopened.russianName("house")!=QString::fromUtf8("Каменный дом")) return fail("name persistence");
    const auto choices=reopened.textureChoices("test","house",{"wood"},scan);
    if(choices.size()!=1 || choices.first().name!="wood") return fail("texture persistence");
    if(reopened.category("house")!="decor" || reopened.region("house")!="suslanger") return fail("section persistence");
    CModelGallery again(models,&reopened,"test",{"wood"},&scan,{},nullptr);
    auto* reopenedCategories=again.findChild<QTabBar*>("modelGalleryCategories");
    auto* reopenedRegions=again.findChild<QTabBar*>("modelGalleryRegions");
    reopenedCategories->setCurrentIndex(8); reopenedRegions->setCurrentIndex(2);
    if(again.selectedModel()!="house") return fail("reopened section filtering");
    CModelGallery cancel(models,&catalog,"test",{},&scan,{},nullptr); cancel.setAttribute(Qt::WA_DontShowOnScreen); cancel.show();
    bool rejected=false; QObject::connect(&cancel,&QDialog::rejected,[&rejected]{rejected=true;});
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QApplication::sendEvent(&cancel,&escape);
    if(!rejected) return fail("Escape cancellation");
    // The real profile has thousands of textures. The small functional fixture
    // above cannot detect per-row updates of an attached completer.
    QStringList manyModels={"house","bridge"}, manyTextures={"stone","wood"};
    for(int i=0;i<495;++i) manyModels.append(QString("asset%1").arg(i));
    for(int i=0;i<7163;++i) manyTextures.append(QString("texture%1").arg(i));
    clock.restart();
    CModelGallery large(manyModels,&catalog,"test",manyTextures,&scan,{},nullptr);
    const qint64 openMs=clock.elapsed();
    clock.restart(); large.setCurrentModel("house"); const qint64 selectMs=clock.elapsed();
    if(openMs>=100 || selectMs>=100) return fail(QString("full catalog responsiveness: open=%1 ms select=%2 ms (limit100)").arg(openMs).arg(selectMs));
    auto* largeTexture=large.findChild<QComboBox*>("modelGalleryTexture");
    if(!largeTexture || largeTexture->count()!=manyTextures.size()+3 || largeTexture->findData("texture7162")<0 || !largeTexture->completer()) return fail("full texture list/completer preserved");
    auto* largeSearch=large.findChild<QLineEdit*>("modelGallerySearch");
    clock.restart(); largeSearch->setText(QString::fromUtf8("Каменный house"));
    if(clock.elapsed()>=100 || large.selectedModel()!="house") return fail("full catalog Russian search responsiveness");
    largeSearch->clear();
    if(largeTexture->model()->flags(largeTexture->model()->index(0,0))!=Qt::NoItemFlags
        || largeTexture->findData("stone")<0 || largeTexture->findData("wood")<0) return fail("non-selectable texture sections and exact choices");
    largeTexture->completer()->setCompletionPrefix("texture7162");
    if(largeTexture->completer()->completionModel()->rowCount()!=1
        || largeTexture->completer()->completionModel()->index(0,0).data().toString()!="texture7162") return fail("full-list completion content");
    // The two production entry points provide the initial model/texture before
    // the first population. Shared preview keys must include the resources.
    std::atomic<int> sharedCalls{0};
    const auto fastProvider=[&sharedCalls](const QString&,const QString& tex){++sharedCalls;QImage image(192,132,QImage::Format_ARGB32);image.fill(tex=="wood"?Qt::red:Qt::blue);return image;};
    // A worker may finish before its queued GUI notification. A resize/scroll
    // timer in that interval must not replace its future or submit it twice.
    {
        std::atomic<int> jobs{0};
        CModelGallery notification({"a","b"},&catalog,"test",{},&scan,
            [&jobs](const QString&,const QString&){++jobs;QImage image(192,132,QImage::Format_ARGB32);image.fill(Qt::red);return image;});
        notification.setAttribute(Qt::WA_DontShowOnScreen);notification.show();
        notification.m_thumbnailTimer.stop();notification.loadVisibleThumbnail();
        notification.m_thumbnailWatcher.waitForFinished();
        notification.loadVisibleThumbnail();notification.m_thumbnailWatcher.waitForFinished();
        if(jobs.load()!=1) return fail("finished worker must await GUI result notification without resubmission");
    }
    const auto ready=[&](CModelGallery& dialog,int limit) {
        auto* tiles=dialog.findChild<QListWidget*>("modelGalleryTiles");
        QEventLoop loop; QTimer deadline,probe; bool complete=false;
        const auto inspect=[&] {
            complete=true;
            for(int i=0;i<tiles->count();++i) if(tiles->visualItemRect(tiles->item(i)).intersects(tiles->viewport()->rect()) && tiles->item(i)->icon().isNull()) complete=false;
            if(complete) loop.quit();
        };
        // Exercise the normal queued event loop, including completion ordering;
        // a busy processEvents loop can repeatedly repaint other UI fixtures.
        deadline.setSingleShot(true);probe.setTimerType(Qt::PreciseTimer);probe.setInterval(5);
        QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
        QObject::connect(&probe,&QTimer::timeout,&loop,inspect);
        const auto finished=QObject::connect(&dialog.m_thumbnailWatcher,&QFutureWatcher<QImage>::finished,&loop,inspect);
        deadline.start(limit);probe.start();loop.exec();QObject::disconnect(finished);
        return complete;
    };
    const QString space=temp.path()+"/resources-1";
    const QString resourcePath=temp.filePath("preview-resource.res");
    QFile resource(resourcePath);
    if(!resource.open(QIODevice::WriteOnly) || resource.write("first")!=5) return fail("resource identity fixture");
    resource.close();
    const QString firstIdentity=CModelGallery::resourceThumbnailNamespace({resourcePath});
    if(firstIdentity!=CModelGallery::resourceThumbnailNamespace({resourcePath})) return fail("stable resource identity");
    if(!resource.open(QIODevice::WriteOnly|QIODevice::Truncate) || resource.write("replacement")!=11) return fail("changed resource identity fixture");
    resource.close();
    if(firstIdentity==CModelGallery::resourceThumbnailNamespace({resourcePath})) return fail("resource metadata invalidates identity");
    {
        CModelGallery cold(manyModels,&catalog,"test",manyTextures,&scan,fastProvider,nullptr,"house","wood",space);
        if(cold.selectedModel()!="house" || cold.selectedTexture()!="wood") return fail("initial model and explicit texture");
        cold.setAttribute(Qt::WA_DontShowOnScreen);cold.show();clock.restart();
        if(!ready(cold,300)) return fail("visible thumbnail queue responsiveness (limit300ms)");
        auto* tiles=cold.findChild<QListWidget*>("modelGalleryTiles");
        if(sharedCalls.load()==0 || sharedCalls.load()>=manyModels.size()) return fail("full-profile visible-only jobs");
        const int before=sharedCalls.load();
        tiles->verticalScrollBar()->setValue(0);
        if(!ready(cold,300) || sharedCalls.load()<=before) return fail("new viewport thumbnails");
        tiles->scrollToItem(tiles->currentItem());
        const int back=sharedCalls.load();
        if(!ready(cold,300) || sharedCalls.load()!=back) return fail("return to cached viewport");
    }
    {
        const int before=sharedCalls.load();
        CModelGallery warm(manyModels,&catalog,"test",manyTextures,&scan,fastProvider,nullptr,"house","wood",space);
        warm.setAttribute(Qt::WA_DontShowOnScreen);warm.show();
        if(!ready(warm,300) || sharedCalls.load()!=before) return fail("preview cache survives gallery reopening");
        warm.setCurrentTexture("stone");
        if(!ready(warm,300)) return fail("changed texture preview");
        // ready() may observe the old icon until the queued job finishes. Wait
        // for the actual new cache key before examining the displayed result.
        auto* tiles=warm.findChild<QListWidget*>("modelGalleryTiles");clock.restart();
        while(tiles->currentItem()->data(Qt::UserRole+2).toString()!="house|stone" && clock.elapsed()<300) QApplication::processEvents(QEventLoop::AllEvents,10);
        if(tiles->currentItem()->data(Qt::UserRole+2).toString()!="house|stone"
            || tiles->currentItem()->icon().pixmap(192,132).toImage().pixelColor(0,0)!=QColor(Qt::blue)) return fail("texture change invalidates tile identity");
    }
    {
        const int before=sharedCalls.load();
        CModelGallery changed(manyModels,&catalog,"test",manyTextures,&scan,fastProvider,nullptr,"house","wood",space+"-changed");
        changed.setAttribute(Qt::WA_DontShowOnScreen);changed.show();
        if(!ready(changed,300) || sharedCalls.load()<=before) return fail("resource generation invalidates shared previews");
    }
    catalog.bindTexture("test","asset0","texture99");
    large.setCurrentModel("asset0");
    if(large.selectedTexture()!="texture99") return fail("catalog revision invalidates cached texture choices");
    if(diagnostic) *diagnostic=QString("functional sections/search/persistence/Escape; 497 models/7165 textures: open=%1ms select=%2ms; groups/completion, initial selection, visible queue, scroll/reopen cache and resource/texture invalidation PASS").arg(openMs).arg(selectMs);
    return true;
}
