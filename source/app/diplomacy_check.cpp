#include "diplomacy_check.h"
#include "diplomacy_dialog.h"
#include "diplomacy_analysis.h"
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
    mob->setDiplomacyField(QVector<QVector<uint>>(32,QVector<uint>(32,0)));
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
    check(relations->rowCount()==32 && relations->item(1,2)->text()=="0" && relations->editTriggers()==QAbstractItemView::NoEditTriggers,"read-only actual relation matrix");
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
    view->m_aMob.removeOne(second); delete second;
    unit->markAsDeleted(); dialog->invalidate();
    check(waitFor(*dialog) && dialog->result().sources[0].npcs.isEmpty(),"deleted NPC removed from live counts");
    view->m_aMob.removeOne(mob); view->m_activeMob=nullptr; dialog->invalidate();
    check(waitFor(*dialog) && dialog->result().sources.isEmpty(),"unloaded MOB leaves no stale membership");
    button->click(); check(window.findChildren<CDiplomacyDialog*>().size()==1,"repeated clicks reuse inspector");
    dialog->hide(); window.m_undoStack->clear(); delete mob;
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
            check(waitFor(preview) && preview.result().sources.size()==inspected.sources.size()
                && preview.result().diagnostics==inspected.diagnostics
                && preview.grab().save(qEnvironmentVariable("EI_DIPLOMACY_CAPTURE")+".corpus.png"),"hidden real corpus overview capture");
        }
    }
    qApp->removeEventFilter(&hidden); out<<"RESULT: "<<passed<<" PASS, "<<failed<<" FAIL\n";
    return failed?2:0;
}
