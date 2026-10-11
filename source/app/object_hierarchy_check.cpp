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
#include "undo.h"

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
#include <QSettings>
#include "lever.h"
#include "light.h"
#include "endurance_document.h"
#include "main_window.h"
#include <QToolButton>
#include <QMenu>
#include <QDockWidget>
#include <QTranslator>
#include <QLayout>
#include <QTimer>
#include "resource_setup_dialog.h"

namespace {
class HiddenHierarchyCheckWindows : public QObject {
    bool eventFilter(QObject* target, QEvent* event) override {
        if(event->type()==QEvent::Polish)
            if(auto* widget=qobject_cast<QWidget*>(target))
                if(widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
class FixtureObject : public CWorldObj {
public:
    FixtureObject(uint id, QString name, QString model) {
        setMapId(id); m_name=name; m_modelName=model; setState(eDraw);
    }
    void rename(const QString& name) { m_name=name; }
    void textures(const QString& primary, const QString& secondary) { m_primaryTexture=primary; m_secondaryTexture=secondary; }
    void loadTexture() override {} // Search fixtures do not require GPU resources.
};
template<class Base> class FixtureVisual : public Base {
public:
    FixtureVisual(uint id, const QString& model, const QString& primary, const QString& secondary) {
        this->setMapId(id); this->m_name="Visual fixture"; this->m_modelName=model;
        this->m_primaryTexture=primary; this->m_secondaryTexture=secondary;
        this->m_bodyParts=QStringList{"fixture_hand_part", "fixture_body_part"}; this->setState(eDraw);
    }
};
CTreeObject* rowFor(CTreeView& tree,CNode* node) {
    QTreeWidgetItemIterator it(&tree);
    while(*it) { auto* row=dynamic_cast<CTreeObject*>(*it); if(row && row->node()==node) return row; ++it; }
    return nullptr;
}
}
int runObjectHierarchyCheck()
{
    HiddenHierarchyCheckWindows hidden; qApp->installEventFilter(&hidden);
    QTextStream out(stdout); int failures=0, checks=0;
    auto check=[&](bool ok,const char* name){ ++checks; out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n";out.flush();if(!ok)++failures; };
    QTemporaryDir scratch; if(!scratch.isValid()) return 1;
    const int savedFields = QSettings().value("ui/outlinerSearchFields", int(CTreeView::SearchAll)).toInt();
    struct RestoreFields { int value; ~RestoreFields() { QSettings().setValue("ui/outlinerSearchFields", value); } } restore{savedFields};
    QSettings().setValue("ui/outlinerSearchFields", int(CTreeView::SearchAll));
    if(qEnvironmentVariableIsSet("EI_OUTLINER_PANEL_ONLY")) {
        QTranslator russian;
        check(russian.load(":/i18n/ei_mapper_ru.qm"), "Russian search catalog loads from compiled resources");
        qApp->installTranslator(&russian);
        const QString previousDirectory=QDir::currentPath();
        QDir::setCurrent(scratch.path());
        QTimer setupDriver;
        QObject::connect(&setupDriver,&QTimer::timeout,[]() {
            for(auto* widget:QApplication::topLevelWidgets())
                if(auto* setup=qobject_cast<CResourceSetupDialog*>(widget))
                    if(setup->isVisible()) setup->reject();
        });
        setupDriver.start(1);
        // Like the production window in --ui-widget-check, retain the main
        // window until process teardown. This check does not test shutdown.
        MainWindow& panelWindow=*new MainWindow;
        setupDriver.stop(); QDir::setCurrent(previousDirectory); panelWindow.setAttribute(Qt::WA_DontShowOnScreen);
        auto* button=panelWindow.findChild<QToolButton*>("outlinerSearchFields");
        auto* dock=panelWindow.findChild<QDockWidget*>("objectOutlinerDock");
        auto* panelTree=dock ? dock->findChild<CTreeView*>() : nullptr;
        check(button && button->menu() && button->menu()->actions().size()==4 && panelTree,
              "production outliner exposes four search checkboxes");
        if(button && button->menu() && panelTree) {
            const auto actions=button->menu()->actions();
            check(button->text()==QString::fromUtf8("Поля")
                  && actions[1]->text()==QString::fromUtf8("Имя объекта")
                  && actions[2]->text()==QString::fromUtf8("Модель")
                  && actions[3]->text()==QString::fromUtf8("Текстура"), "search menu uses compiled Russian labels");
            actions[1]->trigger(); actions[3]->trigger();
            check(panelTree->searchFields()==(CTreeView::SearchId | CTreeView::SearchModel),
                  "production menu toggles persist combined search flags");
            actions[1]->trigger(); actions[3]->trigger();
            if(qEnvironmentVariableIsSet("EI_HIERARCHY_SCREENSHOT")) {
                const QDir shots=QFileInfo(qEnvironmentVariable("EI_HIERARCHY_SCREENSHOT")).dir();
                dock->widget()->resize(380,460); dock->widget()->layout()->activate();
                check(dock->widget()->grab().save(shots.filePath("search-panel-ru.png")), "Russian panel screenshot saved");
                auto* menu=button->menu(); menu->setAttribute(Qt::WA_DontShowOnScreen);
                menu->resize(menu->sizeHint());
                check(menu->grab().save(shots.filePath("search-fields-ru.png")), "Russian checked menu screenshot saved");
            }
        }
        qApp->removeTranslator(&russian);
        out<<"Outliner panel checks: "<<checks<<" failures: "<<failures<<"\n";
        return failures ? 1 : 0;
    }
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
    // All 15 nonempty combinations: OR across checked fields, AND across terms.
    a->rename("Needle named object"); b->setModelName("needle_model");
    c->textures("NeedlePrimary", "SecondLayer"); s1->textures("stone", "NeedleSecondary");
    view.collectObjectTreeData();
    int found=-1, total=-1;
    QObject::connect(&tree, &CTreeView::filterMatchCountChanged, &tree,
                     [&](int f, int t) { found=f; total=t; });
    auto filter = [&](int fields, const QString& text) {
        tree.setSearchFields(fields); tree.setFilterText(text); tree.applyFilter();
    };
    for(int mask=1; mask<=CTreeView::SearchAll; ++mask) {
        filter(mask, "nEeDlE");
        const bool name = mask & CTreeView::SearchName;
        const bool model = mask & CTreeView::SearchModel;
        const bool texture = mask & CTreeView::SearchTexture;
        check(rowFor(tree,a)->isHidden()!=name && rowFor(tree,b)->isHidden()!=model
              && rowFor(tree,c)->isHidden()!=texture && rowFor(tree,s1)->isHidden()!=texture
              && rowFor(tree,s2)->isHidden(), qPrintable(QString("field OR mask %1").arg(mask)));
        check(found == int(name)+int(model)+2*int(texture) && total==mob->nodes().size(),
              qPrintable(QString("match counter mask %1").arg(mask)));
    }
    filter(CTreeView::SearchId, "81");
    check(!rowFor(tree,a)->isHidden() && rowFor(tree,c)->isHidden(), "plain numeric ID is exact, not a prefix of 816");
    filter(CTreeView::SearchId, "00081");
    check(!rowFor(tree,a)->isHidden() && found==1, "plain numeric ID accepts leading zeros");
    filter(CTreeView::SearchId, "18446744073709551616");
    check(found==0, "overflowing ID does not match zero or wrap");
    filter(CTreeView::SearchName, "id:81");
    check(!rowFor(tree,a)->isHidden() && !rowFor(tree,c)->isHidden() && found==2, "explicit partial ID bypasses flags");
    filter(CTreeView::SearchTexture, "id=81");
    check(found==1 && !rowFor(tree,a)->isHidden(), "explicit exact ID bypasses flags");
    filter(CTreeView::SearchName, "id:~86");
    check(found==1 && !rowFor(tree,c)->isHidden(), "legacy fuzzy field query preserved");
    filter(CTreeView::SearchModel, "~NEEDLE_MODEL");
    check(found==1 && !rowFor(tree,b)->isHidden(), "legacy fuzzy query remains available");
    filter(0, "~NEEDLE_MODEL");
    check(found==1 && !rowFor(tree,b)->isHidden(), "bare fuzzy syntax bypasses flags for backward compatibility");
    filter(CTreeView::SearchName, "model=needle_model");
    check(found==1 && !rowFor(tree,b)->isHidden(), "legacy model exact bypasses flags");
    filter(CTreeView::SearchTexture, "needle id=816");
    check(found==1 && !rowFor(tree,c)->isHidden(), "mixed plain and explicit terms combine with AND");
    filter(CTreeView::SearchTexture, "texture=SecondLayer");
    check(found==1 && !rowFor(tree,c)->isHidden(), "exact texture matches any individual texture");
    filter(CTreeView::SearchTexture, "edited_second");
    {
        CChangeProp edit(&view,c,QSharedPointer<IPropertyBase>(new propStr(eObjParam_SEC_TXTR,"edited_second")));
        QObject::connect(&edit,&CChangeProp::updateParam,&tree,&CTreeView::applyFilter);
        edit.redo();
        check(found==1 && !rowFor(tree,c)->isHidden(), "property edit invalidates cached secondary texture without tree rebuild");
        edit.undo();
        check(found==0 && rowFor(tree,c)->isHidden(), "property undo invalidates cached secondary texture");
        edit.redo();
        check(found==1 && !rowFor(tree,c)->isHidden(), "property redo invalidates cached secondary texture");
        edit.undo();
    }
    filter(0, "needle"); check(found==0, "no checked fields finds no plain-word matches");
    filter(0, "id=81"); check(found==1, "explicit syntax works with all flags off");
    filter(CTreeView::SearchId | CTreeView::SearchModel, "");
    { CTreeView reopened; check(reopened.searchFields()==(CTreeView::SearchId | CTreeView::SearchModel),
                               "field flags persist in QSettings and restore in a new tree"); }
    auto* collapsed = rowFor(tree,c)->parent();
    collapsed->setExpanded(false);
    auto* collapsedType = collapsed->parent(); collapsedType->setExpanded(false);
    view.selectNode(c, false);
    tree.markNodes({c}, c, true); QApplication::processEvents();
    check(!collapsed->isExpanded() && !collapsedType->isExpanded() && rowFor(tree,c)->isSelected(),
          "viewport/scroll selection marks hidden leaf without expanding either ancestor");
    view.collectObjectTreeData(); QApplication::processEvents();
    check(!collapsed->isExpanded() && !collapsedType->isExpanded() && rowFor(tree,c)->isSelected(),
          "refresh preserves collapsed selected branch");
    filter(CTreeView::SearchTexture, "needle");
    check(collapsed->isExpanded() && collapsedType->isExpanded(), "search expands ancestors containing matches");
    view.collectObjectTreeData();
    filter(CTreeView::SearchAll, ""); QApplication::processEvents();
    check(!collapsed->isExpanded() && !collapsedType->isExpanded(), "clear search restores pre-search expansion even after refresh");
    tree.revealNode(c); QApplication::processEvents();
    check(!collapsed->isExpanded() && !collapsedType->isExpanded(), "viewport picking and selection undo reveal path preserves collapsed ancestors");
    a->rename("ReedBush01-99"); b->setModelName("reedbush01"); view.collectObjectTreeData();
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
    auto* unit=new FixtureVisual<CUnit>(7000,"unit_fixture","unit_skin","unit_second");
    auto* lever=new FixtureVisual<CLever>(7001,"lever_fixture","lever_skin","lever_second");
    auto* light=new CLight; light->setMapId(7002); light->setModelName("light_fixture");
    mob->nodes() << unit << lever << light; view.collectObjectTreeData();
    for(const auto& pair : QList<QPair<CNode*,QString>>{{unit,"UNIT_FIXTURE"},{lever,"LEVER_FIXTURE"},{light,"LIGHT_FIXTURE"}}) {
        filter(CTreeView::SearchModel,pair.second);
        check(found==1 && !rowFor(tree,pair.first)->isHidden(), "model search isolates unit/lever/light type");
    }
    for(const auto& pair : QList<QPair<CNode*,QString>>{{unit,"UNIT_SKIN"},{unit,"UNIT_SECOND"},{lever,"LEVER_SKIN"},{lever,"LEVER_SECOND"}}) {
        filter(CTreeView::SearchTexture,pair.second);
        check(found==1 && !rowFor(tree,pair.first)->isHidden(), "primary/secondary texture search isolates unit/lever");
    }
    filter(CTreeView::SearchModel,"fixture_hand_part");
    check(found==1 && !rowFor(tree,unit)->isHidden(), "unit authored body-part name is included in model search");
    filter(CTreeView::SearchAll,"");
    auto* logic=unit->logicAt(1); logic->createPatrolByIndex(-1); logic->createPatrolByIndex(0);
    auto* point=logic->patrolPoints().front();
    mob->logicNodesUpdate();
    { const QSignalBlocker guard(CScene::getInstance()); CScene::getInstance()->changeMode(eEditModeLogic); }
    view.collectObjectTreeData();
    check(rowFor(tree,unit) && !rowFor(tree,a) && rowFor(tree,point),"behavior presentation excludes scenery and retains patrol hierarchy");
    tree.setCurrentItem(rowFor(tree,point),0,QItemSelectionModel::ClearAndSelect);
    check(view.logicSlotIndex()==1 && view.selectionSnapshot().contains(point),"native patrol selection activates correct logic slot");
    auto* pointRow=rowFor(tree,point);
    auto* slotRow=pointRow->parent(); slotRow->setExpanded(false);
    rowFor(tree,unit)->setExpanded(false);
    tree.markNodes({point},point,true); QApplication::processEvents();
    check(!slotRow->isExpanded() && !rowFor(tree,unit)->isExpanded(), "logic point synchronization preserves collapsed behavior branches");
    tree.focusLogicSlot(unit,1,true); QApplication::processEvents();
    check(!slotRow->isExpanded() && !rowFor(tree,unit)->isExpanded(), "logic slot focus preserves collapsed owner and slot");
    view.refreshBehaviorOrder(unit,1);
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

    qint64 worst=0;
    for(int mask=1;mask<=CTreeView::SearchAll;++mask) {
        elapsed.restart(); filter(mask, "Large-49");
        worst=qMax(worst,elapsed.elapsed());
    }
    out<<"BENCH: 15 field masks / "<<mob->nodes().size()<<" objects worst filter "<<worst<<" ms\n";
    check(worst<250, "5000-object cached filtering stays below 250 ms per mask");
    filter(CTreeView::SearchAll, "");
    if(!qEnvironmentVariableIsEmpty("EI_OUTLINER_BENCH_MOB")) {
        CMobDocument document; const QFileInfo input(qEnvironmentVariable("EI_OUTLINER_BENCH_MOB"));
        check(document.load(input), "large map copy passes strict MOB reader");
        auto* corpus=new CMob; corpus->setFileName(input);
        for(const auto& object : document.objects()) {
            if(object.unit) corpus->nodes().append(new FixtureVisual<CUnit>(object.nid,object.model,object.primaryTexture,object.secondaryTexture));
            else {
                auto* node=new FixtureObject(object.nid,object.name,object.model);
                node->textures(object.primaryTexture,object.secondaryTexture); corpus->nodes().append(node);
            }
        }
        view.m_aMob.append(corpus); view.m_activeMob=corpus; view.clearObjectSelection();
        elapsed.restart(); view.collectObjectTreeData();
        out<<"BENCH: MOB metadata projection "<<corpus->nodes().size()<<" objects populate "<<elapsed.elapsed()<<" ms\n";
        check(corpus->nodes().size()>=1000,"large map copy supplies at least 1000 actual object records");
        qint64 worst=0; int queryCount=0;
        for(int mask=1; mask<=CTreeView::SearchAll; ++mask) {
            for(const QString& query : {QString("81"),QString("un"),QString("tree"),QString("__absent__")}) {
                elapsed.restart(); filter(mask,query); worst=qMax(worst,elapsed.elapsed()); ++queryCount;
            }
        }
        out<<"BENCH: MOB "<<input.fileName()<<" objects "<<corpus->nodes().size()
           <<" queries "<<queryCount<<" worst filter "<<worst<<" ms\n";
        check(worst<250,"large map metadata filtering stays below 250 ms per query");
    }
    tree.hide();
    out<<"Hierarchy checks: "<<checks<<" failures: "<<failures<<"\n";
    return failures?1:0;
}
