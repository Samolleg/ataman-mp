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
#include <QLabel>
#include <QMouseEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QHeaderView>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPointer>
#include <algorithm>

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
    properties.horizontalHeader()->setStretchLastSection(true);
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
    const QString authorProfile=ei::TechnicalModelCatalog::profileKey({scratch.filePath("author-machine/Maps")});
    catalog.bindTexture(authorProfile,"fixture-house","stone"); catalog.bindTexture(authorProfile,"fixture-bridge","wood");
    check(catalog.save(&error),"profile catalog fixture");
    // Reproduce a copied v95 file: different absolute map roots, no portable
    // field yet. Every property/gallery action below must work with this file.
    QFile legacyFixture(catalog.path()); legacyFixture.open(QIODevice::ReadOnly);
    auto legacyJson=QJsonDocument::fromJson(legacyFixture.readAll()).object();legacyFixture.close();
    legacyJson.remove("modelTextures"); legacyFixture.open(QIODevice::WriteOnly|QIODevice::Truncate);
    legacyFixture.write(QJsonDocument(legacyJson).toJson());legacyFixture.close();
    catalog.load(catalog.path(),&error);
    ei::TechnicalTextureCatalog relocatedScan;
    check(catalog.hasTextureBindings(profile) && catalog.textureChoices(profile,"fixture-bridge",{"wood"},relocatedScan).size()==1,
        "copied v95 catalog resolves bindings under different map roots without MOB scanning");
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
    auto modelCombo=[&]() -> CComboDynItem* {
        for(int row=0;row<properties.rowCount();++row) if(auto* widget=properties.cellWidget(row,1))
            if(auto* combo=widget->findChild<CComboDynItem*>())
                if(combo->value()->type()==eObjParam_TEMPLATE) return combo;
        return nullptr;
    };
    auto inlineName=[&]() -> QLabel* {
        for(int row=0;row<properties.rowCount();++row) if(auto* widget=properties.cellWidget(row,1))
            if(auto* label=widget->findChild<QLabel*>("objectModelRussianName")) return label;
        return nullptr;
    };
    bool separateName=false;
    for(int row=0;row<properties.rowCount();++row)
        if(properties.item(row,0) && properties.item(row,0)->text()==QString::fromUtf8("Русское название модели")) separateName=true;
    check(inlineName() && inlineName()->text()==QString::fromUtf8("Каменный дом") && !separateName,
          "Russian name beside model; no separate property row");
    auto* combo=modelCombo();
    combo->setEditText(QString::fromUtf8("каменный")); combo->_onFilterTextChanged(combo->currentText());
    check(combo->completer()->completionCount()==1 && combo->completer()->completionModel()->index(0,0).data().toString().contains("fixture-house"),
          "model search finds Russian alias");
    combo->setEditText("fixture-house"); combo->_onFilterTextChanged(combo->currentText());
    check(combo->completer()->completionCount()==1,"model search finds technical ID");
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
    QApplication::sendEvent(combo->lineEdit(),&escape);
    check(combo->currentText()=="fixture-house" && object->modelName()=="fixture-house" && history.count()==0,"alias search cancel preserves technical value");
    // A compound editor must consume its mouse drag instead of forwarding it
    // to the table viewport, where it becomes a multi-row selection gesture.
    properties.setAttribute(Qt::WA_DontShowOnScreen); properties.resize(760,900); properties.show();
    QApplication::processEvents(); properties.selectionModel()->clear();
    check(inlineName() && inlineName()->width()>40 && inlineName()->x()>combo->x(),"inline alias has visible space to the right of model");
    QWidget* editor=combo->parentWidget();
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(1,1),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove,QPointF(1,editor->height()+60),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(1,editor->height()+60),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(editor,&press); QApplication::sendEvent(editor,&move); QApplication::sendEvent(editor,&release);
    QSet<int> selectedRows; for(auto* item:properties.selectedItems()) selectedRows.insert(item->row());
    check(selectedRows.size()<=1,"model editor mouse drag does not select neighbouring property rows");
    properties.selectAll();
    QFocusEvent editorFocus(QEvent::FocusIn,Qt::MouseFocusReason);
    QApplication::sendEvent(combo->lineEdit(),&editorFocus);
    check(properties.selectedItems().isEmpty(),"entering model editor clears stale property selection");
    properties.hide();
    view.collectObjectTreeData();
    check(tree.modelAlias("fixture-house")==QString::fromUtf8("Каменный дом"),"hierarchy reads profile-adjacent aliases");
    auto* texture=properties.findChild<CComboDynItem*>("objectModelTextureChoices");
    bool groups=false;
    if(texture) {
        const int stone=texture->findData("stone"), other=texture->findData("other");
        groups=stone>0 && other>stone+1 && !(texture->model()->flags(texture->model()->index(0,0)) & Qt::ItemIsEnabled)
            && !(texture->model()->flags(texture->model()->index(other-1,0)) & Qt::ItemIsEnabled);
    }
    check(groups && texture->currentText()=="other","nonselectable texture group headings; current value preserved");
    QMetaObject::invokeMethod(texture,"activated",Q_ARG(QString,texture->itemText(0)));
    check(texture->currentText()=="other" && object->textureName()=="other" && history.count()==0,"texture heading cannot become an object value");
    combo=modelCombo(); combo->setEditText(QString::fromUtf8("каменный")); combo->_onFilterTextChanged(combo->currentText());
    const QString completion=combo->completer()->currentCompletion();
    QMetaObject::invokeMethod(combo->completer(),"activated",Q_ARG(QString,completion));
    check(combo->currentText()=="fixture-house" && object->modelName()=="fixture-house" && history.count()==0,
          "alias completion commits technical ID, never its displayed caption");
    combo->hidePopup(); combo->completer()->popup()->hide();
    properties.show(); QApplication::processEvents(); combo->showPopup(); QApplication::processEvents();
    const int bridgeRow=combo->findData("fixture-bridge");
    const QPoint choice=combo->view()->visualRect(combo->model()->index(bridgeRow,0)).center();
    QMouseEvent choosePress(QEvent::MouseButtonPress,choice,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent chooseRelease(QEvent::MouseButtonRelease,choice,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(combo->view()->viewport(),&choosePress);
    QApplication::sendEvent(combo->view()->viewport(),&chooseRelease);
    out << "Popup click: row=" << bridgeRow << "; choice=" << choice.x() << "," << choice.y()
        << "; model=" << object->modelName() << "; history=" << history.count() << "\n";
    check(object->modelName()=="fixture-bridge" && history.count()==1,"real model popup click applies technical ID once");
    history.undo(); check(object->modelName()=="fixture-house" && object->textureName()=="other","popup model choice supports undo");
    history.clear(); view.viewParameters();
    properties.hide();
    const auto position=object->position(); const auto instanceName=object->mapName();
    auto catalogBytes=[&]{QFile file(catalog.path());file.open(QIODevice::ReadOnly);return file.readAll();};
    const auto beforeGalleryCatalog=catalogBytes();
    QFile scanSentinel(scratch.filePath("explicit-scan-only.mob"));
    check(scanSentinel.open(QIODevice::WriteOnly) && scanSentinel.write("synthetic-invalid-mob")==21,"scan sentinel fixture");scanSentinel.close();
    bool currentTextureRetained=false;
    QTimer::singleShot(0,[&](){for(QWidget* widget:QApplication::topLevelWidgets()) if(auto* gallery=qobject_cast<CModelGallery*>(widget)) {
        currentTextureRetained = gallery->selectedTexture()=="other";
        QTimer::singleShot(60,gallery,&CModelGallery::accept);
    }});
    galleryButton()->click();
    check(currentTextureRetained && history.count()==0 && object->textureName()=="other","unchanged gallery choice preserves current texture and history");
    auto cachedFiles=[&]{QFile json(catalog.path());json.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(json.readAll()).object().value("profiles").toObject().value(profile).toObject().value("scanCache").toObject().value("files").toArray().size();};
    check(cachedFiles()==0,"populated relocated library opens gallery without scanning MOB files");
    bool refreshAvailable=false;
    QTimer::singleShot(0,[&](){for(QWidget* widget:QApplication::topLevelWidgets()) if(auto* gallery=qobject_cast<CModelGallery*>(widget)) {
        auto* refresh=gallery->findChild<QPushButton*>("modelGalleryScanTextures");
        refreshAvailable=refresh && refresh->isEnabled(); if(refreshAvailable)refresh->click();
        QTimer::singleShot(60,gallery,&CModelGallery::reject);
    }});
    galleryButton()->click();
    check(!refreshAvailable && cachedFiles()==0,"gallery has no MOB scanning action and does not write scan cache");
    check(object->modelName()=="fixture-house" && object->textureName()=="other" && history.count()==0,"refresh cancel does not alter MOB object");
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
    check(catalogBytes()==beforeGalleryCatalog,"opening, choosing model/texture and Undo/Redo do not rewrite the ready catalog");
    bool emptyNameHidden=true;
    for(int row=0;row<properties.rowCount();++row) if(properties.item(row,0) && properties.item(row,0)->text()==QString::fromUtf8("Русское название модели")) emptyNameHidden=false;
    check(emptyNameHidden && !inlineName(),"no Russian-name row or subtitle for unnamed model");
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
    bool otherName=inlineName() && inlineName()->text()==QString::fromUtf8("Другой мост");
    check(otherName,"profile switch uses its database-adjacent JSON");
    view.collectObjectTreeData();
    check(tree.modelAlias("fixture-bridge")==QString::fromUtf8("Другой мост") && tree.modelAlias("fixture-house").isEmpty(),"hierarchy switches profile without alias leakage");
    // Selection must remain responsive with a real-sized profile, not just
    // the three textures used by the functional fixtures above.
    for(int i=models.size(); i<493; ++i) models.append(QString("model-%1").arg(i));
    for(int i=textures.size(); i<7164; ++i) textures.append(QString("texture-%1").arg(i));
    const QString firstModel=object->modelName(), firstTexture=object->textureName();
    const QString secondModel=secondObject->modelName(), secondTexture=secondObject->textureName();
    QList<qint64> selectionTimes;
    for(int i=0;i<5;++i) {
        QElapsedTimer timer; timer.start();
        view.selectNodes(QList<CNode*>() << (i%2 ? secondObject : object), false);
        selectionTimes.append(timer.nsecsElapsed());
        // Retire replaced cell editors between ordinary UI events.
        QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    std::sort(selectionTimes.begin(),selectionTimes.end());
    out << "Selection timings, 7164 textures/493 models: median_ms="
        << selectionTimes.at(2)/1e6 << "; maximum_ms=" << selectionTimes.last()/1e6 << "\n";
    check(selectionTimes.at(2)<200000000,
          "repeated object selection with 7164 textures stays below 200 ms median");
    check(selectionTimes.last()<200000000,
          "every selection including first large-list attachment stays below 200 ms");
    texture=properties.findChild<CComboDynItem*>("objectModelTextureChoices");
    check(texture && texture->count()==7167 && texture->findData("texture-7163")>=0
          && texture->currentText()==firstTexture,
          "large texture list remains complete and preserves selected value");
    const int historyBeforeSearch=history.index();
    texture->_onFilterTextChanged("texture-7163");
    check(texture->completer()->completionCount()==1
          && texture->completer()->currentCompletion()=="texture-7163",
          "large grouped texture list still searches resource IDs");
    texture->completer()->popup()->hide();
    check(object->modelName()==firstModel && object->textureName()==firstTexture
          && secondObject->modelName()==secondModel && secondObject->textureName()==secondTexture
          && history.index()==historyBeforeSearch,
          "repeated selection does not mutate objects; search does not add undo commands");
    // Uniform row sizing must retain real popup hit-testing, disabled group
    // headings/separator and a complete scrollable list, not just findData().
    const auto activeTexture=[&]() -> CComboDynItem* {
        // Replaced cell editors remain QObject children until DeferredDelete.
        // Only the table's current cell widget belongs to this selection.
        for(int row=0;row<properties.rowCount();++row)
            if(auto* item=qobject_cast<CComboDynItem*>(properties.cellWidget(row,1)))
                if(item->value()->type()==eObjParam_PRIM_TXTR)return item;
        return nullptr;
    };
    properties.setAttribute(Qt::WA_DontShowOnScreen);properties.resize(760,900);
    properties.show();QApplication::processEvents();
    texture=activeTexture();
    for(int row=0;row<properties.rowCount();++row)
        if(properties.cellWidget(row,1)==texture)properties.scrollToItem(properties.item(row,0));
    texture->showPopup();QApplication::processEvents();
    auto mouseChoice=[&](CComboDynItem* item,int row) {
        QPointer<CComboDynItem> guard(item);
        const auto index=item->model()->index(row,0);
        item->view()->scrollTo(index,QAbstractItemView::PositionAtCenter);
        QApplication::processEvents();
        if(!guard)return false;
        const QRect rect=item->view()->visualRect(index);
        out<<"Texture popup row="<<row<<" rect="<<rect.x()<<","<<rect.y()<<","<<rect.width()<<","<<rect.height()
           <<" viewport="<<item->view()->viewport()->width()<<","<<item->view()->viewport()->height()<<"\n";out.flush();
        if(!rect.isValid() || !item->view()->viewport()->rect().intersects(rect))return false;
        const QPoint point=rect.center();
        QMouseEvent press(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(item->view()->viewport(),&press);
        QApplication::sendEvent(item->view()->viewport(),&release);
        return true;
    };
    const int historyBeforeTexture=history.index();
    const bool clickedHeader=mouseChoice(texture,0);
    out<<"Texture header expected="<<firstTexture<<" actual="<<object->textureName()
       <<" history="<<historyBeforeTexture<<"/"<<history.index()<<"\n";out.flush();
    check(clickedHeader && object->textureName()==firstTexture
          && history.index()==historyBeforeTexture,"large texture popup heading cannot select or mutate history");
    texture=activeTexture();texture->showPopup();
    const int lastTextureRow=texture->findData("texture-7163");
    check(!(texture->model()->flags(texture->model()->index(1,0)) & Qt::ItemIsEnabled),
          "empty preferred-group separator remains disabled");
    check(mouseChoice(texture,1) && object->textureName()==firstTexture
          && history.index()==historyBeforeTexture,"large texture popup separator cannot commit the highlighted leaf");
    const bool clickedLast=mouseChoice(texture,lastTextureRow);
    out<<"Texture choice actual="<<object->textureName()<<" history="<<historyBeforeTexture<<"/"<<history.index()<<"\n";out.flush();
    check(clickedLast && object->textureName()=="texture-7163"
          && history.index()==historyBeforeTexture+1,"last texture popup row is reachable by real mouse and commits once");
    history.undo();
    check(object->textureName()==firstTexture && object->modelName()==firstModel
          && secondObject->textureName()==secondTexture,"large texture choice Undo restores only its exact original target");
    history.redo();
    check(object->textureName()=="texture-7163" && object->modelName()==firstModel,"large texture choice Redo retains technical ID and model");
    history.undo();view.viewParameters();
    texture=activeTexture();
    texture->setEditText("texture-7163");texture->_onFilterTextChanged(texture->currentText());
    QKeyEvent textureEscape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
    QApplication::sendEvent(texture,&textureEscape);
    check(texture->currentText()==firstTexture && object->textureName()==firstTexture
          && history.index()==historyBeforeTexture,"Escape cancels large-list search without a property command");
    textures.append("texture-added-after-selection");view.viewParameters();
    texture=activeTexture();
    check(texture->findData("texture-added-after-selection")>=0 && texture->currentText()==firstTexture,
          "resource catalog update reaches the next property selection without stale choices");
    textures.removeLast();view.viewParameters();
    texture=activeTexture();
    check(texture->findData("texture-added-after-selection")==-1 && texture->count()==7167,
          "removed resource disappears while full current catalog remains intact");
    properties.hide();
    const QString screenshot=qEnvironmentVariable("EI_PROFILE_CATALOG_CHECK_SCREENSHOT");
    if(!screenshot.isEmpty()){properties.resize(760,900);properties.show();QApplication::processEvents();check(properties.grab().save(screenshot),"property screenshot");}
    out<<"Profile catalog checks: "<<failures<<" failure(s)\n";
    return failures?1:0;
}
