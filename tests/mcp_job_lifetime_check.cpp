#include <QtWidgets>
#include "main_window.h"
#include "ai_agent_server.h"

int runAssemblyCatalogCheck() {
    QTextStream out(stdout);int passes=0;
    const auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<'\n';out.flush();if(!ok)throw what;++passes;};
    try {
        const auto root=qEnvironmentVariable("EI_AUDIT_FIX_ROOT");check(!root.isEmpty(),"isolated root");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);
        auto* server=window.m_aiAgent;
        server->m_jobs["finished"].created=QDateTime::currentDateTimeUtc().addSecs(-700);
        server->m_jobs["finished"].result={{"status","opened"}};
        server->m_jobs["finished"].markComplete();server->prune();
        check(server->m_jobs.contains("finished")&&server->m_jobs["finished"].result["status"]=="opened","late completion has full result retention window");
        server->m_jobs["finished"].finishedAt=QDateTime::currentDateTimeUtc().addSecs(-61);server->prune();
        check(!server->m_jobs.contains("finished"),"terminal result expires after 60 seconds");
        server->m_jobs["worker"].created=QDateTime::currentDateTimeUtc().addSecs(-70);
        auto cancelled=server->m_jobs["worker"].cancel;server->prune();
        check(cancelled->load()&&server->m_jobs["worker"].complete&&server->m_jobs["worker"].finishedAt.isValid(),"expired preparation cancelled with readable terminal result");
        server->prune();check(server->m_jobs.contains("worker"),"timeout result survives subsequent prune");
        auto& commit=server->m_jobs["commit"];commit.created=QDateTime::currentDateTimeUtc().addSecs(-700);commit.edit=true;commit.committing=true;
        cancelled=commit.cancel;server->m_committing=true;server->prune();
        check(server->m_jobs.contains("commit")&&!cancelled->load()&&!commit.complete,"active GUI commit survives deadline and prune");
        auto status=server->dispatch("operation_status",{{"operation","commit"}});
        check(status["status"]=="running","status remains readable during GUI commit");
        auto response=server->dispatch("cancel_operation",{{"operation","commit"}});
        check(response.contains("error")&&server->m_jobs.contains("commit")&&!cancelled->load(),"cancel refuses interruption of committing writer");
        server->stop();check(server->m_jobs.contains("commit")&&!cancelled->load(),"access revocation retains in-flight commit ownership");
        server->m_jobs["commit"].committing=false;server->m_jobs["commit"].markComplete();server->m_committing=false;server->prune();
        check(server->m_jobs.contains("commit"),"commit completion is retained after long elapsed duration");
        server->m_jobs.clear();out<<passes<<" PASS / 0 FAIL\n";return 0;
    }catch(const QString& error){out<<"FAIL "<<error<<'\n';return 1;}
}
