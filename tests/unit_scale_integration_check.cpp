// Link with the tested production objects, replacing only scenario_layer_check.o.
// Private inspection is confined to this native regression entry point.
#include <QtWidgets>
#include <QtOpenGL>
#include <limits>
#include <functional>
#define private public
#define protected public
#include "unit.h"
#undef protected
#undef private
#include "view.h"
#include "settings.h"
#include "resource_profile.h"
#include "profile_database.h"
#include "profile_visual_provider.h"
#include "resourcemanager.h"
#include "mob.h"
#include "undo.h"
#include "property.h"
#include "scene.h"
#include "operationmanager.h"
#include "progressview.h"
#include "tree_view.h"
#include "tile_form.h"
#include <QThread>

namespace {
QByteArray visual(CUnit* unit, bool geometry, bool poses)
{
    QByteArray bytes; QDataStream stream(&bytes,QIODevice::WriteOnly);
    for(const auto& item:unit->staticRenderItems()) {
        const auto* part=item.part;
        stream<<part->name()<<quint64(quintptr(item.texture))<<item.textureUvScale<<item.materialEmission;
        if(geometry)for(const auto& vertex:part->vertData())stream<<vertex.position<<vertex.normal<<vertex.texCoord;
        if(poses)stream<<part->poseMatrix();
    }
    return bytes;
}
QJsonObject withoutScale(CNode* node)
{
    auto json=node->toJson();
    auto world=json.value("World object").toObject();auto base=world.value("Base object").toObject();
    base.remove("Complection");world.insert("Base object",base);json.insert("World object",world);return json;
}
}

int runScenarioLayerCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& label){out<<(ok?"PASS ":"FAIL ")<<label<<"\n";out.flush();if(!ok)throw label;++passes;};
    try {
        const QString fixture=qEnvironmentVariable("EI_SCALE_FIXTURE");
        const QString result=qEnvironmentVariable("EI_SCALE_OUTPUT");QDir().mkpath(result);
        QTemporaryDir scratch;check(scratch.isValid(),"scratch");
        struct Restore {QString path=QDir::currentPath();~Restore(){QDir::setCurrent(path);}} restore;
        QDir::setCurrent(scratch.path());QApplication::setApplicationName("Ataman189ScaleRegression");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
        auto* settings=new CSettings;
        dynamic_cast<COptString*>(settings->opt("resourceProfile"))->setValue("deja-vu");
        dynamic_cast<COptString*>(settings->opt("gameRoot"))->setValue(qEnvironmentVariable("EI_SCALE_GAME_ROOT"));
        const QStringList roots=qEnvironmentVariable("EI_SCALE_RESOURCE_ROOTS").split(';',QString::SkipEmptyParts);
        dynamic_cast<COptStringList*>(settings->opt(eOptSetResource,"resourceRoots"))->setValue(roots.toVector());
        for(const auto& profile:loadResourceProfiles())if(profile.id=="deja-vu") {
            const auto evaluation=evaluateResourceProfile(qEnvironmentVariable("EI_SCALE_GAME_ROOT"),profile);
            check(evaluation.valid,"profile evaluation");ei::SProfileDatabaseLoadRequest request;
            const auto binding=evaluation.providers.value("database");request.providerId=binding.id;
            request.providerInterfaceVersion=binding.interfaceVersion;request.profileId=evaluation.profileId;
            request.runtimeManifestId=evaluation.runtimeManifestId;request.resourceDirectories=evaluation.resourceDirectories;
            ei::ProfileDatabaseService service;QMap<QString,QString> bindings;
            for(auto it=evaluation.providers.cbegin();it!=evaluation.providers.cend();++it)bindings[it.key()]=it->id;
            ei::ProfileDatabaseContext::instance().activate(service.snapshot(request),bindings);
        }
        CTextureList::getInstance()->attachSettings(settings);CObjectList::getInstance()->attachSettings(settings);
        auto* history=new QUndoStack;auto* tree=new CTreeView;auto* table=new QTableWidget;table->setColumnCount(2);
        auto* progress=new CProgressView;auto* view=new CView(nullptr,nullptr);
        view->setAttribute(Qt::WA_DontShowOnScreen);view->attach(settings,table,history,progress,new QLineEdit,tree,new CTileForm);
        view->resize(640,480);view->show();QCoreApplication::processEvents();check(view->isValid(),"hidden desktop GL");
        QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,view,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w))box->reject();});dismiss.start(100);
        view->makeCurrent();CTextureList::getInstance()->initResource();CObjectList::getInstance()->initResource();
        view->attachLandscapeTools(new CLandscapeTools);
        view->loadLandscape(QFileInfo(fixture+"/zone3obr.mpr"),false);view->makeCurrent();
        auto* mob=new CMob;mob->attach(view,progress);QFileInfo source(fixture+"/zone3obr.mob");
        check(mob->readMob(source)==0,"read private real MOB");view->m_aMob.append(mob);view->m_activeMob=mob;
        CUnit* unit=nullptr;
        for(auto* node:mob->nodes()){node->setState(ENodeState::eDraw);if(node->mapId()==4634)unit=dynamic_cast<CUnit*>(node);}
        check(unit && unit->databaseName()=="OrcFemaleRuneEM3","original NPC 4634 prototype");unit->setState(ENodeState::eSelect);
        CScene::getInstance()->changeMode(eEditModeObjects);
        auto settle=[&](CUnit* actor,qint64 time=2300){
            view->makeCurrent();AnimationFrameContext::instance().beginFrame(actor->position(),time,true);
            QElapsedTimer timer;timer.start();
            do {view->makeCurrent();AnimationFrameContext::instance().beginFrame(actor->position(),time,true);actor->preloadVisualResources();if(!actor->needsVisualResolution())break;QCoreApplication::processEvents();QThread::msleep(5);}while(timer.elapsed()<10000);
            check(!actor->needsVisualResolution(),"visual settled");
        };
        settle(unit);const auto original=unit->toJson();const auto stable=withoutScale(unit);
        const auto baseShape=unit->constitution();const auto equipment=visual(unit,false,false);const auto baseline=visual(unit,true,true);
        const double baselineFrame=unit->m_lastAppliedAnimationFrame;
        const auto baselineGeometry=visual(unit,true,false);const auto baselinePose=visual(unit,false,true);
        const auto clip=unit->m_idleClip.actionName;const auto animationIdentity=unit->m_animationFingerprint;
        check(unit->m_idleClip.valid(),"real idle animation loaded");
        QStringList parts;for(const auto& item:unit->staticRenderItems())parts.append(item.part->name());
        out<<"PARTS "<<parts.join(",")<<" LAYERS "<<unit->renderedBodyTextureLayers().join(",")<<"\n";out.flush();
        check(parts.contains("hd.armor04") && parts.contains("rh3.pike01") && parts.contains("lh2.shield08") && unit->renderedBodyTextureLayers().size()==5,"helmet spear shield and skin+4 clothing layers");
        auto verify=[&](CUnit* actor,const QByteArray& expectedEquipment,const QJsonObject& stableData,const QString& label){
            settle(actor);check(visual(actor,false,false)==expectedEquipment,label+" equipment textures/UV/emission exact");
            check(withoutScale(actor)==stableData,label+" serialized inventory/prototype/other values exact");
            auto referenceData=actor->toJson();auto* reference=new CUnit(referenceData);reference->setMapId(actor->mapId());reference->setState(ENodeState::eSelect);settle(reference);
            check(visual(actor,true,true)==visual(reference,true,true),label+" geometry and pose equal fresh same-scale unit");
            check(actor->m_idleClip.actionName==clip && actor->m_animationFingerprint==animationIdentity,label+" animation clip/identity retained");
        };
        for(int axis=0;axis<3;++axis){
            const EObjParam type=axis==0?eObjParam_COMPLECTION_X:axis==1?eObjParam_COMPLECTION_Y:eObjParam_COMPLECTION_Z;
            const int before=history->index();view->onParamChange(QSharedPointer<IPropertyBase>(new propFloat(type,baseShape[axis]+0.13f)));
            check(history->index()==before+1,"component creates one history action");
            check(visual(unit,false,false)==equipment,"component keeps equipped wrappers before render");
            verify(unit,equipment,stable,"axis "+QString::number(axis));
            check(visual(unit,true,true)!=baseline,"axis actually changes geometry");
            history->undo();verify(unit,equipment,stable,"component Undo");if(unit->constitution()!=baseShape || visual(unit,true,true)!=baseline) {
                out<<"UNDO DEBUG shape="<<unit->constitution().x()<<","<<unit->constitution().y()<<","<<unit->constitution().z()<<" old="<<baseShape.x()<<","<<baseShape.y()<<","<<baseShape.z()
                   <<" frame="<<unit->m_lastAppliedAnimationFrame<<" oldFrame="<<baselineFrame<<" geometryEqual="<<(visual(unit,true,false)==baselineGeometry)<<" poseEqual="<<(visual(unit,false,true)==baselinePose)<<"\n";out.flush();
            }
            check(unit->constitution()==baseShape && visual(unit,true,true)==baseline,"Undo exact shape and animated baseline");
            history->redo();verify(unit,equipment,stable,"component Redo");history->undo();settle(unit);
        }
        const QVector3D vectorShape=baseShape+QVector3D(.07f,.09f,.11f);
        view->onParamChange(QSharedPointer<IPropertyBase>(new prop3D(eObjParam_COMPLECTION,vectorShape)));
        verify(unit,equipment,stable,"whole vector");history->undo();settle(unit);
        for(int axis=0;axis<3;++axis){
            const auto direction=axis==0?EOperateAxisX:axis==1?EOperateAxisY:EOperateAxisZ;
            const int before=history->index();
            view->m_pOp->changeState(new CScaleAxis(view,direction));const auto anchor=view->operationAnchor();
            QMouseEvent move(QEvent::MouseMove,anchor+QPoint(20,0),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            view->m_pOp->mouseMoveEvent(&move);check(unit->constitution()!=baseShape,"mouse preview changes shape");
            check(visual(unit,false,false)==equipment,"mouse keeps equipment before render");verify(unit,equipment,stable,"mouse preview");
            view->m_pOp->keyPress(eKey_Esc);settle(unit);
            check(history->index()==before && unit->constitution()==baseShape && visual(unit,true,true)==baseline,"Escape exact baseline and no history");
            view->m_pOp->changeState(new CScaleAxis(view,direction));
            QMouseEvent secondMove(QEvent::MouseMove,view->operationAnchor()+QPoint(30,0),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            view->m_pOp->mouseMoveEvent(&secondMove);settle(unit);const auto after=unit->constitution();
            QMouseEvent release(QEvent::MouseButtonRelease,QPoint(30,0),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            view->m_pOp->mouseReleaseEvent(&release);verify(unit,equipment,stable,"mouse commit");
            check(history->index()==before+1 && unit->constitution()==after,"mouse commit one history action");
            history->undo();settle(unit);check(unit->constitution()==baseShape && visual(unit,true,true)==baseline,"mouse Undo exact");
            history->redo();verify(unit,equipment,stable,"mouse Redo");history->undo();settle(unit);
        }
        // A second clock sample proves that the equipment still follows a live clip.
        settle(unit,4300);check(visual(unit,true,true)!=baseline,"animation advances after scaling");settle(unit);
        ProfileVisualProviderService providers;const auto& context=ei::ProfileDatabaseContext::instance();
        const auto db=context.current();SCharacterAppearanceRequest request;
        request.profileId=db->profileId;request.runtimeManifestId=db->runtimeManifestId;request.prototype=unit->databaseName();
        request.parentTemplate=unit->m_parentTemplate;request.model=unit->m_modelName;request.primaryTexture=unit->m_primaryTexture;
        request.secondaryTexture=unit->m_secondaryTexture;request.bodyParts=unit->m_bodyParts;request.dataSource=EUnitVisualDataSource::ProfileDatabase;
        QJsonObject appearance;QString diagnostic;
        check(providers.appearanceResolver(context.providerForRole("characterAppearance"),db,&diagnostic)->resolve(request,&appearance).success,"explicit equipment source for imported unit");
        auto importedData=original;importedData.insert("Is use mob stats?",true);
        importedData.insert("Armors",appearance.value("armor"));importedData.insert("Weapons",appearance.value("weapons"));
        auto* imported=new CUnit(importedData);imported->setMapId(unit->mapId());imported->setState(ENodeState::eSelect);settle(imported);
        check(imported->renderedBodyTextureLayers().size()>1 && !imported->embeddedArmor().isEmpty() && !imported->embeddedWeapons().isEmpty(),"imported fixture has explicit worn armor/weapons");
        const auto importedEquipment=visual(imported,false,false);const auto importedStable=withoutScale(imported);
        auto importedShape=imported->constitution()+QVector3D(.1f,.05f,.02f);imported->setConstitution(importedShape);settle(imported);
        check(imported->usesEmbeddedMobData() && visual(imported,false,false)==importedEquipment && withoutScale(imported)==importedStable,"embedded-MOB unit retains equipment and inventory");
        // A direct setter may run outside GL, including several deltas before a paint.
        view->doneCurrent();auto intermediate=baseShape+QVector3D(.03f,.04f,.05f);unit->setConstitution(intermediate);
        auto intermediate2=baseShape+QVector3D(.06f,.02f,.01f);unit->setConstitution(intermediate2);
        check(visual(unit,false,false)==equipment,"coalesced direct setters without GL retain equipped visual");
        auto reset=baseShape;unit->setConstitution(reset);settle(unit);
        check(visual(unit,true,true)==baseline,"coalesced cancellation restores exact pose/geometry");
        // Static world objects still use their normal immediate geometry path.
        CWorldObj* staticSource=nullptr;
        for(auto* node:mob->nodes())if(node->nodeType()==eWorldObject){staticSource=dynamic_cast<CWorldObj*>(node);if(staticSource)break;}
        check(staticSource!=nullptr,"ordinary world-object fixture");
        auto* ordinary=new CWorldObj(*staticSource);ordinary->loadFigure();ordinary->loadTexture();
        const auto oldVertices=ordinary->connectionPreviewVertices();auto scale=ordinary->constitution()+QVector3D(.15f,.1f,.12f);
        ordinary->setConstitution(scale);check(ordinary->connectionPreviewVertices()!=oldVertices,"ordinary object still rebuilds geometry immediately");
        auto* ordinaryReference=new CWorldObj(ordinary->toJson());ordinaryReference->loadFigure();ordinaryReference->loadTexture();
        check(ordinary->connectionPreviewVertices()==ordinaryReference->connectionPreviewVertices(),"ordinary scale equals fresh geometry");
        // Preserve every projected node value across native save/reopen, apart from the one intended scale.
        view->onParamChange(QSharedPointer<IPropertyBase>(new prop3D(eObjParam_COMPLECTION,vectorShape)));settle(unit);
        QVector<QJsonObject> expected;for(auto* node:mob->nodes())expected.append(node->toJson());
        check(mob->saveAs(QFileInfo(result+"/scaled.mob")),"native save with validation/atomic replacement");
        auto* reopened=new CMob;reopened->attach(view,progress);QFileInfo saved(result+"/scaled.mob");check(reopened->readMob(saved)==0,"native reopen saved map");
        check(reopened->nodes().size()==mob->nodes().size(),"saved node count exact");
        CUnit* reopenedUnit=nullptr;bool allExact=true;
        int index=0;
        for(auto* node:reopened->nodes()){
            const auto actual=node->toJson();const bool same=actual==expected.value(index);
            if(!same){
                out<<"ROUNDTRIP DIFFERENCE index="<<index<<" NID="<<node->mapId()<<" type="<<int(node->nodeType())<<"\n";out.flush();
                QFile before(result+"/roundtrip-before-"+QString::number(index)+".json");before.open(QIODevice::WriteOnly);before.write(QJsonDocument(expected.value(index)).toJson());
                QFile after(result+"/roundtrip-after-"+QString::number(index)+".json");after.open(QIODevice::WriteOnly);after.write(QJsonDocument(actual).toJson());
            }
            allExact &= same;++index;
            if(node->mapId()==4634)reopenedUnit=dynamic_cast<CUnit*>(node);
        }
        check(allExact,"all projected nodes and inventory round-trip exact");check(reopenedUnit!=nullptr,"saved NPC exists");
        reopenedUnit->setState(ENodeState::eSelect);verify(reopenedUnit,equipment,stable,"saved NPC");
        check(reopenedUnit->constitution()==vectorShape,"intended scale persisted");
        history->undo();settle(unit);check(unit->constitution()==baseShape && visual(unit,true,true)==baseline,"Undo after save exact");
        history->redo();verify(unit,equipment,stable,"Redo after save");
        QJsonObject receipt{{"result","PASS"},{"passes",passes},{"npc",4634},{"nodes",mob->nodes().size()},{"animation",clip}};
        QFile file(result+"/receipt.json");check(file.open(QIODevice::WriteOnly),"receipt");file.write(QJsonDocument(receipt).toJson());
        mob->setDirty(false);out<<"PASS unit scale integration "<<passes<<" checks\n";return 0;
    } catch(const QString& error){out<<"FAIL unit scale integration: "<<error<<"\n";return 1;}
}
