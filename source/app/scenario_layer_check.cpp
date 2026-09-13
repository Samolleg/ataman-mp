#include "scenario_layer_check.h"
#include "scenario_panel.h"
#include "mob_script_window.h"
#include "script_editor.h"
#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "settings.h"
#include "profile_database.h"
#include "tree_view.h"
#include "progressview.h"
#include "tile_form.h"
#include "utils.h"
#include "localization.h"
#include "camera.h"
#include "gl_dispatch.h"
#include "runtime/providers/profile_database_provider_support.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QUndoStack>
#include <QTextCursor>
#include <QTimer>
#include <QMessageBox>
#include <QCheckBox>
#include <QImage>
#include <QAbstractButton>
#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QElapsedTimer>
#include <QCryptographicHash>

namespace {
class HiddenCheckWidgets : public QObject {
    bool eventFilter(QObject* target,QEvent* event) override {
        if(event->type()==QEvent::Polish) if(auto* widget=qobject_cast<QWidget*>(target))
            if(widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
}

int runScenarioLayerCheck()
{
    QTextStream output(stdout);
    int failures=0,checks=0;
    auto check=[&](bool pass,const char* name) { ++checks; if(!pass) ++failures; output << (pass?"PASS: ":"FAIL: ") << name << "\n"; output.flush(); };
    QTemporaryDir scratch;
    if(!scratch.isValid()) return 1;
    struct CurrentDirectoryGuard { QString old=QDir::currentPath(); ~CurrentDirectoryGuard(){QDir::setCurrent(old);} } directoryGuard;
    QDir::setCurrent(scratch.path());
    HiddenCheckWidgets hiddenWidgets; qApp->installEventFilter(&hiddenWidgets);
    if(qEnvironmentVariable("EI_SCENARIO_CHECK_LANGUAGE")=="ru")
        check(ei::installApplicationTranslator(*qApp,"ru"),"Russian scenario catalog loaded");
    CSettings settings;
    QUndoStack history;
    CTreeView tree; QTableWidget properties; properties.setColumnCount(2);
    CProgressView progress; QLineEdit coordinates; CTileForm tiles;
    CView view(nullptr,nullptr); view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
    CMob* mob=new CMob;
    mob->setFileName(QFileInfo(scratch.filePath("scenario.mob")));
    QSharedPointer<ei::SProfileDatabaseSnapshot> database(new ei::SProfileDatabaseSnapshot);
    database->monsters.insert("fixture.guard",ei::SMonsterDatabaseRecord());
    ei::ProfileDatabaseContext::instance().activate(database,{});
    const QString original="WorldScript(\nSetCP(GetObject(7),12,14,2)\nSwitchLeverState(GetObjectByID(\"8\"),1)\n"
        "AddUnitToServer(\"visitor\",\"fixture.guard\",\"Visitor\",6,7,2)\nSetCP(GetObjectByName(\"visitor\"),8,9,3)\n)";
    mob->setScript(original);
    auto* object=new CWorldObj; object->setMapId(7); object->setState(eDraw);
    QVector3D position(1,2,3); object->setPos(position); object->setDrawPosition(position);
    mob->nodes().append(object);
    auto* lever=new CWorldObj; lever->setMapId(8); lever->setState(eDraw);
    position=QVector3D(4,5,6); lever->setPos(position); lever->setDrawPosition(position);
    mob->nodes().append(lever);
    mob->captureCleanProjection(); mob->setDirty(false);
    view.m_aMob.append(mob); view.m_activeMob=mob;
    CScenarioPanel panel(&view); panel.setAttribute(Qt::WA_DontShowOnScreen);
    panel.resize(1050,760); panel.setLayerEnabled(true);
    QByteArray before; mob->serializeMob(before,false);
    check(panel.analysis().markers.size()==3 && view.m_scenarioParticipants.size()==2 && view.m_scenarioSpawns.size()==1,"live object, spawn and destination snapshots");
    auto* refs=panel.findChild<QTreeWidget*>("scenarioReferences");
    check(refs && refs->topLevelItemCount()==7,"operation and nested lookup rows");
    refs->setCurrentItem(refs->topLevelItem(0));
    check(view.m_scenarioLinks.size()==2 && view.m_scenarioDestinations.size()==1,"selected participant links only");
    const QString capture=qEnvironmentVariable("EI_SCENARIO_CHECK_OUTPUT");
    if(!capture.isEmpty()) { QDir().mkpath(capture); check(panel.grab().save(QDir(capture).filePath("scenario-panel.png")),"offscreen scenario panel capture"); }
    if(qEnvironmentVariableIntValue("EI_SCENARIO_CHECK_GL")) {
        view.resize(640,480); view.makeCurrent();
        check(view.isValid() && QOpenGLContext::currentContext(),"native GL context for scenario glyphs");
        if(view.isValid() && QOpenGLContext::currentContext()) {
            auto* gl=QOpenGLContext::currentContext()->functions();
            const bool shader=view.m_program.addShaderFromSourceFile(QOpenGLShader::Vertex,":/vshader.vsh")
                && view.m_program.addShaderFromSourceFile(QOpenGLShader::Fragment,":/fshader.fsh") && view.m_program.link();
            check(shader,"production overlay shader compilation");
            if(shader) {
                view.m_cam->setLocation(QVector3D(0,0,30),QVector3D(),QVector3D());
                view.m_projection.setToIdentity(); view.m_projection.ortho(-2.f,20.f,-2.f,20.f,0.1f,60.f);
                QOpenGLFramebufferObject frame(640,480,QOpenGLFramebufferObject::CombinedDepthStencil);
                frame.bind(); gl->glViewport(0,0,640,480); gl->glClearColor(0,0,0,1); gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                view.m_program.bind();
                view.m_program.setUniformValue("u_instanced",true); view.m_program.setUniformValue("u_partAnimated",true);
                QMatrix4x4 stale; stale.translate(1000,1000,1000); view.m_program.setUniformValue("u_partMmatrix",stale);
                while(gl->glGetError()!=GL_NO_ERROR) {}
                view.drawTestOverlaysOpenGl();
                check(gl->glGetError()==GL_NO_ERROR,"scenario overlay GL state regression");
                const QImage image=frame.toImage().convertToFormat(QImage::Format_RGB32);
                int cyan=0,purple=0,amber=0;
                for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
                    const QColor c=image.pixelColor(x,y);
                    if(c.red()<80 && c.green()>160 && c.blue()>170) ++cyan;
                    if(c.red()>160 && c.green()<150 && c.blue()>170) ++purple;
                    if(c.red()>170 && c.green()>120 && c.blue()<100) ++amber;
                }
                check(cyan>10 && purple>10 && amber>10,"rendered participant, spawn and selected destination pixels");
                if(!capture.isEmpty()) image.save(QDir(capture).filePath("scenario-overlay.png"));
                frame.release();
            }
        }
    }
    panel.findChild<QPushButton*>("scenarioGoToTarget")->click();
    check(object->nodeState()==eSelect && history.count()==0 && !mob->isDirty(),"object navigation does not edit or add history");
    check(view.openScenarioSource(mob,view.scenarioScriptText(mob),original.indexOf("SetCP")),"open exact source revision");
    auto window=view.m_scriptWindows.value(mob);
    window->setAttribute(Qt::WA_DontShowOnScreen);
    auto* editor=window->findChild<CScriptEditor*>();
    check(editor && editor->textCursor().blockNumber()==1,"code navigation source line");
    const QString draft="// draft line\n"+original;
    editor->setPlainText(draft); panel.refresh();
    check(panel.snapshot().sources[0].draft && panel.analysis().references[0].line==3 && mob->script()==original,"unapplied buffer analyzed with correct source lines");
    check(!view.openScenarioSource(mob,original,0),"stale source navigation rejected");
    const int draftHistory=history.count();
    check(view.openScenarioSource(mob,draft,draft.indexOf("SwitchLeverState")) && editor->textCursor().blockNumber()==3
          && editor->toPlainText()==draft && history.count()==draftHistory,"draft navigation preserves text and history");
    QByteArray after; mob->serializeMob(after,false);
    check(before==after && !mob->isDirty(),"analysis and navigation preserve MOB bytes and dirty state");
    const QString applied="WorldScript(GetObject(8))";
    view.commitMobScriptEdit(mob,applied,"synthetic scenario edit"); panel.refresh();
    check(editor->toPlainText()==draft && panel.snapshot().sources[0].text==draft,"document edit retains independent local draft");
    history.undo(); panel.refresh();
    check(mob->script()==original && editor->toPlainText()==draft,"document undo retains independent local draft");
    history.redo(); panel.refresh();
    check(mob->script()==applied && editor->toPlainText()==draft,"document redo retains independent local draft");
    if(QGuiApplication::platformName()=="windows") {
        QTimer::singleShot(30,[](){ for(QWidget* w:QApplication::topLevelWidgets()) if(auto* box=qobject_cast<QMessageBox*>(w)) box->button(QMessageBox::Cancel)->click(); });
        check(!window->requestClose() && window->mob()==mob && editor->toPlainText()==draft,"cancel close preserves draft and owner");
        QTimer::singleShot(30,[](){ for(QWidget* w:QApplication::topLevelWidgets()) if(auto* box=qobject_cast<QMessageBox*>(w)) box->button(QMessageBox::Discard)->click(); });
        check(window->requestClose(),"actual close/discard accepted without showing a window");
    } else {
        output << "SKIP: offscreen/minimal QMessageBox crashes independently; modal close tested with the hidden native Windows platform\n";
        editor->setPlainText(applied);
        check(window->requestClose(),"accepted close with no local draft");
    }
    panel.refresh();
    check(panel.snapshot().sources[0].text==applied && !panel.snapshot().sources[0].draft,"close restores committed analysis before deferred deletion");
    check(window->requestClose(),"repeated accepted close is idempotent before deferred deletion");
    view.openScenarioSource(mob,applied,0);
    auto replacement=view.m_scriptWindows.value(mob);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    check(replacement && view.m_scriptWindows.value(mob)==replacement && replacement->mob()==mob,"old deferred deletion preserves replacement editor registry");
    if(QGuiApplication::platformName()=="windows") {
        const QString saved=applied+"\n// saved through close";
        replacement->findChild<CScriptEditor*>()->setPlainText(saved);
        QTimer::singleShot(30,[](){ for(QWidget* w:QApplication::topLevelWidgets()) if(auto* box=qobject_cast<QMessageBox*>(w)) box->button(QMessageBox::Save)->click(); });
        check(replacement->requestClose() && mob->script()==saved && history.index()==2,"save on close uses the normal undoable script transaction");
        history.undo();
        check(mob->script()==applied,"save-on-close undo restores previous script");
    } else replacement->requestClose();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    history.undo(); panel.refresh();
    check(panel.analysis().markers.size()==3,"closed-editor script undo refresh");
    object->setMapId(9); emit view.mobStateChanged(); panel.refresh();
    check(panel.analysis().markers.size()==2,"object ID change invalidates its marker");
    object->setMapId(7); emit view.mobStateChanged(); panel.refresh();
    check(panel.analysis().markers.size()==3,"restored object identity restores marker");
    view.restoreSelection({});
    for(int row=0;row<refs->topLevelItemCount();++row) if(refs->topLevelItem(row)->text(0)=="AddUnitToServer") { refs->setCurrentItem(refs->topLevelItem(row)); break; }
    check(view.m_scenarioDestinations.size()==1 && view.m_scenarioLinks.size()==2,"virtual participant selection focuses its destination");
    CMob* second=new CMob; second->setFileName(QFileInfo(scratch.filePath("second.mob")));
    second->setScript("WorldScript(GetObject(7))"); view.m_aMob.append(second); view.m_activeMob=second;
    emit view.mobStateChanged(); panel.refresh();
    check(panel.analysis().markers.isEmpty() && refs->topLevelItemCount()==1 && view.m_scenarioParticipants.isEmpty(),"map switch treats old map identity as external");
    view.m_aMob.removeOne(second); view.m_activeMob=mob; delete second; emit view.mobStateChanged(); panel.refresh();
    QVector3D xy(12,14,0);
    check(!view.scenarioMarkerPosition(&xy,false),"MOB-only XY marker gets no fictional ground");
    panel.setLayerEnabled(false);
    check(view.m_scenarioParticipants.isEmpty() && view.m_scenarioSpawns.isEmpty() && view.m_scenarioLinks.isEmpty(),"layer disabled clears transient geometry");
    panel.setLayerEnabled(true);
    history.clear(); mob->setDirty(false);
    view.m_aMob.clear(); view.m_activeMob=nullptr; emit view.mobStateChanged();
    delete mob; panel.refresh();
    check(panel.analysis().references.isEmpty() && view.m_scenarioParticipants.isEmpty(),"map close clears analysis without retaining document pointers");
    auto* shutdownView=new CView(nullptr,nullptr);
    auto* shutdownMob=new CMob; shutdownMob->setFileName(QFileInfo(scratch.filePath("shutdown.mob")));
    shutdownMob->setScript("WorldScript()"); shutdownView->m_aMob.append(shutdownMob); shutdownView->m_activeMob=shutdownMob;
    shutdownView->openScenarioSource(shutdownMob,shutdownView->scenarioScriptText(shutdownMob),0);
    QPointer<CMobScriptWindow> shutdownWindow=shutdownView->m_scriptWindows.value(shutdownMob);
    delete shutdownView;
    check(shutdownWindow.isNull(),"view destruction retires open script callbacks before maps and members");
    const QString corpusMob=qEnvironmentVariable("EI_SCENARIO_CHECK_MOB");
    if(!corpusMob.isEmpty()) {
        QFile input(corpusMob); const bool readable=input.open(QIODevice::ReadOnly);
        const QByteArray hash=readable?QCryptographicHash::hash(input.readAll(),QCryptographicHash::Sha256):QByteArray(); input.close();
        check(readable,"read-only corpus input");
        if(readable) {
            SMobFamilyValidationRequest familyRequest;
            familyRequest.profileId=qEnvironmentVariable("EI_SCENARIO_CHECK_PROFILE","base-game");
            familyRequest.seedMobPaths=QStringList{corpusMob}; familyRequest.preferredMobPath=corpusMob;
            familyRequest.mappedPrimaryMobPath=corpusMob; familyRequest.mapRoots=QStringList{QFileInfo(corpusMob).absolutePath()};
            const auto family=CMobFamilyValidator::validate(familyRequest);
            scenario::Request request; request.activeSource=corpusMob; request.familyRoots=QStringList{corpusMob};
            for(const auto& member:family.members) if(!member.missing && member.error.isEmpty()) {
                scenario::Source src; src.key=member.path; src.text=member.script;
                if(member.inclusionReason=="profile-wide autorun") request.familyRoots.append(member.path);
                for(const auto& entry:member.objects) if(entry.hasNid) {
                    scenario::Object obj; obj.key=entry.sectionPath; obj.id=entry.nid; obj.name=entry.name; src.objects.append(obj);
                }
                request.sources.append(src);
            }
            const QString databasePath=qEnvironmentVariable("EI_SCENARIO_CHECK_DATABASE");
            if(!databasePath.isEmpty()) {
                QVector<ei::SDatabaseDiagnostic> diagnostics;
                ei::SProfileDatabaseSnapshot native;
                native.rawMembers=ei::readDatabaseArchive(databasePath,&diagnostics);
                ei::applyRelationalMembers(native.rawMembers,"read-only-test",&native);
                for(auto it=native.monsters.cbegin();it!=native.monsters.cend();++it) request.prototypes.insert(it.key());
                check(!request.prototypes.isEmpty(),"read-only corpus prototype catalog");
            }
            QElapsedTimer timer; timer.start(); const auto result=scenario::analyze(request); const qint64 elapsed=timer.elapsed();
            int conditioned=0,spawns=0,unresolved=0;
            for(const auto& ref:result.references) {
                if(!ref.conditions.isEmpty()) ++conditioned;
                if(ref.kind==scenario::Kind::Spawn) ++spawns;
                for(const auto& target:ref.targets) if(!scenario::navigable(target.resolution) && target.resolution!=scenario::Resolution::Virtual) ++unresolved;
            }
            output << "Corpus aggregate: sources=" << request.sources.size() << " references=" << result.references.size()
                << " conditioned=" << conditioned << " spawns=" << spawns << " unresolved=" << unresolved
                << " markers=" << result.markers.size() << " familyComplete=" << result.familyComplete << " elapsedMs=" << elapsed << "\n";
            check(!result.references.isEmpty(),"real-language corpus produces scenario records");
            input.open(QIODevice::ReadOnly);
            check(QCryptographicHash::hash(input.readAll(),QCryptographicHash::Sha256)==hash,"read-only corpus input hash unchanged");
        }
    }
    output << "Scenario integration: " << checks-failures << " passed, " << failures << " failed\n"; output.flush();
    return failures?1:0;
}
