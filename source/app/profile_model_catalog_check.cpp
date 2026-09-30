#include "profile_model_catalog_check.h"
#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "tree_view.h"
#include "tablemanager.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "scene.h"
#include "model_gallery.h"
#include "profile_database.h"
#include "resourcemanager.h"
#include <QTemporaryDir>
#include <QDir>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QTimer>
#include <QTextStream>
#include <QUndoStack>
#include <QApplication>

int runProfileModelCatalogCheck()
{
    QTemporaryDir scratch;
    struct DirectoryGuard { QString old=QDir::currentPath(); ~DirectoryGuard(){QDir::setCurrent(old);} } directory;
    if(!scratch.isValid() || !QDir::setCurrent(scratch.path())) return 2;
    QTextStream out(stdout); int failures=0;
    auto check=[&](bool ok,const char* name){out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n";out.flush();if(!ok)++failures;};
    const auto originalDatabase=ei::ProfileDatabaseContext::instance().current();
    struct DatabaseGuard { QSharedPointer<const ei::SProfileDatabaseSnapshot> original;
        ~DatabaseGuard(){ei::ProfileDatabaseContext::instance().publishEditedSnapshot(original);} } databaseGuard{originalDatabase};
    CSettings settings; QUndoStack history; CTreeView tree; QTableWidget properties;
    CProgressView progress; QLineEdit coordinates; CTileForm tiles;
    CView view(nullptr,nullptr); view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
    const QStringList roots{scratch.path()};
    dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"mapRoots"))->setValue(roots.toVector());
    const QString profile=ei::TechnicalModelCatalog::profileKey(roots);
    QSharedPointer<ei::SProfileDatabaseSnapshot> database(new ei::SProfileDatabaseSnapshot);
    database->sourceArchivePath=scratch.filePath("database.res");
    ei::ProfileDatabaseContext::instance().publishEditedSnapshot(database);
    ei::TechnicalModelCatalog catalog; QString error;
    catalog.load(ei::TechnicalModelCatalog::pathForDatabase(database->sourceArchivePath),&error);
    catalog.setRussianName("fixture-house",QString::fromUtf8("Каменный дом"));
    catalog.bindTexture(profile,"fixture-house","stone"); catalog.bindTexture(profile,"fixture-bridge","wood");
    check(catalog.save(&error),"profile catalog fixture");
    auto& models=CObjectList::getInstance()->figureList(); const auto originalModels=models;
    auto& textures=const_cast<QList<QString>&>(CTextureList::getInstance()->textureList()); const auto originalTextures=textures;
    struct ResourceGuard { QList<QString>& models; QList<QString>& textures; QList<QString> oldModels,oldTextures;
        ~ResourceGuard(){models=oldModels;textures=oldTextures;} } resourceGuard{models,textures,originalModels,originalTextures};
    models={"fixture-house","fixture-bridge"}; textures={"other","stone","wood"};
    auto* mob=new CMob; mob->setFileName(QFileInfo(scratch.filePath("synthetic.mob")));
    auto* object=new CWorldObj;
    object->setMapId(129); object->setState(eSelect);
    object->applyParam(QSharedPointer<IPropertyBase>(new propStr(eObjParam_TEMPLATE,"fixture-house")));
    object->applyParam(QSharedPointer<IPropertyBase>(new propStr(eObjParam_PRIM_TXTR,"other")));
    mob->nodes()<<object; view.m_aMob.append(mob); view.m_activeMob=mob;
    const auto previousMode=CScene::getInstance()->getMode(); CScene::getInstance()->changeMode(eEditModeObjects);
    struct ModeGuard { EEditMode mode; ~ModeGuard(){CScene::getInstance()->changeMode(mode);} } modeGuard{previousMode};
    view.viewParameters();
    auto galleryButton=[&]() -> QPushButton* {
        for(int row=0;row<properties.rowCount();++row) if(auto* widget=properties.cellWidget(row,1))
            if(auto* button=widget->findChild<QPushButton*>("objectModelGallery")) return button;
        return nullptr;
    };
    bool russian=false;
    for(int row=0;row<properties.rowCount();++row)
        if(properties.item(row,1) && properties.item(row,1)->text()==QString::fromUtf8("Каменный дом")) russian=true;
    check(russian,"Russian name in existing object properties");
    view.collectObjectTreeData();
    check(tree.modelAlias("fixture-house")==QString::fromUtf8("Каменный дом"),"hierarchy reads profile-adjacent aliases");
    auto* texture=properties.findChild<CComboDynItem*>("objectModelTextureChoices");
    check(texture && texture->itemText(0)=="stone" && texture->currentText()=="other","matched textures first, current value preserved");
    const auto position=object->position(); const auto instanceName=object->mapName();
    bool currentTextureRetained=false;
    QTimer::singleShot(0,[&](){for(QWidget* widget:QApplication::topLevelWidgets()) if(auto* gallery=qobject_cast<CModelGallery*>(widget)) {
        currentTextureRetained = gallery->selectedTexture()=="other"; gallery->accept();
    }});
    galleryButton()->click();
    check(currentTextureRetained && history.count()==0 && object->textureName()=="other","unchanged gallery choice preserves current texture and history");
    bool badTextureBlocked=false;
    QTimer::singleShot(0,[&](){for(QWidget* widget:QApplication::topLevelWidgets()) if(auto* gallery=qobject_cast<CModelGallery*>(widget)) {
        gallery->setCurrentModel("fixture-bridge"); gallery->findChild<QComboBox*>("modelGalleryTexture")->setEditText("missing-texture");
        gallery->accept(); badTextureBlocked = gallery->result()!=QDialog::Accepted; gallery->reject();
    }});
    galleryButton()->click();
    check(badTextureBlocked && history.count()==0,"unavailable texture cannot be accepted into existing object");
    QTimer::singleShot(0,[&](){for(QWidget* widget:QApplication::topLevelWidgets()) if(auto* gallery=qobject_cast<CModelGallery*>(widget)) gallery->reject();});
    galleryButton()->click();
    check(object->modelName()=="fixture-house" && object->textureName()=="other" && history.count()==0,"cancel does not mutate object or undo history");
    QTimer::singleShot(0,[&](){for(QWidget* widget:QApplication::topLevelWidgets()) if(auto* gallery=qobject_cast<CModelGallery*>(widget)) {
        gallery->setCurrentModel("fixture-bridge"); gallery->findChild<QComboBox*>("modelGalleryTexture")->setCurrentText("wood");
        gallery->findChild<QPushButton*>("modelGalleryChoose")->click();
    }});
    galleryButton()->click();
    check(object->modelName()=="fixture-bridge" && object->textureName()=="wood","gallery applies model and texture to existing object");
    check(history.count()==1 && object->position()==position && object->mapId()==129 && object->mapName()==instanceName,"one undo action; position, identity, instance name preserved");
    history.undo(); check(object->modelName()=="fixture-house" && object->textureName()=="other","undo restores both original fields");
    history.redo(); check(object->modelName()=="fixture-bridge" && object->textureName()=="wood","redo restores both chosen fields");
    bool emptyNameHidden=true;
    for(int row=0;row<properties.rowCount();++row) if(properties.item(row,0) && properties.item(row,0)->text()==QString::fromUtf8("Русское название модели")) emptyNameHidden=false;
    check(emptyNameHidden,"no Russian-name row for unnamed model");
    auto* secondObject=new CWorldObj; secondObject->setMapId(129); secondObject->setState(eSelect);
    secondObject->applyParam(QSharedPointer<IPropertyBase>(new propStr(eObjParam_TEMPLATE,"fixture-house")));
    secondObject->applyParam(QSharedPointer<IPropertyBase>(new propStr(eObjParam_PRIM_TXTR,"stone")));
    mob->nodes()<<secondObject; view.viewParameters();
    history.clear();
    QTimer::singleShot(0,[&](){for(QWidget* widget:QApplication::topLevelWidgets()) if(auto* gallery=qobject_cast<CModelGallery*>(widget)) {
        gallery->setCurrentModel("fixture-bridge"); gallery->findChild<QComboBox*>("modelGalleryTexture")->setCurrentText("wood"); gallery->accept();
    }});
    galleryButton()->click();
    check(object->modelName()=="fixture-bridge" && secondObject->modelName()=="fixture-bridge" && secondObject->textureName()=="wood" && history.count()==1,"mixed-model group shares one gallery undo action despite duplicate IDs");
    history.undo();
    check(secondObject->modelName()=="fixture-house" && secondObject->textureName()=="stone" && object->modelName()=="fixture-bridge" && object->textureName()=="wood","group undo restores each object's own model/texture");
    history.redo(); secondObject->setState(eDraw); view.viewParameters();
    view.m_tableManager->setReadOnly(true);
    check(!galleryButton()->isEnabled(),"locked MOB disables gallery");
    view.m_tableManager->setReadOnly(false);
    const int originalIndex=history.index();
    QTimer::singleShot(0,[&](){ view.viewParameters(); for(QWidget* widget:QApplication::topLevelWidgets()) if(auto* gallery=qobject_cast<CModelGallery*>(widget)) {
        gallery->setCurrentModel("fixture-house"); gallery->accept();
    }});
    view.m_tableManager->openModelGallery();
    check(object->modelName()=="fixture-bridge" && history.index()==originalIndex,"stale property selection cannot receive gallery result");
    QSharedPointer<ei::SProfileDatabaseSnapshot> other(new ei::SProfileDatabaseSnapshot);
    QDir(scratch.path()).mkdir("other-profile"); other->sourceArchivePath=scratch.filePath("other-profile/database.res");
    ei::TechnicalModelCatalog second; second.load(ei::TechnicalModelCatalog::pathForDatabase(other->sourceArchivePath));
    second.setRussianName("fixture-bridge",QString::fromUtf8("Другой мост")); second.save();
    ei::ProfileDatabaseContext::instance().publishEditedSnapshot(other); view.viewParameters();
    bool otherName=false;
    for(int row=0;row<properties.rowCount();++row) if(properties.item(row,1) && properties.item(row,1)->text()==QString::fromUtf8("Другой мост")) otherName=true;
    check(otherName,"profile switch uses its database-adjacent JSON");
    view.collectObjectTreeData();
    check(tree.modelAlias("fixture-bridge")==QString::fromUtf8("Другой мост") && tree.modelAlias("fixture-house").isEmpty(),"hierarchy switches profile without alias leakage");
    const QString screenshot=qEnvironmentVariable("EI_PROFILE_CATALOG_CHECK_SCREENSHOT");
    if(!screenshot.isEmpty()){properties.resize(760,900);properties.show();QApplication::processEvents();check(properties.grab().save(screenshot),"property screenshot");}
    out<<"Profile catalog checks: "<<failures<<" failure(s)\n";
    return failures?1:0;
}
