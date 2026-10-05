#include "terrain_mob_height_check.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "worldobj.h"
#include "lever.h"
#include "torch.h"
#include "magictrap.h"
#include "light.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "tree_view.h"
#include "endurance_document.h"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTranslator>
#include <QUndoStack>

int runTerrainMobHeightCheck()
{
    QTextStream out(stdout);int failed=0;
    auto check=[&](bool ok,const char* name){out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n";out.flush();if(!ok)++failed;};
    auto heightClose=[](float a,float b){return qAbs(a-b)<0.0001f;};
    QTemporaryDir temp;if(!temp.isValid())return 2;
    QTranslator translator;
    if(!translator.load(QStringLiteral(":/i18n/ei_mapper_ru.qm")))return 2;
    QApplication::installTranslator(&translator);
    const QString fixture=qEnvironmentVariable("EI_TEST_TERRAIN_FIXTURE");
    CMprDocument source;CValidationReport report;
    if(!source.load(QFileInfo(fixture),&report)){out<<report.toText();return 2;}
    SMprCreationRequest request;request.innerNamespace="height115";request.maxZ=100;
    request.flatRawHeight=10000;request.sectorCountX=2;request.sectorCountY=2;
    QByteArray bytes;if(!source.buildNewCanvas(request,&bytes,&report))return 2;
    const QString path=temp.filePath("height115.mpr");QFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return 2;file.close();
    CSettings settings;QTableWidget properties;properties.setColumnCount(2);
    QUndoStack history;CProgressView progress;QLineEdit coordinates;CTreeView tree;CTileForm tiles;
    CLandscapeTools tools;CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
    view.attachLandscapeTools(&tools);view.resize(640,480);view.show();QApplication::processEvents();view.makeCurrent();
    view.m_pLand->readMap(QFileInfo(path),false);
    if(!view.m_mprDocument.loadBytes(bytes,path,&report)||!view.m_terrainEdit.load(view.m_mprDocument,&report))return 2;
    view.m_terrainEditAvailable=true;view.m_pLand->setCanonicalTerrainModel(&view.m_terrainEdit);
    view.m_worldTerrainEnabled=false;
    tools.activateMode(ELandscapeEditMode::Sculpt);tools.setSculptTool(ELandscapeSculptTool::Raise);
    const auto spins=tools.findChildren<QDoubleSpinBox*>();
    for(auto* spin:spins){if(spin->objectName()=="landscapeSculptRadius")spin->setValue(2);
        if(spin->objectName()=="landscapeSculptStrength")spin->setValue(1);
        if(spin->objectName()=="landscapeRaiseLowerStep")spin->setValue(400);}
    auto* follow=tools.findChild<QCheckBox*>("landscapeMoveMobObjects");if(!follow)return 2;
    check(follow->text()==QString::fromUtf8("Перемещать объекты с рельефом"),"production Russian caption available");
    tools.resize(380,850);tools.setAttribute(Qt::WA_DontShowOnScreen);tools.show();QApplication::processEvents();
    tools.grab().save(QStringLiteral("landscape-options.png"));
    auto* mob=new CMob;mob->setFileName(QFileInfo(temp.filePath("height115.mob")));
    QList<CNode*> objects{new CWorldObj,new CLever,new CTorch,new CMagicTrap,new CWorldObj,new CWorldObj,new CLight};
    uint id=101;for(CNode* node:objects){node->setMapId(id++);mob->nodes().append(node);}
    view.m_aMob.append(mob);view.m_activeMob=mob;
    QVector<float> worldZ,offsets;
    for(int i=0;i<objects.size();++i){QVector3D pos(i==5?50.f:16.f,16,2.75f);objects[i]->updatePos(pos);
        worldZ<<objects[i]->drawPosition().z();offsets<<objects[i]->position().z();}
    objects[4]->setState(eHidden);mob->captureCleanProjection();
    follow->setChecked(true);view.sculptTerrainAt(QVector3D(16,16,0),1.0f);
    check(history.count()==1&&objects[0]->drawPosition().z()>worldZ[0]&&objects[0]->position().z()==offsets[0],"enabled option follows terrain without changing stored Z");
    history.undo();check(heightClose(objects[0]->drawPosition().z(),worldZ[0]),"enabled option undo returns to original surface");history.clear();
    follow->setChecked(false);view.sculptTerrainAt(QVector3D(16,16,0),1.0f);
    bool unchanged=true;for(int i=0;i<5;++i)unchanged&=heightClose(objects[i]->drawPosition().z(),worldZ[i])&&objects[i]->position().z()<offsets[i];
    check(unchanged&&history.count()==1,"disabled option preserves four terrain-relative types and hidden world object");
    check(objects[5]->position().z()==offsets[5]&&objects[6]->position().z()==offsets[6],"outside brush and absolute-height light unchanged");
    check(mob->isDirty(),"compensation marks owning MOB dirty");
    QVector<float> after;for(auto* node:objects)after<<node->position().z();
    follow->setChecked(true);history.undo();bool exact=true;for(int i=0;i<objects.size();++i)exact&=objects[i]->position().z()==offsets[i];
    check(exact&&!mob->isDirty(),"undo restores exact stored Z and clean MOB despite changed checkbox");
    history.redo();exact=true;for(int i=0;i<objects.size();++i)exact&=objects[i]->position().z()==after[i];
    check(exact&&heightClose(objects[0]->drawPosition().z(),worldZ[0]),"redo restores exact compensated Z despite changed checkbox");
    QByteArray mobBytes;mob->serializeMob(mobBytes,false);CMobDocument doc;CMob reopened;
    check(doc.loadBytes(mobBytes,"height115.mob",&report)&&reopened.deserialize(mobBytes,&doc)
          &&reopened.nodeByMapId(101)&&reopened.nodeByMapId(101)->position().z()==after[0],"MOB binary serialization and fresh reopen preserve compensated Z");
    CMprEditTransaction transaction;QByteArray saved;
    check(view.m_terrainEdit.appendSectorEdits(&transaction,&report)
          &&view.m_mprDocument.applyTransaction(transaction,&saved,&report),"canonical MPR candidate contains height edits");
    CMprDocument reopenedMpr;CTerrainEditModel reopenedTerrain;
    check(reopenedMpr.loadBytes(saved,"height115.mpr",&report)&&reopenedTerrain.load(reopenedMpr,&report)
          &&heightClose(reopenedTerrain.terrainSurfaceHeight(16,16)+after[0],worldZ[0]),"saved MPR and MOB together preserve world height on reopen");
    history.undo();history.clear();follow->setChecked(false);
    auto stroke=[&](bool world){
        tools.setSculptTool(ELandscapeSculptTool::Lower);
        view.m_worldTerrainEnabled=world;view.m_hasActiveMapWorldOrigin=world;
        view.m_neighborLayoutStorageWritable=world;view.m_activeMapWorldOrigin=QPointF();
        view.m_sculptStrokeCommand=STerrainHeightCommand();view.m_worldTerrainStroke=SWorldTerrainStroke();
        view.m_sculptStrokeMobHeightState=view.makeTerrainMobHeightState();
        view.m_sculptStrokeActive=true;view.m_sculptStrokeChanged=false;
        view.sculptTerrainAt(QVector3D(16,16,0),1.0f);view.sculptTerrainAt(QVector3D(16,16,0),1.0f);
        view.finishSculptStroke();
    };
    stroke(false);check(history.count()==1&&heightClose(objects[0]->drawPosition().z(),worldZ[0])&&objects[0]->position().z()>offsets[0],"two-stamp lowering stroke preserves height in one local history step");
    after[0]=objects[0]->position().z();history.undo();check(objects[0]->position().z()==offsets[0],"local stroke undo exact");
    history.redo();check(objects[0]->position().z()==after[0],"local stroke redo exact");history.undo();history.clear();
    stroke(true);check(view.m_worldTerrainSession.canUndo()&&history.count()==0&&heightClose(objects[0]->drawPosition().z(),worldZ[0]),"world sculpt stores companion MOB state in shared terrain history");
    after[0]=objects[0]->position().z();follow->setChecked(true);view.worldTerrainHistory(false,true);
    check(objects[0]->position().z()==offsets[0]&&heightClose(objects[0]->drawPosition().z(),worldZ[0]),"world stroke undo exact despite changed checkbox");
    view.worldTerrainHistory(true,true);check(objects[0]->position().z()==after[0]&&heightClose(objects[0]->drawPosition().z(),worldZ[0]),"world stroke redo exact despite changed checkbox");
    const auto state=view.m_worldTerrainSession.nextCommand(false)->mobHeightState;
    mob->nodes().removeAll(objects[0]);delete objects[0];objects[0]=nullptr;
    check(!view.terrainMobHeightStateAvailable(state.data()),"unloaded affected node prevents partial world-history replay");
    const int index=view.m_worldTerrainSession.canRedo();view.worldTerrainHistory(false,true);
    check(view.m_worldTerrainSession.canUndo()&&!view.m_worldTerrainSession.canRedo()&&index==0,"refused world undo leaves terrain history untouched");
    history.clear();view.m_sculptStrokeMobHeightState.clear();view.m_worldTerrainSession.clear();
    view.m_pLand->setCanonicalTerrainModel(nullptr);view.m_pLand->unloadMpr();
    out<<"Terrain MOB height failures="<<failed<<"\n";out.flush();return failed?1:0;
}
