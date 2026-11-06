// EI-MINIMAP-IDENTITY-281: isolated production MainWindow regression, no desktop capture.
#include <QtWidgets>
#include <QtTest>
#include <windows.h>
#include <cstring>
#include <algorithm>
#define private public
#include "main_window.h"
#include "view.h"
#undef private
#include "landscape.h"
#include "mob.h"
#include "settings.h"
#include "resource_setup_dialog.h"
#include "viewport_minimap_widget.h"
#include "resourcemanager.h"
#include "jmv_map_test_launcher.h"
#include "travel_document.h"

namespace {
QString root;
QTextStream out(stdout);
int passes=0, visible=0;
void check(bool ok,const QString& text) { out<<(ok?"PASS ":"FAIL ")<<text<<'\n';out.flush();if(!ok)throw text;++passes; }
struct Hidden:QObject { bool eventFilter(QObject* object,QEvent* event) override {
    if(event->type()==QEvent::Polish||event->type()==QEvent::Show)
        if(auto* w=qobject_cast<QWidget*>(object))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
    return false;
}};
BOOL CALLBACK inspect(HWND w,LPARAM) {DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(w))++visible;return TRUE;}
// Qt Windows uses Known Folders, not APPDATA environment variables. Redirect only
// this process's writable locations before constructing QApplication; never patch a DLL file.
QString writable(QStandardPaths::StandardLocation location) {return qEnvironmentVariable("EI_IDENTITY_CASE")+"/known-folders/"+QString::number(int(location));}
void isolate() {
    auto entry=GetProcAddress(GetModuleHandleW(L"Qt5Core.dll"),"_ZN14QStandardPaths16writableLocationENS_16StandardLocationE");
    if(!entry)throw QString("Qt writableLocation export missing");
    unsigned char jump[12]={0x48,0xb8};auto target=reinterpret_cast<quint64>(&writable);std::memcpy(jump+2,&target,8);jump[10]=0xff;jump[11]=0xe0;
    DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(entry),12,PAGE_EXECUTE_READWRITE,&old))throw QString("Qt isolation failed");
    std::memcpy(reinterpret_cast<void*>(entry),jump,12);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(entry),12);DWORD ignored;VirtualProtect(reinterpret_cast<void*>(entry),12,old,&ignored);
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
    if(qEnvironmentVariableIsEmpty("EI_IDENTITY_CASE")||qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"))return 2;
    isolate();QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication app(argc,argv);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setOrganizationName("Ataman isolated identity281");QCoreApplication::setApplicationName("minimap-identity-check");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("EI_MAPPER_SETTINGS_DIR"));
    app.setProperty("workspaceLoadSmoke",true);Hidden hidden;app.installEventFilter(&hidden);out.setCodec("UTF-8");
    root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_IDENTITY_CASE"));
    const QString fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_IDENTITY_FIXTURE"));
    const bool red=app.arguments().contains("--expect-red");
    const bool renamedRed=app.arguments().contains("--expect-renamed-red");
    try {
        check(root.contains("/work/minimap-identity-281-20261106/")&&fixture.contains("/work/minimap-identity-281-20261106/"),"owned isolated paths");
        QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[] {
            EnumWindows(inspect,0);
            for(auto* w:QApplication::topLevelWidgets()) {
                if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();
                if(auto* b=qobject_cast<QMessageBox*>(w))b->reject();
            }
        });dialogs.start(5);
        MainWindow window;window.configureAutosaveTimer(false,300000);
        window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        auto& settings=*window.m_settings;
        dynamic_cast<COptString*>(settings.opt("resourceProfile"))->setValue("deja-vu");
        dynamic_cast<COptString*>(settings.opt("gameRoot"))->setValue(fixture);
        dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue({fixture+"/res"});
        dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"mapRoots"))->setValue({fixture+"/Maps"});
        CTextureList::getInstance()->attachSettings(&settings);
        window.setAttribute(Qt::WA_DontShowOnScreen);window.show();QCoreApplication::processEvents();
        window.refreshMapsBrowser();
        const QString mpr=fixture+"/Maps/zone1.mpr";
        QJsonArray catalog;QStringList ids;
        for(const auto& e:window.m_mapCatalog.entries)if(e.mprName=="zone1") {
            ids<<e.zoneId;catalog.append(QJsonObject{{"zone",e.zoneId},{"type",e.zoneType},{"mpr",e.mprPath},{"mob",e.mobPath}});
            check(QFileInfo(e.mprPath).absoluteFilePath()==QFileInfo(mpr).absoluteFilePath(),"catalog/openMapPair same copied MPR: "+e.zoneId);
        }
        check(ids.indexOf("bz1g")>=0&&ids.indexOf("bz1g")<ids.indexOf("gz1g")&&ids.contains("cz1g"),"live catalog order bz1g before gz1g, cz1g retained");
        QJsonArray states;
        auto open=[&](const QString& zone,const QString& mob,const QString& tag,const QString& kind=QString()) {
            window.openMapPair(mpr,fixture+"/Maps/"+mob+".mob",zone);
            QElapsedTimer timer;timer.start();
            while((window.view()->hasPendingOperation()||window.backgroundMobLoading())&&timer.elapsed()<120000)QTest::qWait(10);
            check(timer.elapsed()<120000&&CLandscape::getInstance()->isMprLoad()&&window.view()->hasActiveMob(),tag+": ordinary openMapPair completed");
            if(!kind.isEmpty()) {window.m_workspaceIdentityKind=kind;window.refreshMapTestContext(false,false);}
            window.m_minimapRefreshTimer->stop();window.refreshViewportMinimapSources();
            const auto& e=window.m_mapTestEntry;
            window.scheduleViewportMinimapRefresh(60000,true);
            const bool generation=window.m_minimapRefreshTimer->isActive()||window.m_minimapCaptureRunning;window.m_minimapRefreshTimer->stop();
            QJsonArray entrances;for(int i=0;i<window.m_entranceCombo->count();++i)entrances.append(window.m_entranceCombo->itemText(i));
            const auto profile=window.activeResourceProfile();QString autoReason;
            const bool autoSpell=CJmvMapTestLauncher::hasAutoSpellMob(profile,e.zoneType,e.zoneId.isEmpty(),&autoReason);
            // Read-only capability check against the authorized game installation;
            // no runtime is staged or started, and the editor profile stays isolated.
            auto support=profile;QString liveReason;bool liveAuto=false;
            if(!qEnvironmentVariableIsEmpty("EI_IDENTITY_READONLY_GAME_ROOT")) {
                support.gameRoot=qEnvironmentVariable("EI_IDENTITY_READONLY_GAME_ROOT");
                support.modRoot=support.gameRoot+"/MODS/Dejavu";support.mapDirectories=QStringList{support.modRoot+"/Maps"};
                support.runtimeComponents.clear();
                liveAuto=CJmvMapTestLauncher::hasAutoSpellMob(support,e.zoneType,e.zoneId.isEmpty(),&liveReason);
            }
            auto* staging=window.mapTestStagingMob();
            QJsonArray seeds;const auto request=window.mobFamilyValidationRequest(true);for(const auto& p:request.seedMobPaths)seeds.append(p);
            QJsonObject state{{"tag",tag},{"zone",e.zoneId},{"type",e.zoneType},{"briefing",e.isBriefing()},
                {"visible",window.m_viewportMinimap->isVisible()},{"generationAllowed",generation},{"captureRunning",window.m_minimapCaptureRunning},{"activeMob",window.view()->currentMob()->filePath().fileName()},
                {"stagingMob",staging?staging->filePath().fileName():QString()},{"entryMob",e.mobName},{"sky",e.skyType},{"allod",e.allodId},
                {"minimapTexture",e.minimapTexture},{"contextKey",window.currentMapContextKey()},{"entrances",entrances},
                {"playFromHere",window.m_playFromHereButton->isEnabled()},{"godmode",window.m_godmodeCheckBox->isEnabled()},
                {"autoSpell",autoSpell},{"autoSpellReason",autoReason},{"mapTestEnabled",window.m_mapTestAction->isEnabled()},
                {"mapTestReason",window.m_mapTestAction->toolTip()},{"familySeeds",seeds},{"familyLabel",request.familyLabel},
                {"mappedPrimaryMob",QFileInfo(request.mappedPrimaryMobPath).fileName()},{"liveAutoSpell",liveAuto},{"liveAutoSpellReason",liveReason},
                {"travelZone",window.m_travelDocument&&window.m_travelZone>=0?window.m_travelDocument->table().zones()[window.m_travelZone].zoneId:QString()},
                {"cameraStatus",window.m_cameraStatus?window.m_cameraStatus->text():QString()}};
            states.append(state);out<<"STATE "<<QJsonDocument(state).toJson(QJsonDocument::Compact)<<'\n';out.flush();
            const bool expectBrief=(red&&kind!="world")||zone=="bz1g"||mob=="basecam";
            check(e.zoneId==(expectBrief?"bz1g":"gz1g")&&e.isBriefing()==expectBrief,tag+": "+(red?"RED wrong bz1g reproduced":"correct zone identity"));
            check(window.m_viewportMinimap->isVisible()==!expectBrief&&generation==!expectBrief,tag+": minimap visibility and generation guard");
            check(window.m_validEntrances.size()==(expectBrief?1:6)&&staging&&staging->filePath().completeBaseName()==mob,tag+": entrances and staging MOB");
            if(!qEnvironmentVariableIsEmpty("EI_IDENTITY_READONLY_GAME_ROOT"))check(liveAuto==!expectBrief,tag+": read-only real autorun capability gate");
            return state;
        };
        open("gz1g","zone1","gz-first");
        {
            open("bz1g","basecam","briefing");open("gz1g","zone1","gz-return");
            open(QString(),"zone1","implicit-game");open(QString(),"basecam","implicit-briefing");
            open(QString(),"zone1","world-game","world");open(QString(),"basecam","world-briefing","world");
        }
        // R2: retain 280's MPR fallback for copied/renamed primary MOBs.
        // Use the real catalog snapshot; do not synthesize its zone order/types.
        const auto fullCatalog=window.m_mapCatalog;
        QVector<SMapCatalogEntry> unique;
        for(const auto& e:fullCatalog.entries)if(e.mprName=="zone10")unique.append(e);
        check(unique.size()==1&&!unique.first().travelZones.isEmpty(),"zone10 has one real catalog entry with entrances");
        auto copied=[&](const QString& map,const QString& expected,const QString& tag,const QString& kind=QString(),const QString& explicitId=QString()) {
            window.m_mapCatalog=fullCatalog;
            window.openMapPair(fixture+"/Maps/"+map+".mpr",fixture+"/Maps/"+map+"_work.mob",explicitId);
            QElapsedTimer timer;timer.start();
            while((window.view()->hasPendingOperation()||window.backgroundMobLoading())&&timer.elapsed()<120000)QTest::qWait(10);
            check(timer.elapsed()<120000&&window.view()->hasActiveMob(),tag+": copied primary opened");
            if(!kind.isEmpty()){window.m_workspaceIdentityKind=kind;window.refreshMapTestContext(false,false);}
            window.m_minimapRefreshTimer->stop();
            auto* primary=window.mapTestStagingMob();
            check(primary&&primary->filePath().completeBaseName()==map+"_work"&&!primary->isQuestMob(),tag+": structurally primary copied MOB retained");
            const auto& entry=window.m_mapTestEntry;
            QJsonObject state{{"tag",tag},{"zone",entry.zoneId},{"type",entry.zoneType},{"entranceCount",window.m_validEntrances.size()},
                {"stagingMob",primary->filePath().fileName()},{"travelZone",window.m_travelDocument&&window.m_travelZone>=0?window.m_travelDocument->table().zones()[window.m_travelZone].zoneId:QString()}};
            states.append(state);out<<"STATE "<<QJsonDocument(state).toJson(QJsonDocument::Compact)<<'\n';out.flush();
            const QString wanted=renamedRed?QString():expected;
            check(entry.zoneId==wanted,tag+(renamedRed?": RED lost identity reproduced":": 280-compatible identity"));
            int expectedEntrances=0;
            if(!wanted.isEmpty())for(const auto& e:fullCatalog.entries)if(e.zoneId==wanted)
                for(const auto& entrance:e.travelZones)if(entrance.exitId>0&&entrance.hasDeployRectangle&&!entrance.deployRectangle.normalized().isEmpty())++expectedEntrances;
            check(window.m_validEntrances.size()==expectedEntrances,tag+": entrance availability");
        };
        copied("zone10",unique.first().zoneId,"renamed-unique");
        copied("zone1","bz1g","renamed-shared");
        copied("zone10",QString(),"renamed-world","world");
        copied("zone10",unique.first().zoneId,"unknown-explicit-fallback",QString(),"not-in-catalog");
        window.m_mapCatalog=fullCatalog;
        std::reverse(window.m_mapCatalog.entries.begin(),window.m_mapCatalog.entries.end());
        window.openMapPair(mpr,fixture+"/Maps/basecam.mob",QString());
        QElapsedTimer orderTimer;orderTimer.start();
        while((window.view()->hasPendingOperation()||window.backgroundMobLoading())&&orderTimer.elapsed()<120000)QTest::qWait(10);
        check(orderTimer.elapsed()<120000&&window.m_mapTestEntry.zoneId=="bz1g","MOB match beats earlier MPR fallback with reversed catalog order");
        window.m_minimapRefreshTimer->stop();
        EnumWindows(inspect,0);check(visible==0,"zero native visible windows");
        QFile result(root+"/result.json");check(result.open(QIODevice::WriteOnly),"evidence writable");
        result.write(QJsonDocument(QJsonObject{{"red",red},{"renamedRed",renamedRed},{"catalog",catalog},{"states",states},{"passes",passes},{"visibleWindows",visible}}).toJson());
        window.view()->unloadLand(false);out<<"PASS TOTAL "<<passes<<'\n';return 0;
    }catch(const QString& error) {out<<"FAIL "<<error<<'\n';return 1;}
}
