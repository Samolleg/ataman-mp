#include "model_gallery_check.h"
#include "model_gallery.h"
#include "gallery_thumbnail_cache.h"
#include <QTemporaryDir>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTabBar>
#include <QSet>
#include <QCompleter>
#include <QScrollBar>
#include <QFile>
#include <atomic>
#include <QThread>
#include <QDebug>

bool runModelGalleryWidgetCheck(QString* diagnostic)
{
    QString thumbnailFailure;
    QString thumbnailTimingSample;
    QString coldThumbnailTimingSample;
    const auto fail=[diagnostic,&thumbnailFailure](const QString& message){if(diagnostic)*diagnostic=message+thumbnailFailure;return false;};
    QTemporaryDir temp; if(!temp.isValid()) return fail("temporary directory");
    ei::TechnicalModelCatalog catalog; QString error;
    if(!catalog.load(temp.filePath("model-catalog.json"),&error)) return fail(error);
    QStringList models={"house","bridge"};
    for(int i=0;i<250;++i) models.append(QString("model%1").arg(i));
    catalog.registerModels(models); catalog.setRussianName("house",QString::fromUtf8("Домик"));
    catalog.bindTexture("test","house","stone"); if(!catalog.save(&error)) return fail(error);
    ei::TechnicalTextureCatalog scan;
    auto previews=QSharedPointer<std::atomic<int>>::create(0);
    CModelGallery gallery(models,&catalog,"test",{"stone","wood"},&scan,
        [previews](const QString&,const QString&){++*previews;QImage image(192,132,QImage::Format_ARGB32);image.fill(Qt::red);return image;});
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
    while(clock.elapsed()<800 && previews->load()<2) {QApplication::processEvents(QEventLoop::AllEvents,20);}
    if(previews->load()==0 || previews->load()>=models.size()) return fail("visible-only thumbnails");
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
    large.enableMultiSelection();large.setAttribute(Qt::WA_DontShowOnScreen);large.show();
    auto* rangeList=large.findChild<QListWidget*>("modelGalleryTiles");
    const auto click=[&](int row,Qt::KeyboardModifiers modifiers) {
        rangeList->scrollToItem(rangeList->item(row));rangeList->doItemsLayout();
        const QPoint point=rangeList->visualItemRect(rangeList->item(row)).center();
        QMouseEvent press(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,modifiers);
        QMouseEvent release(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton,modifiers);
        QApplication::sendEvent(rangeList->viewport(),&press);QApplication::sendEvent(rangeList->viewport(),&release);
    };
    click(0,Qt::NoModifier);clock.restart();click(142,Qt::ShiftModifier);
    const qint64 rangeMs=clock.elapsed();
    if(rangeList->selectedItems().size()!=143) return fail(QString("Shift range across scroll: expected143 actual%1").arg(rangeList->selectedItems().size()));
    for(int i=0;i<rangeList->count();++i)if(rangeList->item(i)->isSelected()!=(i<=142))return fail("Shift must follow model order rather than grid rectangle");
    clock.restart();const auto rangeEntries=large.selectedEntries();const qint64 entriesMs=clock.elapsed();
    if(rangeEntries.size()!=143 || rangeMs>=100 || entriesMs>=100)return fail(QString("143 selection responsiveness range=%1 entries=%2ms").arg(rangeMs).arg(entriesMs));
    click(9,Qt::ShiftModifier);if(rangeList->selectedItems().size()!=10)return fail("Shift shrinking retains original anchor");
    click(25,Qt::ControlModifier);if(rangeList->selectedItems().size()!=11)return fail("Ctrl toggles independent tile");
    click(30,Qt::ControlModifier|Qt::ShiftModifier);if(rangeList->selectedItems().size()!=16)return fail("Ctrl Shift adds range from latest Ctrl anchor");
    click(20,Qt::NoModifier);click(5,Qt::ShiftModifier);
    if(rangeList->selectedItems().size()!=16)return fail("reverse Shift range");
    click(2,Qt::NoModifier);rangeList->scrollToItem(rangeList->item(13));rangeList->doItemsLayout();
    const auto dragStart=rangeList->visualItemRect(rangeList->item(13)).center();
    QMouseEvent shiftPress(QEvent::MouseButtonPress,dragStart,Qt::LeftButton,Qt::LeftButton,Qt::ShiftModifier);
    QMouseEvent shiftMove(QEvent::MouseMove,dragStart+QPoint(20,20),Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);
    QMouseEvent shiftRelease(QEvent::MouseButtonRelease,dragStart+QPoint(20,20),Qt::LeftButton,Qt::NoButton,Qt::ShiftModifier);
    QApplication::sendEvent(rangeList->viewport(),&shiftPress);QApplication::sendEvent(rangeList->viewport(),&shiftMove);QApplication::sendEvent(rangeList->viewport(),&shiftRelease);
    if(rangeList->selectedItems().size()!=12)return fail(QString("Shift click with pointer motion: expected12 actual%1").arg(rangeList->selectedItems().size()));
    QKeyEvent shiftRight(QEvent::KeyPress,Qt::Key_Right,Qt::ShiftModifier);QApplication::sendEvent(rangeList,&shiftRight);
    if(rangeList->selectedItems().size()!=13)return fail("Shift arrow extends ordered range");
    large.hide();large.setCurrentModel("house");
    if(largeTexture->model()->flags(largeTexture->model()->index(0,0))!=Qt::NoItemFlags
        || largeTexture->findData("stone")<0 || largeTexture->findData("wood")<0) return fail("non-selectable texture sections and exact choices");
    largeTexture->completer()->setCompletionPrefix("texture7162");
    if(largeTexture->completer()->completionModel()->rowCount()!=1
        || largeTexture->completer()->completionModel()->index(0,0).data().toString()!="texture7162") return fail("full-list completion content");
    // The two production entry points provide the initial model/texture before
    // the first population. Shared preview keys must include the resources.
    auto sharedCalls=QSharedPointer<std::atomic<int>>::create(0);
    const auto fastProvider=[sharedCalls](const QString&,const QString& tex){++*sharedCalls;QImage image(192,132,QImage::Format_ARGB32);image.fill(tex=="wood"?Qt::red:Qt::blue);return image;};
    // A worker may finish before its queued GUI notification. A resize/scroll
    // timer in that interval must not replace its future or submit it twice.
    {
        std::atomic<int> jobs{0};
        CModelGallery notification({"a","b"},&catalog,"test",{},&scan,
            [&jobs](const QString&,const QString&){++jobs;QImage image(192,132,QImage::Format_ARGB32);image.fill(Qt::red);return image;});
        notification.setAttribute(Qt::WA_DontShowOnScreen);notification.show();
        notification.m_thumbnailTimer.stop();notification.loadVisibleThumbnail();
        for(auto* watcher:notification.m_thumbnailJobs)watcher->waitForFinished();
        notification.loadVisibleThumbnail();
        if(jobs.load()!=2) return fail("finished worker must await GUI result notification without resubmission");
    }
    const auto ready=[&](CModelGallery& dialog,int limit) {
        auto* tiles=dialog.findChild<QListWidget*>("modelGalleryTiles");
        QEventLoop loop; QTimer deadline,probe; bool complete=false;
        QElapsedTimer elapsed;elapsed.start();qint64 last=0,gap=0,first=-1;int visible=0,done=0;
        const auto inspect=[&] {
            const qint64 now=elapsed.elapsed();gap=qMax(gap,now-last);last=now;
            complete=true;
            visible=done=0;
            for(int i=0;i<tiles->count();++i) if(tiles->visualItemRect(tiles->item(i)).intersects(tiles->viewport()->rect())) {
                ++visible;auto* item=tiles->item(i);const QString model=item->data(Qt::UserRole).toString();
                if(item->icon().isNull() || item->data(Qt::UserRole+2).toString()!=dialog.cacheKey(model,dialog.textureForModel(model)))complete=false;else ++done;
            }
            complete=complete && visible>0;
            if(done&&first<0)first=now;
            if(complete) loop.quit();
        };
        // Exercise the normal event loop. Keep the 300ms completion bound and
        // independently reject a >=100ms GUI gap, even if workers finish early.
        deadline.setSingleShot(true);deadline.setTimerType(Qt::PreciseTimer);probe.setTimerType(Qt::PreciseTimer);probe.setInterval(5);
        QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
        QObject::connect(&probe,&QTimer::timeout,&loop,inspect);
        deadline.start(limit);probe.start();loop.exec();
        gap=qMax(gap,elapsed.elapsed()-last);
        const bool passed=complete && elapsed.elapsed()<limit && gap<100;
        thumbnailFailure=passed?QString():QString("; thumbnails elapsed=%1ms first=%2ms GUI-gap=%3ms visible=%4 ready=%5 (completion<%6ms, GUI-gap<100ms)").arg(elapsed.elapsed()).arg(first).arg(gap).arg(visible).arg(done).arg(limit);
        thumbnailTimingSample = QString("thumbnail timing: elapsed=%1ms first=%2ms GUI-gap=%3ms visible=%4 ready=%5 complete=%6")
            .arg(elapsed.elapsed()).arg(first).arg(gap).arg(visible).arg(done).arg(complete);
        if(qEnvironmentVariableIsSet("EI_GALLERY_TIMING_TRACE"))qInfo("GALLERY ready elapsed_ms=%lld first_ms=%lld gap_ms=%lld visible=%d done=%d complete=%d passed=%d",(long long)elapsed.elapsed(),(long long)first,(long long)gap,visible,done,complete,passed);
        return passed;
    };
    // Bounded overlap, stale texture/tab delivery, scrolling, and all-N close.
    struct ParallelState { std::atomic<int> active{0},maximum{0},calls{0}; };
    auto parallel=QSharedPointer<ParallelState>::create();
    const auto slowProvider=[parallel](const QString&,const QString& tex){
        ++parallel->calls;const int active=++parallel->active;
        int maximum=parallel->maximum.load();while(maximum<active&&!parallel->maximum.compare_exchange_weak(maximum,active)){}
        QThread::msleep(tex=="wood"?100:20);
        QImage image(192,132,QImage::Format_ARGB32);image.fill(tex=="wood"?Qt::red:Qt::blue);
        --parallel->active;return image;
    };
    {
        CModelGallery concurrent({"house","bridge","a","b","c","d","e","f","g","h"},&catalog,"test",{"wood","stone"},&scan,slowProvider,nullptr,"house","wood");
        concurrent.setAttribute(Qt::WA_DontShowOnScreen);concurrent.show();
        concurrent.m_thumbnailTimer.stop();concurrent.loadVisibleThumbnail();
        clock.restart();while(parallel->active.load()<2 && clock.elapsed()<300)QApplication::processEvents();
        if(parallel->active.load()<2 || concurrent.m_thumbnailJobs.size()>4)return fail("bounded parallel provider overlap");
        concurrent.setCurrentTexture("stone");
        auto* tiles=concurrent.findChild<QListWidget*>("modelGalleryTiles");
        clock.restart();while(tiles->currentItem()->data(Qt::UserRole+2).toString()!=concurrent.cacheKey("house","stone")&&clock.elapsed()<500)QApplication::processEvents(QEventLoop::AllEvents,10);
        if(tiles->currentItem()->data(Qt::UserRole+2).toString()!=concurrent.cacheKey("house","stone") || tiles->currentItem()->icon().pixmap(192,132).toImage().pixelColor(0,0)!=QColor(Qt::blue))return fail("stale concurrent texture cannot overwrite newer icon");
        tiles->verticalScrollBar()->setValue(tiles->verticalScrollBar()->maximum());
        concurrent.m_search->setText("bridge");
        if(!ready(concurrent,500) || concurrent.selectedModel()!="bridge")return fail("stale filtered/scrolled jobs");
    }
    clock.restart();while(parallel->active.load()!=0&&clock.elapsed()<500)QApplication::processEvents();
    if(parallel->active.load()!=0||parallel->maximum.load()<2||parallel->maximum.load()>4)return fail("worker cleanup and concurrency bounds");
    {
        auto* closing=new CModelGallery({"house","bridge","a","b"},&catalog,"test",{"wood","stone"},&scan,slowProvider);
        closing->setAttribute(Qt::WA_DontShowOnScreen);closing->show();closing->m_thumbnailTimer.stop();closing->loadVisibleThumbnail();
        clock.restart();while(parallel->active.load()<2&&clock.elapsed()<300)QApplication::processEvents();
        if(parallel->active.load()<2){delete closing;return fail("N workers started before close");}
        clock.restart();delete closing;if(clock.elapsed()>150)return fail("all-N close blocks UI");
        clock.restart();while(parallel->active.load()!=0&&clock.elapsed()<500)QApplication::processEvents();
        if(parallel->active.load()!=0)return fail("all-N close leaks workers");
    }
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
        if(!ready(cold,300)) return fail(QString("visible thumbnail queue responsiveness (limit300ms): calls=%1 viewport=%2x%3 elapsed=%4ms").arg(sharedCalls->load()).arg(cold.findChild<QListWidget*>("modelGalleryTiles")->viewport()->width()).arg(cold.findChild<QListWidget*>("modelGalleryTiles")->viewport()->height()).arg(clock.elapsed()));
        coldThumbnailTimingSample = thumbnailTimingSample;
        auto* tiles=cold.findChild<QListWidget*>("modelGalleryTiles");
        if(sharedCalls->load()==0 || sharedCalls->load()>=manyModels.size()) return fail("full-profile visible-only jobs");
        const int before=sharedCalls->load();
        const int originalScroll=tiles->verticalScrollBar()->value();
        tiles->verticalScrollBar()->setValue(0);
        if(!ready(cold,300) || sharedCalls->load()<=before) return fail("new viewport thumbnails");
        // Return to the same viewport. EnsureVisible may align the selected row
        // differently after the first layout and reveal uncached extra tiles.
        tiles->verticalScrollBar()->setValue(originalScroll);
        const int back=sharedCalls->load();
        if(!ready(cold,300) || sharedCalls->load()!=back) return fail("return to cached viewport");
        // Scroll may leave bounded jobs from the previous viewport completing.
        // Consume them before measuring a separate window's provider count.
        clock.restart();while(!cold.m_thumbnailJobs.isEmpty()&&clock.elapsed()<300)QApplication::processEvents(QEventLoop::AllEvents,10);
        if(!cold.m_thumbnailJobs.isEmpty())return fail("scroll job completion deadline300ms");
    }
    {
        const int before=sharedCalls->load();
        CModelGallery warm(manyModels,&catalog,"test",manyTextures,&scan,fastProvider,nullptr,"house","wood",space);
        warm.setAttribute(Qt::WA_DontShowOnScreen);warm.show();
        if(!ready(warm,300) || sharedCalls->load()!=before) return fail(QString("preview cache survives gallery reopening before=%1 after=%2 jobs=%3").arg(before).arg(sharedCalls->load()).arg(warm.m_thumbnailJobs.size()));
        warm.setCurrentTexture("stone");
        if(!ready(warm,300)) return fail("changed texture preview");
        // ready() requires the current key, including the changed texture.
        auto* tiles=warm.findChild<QListWidget*>("modelGalleryTiles");clock.restart();
        while(tiles->currentItem()->data(Qt::UserRole+2).toString()!=warm.cacheKey("house","stone") && clock.elapsed()<300) QApplication::processEvents(QEventLoop::AllEvents,10);
        if(tiles->currentItem()->data(Qt::UserRole+2).toString()!=warm.cacheKey("house","stone")
            || tiles->currentItem()->icon().pixmap(192,132).toImage().pixelColor(0,0)!=QColor(Qt::blue)) return fail("texture change invalidates tile identity");
    }
    {
        const int before=sharedCalls->load();
        CModelGallery changed(manyModels,&catalog,"test",manyTextures,&scan,fastProvider,nullptr,"house","wood",space+"-changed");
        changed.setAttribute(Qt::WA_DontShowOnScreen);changed.show();
        clock.restart();
        if(!ready(changed,300) || sharedCalls->load()<=before) return fail(QString("resource generation invalidates shared previews before=%1 after=%2 jobs=%3 elapsed=%4ms").arg(before).arg(sharedCalls->load()).arg(changed.m_thumbnailJobs.size()).arg(clock.elapsed()));
    }
    catalog.bindTexture("test","asset0","texture99");
    large.setCurrentModel("asset0");
    if(large.selectedTexture()!="texture99") return fail("catalog revision invalidates cached texture choices");
    catalog.bindTexture("test","house","wood");catalog.bindTexture("test","bridge","stone");
    CModelGallery batch({"house","bridge"},&catalog,"test",{"wood","stone"},&scan,{},nullptr);
    batch.enableMultiSelection();
    auto* batchList=batch.findChild<QListWidget*>("modelGalleryTiles");
    batchList->selectAll();
    const auto entries=batch.selectedEntries();
    if(entries.size()!=2 || batchList->selectionMode()!=QAbstractItemView::ExtendedSelection) return fail("multiple model selection");
    auto* batchTexture=batch.findChild<QComboBox*>("modelGalleryTexture");
    const auto originalTexture=batchTexture->currentText();batchTexture->setEditText("missing-texture");
    batch.accept();if(batch.result()==QDialog::Accepted)return fail("batch silently substituted invalid texture");
    batchTexture->setCurrentText(originalTexture);
    batch.accept();if(batch.result()!=QDialog::Accepted)return fail("accept multiple models with bound textures");
    auto started=QSharedPointer<std::atomic_bool>::create(false);
    auto* slow=new CModelGallery({"house"},&catalog,"test",{"wood"},&scan,[started](const QString&,const QString&){
        started->store(true);QThread::msleep(600);QImage image(192,132,QImage::Format_ARGB32);image.fill(Qt::red);return image;
    },nullptr,{}, {},temp.path()+"/close-disk-worker");
    slow->setAttribute(Qt::WA_DontShowOnScreen);slow->show();
    clock.restart();while(!started->load() && clock.elapsed()<500)QApplication::processEvents();
    if(!started->load()){delete slow;return fail("slow worker started");}
    clock.restart();delete slow;const auto closeMs=clock.elapsed();
    if(closeMs>150)return fail("closing gallery waits for thumbnail worker");
    if(diagnostic) *diagnostic=QString("functional sections/search/manual persistence/Escape/multi-select; 497 models/7165 textures: open=%1ms select=%2ms;143 Shift=%4ms entries=%5ms; Shift pointer-motion/keyboard/Ctrl/reverse PASS; close during600ms worker=%3ms; thumbnail cache / bounded parallel / stale results / all-N close PASS; %6")
        .arg(openMs).arg(selectMs).arg(closeMs).arg(rangeMs).arg(entriesMs).arg(coldThumbnailTimingSample);
    return true;
}
