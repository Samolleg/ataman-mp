#include <QtWidgets>
#include <QtEndian>
#include "main_window.h"
#include "view.h"
#include "mob.h"
#include "resource_setup_dialog.h"
#include "background_mob_load.h"
#include "endurance_document.h"

namespace {
struct Hidden : QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object)) if(widget->isWindow())
                widget->setAttribute(Qt::WA_DontShowOnScreen);
        if(event->type()==QEvent::Show) {
            if(auto* setup=qobject_cast<CResourceSetupDialog*>(object))
                QTimer::singleShot(0,setup,&QDialog::reject);
            if(auto* box=qobject_cast<QMessageBox*>(object))
                QTimer::singleShot(0,box,&QDialog::reject);
        }
        return false;
    }
};
QByteArray u32(quint32 v){QByteArray b(4,'\0');qToLittleEndian(v,reinterpret_cast<uchar*>(b.data()));return b;}
QByteArray section(quint32 id,const QByteArray& b={}){return u32(id)+u32(b.size()+8)+b;}
QByteArray mob(const QByteArray& script,const QByteArray& objects={}) {
    quint32 key=0x12345678;QByteArray encrypted=u32(key);
    for(char c:script){quint32 t=((((key*13)<<4)+key)<<8)-key;key+=(t<<2)+2531011;encrypted.append(char(c^char(key>>16)));}
    return section(0xA000,section(0xC000)+section(0xB000,objects)+section(0xACCEECCB,encrypted));
}
void write(const QString& path,const QByteArray& bytes) {
    QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())throw path;
}
bool settle(MainWindow& window) {
    QElapsedTimer timer;timer.start();
    while(window.view()->property("loadingAddMobs").toBool()&&timer.elapsed()<60000){qApp->processEvents();QThread::msleep(1);}
    return !window.view()->property("loadingAddMobs").toBool();
}
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);int passed=0;
    auto check=[&](bool ok,const QString& message){out<<(ok?"PASS ":"FAIL ")<<message<<'\n';out.flush();if(!ok)throw message;++passed;};
    Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
    try {
        const QString root=qEnvironmentVariable("EI_ADDMOB_INFO_ROOT");
        check(!root.isEmpty(),"explicit isolated root");QDir().mkpath(root+"/maps");
        const QByteArray object=section(0xB001,section(0xB002,u32(42))+section(0xB003,u32(1))+section(0xB004,"test"));
        const auto clean=mob("// clean dependency"),duplicate=mob("// duplicate dependency",object+object);
        write(root+"/maps/clean.mob",clean);write(root+"/maps/duplicate.mob",duplicate);
        write(root+"/maps/root-clean.mob",mob("AddMob(\"clean.mob\")"));
        write(root+"/maps/root-duplicate.mob",mob("AddMob(\"duplicate.mob\")"));
        CMobDocument document;CValidationReport report;
        check(document.loadBytes(clean,"clean.mob",&report)&&report.findings().size()>=2,"clean fixture actually produces Info findings");
        report=CValidationReport();check(document.loadBytes(duplicate,"duplicate.mob",&report),"duplicate fixture parses");
        QString warning;for(const auto& finding:report.findings())if(finding.code=="mob.duplicate_id")warning=finding.message;
        check(!warning.isEmpty(),"duplicate_id warning exists in parser report");
        auto cancel=std::make_shared<std::atomic_bool>(false);
        const auto prepared=prepareBackgroundMob(QFileInfo(root+"/maps/clean.mob"),cancel);
        check(prepared.valid()&&prepared.warnings.isEmpty(),"background clean Info are excluded from warnings");
        const auto preparedDuplicate=prepareBackgroundMob(QFileInfo(root+"/maps/duplicate.mob"),cancel);
        check(preparedDuplicate.valid()&&preparedDuplicate.warnings==QStringList{warning},"background duplicate retains exactly the real warning");
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);
        window.resize(1200,800);window.show();window.view()->show();qApp->processEvents();window.view()->makeCurrent();
        check(window.view()->isValid(),"hidden native OpenGL context");
        check(window.loadMobWithProgress(QFileInfo(root+"/maps/root-clean.mob"),false)&&settle(window)&&window.view()->mobs().size()==2,"clean AddMob complete through production queue");
        check(!window.m_mapTestResultNotification||window.m_mapTestResultNotification->isHidden(),"clean AddMob has no notification");
        window.closeAll();
        check(window.loadMobWithProgress(QFileInfo(root+"/maps/root-duplicate.mob"),false)&&settle(window)&&window.view()->mobs().size()==2,"duplicate AddMob complete through production queue");
        auto* notification=window.m_mapTestResultNotification;
        QString text;if(notification)for(auto* label:notification->findChildren<QLabel*>())text+=label->text();
        check(notification&&!notification->isHidden()&&text.contains(warning),"duplicate ID notification contains parser warning");
        check(!text.contains("Strict section parsing")&&!text.contains("byte-identical"),"notification excludes both Info messages");
        for(auto* loaded:window.view()->mobs()){QByteArray candidate;check(loaded->buildValidatedCandidate(&candidate),"validated no-op candidate");QFile source(loaded->filePath().absoluteFilePath());check(source.open(QIODevice::ReadOnly)&&candidate==source.readAll(),"source and no-op bytes unchanged");}
        window.closeAll();window.close();out<<passed<<" PASS / 0 FAIL\n";out.flush();return 0;
    }catch(const QString& error){out<<"FAIL "<<error<<'\n';out.flush();return 1;}
}
