#include "view.h"
#include "settings.h"
#include "tablemanager.h"
#include "tree_view.h"
#include "tile_form.h"
#include "progressview.h"
#include "scene.h"
#include "unit.h"
#include "light.h"
#include "magictrap.h"
#include "resourcemanager.h"
#include "utils.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QTextStream>
#include <QUndoStack>
#include <QLineEdit>

// Reuse the existing native integration entrypoint; no new production CLI.
int runNeighborPerformanceCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");
    QTemporaryDir scratch;if(!scratch.isValid())return 2;
    const QString oldDirectory=QDir::currentPath();
    struct Restore {QString path;~Restore(){QDir::setCurrent(path);}} restore{oldDirectory};
    QDir::setCurrent(scratch.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
    QTableWidget table;CTableManager manager(&table);
    manager.setNewData({}); // Exact empty-list failure from the181 dump.
#ifdef EIPROPS_BASELINE
    out<<"FAIL baseline unexpectedly accepted empty property list\n";out.flush();return 1;
#else
    int passed=0,failed=0;
    const auto check=[&](bool ok,const char* description) {
        out<<(ok?"PASS ":"FAIL ")<<description<<"\n";out.flush();ok?++passed:++failed;
    };
    check(table.rowCount()==0 && !table.signalsBlocked(),"empty selection clears panel safely");
    QList<QSharedPointer<IPropertyBase>> properties;
    check(!util::constProp(properties,eObjParam_TEMPLATE),"empty lookup is nullable");
    auto comment=QSharedPointer<IPropertyBase>(new propStr(eObjParam_COMMENTS,"unchanged"));
    properties<<QSharedPointer<IPropertyBase>()<<comment;
    check(!util::constProp(properties,eObjParam_TEMPLATE),"missing model never aliases unrelated last property");
    check(util::constProp(properties,eObjParam_COMMENTS)==comment,"null entry skipped and exact property retained");
    manager.setNewData(properties);
    check(table.rowCount()==1,"non-model property stays editable without fabricated model row");
    manager.setNewData({QSharedPointer<IPropertyBase>(new propFloat(eObjParam_POSITION_X,1.0f))});
    check(table.rowCount()==0,"partial XYZ group never creates invalid editor");
    manager.setNewData({QSharedPointer<IPropertyBase>(new propUint(eObjParam_LOGIC_BEHAVIOUR,eRadius))});
    check(table.rowCount()==1,"missing optional guard radius is omitted");
    manager.setNewData({QSharedPointer<IPropertyBase>(new propBool(eObjParam_UNIT_NEED_IMPORT,true))});
    check(table.rowCount()==1,"missing optional import dependencies are omitted");
    manager.setNewData({QSharedPointer<IPropertyBase>(new propStr(eObjParam_TEMPLATE,"point"))});
    check(table.rowCount()==1,"model without bodyparts never substitutes another property");
    manager.setNewData({});
    check(table.rowCount()==0,"empty refresh removes previous model widgets");

    CSettings settings;
    for(QString option:{QString("resourceRoots"),QString("mapRoots")})
        if(auto* roots=dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,option)))roots->setValue({});
    CTextureList::getInstance()->attachSettings(&settings);CObjectList::getInstance()->attachSettings(&settings);
    QUndoStack undo;CTreeView tree;QTableWidget actual;actual.setColumnCount(2);
    CProgressView progress;QLineEdit coordinates;CTileForm tiles;CView view(nullptr,nullptr);
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&actual,&undo,&progress,&coordinates,&tree,&tiles);
    view.resize(640,480);view.show();QCoreApplication::processEvents();
    check(view.isValid(),"hidden native GL context available for real helper nodes");
    if(!view.isValid())return 3;
    auto* mob=new CMob;view.m_aMob.append(mob);view.m_activeMob=mob;
    auto* unit=new CUnit;unit->setMapId(100);mob->addNode(unit);
    unit->addFirstPatrolPoint();auto* patrol=unit->patrolByIndex(0);
    if(!patrol)return 4;
    patrol->addFirstViewPoint();auto* look=patrol->viewByIndex(0);
    if(!look)return 5;
    auto* light=new CLight;light->setMapId(101);mob->addNode(light);
    auto* trap=new CMagicTrap;trap->setMapId(102);mob->addNode(trap);
    mob->logicNodesUpdate();
    const auto select=[&](const QList<CNode*>& selected,EEditMode mode) {
        CScene::getInstance()->changeMode(mode);
        for(auto* n:mob->nodes())n->setState(eDraw);
        for(auto* n:mob->logicNodes())n->setState(eDraw);
        for(auto* n:selected)n->setState(eSelect);
        view.viewParameters();
    };
    const auto propertyRows=[&]() {
        int count=0;
        for(int row=0;row<actual.rowCount();++row)if(actual.item(row,0))++count;
        return count; // Collection widgets have their own full-width row.
    };
    select({unit,look},eEditModeLogic);
    check(propertyRows()==0,"real mixed Unit+Look selection has no common properties and does not crash");
    select({unit,patrol},eEditModeLogic);
    check(propertyRows()==0,"real mixed Unit+Patrol selection does not crash");
    select({unit,patrol,look,trap},eEditModeLogic);
    check(propertyRows()==1,"mixed Unit+Patrol+Look+Trap retains effective trap targets without crashing");
    select({patrol},eEditModeLogic);
    check(actual.rowCount()>=1,"single patrol position returns after empty mixed panel");
    select({look},eEditModeLogic);
    check(actual.rowCount()>=4,"single look point retains position/wait/speed/flags without model");
    QSharedPointer<IPropertyBase> wait;look->getLogicParam(wait,eObjParam_VIEW_WAIT);
    const int before=dynamic_cast<propInt*>(wait.get())->value();
    view.onParamChange(QSharedPointer<IPropertyBase>(new propInt(eObjParam_VIEW_WAIT,before+17)));
    look->getLogicParam(wait,eObjParam_VIEW_WAIT);
    check(dynamic_cast<propInt*>(wait.get())->value()==before+17 && undo.canUndo(),"logic property edit after mixed selection records Undo");
    undo.undo();look->getLogicParam(wait,eObjParam_VIEW_WAIT);
    check(dynamic_cast<propInt*>(wait.get())->value()==before,"Undo restores exact look wait");
    undo.redo();look->getLogicParam(wait,eObjParam_VIEW_WAIT);
    check(dynamic_cast<propInt*>(wait.get())->value()==before+17,"Redo restores exact look wait");
    select({unit},eEditModeLogic);
    check(actual.rowCount()>2,"single unit logic retains behavior editors without model");
    select({trap},eEditModeLogic);
    check(actual.rowCount()>0,"single trap logic retains editors without model");
    select({light},eEditModeObjects);
    check(actual.rowCount()>2,"ordinary light properties without model remain available");
    select({unit,light},eEditModeObjects);
    check(actual.rowCount()>0,"mixed ordinary objects retain common base properties");
    select({},eEditModeObjects);
    check(actual.rowCount()==0,"deselection clears panel after ordinary objects");
    // Model-bearing ordinary units also exercise optional import/bodypart rows.
    select({unit},eEditModeObjects);
    check(actual.rowCount()>2,"ordinary unit model/import/bodypart panel recovers after deselection");
    check(!actual.signalsBlocked(),"panel signals restored after all selection transitions");
    out<<"RESULT "<<passed<<" PASS "<<failed<<" FAIL\n";out.flush();return failed?1:0;
#endif
}
