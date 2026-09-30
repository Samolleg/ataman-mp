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
    if(!list||!search||!name||!save||!bind||!texture||!choose) return fail("production controls");
    QElapsedTimer clock; clock.start();
    while(clock.elapsed()<800 && previews.load()<2) {QApplication::processEvents(QEventLoop::AllEvents,20);}
    if(previews.load()==0 || previews.load()>=models.size()) return fail("visible-only thumbnails");
    search->setText(QString::fromUtf8("ДОМИК house"));
    if(list->count()!=1 || gallery.selectedModel()!="house") return fail("Russian/technical search");
    name->setText(QString::fromUtf8("Каменный дом")); save->click();
    search->setText(QString::fromUtf8("КАМЕННЫЙ house"));
    if(list->count()!=1 || gallery.selectedModel()!="house") return fail("renamed search");
    texture->setCurrentText("wood"); bind->click();
    choose->click();
    if(gallery.result()!=QDialog::Accepted || gallery.selectedModel()!="house" || gallery.selectedTexture()!="wood") return fail("model/texture choice");
    ei::TechnicalModelCatalog reopened;
    if(!reopened.load(catalog.path(),&error) || reopened.russianName("house")!=QString::fromUtf8("Каменный дом")) return fail("name persistence");
    const auto choices=reopened.textureChoices("test","house",{"wood"},scan);
    if(choices.size()!=1 || choices.first().name!="wood") return fail("texture persistence");
    CModelGallery cancel(models,&catalog,"test",{},&scan,{},nullptr); cancel.setAttribute(Qt::WA_DontShowOnScreen); cancel.show();
    bool rejected=false; QObject::connect(&cancel,&QDialog::rejected,[&rejected]{rejected=true;});
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QApplication::sendEvent(&cancel,&escape);
    if(!rejected) return fail("Escape cancellation");
    if(diagnostic) *diagnostic="search, rename, persistence, selection, visible-only thumbnails and Escape PASS";
    return true;
}
