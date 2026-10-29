#include "diplomacy_check.h"
#include "diplomacy_dialog.h"
#include "diplomacy_analysis.h"
#include "diplomacy_colors.h"
#include "endurance_document.h"
#include "main_window.h"
#include "view.h"
#include "unit.h"
#include "property.h"
#include "undo.h"
#include "localization.h"
#include "resource_setup_dialog.h"
#include "mob_script_window.h"
#include "script_editor.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTreeWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QComboBox>
#include <QTabWidget>
#include <QLineEdit>
#include <QGroupBox>
#include <QLayout>
#include <QFile>
#include <QDataStream>
#include <QTextStream>
#include <QTimer>
#include <QSettings>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QContextMenuEvent>
#include <QCryptographicHash>

namespace {
class HiddenDiplomacyWidgets : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish) if(auto* widget=qobject_cast<QWidget*>(object))
            if(widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QByteArray u32(quint32 value) {
    QByteArray bytes; QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian); stream<<value; return bytes;
}
QByteArray record(quint32 tag,const QByteArray& data) { return u32(tag)+u32(data.size()+8)+data; }
QByteArray scriptRecord(const QByteArray& text) {
    quint32 key=17; QByteArray encrypted=u32(key);
    for(char byte:text) { key=key*214013u+2531011u; encrypted.append(char(byte^char(key>>16))); }
    return record(0xACCEECCB,encrypted);
}
QByteArray mobBytes(quint8 group,const QByteArray& text) {
    const QByteArray npc=record(0xB002,u32(42))+record(0xB004,"Fixture")
        +record(0xB011,QByteArray(1,char(group)))+record(0xBBBB0000,record(0xBBBB0001,"fixture.npc"));
    return record(0xA000,scriptRecord(text)+record(0xB000,record(0xB001,npc)));
}
bool waitFor(CDiplomacyDialog& dialog) {
    QElapsedTimer time; time.start();
    do { QApplication::processEvents(QEventLoop::AllEvents,20); }
    while(dialog.busy() && time.elapsed()<10000);
    return !dialog.busy();
}
}
int runDiplomacyCheck()
{
    QTextStream out(stdout); out.setCodec("UTF-8"); int passed=0,failed=0;
    auto check=[&](bool ok,const char* name) { (ok?passed:failed)++; out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n"; out.flush(); };
    HiddenDiplomacyWidgets hidden; qApp->installEventFilter(&hidden);
    ei::installApplicationTranslator(*qApp,"ru");
    QTemporaryDir files; if(!files.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,files.filePath("ui-settings"));
    diplomacy::Source base; base.key=files.filePath("base.mob"); base.loaded=true;
    diplomacy::Npc npc; npc.id=7; npc.key="npc7"; npc.name="Guard"; npc.group=2; base.npcs.append(npc);
    npc.id=8; npc.key="npc8"; npc.name="Merchant"; npc.group=3; base.npcs.append(npc);
    base.script="WorldScript(\n// SetPlayer(GetObject(7),31)\nConsoleString(\"SetPlayer(GetObject(7),30)\")\n"
        "if(IsAlive(GetObject(7))) then(SetPlayer(GetObject(7),5))\n"
        "SetPlayer(GetObjectByName(\"Merchant\"),6)\n"
        "AddUnitToServer(\"visitor\",\"fixture.npc\",\"Visitor\",1,2,3)\n"
        "SetPlayer(GetObjectByName(\"visitor\"),9)\nSetPlayer(this,31)\n"
        "SetPlayer(GetObject(7),GetPlayer())\nSetPlayer(GetObject(7),32)\nSetDiplomacy(2,3,2)\nGroupAdd(a,b)\n)";
    auto result=diplomacy::analyze({base});
    check(result.sources[0].npcs.size()==3,"placed NPCs plus a script spawn site");
    check(result.sources[0].npcs[0].group==2 && result.sources[0].npcs[0].possibleGroups==QSet<int>{5},"conditional SetPlayer keeps initial membership and valid possible group");
    check(result.sources[0].npcs[1].possibleGroups==QSet<int>{6},"placed NPC lookup by unique literal name");
    check(result.sources[0].npcs[2].scripted && result.sources[0].npcs[2].group==-1 && result.sources[0].npcs[2].possibleGroups==QSet<int>{9},"script spawn has unknown initial group and possible later assignment");
    int assignments=0; bool conditional=false,unknown=false;
    for(const auto& event:result.events) if(event.reference.command=="SetPlayer") {
        ++assignments; conditional|=!event.reference.conditions.isEmpty();
        unknown|=event.group==31 && event.npcKey.isEmpty();
    }
    check(assignments==6 && conditional && unknown,"comments/prose excluded; dynamic target and conditions retained");
    auto duplicate=base; duplicate.key=files.filePath("other.mob"); duplicate.script.clear();
    result=diplomacy::analyze({base,duplicate});
    check(result.sources[0].npcs[0].possibleGroups.isEmpty() && result.sources[1].npcs[0].possibleGroups.isEmpty(),"cross-MOB duplicate IDs never select a guessed NPC");
    auto collision=base;
    scenario::Object object; object.key="npc7"; object.id=7; object.name="Guard"; collision.objects.append(object);
    object.key="scenery7"; collision.objects.append(object);
    result=diplomacy::analyze({collision});
    check(result.sources[0].npcs[0].possibleGroups.isEmpty(),"non-NPC with same ID prevents false ownership match");
    auto duplicateSpawn=base; duplicateSpawn.script+="\nAddUnitToServer(\"visitor\",\"fixture.npc\",\"Visitor\",4,5,6)";
    result=diplomacy::analyze({duplicateSpawn});
    check(result.sources[0].npcs[2].possibleGroups.isEmpty(),"duplicate spawn names stay unresolved");
    const QString extra=files.filePath("extra.mob"); const auto bytes=mobBytes(17,"WorldScript(AddMob(\"base\"))");
    QFile file(extra); check(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size(),"owned synthetic dependency fixture"); file.close();
    CMobDocument document; CValidationReport report;
    check(document.loadBytes(bytes,extra,&report) && document.objects().size()==1 && document.objects()[0].hasPlayer && document.objects()[0].player==17,"raw OBJ_PLAYER on unloaded NPC snapshots");
    diplomacy::Request request; request.sources={base}; request.preferredSource=base.key; request.roots=QStringList{files.path()};
    request.sources[0].script="WorldScript(AddMob(\"extra\") AddMob(\"EXTRA.mob\") AddMob(name) AddMob(\"missing\"))";
    result=diplomacy::inspectConnected(request);
    check(result.sources.size()==2 && result.sources[1].npcs.size()==1 && result.sources[1].npcs[0].group==17,"recursive unopened AddMob, case-insensitive deduplication and cycle guard");
    check(result.diagnostics.size()>=2,"missing and computed dependencies show incomplete inspection");
    auto complete=request; complete.sources[0].script="WorldScript(AddMob(\"extra\"))";
    check(diplomacy::inspectConnected(complete).diagnostics.isEmpty(),"resolved recursive family has no false localized incomplete warning");
    file.open(QIODevice::ReadOnly); check(file.readAll()==bytes,"inspection preserves dependency bytes"); file.close();
    auto live=base; live.key=extra; live.script.clear(); live.npcs[0].group=4;
    request.sources.append(live); result=diplomacy::inspectConnected(request);
    check(result.sources.size()==2 && result.sources[1].npcs[0].group==4,"unsaved live source overrides disk counterpart");
    auto primaryCopy=live; primaryCopy.key=files.filePath("copy/extra.mob");
    diplomacy::Request primary; primary.sources={primaryCopy}; primary.extraSources=QStringList{extra};
    result=diplomacy::inspectConnected(primary);
    check(result.sources.size()==1 && result.sources[0].npcs[0].group==4,"opened primary copy prevents double counting installed primary");

    QTimer setupGuard;
    QObject::connect(&setupGuard,&QTimer::timeout,[] {
        for(QWidget* widget:QApplication::topLevelWidgets())
            if(auto* setup=qobject_cast<CResourceSetupDialog*>(widget)) setup->reject();
    });
    setupGuard.start(50);
    qApp->setProperty("workspaceLoadSmoke",true);
    MainWindow window; window.setAttribute(Qt::WA_DontShowOnScreen); setupGuard.stop();
    auto* view=window.view(); auto* mob=new CMob; mob->setFileName(QFileInfo(files.filePath("loaded.mob")));
    auto* unit=new CUnit; unit->setMapId(71);
    QSharedPointer<IPropertyBase> player(new propChar(eObjParam_PLAYER,char(11))); unit->applyParam(player);
    mob->nodes().append(unit); mob->setScript("WorldScript(SetPlayer(GetObject(71),12))");
    QVector<QVector<uint>> fixtureRelations(32,QVector<uint>(32));
    for(int r=0;r<32;++r) for(int c=0;c<32;++c)
        fixtureRelations[r][c]=r==c?0:uint((r+c)%2+1);
    mob->setDiplomacyField(fixtureRelations);
    mob->captureCleanProjection(); mob->setDirty(false); view->m_aMob.append(mob); view->m_activeMob=mob;
    auto* button=window.findChild<QToolButton*>("diplomacyToolbarButton");
    auto* group=window.findChild<QGroupBox*>("scenarioToolGroup");
    check(button && button->text()==QString::fromUtf8("Дипломатия") && group && group->layout()->itemAt(1)->widget()==button,"Russian diplomacy button immediately follows Behavior");
    check(button && button->iconSize()==QSize(44,44) && button->minimumSize()==QSize(68,72),"uniform toolbar icon and button sizing");
    const auto before=mob->parameterState();
    if(!button) { qApp->removeEventFilter(&hidden); return 2; }
    button->click(); auto* dialog=window.findChild<CDiplomacyDialog*>("diplomacyInspector");
    check(dialog && waitFor(*dialog) && dialog->result().sources.size()==1,"actual button opens responsive live MOB inspector");
    if(!dialog || dialog->result().sources.isEmpty()) { qApp->removeEventFilter(&hidden); return 2; }
    auto* groups=dialog->findChild<QTreeWidget*>("diplomacyGroups");
    auto* npcs=dialog->findChild<QTreeWidget*>("diplomacyNpcs");
    check(groups && groups->topLevelItem(12)->text(1)=="1" && groups->topLevelItem(13)->text(2)=="1","initial NPC count separated from SetPlayer calls");
    groups->setCurrentItem(groups->topLevelItem(13));
    check(npcs->topLevelItemCount()==1 && npcs->topLevelItem(0)->text(2)=="11" && npcs->topLevelItem(0)->text(3)=="12","possible group lists NPC with initial group preserved");
    dialog->findChild<QLineEdit*>("diplomacyNpcFilter")->setText("no-match");
    check(npcs->topLevelItemCount()==0 && groups->topLevelItem(12)->text(1)=="1","search does not falsify group totals");
    dialog->findChild<QLineEdit*>("diplomacyNpcFilter")->clear();
    dialog->findChild<QTabWidget*>()->setCurrentIndex(2);
    auto* relations=dialog->findChild<QTableWidget*>("diplomacyRelations");
    QApplication::processEvents();
    check(relations->rowCount()==32 && relations->item(1,2)->text()==QString::number(fixtureRelations[1][2])
        && dialog->findChild<QPushButton*>("diplomacySetRelation2")->isEnabled(),"actual relation matrix offers direct editing without free-text values");
    check(relations->viewport()->height()>100,"relation matrix has usable height after actual layout");
    if(!qEnvironmentVariableIsEmpty("EI_DIPLOMACY_CAPTURE"))
        check(dialog->grab().save(qEnvironmentVariable("EI_DIPLOMACY_CAPTURE")+".relations.png"),"hidden relation matrix capture");
    check(mob->parameterState()==before && !mob->isDirty() && window.m_undoStack->count()==0,"inspection preserves MOB, script, dirty flag and history");
    view->openScenarioSource(mob,view->scenarioScriptText(mob),-1);
    auto* scriptWindow=view->findChild<CMobScriptWindow*>();
    auto* editor=scriptWindow?scriptWindow->findChild<CScriptEditor*>():nullptr;
    check(editor!=nullptr,"owned internal script editor available");
    if(!editor) return 2;
    editor->setPlainText("WorldScript(SetPlayer(GetObject(71),13))");
    check(waitFor(*dialog) && dialog->result().sources.size()==1 && dialog->result().sources[0].draft
        && dialog->result().sources[0].npcs[0].possibleGroups==QSet<int>{13}
        && mob->parameterState()==before && !mob->isDirty(),"unapplied script buffer refreshes possible groups without applying changes");
    editor->setPlainText("WorldScript(SetPlayer(GetObject(71),12))"); delete scriptWindow;
    check(waitFor(*dialog) && dialog->result().sources.size()==1 && !dialog->result().sources[0].draft
        && dialog->result().sources[0].npcs[0].possibleGroups==QSet<int>{12},"closing unchanged script editor restores committed source");
    dialog->findChild<QTabWidget*>()->setCurrentIndex(0); groups->setCurrentItem(groups->topLevelItem(0));
    QApplication::processEvents();
    check(dialog->findChild<QPlainTextEdit*>("diplomacyDetails")->isVisible(),"NPC details return after visiting relation editor");
    if(!qEnvironmentVariableIsEmpty("EI_DIPLOMACY_CAPTURE"))
        check(dialog->grab().save(qEnvironmentVariable("EI_DIPLOMACY_CAPTURE")),"hidden diplomacy widget capture");
    QSharedPointer<IPropertyBase> changedGroup(new propChar(eObjParam_PLAYER,char(14)));
    window.m_undoStack->push(new CChangeProp(view,mob,unit,changedGroup));
    check(waitFor(*dialog) && dialog->result().sources[0].npcs[0].group==14,"production group edit refreshes inspector");
    window.m_undoStack->undo();
    check(waitFor(*dialog) && dialog->result().sources[0].npcs[0].group==11,"Undo restores membership in inspector");
    window.m_undoStack->redo();
    check(waitFor(*dialog) && dialog->result().sources[0].npcs[0].group==14,"Redo restores membership in inspector");
    auto* second=new CMob; second->setFileName(QFileInfo(files.filePath("second.mob")));
    auto* secondUnit=new CUnit; secondUnit->setMapId(72);
    player.reset(new propChar(eObjParam_PLAYER,char(15))); secondUnit->applyParam(player);
    second->nodes().append(secondUnit); view->m_aMob.append(second); dialog->invalidate();
    check(waitFor(*dialog) && dialog->result().sources.size()==2,"second loaded MOB participates in live overview");
    dialog->findChild<QComboBox*>("diplomacySourceFilter")->setCurrentIndex(2);
    groups->setCurrentItem(groups->topLevelItem(0));
    check(groups->topLevelItem(0)->text(1)=="1" && npcs->topLevelItemCount()==1 && npcs->topLevelItem(0)->text(2)=="15","source filter displays exact NPC counts and membership");
    dialog->findChild<QComboBox*>("diplomacySourceFilter")->setCurrentIndex(0);
    QSet<QRgb> palette;
    for(int g=0;g<32;++g) palette.insert(diplomacy::groupColor(g).rgb());
    check(palette.size()==32 && diplomacy::groupColor(-1)==diplomacy::groupColor(255),"all32 groups have stable distinct colors; invalid groups are gray");
    check(view->diplomacyMapMode() && button->isChecked() && dialog->findChild<QCheckBox*>("diplomacyMapMode")->isChecked(),"toolbar, map mode and menu checkbox are synchronized");
    view->clearDiplomacyAnchor();
    check(view->diplomacyNpcColor(unit)==diplomacy::groupColor(14)
        && view->diplomacyNpcColor(secondUnit)==diplomacy::groupColor(15),"unselected map colors exact MOB groups across sources");
    auto otherTable=fixtureRelations; otherTable[15][14]=0; otherTable[15][15]=1;
    second->setDiplomacyField(otherTable); secondUnit->setMapId(71);
    const QString secondSource=second->filePath().absoluteFilePath(), firstSource=mob->filePath().absoluteFilePath();
    check(view->focusDiplomacyNpc(secondSource,QString("node:%1").arg(secondUnit->innerId()))
        && view->selectionSnapshot()==QList<CNode*>{secondUnit} && view->currentMob()==mob,
        "navigation resolves exact source/internal identity despite duplicate NIDs and keeps active MOB");
    check(view->diplomacyNpcColor(unit)==diplomacy::relationColor(0)
        && view->diplomacyNpcColor(secondUnit)==diplomacy::relationColor(1),"anchor uses its own MOB table: ally green and neutral yellow");
    view->setDiplomacyAnchor(unit);
    check(view->diplomacyNpcColor(secondUnit)==diplomacy::relationColor(2),"reverse direction can independently be enemy red");
    check(!view->focusDiplomacyNpc(secondSource,"node:missing"),"stale NPC key never guesses by duplicate NID");
    dialog->invalidate(); check(waitFor(*dialog),"fresh cross-MOB navigation rows");
    dialog->findChild<QTabWidget*>()->setCurrentIndex(0); groups->setCurrentItem(groups->topLevelItem(0));
    QTreeWidgetItem* secondRow=nullptr;
    for(int i=0;i<npcs->topLevelItemCount();++i)
        if(npcs->topLevelItem(i)->toolTip(5)==secondSource) secondRow=npcs->topLevelItem(i);
    if(secondRow) {
        npcs->setCurrentItem(secondRow); npcs->scrollToItem(secondRow); QApplication::processEvents();
        const QPoint at=npcs->visualItemRect(secondRow).center();
        QContextMenuEvent context(QContextMenuEvent::Mouse,at,npcs->viewport()->mapToGlobal(at));
        QApplication::sendEvent(npcs->viewport(),&context);
    }
    check(secondRow && !dialog->isVisible() && view->selectionSnapshot()==QList<CNode*>{secondUnit}
        && view->m_diplomacyAnchor==secondUnit,"actual NPC right-click selects precise object and returns to map");
    window.openDiplomacy(); check(waitFor(*dialog),"reopen editor after map navigation");
    dialog->findChild<QTabWidget*>()->setCurrentIndex(2);
    auto* relationChooser=dialog->findChild<QComboBox*>("diplomacyRelationSource");
    relationChooser->setCurrentIndex(relationChooser->findData(firstSource)); relations->setCurrentCell(14,15);
    auto* symmetric=dialog->findChild<QCheckBox*>("diplomacySymmetric");
    auto* ally=dialog->findChild<QPushButton*>("diplomacySetRelation0");
    auto* neutral=dialog->findChild<QPushButton*>("diplomacySetRelation1");
    relations->setCurrentCell(14,15);
    QMetaObject::invokeMethod(relations,"cellClicked",Q_ARG(int,14),Q_ARG(int,15));
    auto* relationMenu=dialog->findChild<QMenu*>("diplomacyRelationMenu");
    check(relationMenu && relationMenu->actions().size()==4,"clicking matrix cell opens three-choice relation editor");
    if(relationMenu && relationMenu->actions().size()==4) relationMenu->actions()[3]->trigger();
    check(mob->diplomacyField()[14][15]==2,"popup action routes selected value to owning MOB");
    if(relationMenu) relationMenu->close();
    view->setDiplomacyAnchor(unit); const auto originalState=mob->parameterState();
    mob->captureCleanProjection(); mob->setDirty(false);
    const int historyBefore=window.m_undoStack->count(); symmetric->setChecked(false); ally->click();
    check(waitFor(*dialog) && mob->diplomacyField()[14][15]==0 && mob->diplomacyField()[15][14]==2
        && window.m_undoStack->count()==historyBefore+1 && mob->isDirty()
        && relations->currentRow()==14 && relations->currentColumn()==15,
        "direct one-way edit changes only selected relation, marks dirty and preserves selected pair");
    check(view->diplomacyNpcColor(secondUnit)==diplomacy::relationColor(0),"map color immediately follows relation edit");
    dialog->findChild<QPushButton*>("diplomacyUndo")->click();
    check(waitFor(*dialog) && mob->parameterState()==originalState,"dialog Undo restores complete previous MOB state");
    dialog->findChild<QPushButton*>("diplomacyRedo")->click();
    check(waitFor(*dialog) && mob->diplomacyField()[14][15]==0 && mob->diplomacyField()[15][14]==2,"dialog Redo reapplies one-way edit");
    symmetric->setChecked(true); neutral->click();
    check(waitFor(*dialog) && mob->diplomacyField()[14][15]==1 && mob->diplomacyField()[15][14]==1,"explicit two-way edit changes both directions atomically");
    window.m_undoStack->undo();
    check(waitFor(*dialog) && mob->diplomacyField()[14][15]==0 && mob->diplomacyField()[15][14]==2,"two-way Undo restores asymmetric originals");
    const auto unchanged=mob->parameterState();
    check(!view->editDiplomacyRelation(firstSource,14,15,2,false,7,2)
        && !view->editDiplomacyRelation(firstSource,32,15,0,false,0,0)
        && !view->editDiplomacyRelation(firstSource,14,15,9,false,0,2)
        && mob->parameterState()==unchanged,"stale snapshot, invalid group and unknown requested relation refuse without mutation");
    second->clearDiplomacyTable(); view->setDiplomacyAnchor(secondUnit);
    check(view->diplomacyNpcColor(unit)==diplomacy::groupColor(-1),"missing owning table shows gray instead of invented neutrality");
    check(view->createDiplomacyTable(secondSource) && second->diplomacyField()[15][14]==1
        && second->diplomacyField()[15][15]==0,"empty owning MOB can explicitly create neutral table");
    window.m_undoStack->undo(); check(second->diplomacyField().isEmpty(),"neutral-table creation is undoable");
    view->setDiplomacyMapMode(false);
    check(!button->isChecked() && !dialog->findChild<QCheckBox*>("diplomacyMapMode")->isChecked()
        && view->m_diplomacyAnchor.isNull(),"turning mode off clears transient anchor and synchronizes controls");
    window.m_undoStack->clear();
    view->m_aMob.removeOne(second); delete second;
    unit->markAsDeleted(); dialog->invalidate();
    check(waitFor(*dialog) && dialog->result().sources[0].npcs.isEmpty(),"deleted NPC removed from live counts");
    view->m_aMob.removeOne(mob); view->m_activeMob=nullptr; dialog->invalidate();
    check(waitFor(*dialog) && dialog->result().sources.isEmpty(),"unloaded MOB leaves no stale membership");
    button->click(); check(window.findChildren<CDiplomacyDialog*>().size()==1,"repeated clicks reuse inspector");
    dialog->hide(); window.m_undoStack->clear(); delete mob;

    // Exercise browsing with long names, long conditions and distinct MOB tables.
    diplomacy::Request browsing; browsing.sources={base}; browsing.preferredSource=base.key;
    browsing.sources[0].relations=fixtureRelations;
    browsing.sources[0].groupNames=QStringList();
    for(int g=0;g<32;++g) browsing.sources[0].groupNames.append("Player-"+QString::number(g));
    const QString longName="Guard with a complete long display name that must remain available";
    const QString longPrototype="SkeletonWarriorLongPrototypeVariant";
    browsing.sources[0].npcs[0].name=longName; browsing.sources[0].npcs[0].prototype=longPrototype;
    browsing.sources[0].script+="\nif(And(IsAlive(GetObject(7)),IsEqual(GSGetVar(0,\"long-condition-marker-for-full-details\"),1))) then(SetPlayer(GetObject(7),5))";
    for(uint id:{100u,10u,2u}) {
        auto extraNpc=npc; extraNpc.id=id; extraNpc.key="id"+QString::number(id);
        extraNpc.name="ID fixture "+QString::number(id); extraNpc.group=2;
        browsing.sources[0].npcs.append(extraNpc);
    }
    diplomacy::Source readOnly; readOnly.key=files.filePath("read-only.mob"); readOnly.groupNames=browsing.sources[0].groupNames;
    readOnly.relations=QVector<QVector<uint>>(32,QVector<uint>(32,1)); readOnly.relations[3][4]=7;
    auto readOnlyNpc=npc; readOnlyNpc.id=900; readOnlyNpc.key="npc900"; readOnlyNpc.group=17;
    readOnly.npcs={readOnlyNpc}; browsing.sources.append(readOnly);
    CDiplomacyDialog browser([&browsing]{return browsing;},&window);
    browser.setAttribute(Qt::WA_DontShowOnScreen); browser.show();
    check(waitFor(browser),"browsing fixture completes asynchronous inspection");
    auto* browseGroups=browser.findChild<QTreeWidget*>("diplomacyGroups");
    auto* browseNpcs=browser.findChild<QTreeWidget*>("diplomacyNpcs");
    auto* browseScripts=browser.findChild<QTreeWidget*>("diplomacyScripts");
    auto* browseTabs=browser.findChild<QTabWidget*>();
    auto* hideEmpty=browser.findChild<QCheckBox*>("diplomacyHideEmptyGroups");
    auto* detail=browser.findChild<QPlainTextEdit*>("diplomacyDetails");
    auto* search=browser.findChild<QLineEdit*>("diplomacyNpcFilter");
    auto* relationSources=browser.findChild<QComboBox*>("diplomacyRelationSource");
    auto* browseMatrix=browser.findChild<QTableWidget*>("diplomacyRelations");
    auto* editButton=browser.findChild<QPushButton*>("diplomacyEditRelations");
    check(!browseTabs->tabIcon(0).isNull() && !browseTabs->tabIcon(1).isNull() && !browseTabs->tabIcon(2).isNull(),"NPC, script and relation sections have icons");
    const QString total=browseGroups->topLevelItem(0)->text(1);
    hideEmpty->setChecked(true);
    check(browseGroups->topLevelItem(2)->isHidden() && !browseGroups->topLevelItem(3)->isHidden()
        && browseGroups->topLevelItem(0)->text(1)==total,"empty group filter retains counts and occupied groups");
    hideEmpty->setChecked(false);
    check(!browseGroups->topLevelItem(2)->isHidden(),"empty groups remain available on demand");
    browseGroups->setCurrentItem(browseGroups->topLevelItem(2)); hideEmpty->setChecked(true);
    check(browseGroups->currentItem()==browseGroups->topLevelItem(0),"hiding selected empty group returns to all groups");
    browseNpcs->sortItems(1,Qt::AscendingOrder);
    QStringList sortedIds; for(int i=0;i<browseNpcs->topLevelItemCount();++i) {
        const auto value=browseNpcs->topLevelItem(i)->data(1,Qt::UserRole);
        if(value.isValid()) sortedIds.append(browseNpcs->topLevelItem(i)->text(1));
    }
    check(sortedIds==QStringList{"2","7","8","10","100","900"},"ID sorting is numeric instead of lexical");
    browser.resize(1000,650); QApplication::processEvents();
    check(browseNpcs->columnWidth(0)>=240 && browseScripts->columnWidth(0)>=300,"narrow window preserves readable primary columns with horizontal scrolling");
    auto npcRow=[&](const QString& id)->QTreeWidgetItem* {
        for(int i=0;i<browseNpcs->topLevelItemCount();++i) if(browseNpcs->topLevelItem(i)->text(1)==id) return browseNpcs->topLevelItem(i);
        return nullptr;
    };
    browseNpcs->setCurrentItem(npcRow("7"));
    check(npcRow("7") && npcRow("7")->text(0)==longName && npcRow("7")->text(4)==longPrototype
        && detail->toPlainText().contains(longName) && detail->toPlainText().contains(longPrototype)
        && detail->toPlainText().contains(base.key) && detail->isReadOnly(),"separate NPC/prototype columns and full copyable details");
    search->setText("no-browsing-match");
    check(browseNpcs->topLevelItemCount()==0 && !detail->toPlainText().contains(longName)
        && browseGroups->topLevelItem(0)->text(1)==total,"empty search clears stale details without changing totals");
    search->clear(); browseGroups->setCurrentItem(browseGroups->topLevelItem(3)); browseNpcs->setCurrentItem(npcRow("7"));
    browser.invalidate();
    check(!detail->toPlainText().contains(longName),"refresh clears old details while worker runs");
    check(waitFor(browser) && browseGroups->currentItem()->data(0,Qt::UserRole).toInt()==2
        && browseNpcs->currentItem()->text(1)=="7","refresh preserves selected group and NPC identity");
    browseGroups->setCurrentItem(browseGroups->topLevelItem(0)); browseTabs->setCurrentIndex(1);
    QTreeWidgetItem* conditionRow=nullptr;
    for(int i=0;i<browseScripts->topLevelItemCount();++i)
        if(browseScripts->topLevelItem(i)->toolTip(4).contains("long-condition-marker")) conditionRow=browseScripts->topLevelItem(i);
    if(conditionRow) browseScripts->setCurrentItem(conditionRow);
    check(conditionRow && !conditionRow->text(0).contains('\n') && !conditionRow->text(4).contains('\n')
        && detail->toPlainText().contains("long-condition-marker") && detail->toPlainText().contains("SetPlayer"),"long script conditions use full details instead of multiline list rows");
    search->setText("long-condition-marker");
    check(browseScripts->topLevelItemCount()==1 && detail->toPlainText().contains("long-condition-marker"),"script search includes complete conditions");
    search->clear(); browseTabs->setCurrentIndex(2);
    check(browseMatrix->rowCount()==32 && browseMatrix->columnCount()==32
        && browseMatrix->horizontalHeaderItem(31)->text()=="31"
        && browseMatrix->horizontalHeaderItem(31)->toolTip().contains("Player-31"),"compact matrix retains all groups and full header tooltips");
    int editRequests=0; QString editedSource;
    QObject::connect(&browser,&CDiplomacyDialog::relationChangeRequested,[&](const QString& source,int,int,uint,bool,uint,uint){++editRequests;editedSource=source;});
    relationSources->setCurrentIndex(relationSources->findData(readOnly.key));
    browseMatrix->setCurrentCell(3,4);
    check(browseMatrix->item(3,4)->text()=="7" && !browseMatrix->item(3,4)->data(Qt::BackgroundRole).isValid()
        && !editButton->isEnabled() && browseGroups->topLevelItem(0)->text(1)==total,"independent source chooser displays read-only MOB values and preserves overview counts");
    check(browser.findChild<QLabel*>("diplomacyRelationSelection")->text().contains("3 · Player-3")
        && browser.findChild<QLabel*>("diplomacyRelationSelection")->text().contains("4 · Player-4")
        && browser.findChild<QLabel*>("diplomacyRelationSelection")->text().endsWith("(7)"),"selected relation describes exact source and target including unknown values");
    auto* browseAlly=browser.findChild<QPushButton*>("diplomacySetRelation0");
    browseAlly->click(); relationSources->setCurrentIndex(relationSources->findData(base.key)); browseAlly->click();
    check(editRequests==1 && editedSource==base.key,"direct editing routes only to selected loaded MOB");
    check(browseMatrix->item(0,0)->background().color()==diplomacy::relationColor(0).darker(220)
        && browseMatrix->item(1,2)->background().color()==diplomacy::relationColor(2).darker(220)
        && browseMatrix->item(1,3)->background().color()==diplomacy::relationColor(1).darker(220),"readable relation matrix keeps green/yellow/red meanings");
    browsing.sources={readOnly}; browser.invalidate();
    check(waitFor(browser) && relationSources->count()==1 && browseMatrix->item(3,4)->text()=="7" && !editButton->isEnabled(),"removing selected source cannot show or edit stale relations");
    browsing.sources[0].relations.clear(); browser.invalidate();
    check(waitFor(browser) && browseMatrix->rowCount()==0
        && browser.findChild<QLabel*>("diplomacyRelationSelection")->text().isEmpty(),"incomplete source clears matrix and selected pair");
    browser.hide();
    // The same guarded lossless writer used by Save, against owned synthetic files.
    CMob seed; seed.setDiplomacyField(fixtureRelations); seed.setScript("WorldScript(ConsoleString(\"saved-script\"))");
    QByteArray raw; seed.serializeMob(raw,false);
    raw=record(0xA000,raw.mid(8)+record(0xDEADF00D,QByteArray::fromHex("0100feff112233445566")));
    const QString saveSource=files.filePath("save-source.mob"), saveTarget=files.filePath("save-edited.mob");
    QFile saveFile(saveSource);
    check(saveFile.open(QIODevice::WriteOnly) && saveFile.write(raw)==raw.size(),"owned save fixture with opaque extension"); saveFile.close();
    auto* savedMob=new CMob; savedMob->attach(view,view->m_pProgress); QFileInfo saveInfo(saveSource); QStringList saveWarnings;
    const bool opened=savedMob->readMob(saveInfo,false,&saveWarnings)==0;
    check(opened,"production MOB reader opens save fixture");
    if(opened) {
        view->m_aMob.append(savedMob);
        check(view->editDiplomacyRelation(saveSource,2,3,0,false,fixtureRelations[2][3],fixtureRelations[3][2])
            && savedMob->saveAs(QFileInfo(saveTarget)),"relation edit saves through production safe writer");
        QByteArray expected=raw; const int offset=raw.indexOf(u32(0xDDDDDDD2));
        if(offset>=0) expected.replace(offset+8+(2*32+3)*4,4,u32(0));
        QFile savedFile(saveTarget); const bool readable=savedFile.open(QIODevice::ReadOnly);
        const QByteArray saved=readable?savedFile.readAll():QByteArray(); savedFile.close();
        CMobDocument reopened; CValidationReport savedReport; QVector<QVector<quint32>> savedRelations;
        check(offset>=0 && saved==expected && reopened.loadBytes(saved,saveTarget,&savedReport)
            && reopened.diplomacyTable(&savedRelations) && savedRelations[2][3]==0
            && savedRelations[3][2]==fixtureRelations[3][2],"fresh readback differs only in requested cell; opaque/script/all other bytes preserved");
        saveFile.open(QIODevice::ReadOnly); check(saveFile.readAll()==raw,"Save As preserves source fixture"); saveFile.close();
        window.m_undoStack->clear(); view->m_aMob.removeOne(savedMob);
    }
    delete savedMob;
    if(!qEnvironmentVariableIsEmpty("EI_DIPLOMACY_AUDIT_MOB")) {
        diplomacy::Request corpus; corpus.extraSources=QStringList{qEnvironmentVariable("EI_DIPLOMACY_AUDIT_MOB")};
        corpus.roots=QStringList{QFileInfo(corpus.extraSources[0]).absolutePath()};
        QElapsedTimer elapsed; elapsed.start(); const auto inspected=diplomacy::inspectConnected(corpus);
        int units=0; for(const auto& src:inspected.sources) for(const auto& n:src.npcs) if(!n.scripted) ++units;
        out<<"CORPUS: sources="<<inspected.sources.size()<<" NPCs="<<units<<" events="<<inspected.events.size()<<" diagnostics="<<inspected.diagnostics.size()<<" elapsedMs="<<elapsed.elapsed()<<"\n";
        for(const auto& diagnostic:inspected.diagnostics) out<<"CORPUS-DIAGNOSTIC: "<<diagnostic<<"\n";
        check(!inspected.sources.isEmpty() && units>0,"read-only installed MOB NPC and script corpus");
        if(!qEnvironmentVariableIsEmpty("EI_DIPLOMACY_CAPTURE")) {
            CDiplomacyDialog preview([corpus]{return corpus;},&window); preview.setAttribute(Qt::WA_DontShowOnScreen); preview.show();
            preview.resize(1560,940); QApplication::processEvents();
            check(waitFor(preview) && preview.result().sources.size()==inspected.sources.size()
                && preview.result().diagnostics==inspected.diagnostics
                && preview.grab().save(qEnvironmentVariable("EI_DIPLOMACY_CAPTURE")+".corpus.png"),"hidden real corpus overview capture");
            preview.findChild<QTabWidget*>()->setCurrentIndex(1);
            QApplication::processEvents();
            check(preview.grab().save(qEnvironmentVariable("EI_DIPLOMACY_CAPTURE")+".corpus-scripts.png"),"hidden real corpus compact scripts capture");
            preview.findChild<QTabWidget*>()->setCurrentIndex(2);
            QApplication::processEvents();
            check(preview.grab().save(qEnvironmentVariable("EI_DIPLOMACY_CAPTURE")+".corpus-relations.png"),"hidden real corpus relation matrix capture");
            preview.resize(1000,650); QApplication::processEvents();
            check(preview.grab().save(qEnvironmentVariable("EI_DIPLOMACY_CAPTURE")+".corpus-narrow-relations.png"),"hidden narrow relation matrix capture");
            preview.findChild<QTabWidget*>()->setCurrentIndex(0); QApplication::processEvents();
            check(preview.grab().save(qEnvironmentVariable("EI_DIPLOMACY_CAPTURE")+".corpus-narrow-npcs.png"),"hidden narrow NPC details capture");
        }
    }
    qApp->removeEventFilter(&hidden); out<<"RESULT: "<<passed<<" PASS, "<<failed<<" FAIL\n";
    return failed?2:0;
}
