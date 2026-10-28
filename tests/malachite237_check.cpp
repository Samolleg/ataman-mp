#include <QtWidgets>
#include <QtTest>
#include "view.h"
#include "mob.h"
#include "settings.h"
#include "tree_view.h"
#include "progressview.h"
#include "tile_form.h"
#include "mob_script_window.h"
#include "malachite_bridge.h"
#include "script_editor.h"
#include "endurance_document.h"
#include <qt_windows.h>

namespace {
struct HiddenWindows:QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish||event->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(object))if(w->isWindow()){
                w->setAttribute(Qt::WA_DontShowOnScreen);w->setAttribute(Qt::WA_ShowWithoutActivating);
                if(!(w->windowFlags()&Qt::WindowDoesNotAcceptFocus))w->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
            }
        return false;
    }
};
bool until(const std::function<bool()>& done,int ms=30000){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms)QTest::qWait(10);return done();}
QByteArray section(quint32 type,const QByteArray& payload){QByteArray bytes;QDataStream s(&bytes,QIODevice::WriteOnly);s.setByteOrder(QDataStream::LittleEndian);s<<type<<quint32(payload.size()+8);bytes+=payload;return bytes;}
QByteArray encrypted(const QByteArray& text){quint32 key=0x12345678;QByteArray data;QDataStream s(&data,QIODevice::WriteOnly);s.setByteOrder(QDataStream::LittleEndian);s<<key;for(char c:text){quint32 t=((((key*13)<<4)+key)<<8)-key;key+=(t<<2)+2531011;data+=char(c^char(key>>16));}return section(0xACCEECCB,data);}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw path;return f.readAll();}
void write(const QString& path,const QByteArray& bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())throw path;}
}
int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    auto check=[&](bool ok,const QString& name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw name;++passed;};
    try {
        const QString root=qEnvironmentVariable("EI_MALACHITE_CHECK_ROOT");check(!root.isEmpty()&&QDir(root).exists(),"explicit isolated test root");
        check(QFileInfo::exists(CMalachiteBridge::executable()),"existing binary Malachite module and new companion");
        HiddenWindows hidden;qApp->installEventFilter(&hidden);
        CSettings settings;QUndoStack history;CTreeView tree;QTableWidget properties;properties.setColumnCount(2);
        CProgressView progress;QLineEdit coordinates;CTileForm tiles;
        CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
        view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
        auto* mob=new CMob;mob->attach(&view,&progress);
        QFileInfo path(root+"/prototype.mob");
        auto* codec=QTextCodec::codecForName("Windows-1251");
        const QString original=QString::fromUtf8("WorldScript(\r\nConsoleString(\"Привет\")\r\n)\r\n");
        const auto opaque=section(0x12345678,QByteArray::fromHex("00010203ff80")+"untouched-opaque");
        const auto source=section(0xA000,encrypted(codec->fromUnicode(original))+opaque);write(path.filePath(),source);
        check(mob->readMob(path,false)==0,"production CMob opens CP1251 encrypted fixture");
        view.m_aMob.append(mob);view.m_activeMob=mob;
        const auto normalized=QString(original).replace("\r\n","\n");
        check(mob->script()==QString::fromLatin1(codec->fromUnicode(original)),"source decoded without losing Cyrillic or CRLF");
        qputenv("EI_MALACHITE_VERIFY","1");
        check(view.openScenarioSource(mob,normalized,-1),"normal mapper script command opens editor");
        auto window=view.m_scriptWindows.value(mob);CMalachiteBridge* bridge=nullptr;
        check(until([&](){bridge=window?window->findChild<CMalachiteBridge*>():nullptr;return bridge&&(bridge->ready()||!bridge->failure().isEmpty());}),"companion startup answered");
        check(bridge&&bridge->ready(),"actual WPF component attached to native Qt child HWND: "+(bridge?bridge->failure():QString()));
        out<<"HIDDEN attr="<<window->testAttribute(Qt::WA_DontShowOnScreen)<<" visible="<<IsWindowVisible(HWND(window->winId()))<<" foreground="<<(GetForegroundWindow()==HWND(window->winId()))<<"\n";out.flush();
        check(window->testAttribute(Qt::WA_DontShowOnScreen)&&!IsWindowVisible(HWND(window->winId()))&&GetForegroundWindow()!=HWND(window->winId()),"native test window remains hidden and does not own foreground");
        QMap<int,QJsonObject> responses;
        QObject::connect(bridge,&CMalachiteBridge::response,[&](int id,const QJsonObject& response){responses[id]=response;});
        auto command=[&](const QString& name,QJsonObject data=QJsonObject()){
            const int id=bridge->command(name,data);
            check(until([&](){return responses.contains(id);}),name+" command completes");
            auto response=responses.take(id);check(response.value("ok").toBool(),name+" component response: "+response.value("error").toString());return response;
        };
        check(command("inspect").value("text").toString().replace("\r\n","\n")==normalized&&history.count()==0&&!mob->isDirty(),"opening reads current text without edits or save");
        window->resize(2200,1300);QTest::qWait(100);
        command("render",{{"path",root+"/malachite-embedded.png"}});
        const QImage image(root+"/malachite-embedded.png");check(!image.isNull()&&image.width()>900&&image.height()>500,"actual component render at resized Qt dimensions");
        const QString added=QString::fromUtf8("// Правка из Малахита\n");
        command("edit",{{"start",0},{"length",0},{"text",added}});
        check(until([&](){return history.count()==1;}),"real WPF TextBox edit enters mapper history");
        const auto changed=added+normalized;
        check(bridge->text()==changed&&view.scenarioScriptText(mob)==changed&&mob->isDirty(),"mapper and scenario analysis see live edited text");
        check(read(path.filePath())==source,"editing never writes source MOB");
        command("undo");check(!mob->isDirty()&&bridge->text()==normalized&&history.index()==0,"Malachite Undo restores exact clean script through shared history");
        command("redo");check(mob->isDirty()&&bridge->text()==changed&&history.index()==1,"Malachite Redo restores edit through shared history");
        history.undo();check(until([&](){return command("inspect").value("text").toString().replace("\r\n","\n")==normalized;}),"mapper Undo updates WPF component");
        history.redo();command("inspect");check(bridge->text()==changed,"mapper Redo updates component snapshot");
        command("save");check(!mob->isDirty(),"Malachite Save routes through normal atomic MOB save");
        const auto saved=read(path.filePath());
        CMobDocument before,after;CValidationReport report;
        check(before.loadBytes(source,"before.mob",&report)&&after.loadBytes(saved,"after.mob",&report),"independent structural read of source and saved MOB");
        const auto diff=CMobSemanticDiff::compare(before,after);
        check(diff.documentEntries().size()==1&&diff.documentEntries().front().category=="script"&&saved.contains(opaque),"only script changed; opaque source bytes retained");
        CMob reopened;reopened.attach(&view,&progress);check(reopened.readMob(path,false)==0,"fresh production reopen of saved MOB");
        const auto stored=QString::fromLatin1(codec->fromUnicode(QString(changed).replace("\n","\r\n")));
        check(reopened.script()==stored,"fresh reopen matches full edited Cyrillic script and CRLF");
        command("save");check(read(path.filePath())==saved,"repeated unchanged Save is byte-identical");
        command("edit",{{"start",0},{"length",0},{"text",QString::fromUtf8("// 😀\n")}});
        check(bridge->hasDraft()&&read(path.filePath())==saved&&mob->script()==stored,"unrepresentable text rejected; WPF draft and saved MOB preserved");
        command("edit",{{"start",0},{"length",6},{"text",""}});
        check(!bridge->hasDraft(),"correcting rejected input restores editable session");
        const auto goodPath=mob->filePath();mob->setFileName(QFileInfo(root+"/missing/denied.mob"));
        QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w))box->accept();});dialogs.start(10);
        const auto failed=command("save");dialogs.stop();mob->setFileName(goodPath);
        check(!failed.value("error").toString().isEmpty()&&read(path.filePath())==saved,"failed Save reports error and preserves existing source");
        command("save");check(!mob->isDirty(),"save retry succeeds in same session");
        check(window->requestClose(),"normal editor close completes without draft loss");QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        check(view.openScenarioSource(mob,changed,-1),"MOB script reopens in mapper after save and close");
        auto second=view.m_scriptWindows.value(mob);check(until([&](){auto* b=second->findChild<CMalachiteBridge*>();return b&&b->ready();}),"second native WPF host opens saved current script");
        check(second->analysisText()==changed,"reopened editor has exact saved script");
        bridge=second->findChild<CMalachiteBridge*>();
        QObject::connect(bridge,&CMalachiteBridge::response,[&](int id,const QJsonObject& response){responses[id]=response;});
        responses.clear();command("edit",{{"start",0},{"length",0},{"text",QString::fromUtf8("// 😀\n")}});
        const auto draft=bridge->text();bridge->command("crash");
        check(until([&](){return !second->findChild<CMalachiteBridge*>();}),"owned companion crash returns to classic editor");
        auto* fallback=second->findChild<CScriptEditor*>();
        check(fallback&&!fallback->isReadOnly()&&fallback->toPlainText()==draft&&second->hasUnappliedText()&&read(path.filePath())==saved,"rejected WPF draft survives companion crash in editable fallback without changing MOB");
        QTimer::singleShot(20,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w))box->button(QMessageBox::Discard)->click();});
        check(second->requestClose(),"classic fallback preserves normal close/discard contract");QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        check(view.openScenarioSource(mob,changed,-1),"new script window opens for startup-draft regression");
        auto early=view.m_scriptWindows.value(mob);const auto earlyDraft=changed+"// early draft\n";
        early->findChild<CScriptEditor*>()->setPlainText(earlyDraft);QTest::qWait(50);
        check(!early->findChild<CMalachiteBridge*>()&&early->analysisText()==earlyDraft&&early->hasUnappliedText(),"input before asynchronous startup is never hidden or replaced");
        QTimer::singleShot(20,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w))box->button(QMessageBox::Discard)->click();});
        check(early->requestClose(),"early draft closes under ordinary discard policy");QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        out<<"RESULT "<<passed<<" PASS 0 FAIL\n";out.flush();return 0;
    }catch(const QString& error){out<<"ERROR "<<error<<"\nRESULT "<<passed<<" PASS 1 FAIL\n";out.flush();return 1;}
}
