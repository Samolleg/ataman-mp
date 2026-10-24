#include "ai_agent_server.h"
#include "main_window.h"
#include "view.h"
#include <QtConcurrent>

CAiAgentServer::CAiAgentServer(MainWindow* w):QObject(w),m_window(w),m_view(w->view())
{
    m_dock=new QDockWidget(QString::fromUtf8("ИИ-агент"),w);m_dock->setObjectName("aiAgentDock");
    auto* body=new QWidget(m_dock);auto* layout=new QVBoxLayout(body);
    m_mode=new QComboBox(body);m_mode->setObjectName("aiAgentMode");
    m_mode->addItems({QString::fromUtf8("Выкл"),QString::fromUtf8("Только чтение"),QString::fromUtf8("Правка")});
    layout->addWidget(m_mode);m_status=new QLabel(QString::fromUtf8("Отключено"),body);layout->addWidget(m_status);
    m_allowSave=new QCheckBox(QString::fromUtf8("Разрешить агенту сохранять"),body);m_allowSave->setObjectName("aiAgentAllowSave");m_allowSave->setChecked(false);layout->addWidget(m_allowSave);
    m_events=new QPlainTextEdit(body);m_events->setReadOnly(true);m_events->setMaximumBlockCount(200);layout->addWidget(m_events);
    auto* setup=new QPushButton(QString::fromUtf8("Подключить Codex/Claude Code"),body);layout->addWidget(setup);
    auto* off=new QPushButton(QString::fromUtf8("Отключить"),body);off->setObjectName("aiAgentDisconnect");layout->addWidget(off);
    m_dock->setWidget(body);
    connect(m_mode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int n){setEnabled(n);});
    connect(off,&QPushButton::clicked,this,&CAiAgentServer::stop);
    connect(setup,&QPushButton::clicked,this,&CAiAgentServer::configureClient);
    connect(&m_server,&QLocalServer::newConnection,this,&CAiAgentServer::accept);
    connect(&m_expiry,&QTimer::timeout,this,&CAiAgentServer::prune);m_expiry.start(1000);
    // QPointer also protects the fallback destructor path. Normal shutdown explicitly
    // stops automation BEFORE deleting CView, while its native surface is alive.
    connect(w->view(),&QObject::destroyed,this,&CAiAgentServer::stop);
}
CAiAgentServer::~CAiAgentServer(){stop();}
void CAiAgentServer::event(const QString& text)
{
    m_events->appendPlainText(QTime::currentTime().toString("HH:mm:ss")+" "+text);
    m_status->setText(enabled()?QString::fromUtf8("%1; подключений: %2").arg(m_mode->currentIndex()==2?QString::fromUtf8("Правка"):QString::fromUtf8("Только чтение")).arg(m_clients.size()):QString::fromUtf8("Отключено"));
}
void CAiAgentServer::cancelJob(Job& job)
{
    job.cancel->store(true);job.plan.clear();
    if(job.editWatcher){job.editWatcher->disconnect(this);job.editWatcher->deleteLater();job.editWatcher=nullptr;}
    if(job.watcher){job.watcher->disconnect(this);job.watcher->deleteLater();job.watcher=nullptr;}
    if(job.process) {
        // Only our process, never an editor instance. No wait on filesystem indexing.
        job.process->disconnect(this);job.process->kill();
        // QProcess destructor waits; defer deletion until the process has exited.
        auto* p=job.process.data();
        connect(p,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),p,&QObject::deleteLater);
        if(p->state()==QProcess::NotRunning)p->deleteLater();
        job.process=nullptr;
    }
}
void CAiAgentServer::stop()
{
    m_server.close();m_writer.clear();m_allowSave->setChecked(false);
    const auto sockets=m_clients.keys();m_clients.clear();
    for(auto* socket:sockets){socket->disconnect(this);socket->abort();socket->deleteLater();}
    for(auto& job:m_jobs)cancelJob(job);m_jobs.clear();
    if(!m_descriptor.isEmpty())QFile::remove(m_descriptor);
    m_descriptor.clear();m_secret.clear();m_id.clear();
    {QSignalBlocker blocker(m_mode);m_mode->setCurrentIndex(0);}
    event(QString::fromUtf8("Доступ выключен; операции отменены"));
}
void CAiAgentServer::setEnabled(int mode)
{
    stop();if(mode==0||!m_view||!m_window)return;
    if(!ai::secureDirectory(ai::discoveryDirectory())){event(QString::fromUtf8("Не удалось защитить каталог обнаружения"));return;}
    m_id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_secret=QUuid::createUuid().toString(QUuid::WithoutBraces)+QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if(!m_server.listen("ei-ataman-"+m_id)){stop();event(QString::fromUtf8("Не удалось открыть локальный канал"));return;}
    m_descriptor=QDir(ai::discoveryDirectory()).filePath(m_id+".json");
    QSaveFile file(m_descriptor);
    QJsonObject descriptor{{"instance",m_id},{"pipe",m_server.serverName()},{"secret",m_secret},
        {"pid",double(QCoreApplication::applicationPid())},{"version",ENDURANCE_VERSION},
        {"mode",mode==2?"edit":"read-only"},{"started",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(descriptor).toJson(QJsonDocument::Compact))<0||!file.commit()) {
        stop();event(QString::fromUtf8("Не удалось записать обнаружение"));return;
    }
    {QSignalBlocker blocker(m_mode);m_mode->setCurrentIndex(mode);}
    event(mode==2?QString::fromUtf8("Правка разрешена пользователем; сохранение выключено"):QString::fromUtf8("Чтение разрешено пользователем"));
}
void CAiAgentServer::accept()
{
    while(m_server.hasPendingConnections()) {
        auto* s=m_server.nextPendingConnection();
        if(m_clients.size()>=4){s->abort();s->deleteLater();continue;}
        s->setReadBufferSize(ai::MaxFrame+1);m_clients.insert(s,{});
        s->setProperty("authenticated",false);
        connect(s,&QLocalSocket::readyRead,this,[this,s](){receive(s);});
        connect(s,&QLocalSocket::disconnected,this,[this,s](){
            for(auto it=m_jobs.begin();it!=m_jobs.end();)if(it->owner==s){cancelJob(*it);it=m_jobs.erase(it);}else ++it;
            if(m_writer==s)m_writer.clear();m_clients.remove(s);s->deleteLater();event(QString::fromUtf8("Агент отключён; его незавершённые операции отброшены"));});
        QTimer::singleShot(5000,s,[s](){if(!s->property("authenticated").toBool())s->abort();});
        event(QString::fromUtf8("Локальное подключение; ожидается проверка"));
    }
}
void CAiAgentServer::receive(QLocalSocket* s)
{
    if(!m_clients.contains(s))return;
    auto& buffer=m_clients[s];buffer+=s->readAll();int frames=0;
    while(buffer.contains('\n')) {
        QJsonObject request;QString diagnostic;
        if(++frames>16||!ai::takeFrame(buffer,&request,&diagnostic)){s->abort();return;}
        const auto id=request["id"];
        if(id.isUndefined()||(!id.isString()&&!id.isDouble())||!enabled()||!m_view||!m_window
           ||request["secret"].toString()!=m_secret||m_secret.isEmpty()) {
            event(QString::fromUtf8("Подключение отклонено"));s->abort();return;
        }
        s->setProperty("authenticated",true);
        const auto method=request["method"].toString();const auto args=request["arguments"].toObject();
        QJsonObject result;
        if(!ai::validateArguments(method,args,&diagnostic))result=ai::error(diagnostic);
        else {m_currentClient=s;result=dispatch(method,args);m_currentClient.clear();}
        QByteArray out=QJsonDocument(QJsonObject{{"id",id},{"result",result}}).toJson(QJsonDocument::Compact);
        if(out.size()>ai::MaxResult)out=QJsonDocument(QJsonObject{{"id",id},{"result",ai::error("Response limit; use a smaller region/page")}}).toJson(QJsonDocument::Compact);
        if(s->bytesToWrite()>ai::MaxFrame){s->abort();return;}
        s->write(out+'\n');
        // Deliver the operation ID before a queued normal file commit can
        // enter its atomic section on the next GUI turn.
        s->flush();
        // Only validated tool names. Never arguments, secrets or document content.
        event(QString::fromUtf8("Чтение: ")+ (diagnostic.isEmpty()?method:QString("invalid request")));
    }
    if(buffer.size()>ai::MaxFrame)s->abort();
}
void CAiAgentServer::prune()
{
    const auto now=QDateTime::currentDateTimeUtc();
    for(auto it=m_jobs.begin();it!=m_jobs.end();) {
        const auto age=it->created.msecsTo(now);
        if(!it->complete&&age>it->deadlineMs){cancelJob(*it);it->complete=true;it->result=ai::error("Operation deadline exceeded");}
        if(age>it->deadlineMs+30000){cancelJob(*it);it=m_jobs.erase(it);}else ++it;
    }
}
QJsonObject CAiAgentServer::startJob(const QString& method,const QJsonObject& args)
{
    prune();if(m_jobs.size()>=8)return ai::error("Operation limit 8; cancel or wait for expiry");
    int running=0;for(const auto& j:m_jobs)if(!j.complete)++running;
    if(running>=2)return ai::error("Two reads already running");
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);m_jobs.insert(id,Job{});
    if(method=="read_terrain"||method=="search_database") {
        const auto model=method=="read_terrain"?CAiReadAdapter::terrain(m_window):CTerrainEditModel();
        const auto db=ei::ProfileDatabaseContext::instance().current();const auto cancel=m_jobs[id].cancel;
        auto* watcher=new QFutureWatcher<QJsonObject>(this);m_jobs[id].watcher=watcher;
        connect(watcher,&QFutureWatcher<QJsonObject>::finished,this,[this,id,watcher](){
            if(m_jobs.contains(id)){m_jobs[id].result=watcher->result();m_jobs[id].complete=true;m_jobs[id].watcher=nullptr;}
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([model,db,method,args,cancel](){
            return method=="read_terrain"?ai::terrainSnapshot(model,args,cancel):ai::databaseSnapshot(db,args,cancel);
        }));
    } else {
        auto* process=new QProcess(qApp);m_jobs[id].process=process;
        QJsonArray roots;for(const auto& path:CAiReadAdapter::mapRoots(m_window))roots.append(path);
        const QByteArray input=QJsonDocument(QJsonObject{{"arguments",args},{"roots",roots}}).toJson(QJsonDocument::Compact)+'\n';
        connect(process,&QProcess::started,this,[process,input](){process->write(input);process->closeWriteChannel();});
        connect(process,&QProcess::readyReadStandardOutput,this,[process](){
            QByteArray bytes=process->property("output").toByteArray()+process->readAllStandardOutput();
            if(bytes.size()>ai::MaxResult){process->setProperty("oversized",true);process->kill();bytes.clear();}
            process->setProperty("output",bytes);
        });
        connect(process,&QProcess::readyReadStandardError,process,[process](){process->readAllStandardError();});
        connect(process,&QProcess::errorOccurred,this,[this,id,process](QProcess::ProcessError e){
            if(e==QProcess::FailedToStart&&m_jobs.contains(id)) {
                m_jobs[id].result=ai::error("Read worker could not start");m_jobs[id].complete=true;m_jobs[id].process=nullptr;process->deleteLater();
            }
        });
        connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this,id,process](int code,QProcess::ExitStatus status){
            if(m_jobs.contains(id)) {
                auto result=QJsonDocument::fromJson(process->property("output").toByteArray()+process->readAllStandardOutput()).object();
                if(status!=QProcess::NormalExit||code!=0||result.isEmpty())result=ai::error("Read worker failed or exceeded limits");
                m_jobs[id].result=result;m_jobs[id].complete=true;m_jobs[id].process=nullptr;
            }
            process->deleteLater();
        });
        process->setProgram(QCoreApplication::applicationFilePath());process->setArguments({"--ai-read-worker"});
        // Worker has no QApplication, settings, recovery, renderer or game process.
        process->start();
    }
    return {{"operation",id},{"status","running"},{"poll","operation_status"},{"expiresMs",60000}};
}
QJsonObject CAiAgentServer::dispatch(const QString& method,const QJsonObject& args)
{
    if(m_committing)return ai::error("Normal map commit in progress; retry after operation_status");
    if(method=="get_revision")return {{"revision",CAiEditAdapter::revision(m_window)},{"mode",m_mode->currentIndex()==2?"edit":"read-only"},{"allowSave",m_allowSave->isChecked()}};
    const QSet<QString> mutations{"create_map","sculpt","paint","generate_terrain","erode","carve_river","auto_paint","preview","apply","undo_last_agent_operation","save"};
    if(mutations.contains(method)) {
        if(m_mode->currentIndex()!=2)return ai::error("Read-only mode: only the human can enable Правка in the dock");
        if(m_writer&&m_writer!=m_currentClient)return ai::error("Another agent owns the writer session; disconnect it before switching writer");
        if(!m_currentClient)return ai::error("No writer connection");
        if(!m_writer)m_writer=m_currentClient;
        if(method=="save"&&!m_allowSave->isChecked())return ai::error("Save disabled: human must check Разрешить агенту сохранять; changes remain in memory");
        if(method=="apply") {
            const auto id=args["operation"].toString();
            if(!m_jobs.contains(id)||m_jobs[id].owner!=m_currentClient||!m_jobs[id].plan)return ai::error("No completed preview owned by this connection");
            auto plan=m_jobs[id].plan;
            if(args["revision"].toString()!=plan->revision||CAiEditAdapter::revision(m_window)!=plan->revision)return ai::error("Document revision conflict; prepare a new preview");
            m_jobs.remove(id);return startCommit(plan,args["snapshot"].toBool());
        }
        if(method=="undo_last_agent_operation") {
            if(args["revision"].toString()!=CAiEditAdapter::revision(m_window))return ai::error("Document revision conflict; get_revision first");
            return CAiEditAdapter::undo(m_window);
        }
        return startEdit(method,args);
    }
    if(QApplication::activeModalWidget()&&method!="operation_status"&&method!="cancel_operation")return ai::error("Editor modal interaction in progress; retry later");
    if(method=="read_terrain"||method=="search_database"||method=="read_profile_map"||method=="list_profile_maps")return startJob(method,args);
    if(method=="operation_status"||method=="cancel_operation") {
        prune();const auto id=args["operation"].toString();if(!m_jobs.contains(id))return ai::error("Unknown or expired operation");
        auto& job=m_jobs[id];
        if(job.edit && job.owner!=m_currentClient)return ai::error("Operation belongs to another writer");
        if(method=="cancel_operation"){cancelJob(job);m_jobs.remove(id);return {{"status","cancelled"}};}
        QJsonObject out{{"operation",id},{"status",job.complete?"complete":"running"}};
        if(job.complete)out.insert("result",job.result);return out;
    }
    auto result=CAiReadAdapter::read(m_window,method,args);
    if(method=="get_context"){result["mode"]=m_mode->currentIndex()==2?"edit":"read-only";result["allowSave"]=m_allowSave->isChecked();}
    return result;
}
namespace {
class AgentCommitDialogs : public QObject {
public:
    QString error;
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Show)if(auto* box=qobject_cast<QMessageBox*>(object)) {
            error=box->text();QPointer<QMessageBox> safe=box;
            QTimer::singleShot(0,box,[safe](){if(safe)safe->done(QMessageBox::Cancel);});
        }
        return false;
    }
};
}
QJsonObject CAiAgentServer::startCommit(const QSharedPointer<ai::EditPlan>& plan,bool snapshot)
{
    prune();if(m_jobs.size()>=8)return ai::error("Operation limit 8; cancel or wait for expiry");
    for(const auto& job:m_jobs)if(job.edit&&!job.complete)return ai::error("A writer operation is already running");
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);Job job;job.edit=true;job.owner=m_currentClient;
    m_jobs[id]=job;QTimer::singleShot(0,this,[this,id,plan,snapshot](){finishCommit(id,plan,snapshot);});
    return {{"operation",id},{"status","running"},{"poll","operation_status"}};
}
void CAiAgentServer::finishCommit(const QString& id,const QSharedPointer<ai::EditPlan>& plan,bool snapshot)
{
    if(!m_jobs.contains(id)||m_jobs[id].cancel->load()||!m_jobs[id].owner||!m_view||!m_window)return;
    auto result=ai::error("Edit authority revoked");
    if(m_mode->currentIndex()==2) {
        QScopedValueRollback<bool> lock(m_committing,true);
        m_window->setProperty("aiCommitBusy",true);
        AgentCommitDialogs dialogs;qApp->installEventFilter(&dialogs);
        if(plan->method=="save") {
            if(!m_allowSave->isChecked())result=ai::error("Save permission revoked; changes remain in memory");
            else if(plan->revision!=CAiEditAdapter::revision(m_window))result=ai::error("Document revision conflict before Save");
            else result=CAiEditAdapter::save(m_window);
        } else result=CAiEditAdapter::apply(m_window,*plan);
        qApp->removeEventFilter(&dialogs);
        if(!dialogs.error.isEmpty())result=ai::error(dialogs.error);
        if(snapshot&&!result.contains("error")&&m_window&&m_view)result["snapshot"]=CAiReadAdapter::read(m_window,"window_snapshot",{});
        if(m_window)m_window->setProperty("aiCommitBusy",false);
    }
    if(m_jobs.contains(id)){m_jobs[id].result=result;m_jobs[id].complete=true;}
}
QJsonObject CAiAgentServer::startEdit(const QString& requested,const QJsonObject& original)
{
    prune();if(m_jobs.size()>=8)return ai::error("Operation limit 8; cancel or wait for expiry");
    for(const auto& job:m_jobs)if(job.edit&&!job.complete)return ai::error("A writer operation is already running");
    const auto method=requested=="preview"?original["tool"].toString():requested;
    auto args=requested=="preview"?original["arguments"].toObject():original;
    if(method=="save") {
        auto plan=QSharedPointer<ai::EditPlan>::create();plan->method="save";plan->revision=args["revision"].toString();
        return startCommit(plan,false);
    }
    if(m_editWorkers->load()!=0)return ai::error("Previous cancelled writer is still releasing its snapshot; retry shortly");
    if(requested=="preview")args["preview"]=true;
    const auto candidate=CAiEditAdapter::capture(m_window,method,args);
    if(!candidate.error.isEmpty())return ai::error(candidate.error);
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);Job job;job.edit=true;job.owner=m_currentClient;
    // A rich Modern131 coast palette may need more than30s to resolve its
    // transition artwork. Keep this detached, cancellable job bounded, while
    // retaining the219 deadline for every existing operation.
    if(method=="auto_paint")job.deadlineMs=120000;
    m_jobs[id]=job;
    const auto cancel=job.cancel;auto* watcher=new QFutureWatcher<ai::EditPlan>(this);m_jobs[id].editWatcher=watcher;
    connect(watcher,&QFutureWatcher<ai::EditPlan>::finished,this,[this,id,watcher,args](){
        if(!m_jobs.contains(id)){watcher->deleteLater();return;}
        auto plan=QSharedPointer<ai::EditPlan>::create(watcher->result());m_jobs[id].editWatcher=nullptr;watcher->deleteLater();
        if(m_jobs[id].cancel->load())return;
        if(!plan->error.isEmpty()){m_jobs[id].result=ai::error(plan->error);m_jobs[id].complete=true;return;}
        if(args["preview"].toBool()) {
            m_jobs[id].plan=plan;m_jobs[id].complete=true;m_jobs[id].result=plan->summary;
            m_jobs[id].result["status"]="preview";m_jobs[id].result["operation"]=id;
        } else {
            QTimer::singleShot(0,this,[this,id,plan,args](){finishCommit(id,plan,args["snapshot"].toBool());});
        }
    });
    const auto workers=m_editWorkers;workers->fetch_add(1);
    watcher->setFuture(QtConcurrent::run([candidate,cancel,workers](){
        struct Release {std::shared_ptr<std::atomic_int> count;~Release(){count->fetch_sub(1);}} release{workers};
        return ai::prepareEdit(candidate,cancel);
    }));
    return {{"operation",id},{"status","running"},{"poll","operation_status"},{"expiresMs",60000}};
}
void CAiAgentServer::configureClient()
{
    QDialog dialog(m_dock);dialog.setWindowTitle(QString::fromUtf8("Подключить Codex/Claude Code"));
    auto* layout=new QVBoxLayout(&dialog);auto* client=new QComboBox(&dialog);client->addItems({"Codex","Claude Code"});layout->addWidget(client);
    auto* text=new QPlainTextEdit(&dialog);text->setReadOnly(true);layout->addWidget(text);
    const QString exe=QDir(QCoreApplication::applicationDirPath()).filePath("EIMapperMcp.exe");
    const auto refresh=[=](){
        const bool codex=client->currentIndex()==0;
        text->setPlainText(codex?QString::fromUtf8("Будет выполнено после подтверждения:\ncodex mcp add ataman -- \"%1\"\n\nИзменится ~/.codex/config.toml. Маппер не запускается мостом.\n").arg(exe)
            :QString::fromUtf8("После выбора файла и подтверждения в выбранном .mcp.json будет добавлен сервер ataman. Другие серверы сохраняются. Claude Code запросит доверие проекту.\n\n")
                +QString::fromUtf8(QJsonDocument(ai::registration("Claude Code",exe)).toJson()));
    };connect(client,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[=](int){refresh();});refresh();
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.resize(650,420);if(dialog.exec()!=QDialog::Accepted)return;
    if(!QFileInfo::exists(exe)){QMessageBox::warning(m_dock,QString::fromUtf8("Подключение"),QString::fromUtf8("EIMapperMcp.exe отсутствует в комплекте"));return;}
    if(client->currentIndex()==0) {
        auto* p=new QProcess(this);const auto registration=ai::registration("Codex",exe);QStringList arguments;
        for(const auto& a:registration["arguments"].toArray())arguments.append(a.toString());
        const auto program=QStandardPaths::findExecutable("codex.exe");
        if(program.isEmpty()){p->deleteLater();QMessageBox::information(m_dock,"Codex",QString::fromUtf8("codex.exe не найден в PATH. Выполните показанную команду вручную."));return;}
        connect(p,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this,p](int code,QProcess::ExitStatus status){event(code==0&&status==QProcess::NormalExit?QString::fromUtf8("Конфиг Codex обновлён"):QString::fromUtf8("Команда Codex завершилась с ошибкой"));p->deleteLater();});
        connect(p,&QProcess::errorOccurred,this,[this,p](QProcess::ProcessError){event(QString::fromUtf8("Не удалось выполнить команду Codex"));p->deleteLater();});
        p->start(program,arguments);QTimer::singleShot(15000,p,[p](){if(p->state()!=QProcess::NotRunning)p->kill();});
    } else {
        const auto path=QFileDialog::getSaveFileName(m_dock,QString::fromUtf8("Выберите .mcp.json проекта"),".mcp.json","JSON (*.json)");if(path.isEmpty())return;
        QJsonObject config;QByteArray baseline;QFile input(path);
        if(input.exists()) {
            if(!input.open(QIODevice::ReadOnly)||input.size()>ai::MaxFrame){QMessageBox::warning(m_dock,"MCP",QString::fromUtf8("Не удалось прочитать конфиг"));return;}
            baseline=input.readAll();QJsonParseError parse;auto doc=QJsonDocument::fromJson(baseline,&parse);input.close();
            if(parse.error!=QJsonParseError::NoError||!doc.isObject()||(doc.object().contains("mcpServers")&&!doc.object()["mcpServers"].isObject())){QMessageBox::warning(m_dock,"MCP",QString::fromUtf8("Некорректный конфиг; запись отменена"));return;}config=doc.object();
        }
        auto servers=config["mcpServers"].toObject();servers.insert("ataman",ai::registration("Claude Code",exe)["mcpServers"].toObject()["ataman"]);config.insert("mcpServers",servers);
        const auto payload=QJsonDocument(config).toJson();
        if(QMessageBox::question(m_dock,QString::fromUtf8("Подтвердите запись"),path+"\n\n"+QString::fromUtf8(payload))!=QMessageBox::Yes)return;
        QFile guard(path);QByteArray current;if(guard.exists()){if(!guard.open(QIODevice::ReadOnly))return;current=guard.readAll();guard.close();}
        if(current!=baseline){QMessageBox::warning(m_dock,"MCP",QString::fromUtf8("Конфиг изменился; повторите подключение"));return;}
        if(!baseline.isEmpty()) {QFile backup(path+".ataman-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".bak");if(!backup.open(QIODevice::WriteOnly|QIODevice::NewOnly)||backup.write(baseline)!=baseline.size())return;}
        QSaveFile output(path);if(output.open(QIODevice::WriteOnly)&&output.write(payload)==payload.size()&&output.commit())event(QString::fromUtf8("Конфиг Claude Code сохранён"));
        else QMessageBox::warning(m_dock,"MCP",QString::fromUtf8("Не удалось сохранить конфиг"));
    }
}
