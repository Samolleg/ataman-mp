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
    if(diagnostic) *diagnostic="empty subtitles, eight icon sections, three islands, assignment/move/unassign, search, persistence, selection, visible-only thumbnails and Escape PASS";
    return true;
}
