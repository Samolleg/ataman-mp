#include "object_hierarchy_check.h"
#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "tree_view.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "scene.h"
#include "unit.h"

#include <QSignalBlocker>
#include <QApplication>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QLineEdit>
#include <QUndoStack>
#include <QTextStream>
#include <QFile>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QScrollBar>
#include <QHeaderView>

namespace {
class FixtureObject : public CWorldObj {
public:
    FixtureObject(uint id, QString name, QString model) {
        setMapId(id); m_name=name; m_modelName=model; setState(eDraw);
    }
    void rename(const QString& name) { m_name=name; }
};
CTreeObject* rowFor(CTreeView& tree,CNode* node) {
    QTreeWidgetItemIterator it(&tree);
    while(*it) { auto* row=dynamic_cast<CTreeObject*>(*it); if(row && row->node()==node) return row; ++it; }
    return nullptr;
}
}
int runObjectHierarchyCheck()
{
    QTextStream out(stdout); int failures=0;
    auto check=[&](bool ok,const char* name){ out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n";out.flush();if(!ok)++failures; };
    QTemporaryDir scratch; if(!scratch.isValid()) return 1;
    CSettings settings; QUndoStack history; CTreeView tree; QTableWidget properties;
    properties.setColumnCount(2); CProgressView progress; QLineEdit coordinates; CTileForm tiles;
    CView view(nullptr,nullptr); view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
    auto* mob=new CMob; mob->setFileName(QFileInfo(scratch.filePath("active.mob")));
    auto* a=new FixtureObject(81,"ReedBush01-81","reedbush01");
    auto* b=new FixtureObject(82,"ReedBush01-82","reedbush01");
    auto* c=new FixtureObject(816,"ReedBush01-816","reedbush01");
    auto* s1=new FixtureObject(122,"Scull00-122","scull00");
    auto* s2=new FixtureObject(123,"Scull00-122","scull00");
    mob->nodes() << a << b << c << s1 << s2;
    view.m_aMob.append(mob); view.m_activeMob=mob; view.collectObjectTreeData();
    auto* row=rowFor(tree,a);
    check(row && row->parent() && row->parent()->childCount()==3,"same model with distinct instance names has one group");
    auto* skull=rowFor(tree,s1);
    if(skull && skull->parent()) {
        tree.itemClickedOver(skull->parent(),0);
        check(view.selectionSnapshot().isEmpty(),"group click only expands, never selects all instances");
    } else check(false,"skull group exists");
    check(tree.indentation()>0,"hierarchy levels have visible indentation");
    check(tree.columnCount()==3,"instance ID is distinct from group count");

    tree.setAttribute(Qt::WA_DontShowOnScreen); tree.resize(600,440); tree.show();
    if(row && row->parent()) {
        auto* group=row->parent(); group->setExpanded(true);
        for(auto* parent=group->parent();parent;parent=parent->parent()) parent->setExpanded(true);
        QApplication::processEvents();
        check(group->child(0)==rowFor(tree,a) && group->child(1)==rowFor(tree,b) && group->child(2)==rowFor(tree,c),
              "numeric instance ordering 81,82,816");
        auto click=[&](CNode* node,int column,Qt::KeyboardModifiers modifiers) {
            auto* item=rowFor(tree,node); tree.scrollToItem(item); QApplication::processEvents();
            QRect rect=tree.visualItemRect(item); QPoint point(220,rect.center().y());
            if(column==1) point.setX(tree.header()->sectionPosition(1)+10);
            QMouseEvent press(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,modifiers);
            QApplication::sendEvent(tree.viewport(),&press);
            QMouseEvent release(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton,modifiers);
            QApplication::sendEvent(tree.viewport(),&release); QApplication::processEvents();
        };
        click(a,0,Qt::NoModifier); check(view.selectionSnapshot()==QList<CNode*>{a},"leaf click selects exact instance");
        click(b,0,Qt::ControlModifier); check(view.selectionSnapshot().size()==2 && view.selectionSnapshot().contains(a) && view.selectionSnapshot().contains(b),"Ctrl adds instance");
        click(a,0,Qt::ControlModifier); check(view.selectionSnapshot()==QList<CNode*>{b},"Ctrl removes only clicked instance");
        click(a,0,Qt::NoModifier); click(c,0,Qt::ShiftModifier);
        check(view.selectionSnapshot().size()==3,"Shift selects contiguous range");
        click(a,1,Qt::NoModifier); check(view.selectionSnapshot()==QList<CNode*>{a},"ID column selects same instance without scene divergence");
        QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier); QApplication::sendEvent(&tree,&down);
        check(view.selectionSnapshot()==QList<CNode*>{b},"keyboard arrow selection synchronizes scene");
        tree.setFilterText("id=81"); tree.applyFilter();
        check(!rowFor(tree,a)->isHidden() && rowFor(tree,b)->isHidden(),"exact ID filter keeps only matching instance");
        tree.selectGroup(group,true); check(view.selectionSnapshot()==QList<CNode*>{a},"explicit select found excludes hidden instances");
        tree.selectGroup(group,false); check(view.selectionSnapshot().size()==3,"explicit select whole group includes filtered instances");
        tree.setFilterText(""); tree.applyFilter();
        view.selectNode(a,false); auto* stableRow=rowFor(tree,a);
        auto* otherGroup=rowFor(tree,s1)->parent(); otherGroup->setExpanded(false);
        a->rename("ReedBush01-99"); view.collectObjectTreeData();
        check(rowFor(tree,a)==stableRow && rowFor(tree,a)->text(0)=="ReedBush01-99" && !otherGroup->isExpanded(),
              "rename updates retained row and preserves unrelated expansion");
        check(view.selectionSnapshot()==QList<CNode*>{a} && rowFor(tree,a)->isSelected(),"refresh preserves scene and tree selection");
        auto* added=new FixtureObject(999,"ReedBush01-999","reedbush01"); mob->nodes().append(added); view.collectObjectTreeData();
        check(rowFor(tree,a)==stableRow && rowFor(tree,added)->parent()==stableRow->parent(),"creation extends existing model group without replacing rows");
        view.selectNode(added,false); view.deleteSelectedNodes();
        check(!rowFor(tree,added),"delete removes exact instance row");
        history.undo(); check(rowFor(tree,added)!=nullptr,"undo restores deleted instance");
        history.redo(); check(!rowFor(tree,added),"redo removes restored instance");
        history.undo();
    }
    QFile catalog(scratch.filePath("model-catalog.json")); catalog.open(QIODevice::WriteOnly);
    catalog.write(R"({"models":{"reedbush01":{"russianName":"Камыш"}}})"); catalog.close();
    tree.setModelCatalogPath(catalog.fileName()); view.collectObjectTreeData();
    tree.setFilterText(QString::fromUtf8("Камыш")); tree.applyFilter();
    check(rowFor(tree,a)->parent()->text(0).contains(QString::fromUtf8("Камыш")) && !rowFor(tree,a)->isHidden() && rowFor(tree,s1)->isHidden(),"Russian catalog alias appears and is searchable");
    tree.setFilterText(""); tree.applyFilter();
    auto* locked=new CMob; locked->setFileName(QFileInfo(scratch.filePath("locked.mob")));
    auto* duplicate=new FixtureObject(81,"Other object","reedbush01"); locked->nodes().append(duplicate);
    view.m_aMob.append(locked); view.collectObjectTreeData();
    check(tree.topLevelItemCount()==1 && !rowFor(tree,duplicate),"scope defaults to active MOB only");
    tree.setShowAllMobs(true);
    check(tree.topLevelItemCount()==2 && rowFor(tree,duplicate) && rowFor(tree,duplicate)!=rowFor(tree,a),"same ID in different MOBs retains distinct instance identity");
    check(rowFor(tree,duplicate)->toolTip(0).contains("locked",Qt::CaseInsensitive),"other MOB is visibly marked locked");
    tree.itemClickedOver(rowFor(tree,duplicate),0);
    check(view.currentMob()==mob && view.selectionSnapshot()==QList<CNode*>{duplicate},"single selection does not silently activate locked MOB");
    tree.setShowAllMobs(false); view.clearObjectSelection();
    auto* unit=new CUnit; unit->setMapId(7000); mob->nodes().append(unit);
    auto* logic=unit->logicAt(1); logic->createPatrolByIndex(-1); logic->createPatrolByIndex(0);
    auto* point=logic->patrolPoints().front();
    mob->logicNodesUpdate();
    { const QSignalBlocker guard(CScene::getInstance()); CScene::getInstance()->changeMode(eEditModeLogic); }
    view.collectObjectTreeData();
    check(rowFor(tree,unit) && !rowFor(tree,a) && rowFor(tree,point),"behavior presentation excludes scenery and retains patrol hierarchy");
    tree.setCurrentItem(rowFor(tree,point),0,QItemSelectionModel::ClearAndSelect);
    check(view.logicSlotIndex()==1 && view.selectionSnapshot().contains(point),"native patrol selection activates correct logic slot");
    auto* pointRow=rowFor(tree,point); view.refreshBehaviorOrder(unit,1);
    check(rowFor(tree,point)==pointRow && rowFor(tree,point)->parent()->child(0)==pointRow,"behavior refresh retains point identity and order");
    tree.setCurrentItem(rowFor(tree,point)->parent(),0,QItemSelectionModel::ClearAndSelect);
    check(tree.focusedLogicUnit()==unit && tree.focusedLogicSlot()==1 && view.selectionSnapshot().contains(unit),"keyboard logic-row selection focuses exact owner and slot");
    { const QSignalBlocker guard(CScene::getInstance()); CScene::getInstance()->changeMode(eEditModeObjects); }
    view.clearObjectSelection(); view.collectObjectTreeData();
    if(qEnvironmentVariableIsSet("EI_HIERARCHY_SCREENSHOT")) {
        auto* group=rowFor(tree,a)->parent(); group->setExpanded(true);
        for(auto* parent=group->parent();parent;parent=parent->parent()) parent->setExpanded(true);
        tree.verticalScrollBar()->setValue(0); tree.grab().save(qEnvironmentVariable("EI_HIERARCHY_SCREENSHOT"));
    }
    for(int i=0;i<5000;++i) mob->nodes().append(new FixtureObject(10000+i,QString("Large-%1").arg(i),"large"));
    QElapsedTimer elapsed; elapsed.start(); view.collectObjectTreeData();
    out<<"BENCH: populate 5000 instances "<<elapsed.elapsed()<<" ms\n";
    auto* retained=rowFor(tree,a); elapsed.restart(); view.collectObjectTreeData();
    out<<"BENCH: unchanged refresh 5000 instances "<<elapsed.elapsed()<<" ms\n";
    check(rowFor(tree,a)==retained,"large refresh retains object row identity");
    auto* manyGroup=rowFor(tree,mob->nodes().back())->parent(); manyGroup->setExpanded(true);
    for(auto* parent=manyGroup->parent();parent;parent=parent->parent()) parent->setExpanded(true);
    elapsed.restart(); QApplication::processEvents();
    out<<"BENCH: expanded layout 5000 instances "<<elapsed.elapsed()<<" ms\n"; out.flush();
    tree.verticalScrollBar()->setValue(150);
    const int scroll=tree.verticalScrollBar()->value(); view.collectObjectTreeData();
    check(tree.verticalScrollBar()->value()==scroll,"refresh preserves scroll position");

    view.selectNode(a,false);
    auto* batchRow=rowFor(tree,a);
    auto* collapsed=rowFor(tree,s1)->parent(); collapsed->setExpanded(false);
    tree.verticalScrollBar()->setValue(150);
    const int batchScroll=tree.verticalScrollBar()->value();
    int refreshes=0;
    const auto connection=QObject::connect(&tree,&CTreeView::filterMatchCountChanged,
        [&refreshes](int,int){++refreshes;});
    elapsed.restart();
    for(int i=0;i<3;++i) view.collectObjectTreeData();
    out<<"BENCH: three immediate refreshes "<<elapsed.elapsed()<<" ms count="<<refreshes<<"\n";
    refreshes=0; elapsed.restart();
    view.setWorkspaceLoadInProgress(true);
    view.setWorkspaceLoadInProgress(true);
    check(!batchRow->node() && !tree.isEnabled(),"workspace load detaches bindings and suspends interaction");
    a->rename("batched rename");
    for(int i=0;i<3;++i) { view.collectObjectTreeData(); view.viewParameters(); tree.applyFilter(); }
    tree.itemClickedOver(batchRow,0); tree.itemDoubleClickedOver(batchRow,0);
    check(refreshes==0 && view.selectionSnapshot()==QList<CNode*>{a},"event-loop callbacks cannot refresh or select during load");
    view.setWorkspaceLoadInProgress(false);
    out<<"BENCH: three coalesced requests "<<elapsed.elapsed()<<" ms count="<<refreshes<<"\n";
    check(refreshes==1,"workspace completion performs one tree/filter refresh");
    check(rowFor(tree,a)==batchRow && batchRow->text(0)=="batched rename" && batchRow->isSelected(),"batch preserves row identity and selected object");
    check(!collapsed->isExpanded() && tree.verticalScrollBar()->value()==batchScroll,"batch preserves expansion and scroll");
    view.setWorkspaceLoadInProgress(false);
    check(refreshes==1 && tree.isEnabled(),"repeated completion is idempotent");
    tree.setFilterText("id=81"); tree.applyFilter();
    tree.setShowAllMobs(true);
    auto* removedRow=rowFor(tree,duplicate);
    view.setWorkspaceLoadInProgress(true);
    view.m_aMob.removeAll(locked); delete locked;
    tree.itemClickedOver(removedRow,0); tree.applyFilter();
    check(!removedRow->node(),"deleted MOB has no stale outliner object binding during event pump");
    view.setWorkspaceLoadInProgress(false);
    check(tree.topLevelItemCount()==1 && !rowFor(tree,a)->isHidden() && rowFor(tree,b)->isHidden(),"completion removes old MOB rows and retains active filter");
    QObject::disconnect(connection);
    CTreeView focusTree;
    auto* focusOwner=new CUnit;
    auto* focusRow=new QTreeWidgetItem(&focusTree);
    focusRow->setData(0,Qt::UserRole+10,1);
    focusRow->setData(0,Qt::UserRole+11,QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(focusOwner)));
    focusTree.focusLogicSlot(focusOwner,1);
    check(focusTree.focusedLogicUnit()==focusOwner,"logic focus uses a live QObject owner");
    delete focusOwner;
    check(focusTree.focusedLogicUnit()==nullptr,"destroyed logic owner automatically invalidates focus");
    tree.hide();
    out<<"Hierarchy reproduction failures: "<<failures<<"\n";
    return failures?1:0;
}
