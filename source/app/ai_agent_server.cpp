#include "native_output.h"
#include "ai_agent_server.h"
#include "main_window.h"
#include "view.h"
#include "rebuild64_viewport_widget.h"
#include <QTemporaryFile>
#include "landscape.h"
#include <QtConcurrent>
#include "log.h"
#include "log_rotation.h"
#include "file_write_activity.h"
#include "topdown_export.h"

namespace {
bool nativePlanRequiresLegacy(const ai::EditPlan& plan) {
    if(QStringList{"paint","auto_paint","generate_terrain","erode","edit_units"}.contains(plan.method))return true;
    if(plan.method=="edit_objects")for(const auto& value:plan.changes) {
        auto object=value.toObject()["after"].toObject();
        if(object.contains("World object"))object=object["World object"].toObject();
        if(object["Model name"].toString().compare("stst8",Qt::CaseInsensitive)==0)return true;
    }
    return false;
}
QJsonObject nativePlanRefusal() {
    return {{"code","REQUIRES_OPENGL"},{"error",QString::fromUtf8("Эта операция требует OpenGL и не применена. Выберите OpenGL через Настройки → Вид карты; документы и правки сохранены.")}};
}
QString agentHistoryPath(){return QDir(QCoreApplication::applicationDirPath()).filePath("logs/agent-workflow.log");}
QJsonObject headlessAuthority() {
    if(!qApp->property("agentHeadless").toBool())return {};
    QSettings source(qApp->property("agentAuthorityFile").toString(),QSettings::Format(qApp->property("agentAuthorityFormat").toInt()));source.sync();
    return {{"mode",source.value("ui/aiAgentMode",0).toInt()},{"allowSave",source.value("ui/aiAgentAllowSave",false).toBool()}};
}
}

CAiAgentServer::CAiAgentServer(MainWindow* w):QObject(w),m_window(w),m_view(w->view())
{
    m_dock=new QDockWidget(QString::fromUtf8("ИИ-агент"),w);m_dock->setObjectName("aiAgentDock");
    auto* body=new QWidget(m_dock);auto* layout=new QVBoxLayout(body);
    m_mode=new QComboBox(body);m_mode->setObjectName("aiAgentMode");
    m_mode->addItems({QString::fromUtf8("Выкл"),QString::fromUtf8("Только чтение"),QString::fromUtf8("Правка")});
    layout->addWidget(m_mode);m_status=new QLabel(QString::fromUtf8("Отключено"),body);layout->addWidget(m_status);
    m_status->setObjectName("aiAgentStatus");
    m_statusBar=new QLabel(w);m_statusBar->setObjectName("aiAgentStatusBar");
    w->statusBar()->addPermanentWidget(m_statusBar);
    m_allowSave=new QCheckBox(QString::fromUtf8("Разрешить агенту сохранять"),body);m_allowSave->setObjectName("aiAgentAllowSave");m_allowSave->setChecked(false);layout->addWidget(m_allowSave);
    m_events=new QPlainTextEdit(body);m_events->setReadOnly(true);m_events->setMaximumBlockCount(200);layout->addWidget(m_events);
    m_events->setObjectName("aiAgentEvents");
    {QFile history(agentHistoryPath());if(history.open(QIODevice::ReadOnly)){history.seek(qMax(qint64(0),history.size()-65536));m_events->setPlainText(QString::fromUtf8(history.readAll()));}}
    auto* setup=new QPushButton(QString::fromUtf8("Подключить Codex/Claude Code"),body);layout->addWidget(setup);
    auto* off=new QPushButton(QString::fromUtf8("Отключить"),body);off->setObjectName("aiAgentDisconnect");layout->addWidget(off);
    m_dock->setWidget(body);
    connect(m_mode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int n){
        QSettings settings;settings.setValue("ui/aiAgentMode",n);settings.sync();setEnabled(n);});
    connect(m_allowSave,&QCheckBox::toggled,this,[](bool checked){
        QSettings settings;settings.setValue("ui/aiAgentAllowSave",checked);settings.sync();});
    connect(off,&QPushButton::clicked,this,[this](){
        QSettings settings;settings.setValue("ui/aiAgentMode",0);settings.sync();setEnabled(0);});
    connect(setup,&QPushButton::clicked,this,&CAiAgentServer::configureClient);
    connect(&m_server,&QLocalServer::newConnection,this,&CAiAgentServer::accept);
    connect(&m_expiry,&QTimer::timeout,this,&CAiAgentServer::prune);m_expiry.start(1000);
    // QPointer also protects the fallback destructor path. Normal shutdown explicitly
    // stops automation BEFORE deleting CView, while its native surface is alive.
    connect(w->view(),&QObject::destroyed,this,&CAiAgentServer::stop);
    // Restore human authority before startup can open a map. Shutdown only
    // closes transport; it must never overwrite the user's saved preference.
    QSettings settings;
    {QSignalBlocker blocker(m_allowSave);m_allowSave->setChecked(settings.value("ui/aiAgentAllowSave",false).toBool());}
    bool valid=false;const int mode=settings.value("ui/aiAgentMode",0).toInt(&valid);
    setEnabled(valid&&mode>=0&&mode<=2?mode:0);
}
CAiAgentServer::~CAiAgentServer(){stop();}
void CAiAgentServer::event(const QString& text)
{
    updateStatus();
    if(qApp->property("agentHeadless").toBool()&&!text.startsWith(QString::fromUtf8("Чтение: operation_status"))) {
        ei::log(eLogInfo,"AI background: "+text);
        const auto path=agentHistoryPath();QDir().mkpath(QFileInfo(path).absolutePath());QLockFile lock(path+".lock");
        if(lock.tryLock(0)) {
            ei::rotateLog(path,text.toUtf8().size()+128);
            QFile history(path);if(history.open(QIODevice::WriteOnly|QIODevice::Append))history.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)+" pid="+QString::number(QCoreApplication::applicationPid())+" "+text+"\n").toUtf8());
        }else ei::log(eLogWarning,"AI history busy; full event retained in process workflow.log");
    }
    const auto line=QTime::currentTime().toString("HH:mm:ss")+" "+text;
    if(m_lastEvent==text) {
        ++m_eventRepeats;QTextCursor cursor(m_events->document());cursor.movePosition(QTextCursor::End);cursor.movePosition(QTextCursor::StartOfBlock,QTextCursor::KeepAnchor);
        cursor.insertText(line+QString::fromUtf8(" (×%1)").arg(m_eventRepeats));
    } else {m_lastEvent=text;m_eventRepeats=1;m_events->appendPlainText(line);}
}
void CAiAgentServer::updateStatus()
{
    int authenticated=0;for(auto* s:m_clients.keys())authenticated+=s->property("authenticated").toBool();
    const auto mode=enabled()?m_mode->currentText():QString::fromUtf8("Выкл");
    const auto text=QString::fromUtf8("ИИ-агент: %1; агентов: %2").arg(mode).arg(authenticated);
    m_status->setText(text);if(m_statusBar)m_statusBar->setText(text);
    m_allowSave->setEnabled(enabled()&&m_mode->currentIndex()==2);
}
void CAiAgentServer::cancelJob(Job& job)
{
    if(job.committing)return;
    if(job.topdown){job.topdown->finished={};job.topdown->cancel();job.topdown.clear();}
    job.cancel->store(true);job.plan.clear();job.unitSource.clear();
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
    m_server.close();m_writer.clear();
    const auto sockets=m_clients.keys();m_clients.clear();
    for(auto* socket:sockets){socket->disconnect(this);socket->abort();socket->deleteLater();}
    for(auto it=m_jobs.begin();it!=m_jobs.end();)
        if(it->committing)++it;else {cancelJob(*it);it=m_jobs.erase(it);}
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
        {"background",qApp->property("agentHeadless").toBool()},{"installation",QCoreApplication::applicationDirPath()},
        {"mode",mode==2?"edit":"read-only"},{"started",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(descriptor).toJson(QJsonDocument::Compact))<0||!file.commit()) {
        stop();event(QString::fromUtf8("Не удалось записать обнаружение"));return;
    }
    {QSignalBlocker blocker(m_mode);m_mode->setCurrentIndex(mode);}
    event(mode==2?(m_allowSave->isChecked()?QString::fromUtf8("Правка и сохранение разрешены пользователем"):QString::fromUtf8("Правка разрешена пользователем; сохранение выключено")):QString::fromUtf8("Чтение разрешено пользователем"));
}
void CAiAgentServer::accept()
{
    while(m_server.hasPendingConnections()) {
        auto* s=m_server.nextPendingConnection();
        int pending=0;for(auto* client:m_clients.keys())pending+=!client->property("authenticated").toBool();
        // Independent budgets: old bridge probes cannot consume authenticated
        // agent slots. Never evict an established client for a new arrival.
        if(s->state()!=QLocalSocket::ConnectedState||pending>=16){s->abort();s->deleteLater();continue;}
        s->setReadBufferSize(ai::MaxFrame+1);m_clients.insert(s,{});
        s->setProperty("authenticated",false);
        connect(s,&QLocalSocket::readyRead,this,[this,s](){receive(s);});
        connect(s,&QLocalSocket::disconnected,this,[this,s](){
            const bool authenticated=s->property("authenticated").toBool();bool discarded=false;
            for(auto it=m_jobs.begin();it!=m_jobs.end();)if(it->owner==s&&!it->committing){discarded|=!it->complete||bool(it->plan);cancelJob(*it);it=m_jobs.erase(it);}else ++it;
            if(m_writer==s)m_writer.clear();m_clients.remove(s);s->deleteLater();updateStatus();
            if(authenticated)event(discarded?QString::fromUtf8("Агент отключён; его незавершённые операции отброшены"):QString::fromUtf8("Агент отключён"));});
        QTimer::singleShot(5000,s,[s](){if(!s->property("authenticated").toBool())s->abort();});
        // Data/disconnect can arrive before nextPendingConnection is consumed.
        // Do not wait for a readyRead signal that has already been delivered.
        if(s->bytesAvailable())receive(s);
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
        if(!s->property("authenticated").toBool()) {
            int authenticated=0;for(auto* client:m_clients.keys())authenticated+=client->property("authenticated").toBool();
            if(authenticated>=16){s->abort();return;}
            s->setProperty("authenticated",true);event(QString::fromUtf8("Агент подключён; проверка пройдена"));
        }
        const auto method=request["method"].toString();const auto args=request["arguments"].toObject();
        m_hadAgent=true;m_noClientsSince=0;
        if(method!="mcp_catalog"){m_lastActivity=QDateTime::currentMSecsSinceEpoch();m_shutdownWarned=false;}
        QJsonObject result;
        if(request.contains("protocolMajor")&&request["protocolMajor"]!=ai::LocalProtocolMajor)
            result=ai::error("Incompatible local protocol; supported protocolMajor=1. Update the MCP bridge.");
        else if(method=="mcp_catalog"&&args.isEmpty())result=ai::mcpCatalog();
        else if(method=="mcp_resource_read"&&args.keys()==QStringList{"uri"}) {
            const auto uri=args["uri"].toString();
            if(uri=="ataman://tools")result=ai::mcpCatalog();
            else if(uri=="ataman://context")result=dispatch("get_context",{});
            else result=ai::error("Resource not found");
        }
        else if(!ai::validateArguments(method,args,&diagnostic))result=ai::error(diagnostic);
        else {m_currentClient=s;result=dispatch(method,args);m_currentClient.clear();}
        QByteArray out=QJsonDocument(QJsonObject{{"id",id},{"result",result}}).toJson(QJsonDocument::Compact);
        if(out.size()>ai::MaxResult)out=QJsonDocument(QJsonObject{{"id",id},{"result",ai::error("Response limit; use a smaller region/page")}}).toJson(QJsonDocument::Compact);
        if(s->bytesToWrite()>ai::MaxFrame){s->abort();return;}
        s->write(out+'\n');
        // Deliver the operation ID before a queued normal file commit can
        // enter its atomic section on the next GUI turn.
        s->flush();
        // Only validated tool names. Never arguments, secrets or document content.
        if(method!="mcp_catalog")event(QString::fromUtf8("Чтение: ")+ (diagnostic.isEmpty()?method:QString("invalid request")));
    }
    if(buffer.size()>ai::MaxFrame)s->abort();
}
void CAiAgentServer::prune()
{
    const auto now=QDateTime::currentDateTimeUtc();
    for(auto it=m_jobs.begin();it!=m_jobs.end();) {
        // A GUI commit may pump events while opening/saving a large map. Its
        // deadline governs cancellable preparation, never an atomic commit.
        if(it->committing){++it;continue;}
        if(it->complete) {
            if(!it->finishedAt.isValid())it->finishedAt=now;
            if(it->finishedAt.msecsTo(now)>60000){cancelJob(*it);it=m_jobs.erase(it);}else ++it;
            continue;
        }
        if(it->snapshot) {
            if(!it->complete&&it->created.msecsTo(now)>=30000) {
                const auto phase=it->phase;cancelJob(*it);it->markComplete();it->finishedAt=now;
                it->result={{"code","SNAPSHOT_TIMEOUT"},{"error","Snapshot exceeded 30 seconds"},{"phase",phase},{"deadlineMs",30000}};
            }
            if(it->complete&&it->finishedAt.msecsTo(now)>60000){cancelJob(*it);it=m_jobs.erase(it);}else ++it;
            continue;
        }
        if(it->topdown) {
            if(!it->complete && it->created.msecsTo(now)>120000)it->topdown->cancel("EXPORT_TIMEOUT");
            if(it->complete && it->finishedAt.msecsTo(now)>60000){cancelJob(*it);it=m_jobs.erase(it);}else ++it;
            continue;
        }
        const auto age=it->created.msecsTo(now);
        if(!it->complete&&age>it->deadlineMs){cancelJob(*it);it->markComplete();it->result=ai::error("Operation deadline exceeded");}
        ++it;
    }
    if(!qApp->property("agentHeadless").toBool())return;
    QSettings authority(qApp->property("agentAuthorityFile").toString(),QSettings::Format(qApp->property("agentAuthorityFormat").toInt()));authority.sync();
    const int mode=authority.value("ui/aiAgentMode",0).toInt();
    const int effective=mode>=1&&mode<=2?mode:0;
    if((enabled()?m_mode->currentIndex():0)!=effective)setEnabled(effective);
    {QSignalBlocker blocker(m_allowSave);m_allowSave->setChecked(authority.value("ui/aiAgentAllowSave",false).toBool());}
    const auto ms=QDateTime::currentMSecsSinceEpoch();
    if(m_clients.isEmpty()){if(!m_noClientsSince)m_noClientsSince=ms;}else m_noClientsSince=0;
    bool valid=false;const int override=qEnvironmentVariableIntValue("EI_MAPPER_HEADLESS_IDLE_MS",&valid);
    const auto idle=valid?qBound(1000,override,600000):600000;
    if(m_committing||m_editWorkers->load())m_lastActivity=ms;
    for(const auto& job:m_jobs)if(!job.complete)m_lastActivity=ms;
    if(ms-m_lastActivity>=idle||(m_hadAgent&&m_noClientsSince&&ms-m_noClientsSince>=1500)||!effective)shutdownHeadless(true);
}
QJsonObject CAiAgentServer::shutdownHeadless(bool automatic)
{
    if(!qApp->property("agentHeadless").toBool())return ai::error("Only a background mapper can be shut down by an agent; owner editor retained");
    if(m_committing||m_editWorkers->load()||CFileWriteActivity::active())return ai::error("Background operation is running; wait before shutdown");
    for(const auto& job:m_jobs)if(!job.complete||job.plan)return ai::error("Background operation or preview is pending; complete or cancel it before shutdown");
    if(CAiEditAdapter::dirty(m_window)) {
        if(!m_shutdownWarned){event(QString::fromUtf8("Не закрыт: есть несохранённые правки. Агент должен сохранить их с разрешением владельца или явно отменить свои операции."));m_shutdownWarned=true;}
        return ai::error("Unsaved changes: background mapper retained. Save with owner permission or undo changes before shutdown.");
    }
    if(m_shutdownScheduled)return {{"status","shutting_down"}};
    m_shutdownScheduled=true;const auto activity=m_lastActivity;
    QTimer::singleShot(100,m_window,[this,automatic,activity](){
        m_shutdownScheduled=false;
        if(!m_window||(automatic&&m_lastActivity!=activity)||m_committing||m_editWorkers->load()||CFileWriteActivity::active()||CAiEditAdapter::dirty(m_window))return;
        for(const auto& job:m_jobs)if(!job.complete||job.plan)return;
        event(QString::fromUtf8("Фоновый маппер завершает работу; все документы сохранены"));
        qApp->setProperty("agentHeadlessClosing",true);if(m_window->close())qApp->quit();
    });
    return {{"status","shutting_down"}};
}
QJsonObject CAiAgentServer::startJob(const QString& method,const QJsonObject& args)
{
    prune();if(m_jobs.size()>=8)return ai::error("Operation limit 8; cancel or wait for expiry");
    int running=0;for(const auto& j:m_jobs)if(!j.complete)++running;
    if(running>=2)return ai::error("Two reads already running");
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);m_jobs.insert(id,Job{});
    if(method=="list_units"||method=="get_unit") {
        m_jobs[id].owner=m_currentClient;
        const auto cancel=m_jobs[id].cancel;
        auto* watcher=new QFutureWatcher<ai::EditPlan>(this);m_jobs[id].editWatcher=watcher;
        connect(watcher,&QFutureWatcher<ai::EditPlan>::finished,this,[this,id,watcher](){
            if(m_jobs.contains(id)){
                auto plan=QSharedPointer<ai::EditPlan>::create(watcher->result());auto& job=m_jobs[id];job.editWatcher=nullptr;
                if(!job.cancel->load()){
                    job.result=plan->error.isEmpty()?plan->summary:ai::error(plan->error);
                    if(plan->error.isEmpty()){job.unitSource=plan;job.result["sourceOperation"]=id;}
                    job.markComplete();
                }
            }watcher->deleteLater();
        });
        QTimer::singleShot(0,this,[this,id,method,args,cancel,watcher](){
            if(!m_jobs.contains(id)||!m_window)return;const auto captured=CAiEditAdapter::captureObjects(m_window,method,args);
            if(!m_jobs.contains(id)||cancel->load())return;
            if(!captured.error.isEmpty()){m_jobs[id].result=ai::error(captured.error);m_jobs[id].markComplete();m_jobs[id].editWatcher=nullptr;watcher->deleteLater();return;}
            watcher->setFuture(QtConcurrent::run([captured,cancel](){return CAiEditAdapter::prepareObjects(captured,cancel);}));
        });
    } else if(method=="query_passability"||method=="validate_map") {
        const auto cancel=m_jobs[id].cancel;
        auto* watcher=new QFutureWatcher<QJsonObject>(this);m_jobs[id].watcher=watcher;
        connect(watcher,&QFutureWatcher<QJsonObject>::finished,this,[this,id,watcher](){
            if(m_jobs.contains(id)){m_jobs[id].result=watcher->result();m_jobs[id].markComplete();m_jobs[id].watcher=nullptr;m_lastActivity=QDateTime::currentMSecsSinceEpoch();}
            watcher->deleteLater();
        });
        QTimer::singleShot(0,this,[this,id,method,args,cancel,watcher](){
            if(!m_jobs.contains(id)||!m_window)return;
            const auto captured=CAiEditAdapter::captureObjects(m_window,method,args);
            if(!m_jobs.contains(id)||cancel->load())return;
            if(!captured.error.isEmpty()){
                m_jobs[id].result=ai::error(captured.error);m_jobs[id].markComplete();m_jobs[id].watcher=nullptr;watcher->deleteLater();return;
            }
            watcher->setFuture(QtConcurrent::run([captured,cancel](){
                const auto result=CAiEditAdapter::prepareObjects(captured,cancel);
                return result.error.isEmpty()?result.summary:ai::error(result.error);
            }));
        });
    } else if(method=="read_terrain"||method=="search_database") {
        const auto model=method=="read_terrain"?CAiReadAdapter::terrain(m_window):CTerrainEditModel();
        const auto db=method=="search_database"?CAiEditAdapter::objectDatabase(m_window):ei::ProfileDatabaseContext::instance().current();const auto cancel=m_jobs[id].cancel;
        auto* watcher=new QFutureWatcher<QJsonObject>(this);m_jobs[id].watcher=watcher;
        connect(watcher,&QFutureWatcher<QJsonObject>::finished,this,[this,id,watcher](){
            if(m_jobs.contains(id)){m_jobs[id].result=watcher->result();m_jobs[id].markComplete();m_jobs[id].watcher=nullptr;m_lastActivity=QDateTime::currentMSecsSinceEpoch();}
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
                m_jobs[id].result=ai::error("Read worker could not start");m_jobs[id].markComplete();m_jobs[id].process=nullptr;process->deleteLater();
            }
        });
        connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this,id,process](int code,QProcess::ExitStatus status){
            if(m_jobs.contains(id)) {
                auto result=QJsonDocument::fromJson(process->property("output").toByteArray()+process->readAllStandardOutput()).object();
                if(status!=QProcess::NormalExit||code!=0||result.isEmpty())result=ai::error("Read worker failed or exceeded limits");
                m_jobs[id].result=result;m_jobs[id].markComplete();m_jobs[id].process=nullptr;m_lastActivity=QDateTime::currentMSecsSinceEpoch();
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
    if(qApp->property("agentHeadlessClosing").toBool())return ai::error("Background mapper is shutting down; reconnect on demand");
    if(m_window&&m_window->m_shutdownInProgress)return ai::error(QString::fromUtf8("Редактор завершает работу. Повторите подключение после запуска; документы и сохранения не менялись."));
    const auto authority=headlessAuthority();
    if(!authority.isEmpty()) {
        if(authority["mode"].toInt()==0)return ai::error(QString::fromUtf8("Владелец выключил доступ агентов в доке"));
        if(authority["mode"].toInt()!=m_mode->currentIndex())return ai::error("Owner changed access mode; wait for automatic reconnect and re-read context");
        QSignalBlocker blocker(m_allowSave);m_allowSave->setChecked(authority["allowSave"].toBool());
    }
    if(method=="shutdown_headless")return shutdownHeadless();
    if(m_committing && method!="operation_status" && method!="cancel_operation") {
        if(method=="window_snapshot") {
            QJsonObject out{{"code","BUSY"},{"error","Normal map commit in progress"},{"phase","opening_map"}};
            for(auto it=m_jobs.constBegin();it!=m_jobs.constEnd();++it)if(it->edit&&!it->complete){out["operation"]=it.key();break;}
            return out;
        }
        return ai::error("Normal map commit in progress; retry after operation_status");
    }
    // Native startup no longer paints an invisible GL world. Materialize the
    // canonical CPU terrain model at the first MCP document query/edit.
    if(method!="operation_status" && method!="cancel_operation" && m_view
       && m_view->property("rebuild64Selected").toBool() && CLandscape::getInstance()->isMprLoad() && !m_view->m_terrainEditAvailable
       && !CAiEditAdapter::busy(m_window))m_view->ensureTerrainAutoCatalogReady();
    const QSet<QString> legacyTerrainTools{"paint","auto_paint","generate_terrain","erode"};
    const auto requestedTool=method=="preview"?args["tool"].toString():method;
    if(m_view && m_view->property("rebuild64Selected").toBool() && legacyTerrainTools.contains(requestedTool))
        return {{"code","REQUIRES_OPENGL"},{"error",QString::fromUtf8("Изменение рельефа, текстур, воды и дорог пока требует OpenGL. Выберите OpenGL через Настройки → Вид карты; документы и правки сохраняются.")}};
    if(m_view && m_view->property("rebuild64Selected").toBool() && method=="edit_units")
        return {{"code","REQUIRES_OPENGL"},{"error",QString::fromUtf8("Правки персонажей, appearance и stst8 пока требуют OpenGL. Выберите OpenGL через Настройки → Вид карты; документы и правки сохраняются.")}};
    if(method=="get_revision")return {{"revision",CAiEditAdapter::revision(m_window)},{"mode",m_mode->currentIndex()==2?"edit":"read-only"},{"allowSave",m_mode->currentIndex()==2&&m_allowSave->isChecked()}};
    const QSet<QString> mutations{"edit_units","edit_objects","rebuild_ai_graph","create_map","open_map","close_map","place_map_in_world","set_world_position","create_mob","sculpt","paint","generate_terrain","erode","carve_river","auto_paint","preview","apply","undo_last_agent_operation","save"};
    if(mutations.contains(method)) {
        if(m_mode->currentIndex()!=2)return ai::error("Read-only mode: only the human can enable Правка in the dock");
        if(m_writer&&m_writer!=m_currentClient)return ai::error("Another agent owns the writer session; disconnect it before switching writer");
        if(!m_currentClient)return ai::error("No writer connection");
        if(!m_writer)m_writer=m_currentClient;
        if(QStringList{"create_map","place_map_in_world","set_world_position","create_mob"}.contains(method)&&!m_allowSave->isChecked())return ai::error("Persistence disabled: human must enable Allow agent to save before writing MPR/MOB/world layout");
        if(method=="save"&&!m_allowSave->isChecked())return ai::error("Save disabled: human must check Разрешить агенту сохранять; changes remain in memory");
        if(method=="apply") {
            const auto id=args["operation"].toString();
            if(!m_jobs.contains(id)||m_jobs[id].owner!=m_currentClient||!m_jobs[id].plan)return ai::error("No completed preview owned by this connection");
            auto plan=m_jobs[id].plan;
            if(m_view && m_view->property("rebuild64Selected").toBool() && nativePlanRequiresLegacy(*plan))
                return {{"code","REQUIRES_OPENGL"},{"error",QString::fromUtf8("Этот предпросмотр требует OpenGL. План сохранён; выберите OpenGL для применения или отмените операцию.")}};
            if(args["revision"].toString()!=plan->revision||CAiEditAdapter::revision(m_window)!=plan->revision)return ai::error("Document revision conflict; prepare a new preview");
            m_jobs.remove(id);return startCommit(plan,args["snapshot"].toBool());
        }
        if(method=="undo_last_agent_operation") {
            if(args["revision"].toString()!=CAiEditAdapter::revision(m_window))return ai::error("Document revision conflict; get_revision first");
            const auto* stack=m_window->m_undoStack;
            if(stack->index()>0&&CAiEditAdapter::lifecycleWrites(stack->command(stack->index()-1))&&!m_allowSave->isChecked())return ai::error("Undo writes world layout; human must enable Allow agent to save");
            return CAiEditAdapter::undo(m_window);
        }
        return startEdit(method,args);
    }
    if(QApplication::activeModalWidget()&&method!="operation_status"&&method!="cancel_operation")return ai::error("Editor modal interaction in progress; retry later");
    if(method=="export_map_topdown")return startTopdownExport(args);
    if(method=="window_snapshot")return startSnapshot();
    if(method=="read_export_image")return ai::readExportImage(args);
    if(method=="list_units"||method=="get_unit"||method=="query_passability"||method=="validate_map"||method=="read_terrain"||method=="search_database"||method=="read_profile_map"||method=="list_profile_maps")return startJob(method,args);
    if(method=="operation_status"||method=="cancel_operation") {
        prune();const auto id=args["operation"].toString();if(!m_jobs.contains(id))return ai::error("Unknown or expired operation");
        auto& job=m_jobs[id];
        if(job.edit && job.owner!=m_currentClient)return ai::error("Operation belongs to another writer");
        if(method=="cancel_operation"){
            if(job.committing)return ai::error("Operation is committing; wait for its result");
            QJsonObject out{{"operation",id},{"status","cancelled"}};
            if(job.topdown && !job.complete){job.topdown->cancel();out["result"]=job.topdown->result();}
            cancelJob(job);m_jobs.remove(id);return out;
        }
        if(job.topdown) {
            QJsonObject out{{"operation",id},{"status",job.topdown->done()?job.topdown->phase():QString("running")},
                {"phase",job.topdown->phase()},{"deadlineMs",120000}};
            if(job.complete){out["result"]=job.result;m_jobs.remove(id);}
            return out;
        }
        if(job.snapshot) {
            QJsonObject out{{"operation",id},{"status",!job.complete?"running":job.result.contains("error")?"failed":"complete"},
                {"phase",job.phase},{"deadlineMs",30000}};
            if(job.complete){out["result"]=job.result;m_jobs.remove(id);}
            return out;
        }
        QJsonObject out{{"operation",id},{"status",job.complete?"complete":"running"}};
        if(job.complete)out.insert("result",job.result);return out;
    }
    auto result=CAiReadAdapter::read(m_window,method,args);
    if(method=="get_context"){result["mode"]=m_mode->currentIndex()==2?"edit":"read-only";result["allowSave"]=m_mode->currentIndex()==2&&m_allowSave->isChecked();result["background"]=qApp->property("agentHeadless").toBool();result["dirty"]=CAiEditAdapter::dirty(m_window);}
    return result;
}
QJsonObject CAiAgentServer::startSnapshot()
{
    prune();if(m_jobs.size()>=8)return ai::error("Operation limit 8; cancel or wait for expiry");
    for(auto it=m_jobs.constBegin();it!=m_jobs.constEnd();++it)if(!it->complete&&(it->snapshot||it->edit||it->topdown))
        return {{"code","BUSY"},{"error","Scene operation in progress"},{"operation",it.key()}};
    if(CAiEditAdapter::busy(m_window))return {{"code","BUSY"},{"error","Scene loading or interaction in progress"}};
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    Job job;job.snapshot=true;job.owner=m_currentClient;job.phase="preparing";m_jobs.insert(id,job);
    QTimer::singleShot(0,this,[this,id](){captureSnapshot(id);});
    return {{"operation",id},{"status","running"},{"phase","preparing"},{"deadlineMs",30000},{"poll","operation_status"}};
}
void CAiAgentServer::captureSnapshot(const QString& id)
{
    if(!m_jobs.contains(id)||m_jobs[id].complete)return;
    auto& job=m_jobs[id];
    const auto fail=[&](QString code,QString reason){job.result={{"code",code},{"error",reason},{"phase",job.phase},{"deadlineMs",30000}};job.markComplete();job.finishedAt=QDateTime::currentDateTimeUtc();};
    if(job.created.msecsTo(QDateTime::currentDateTimeUtc())>=30000){fail("SNAPSHOT_TIMEOUT","Snapshot preparation exceeded 30 seconds");return;}
    if(CAiEditAdapter::busy(m_window)||m_committing){fail("BUSY","Map loading/interaction in progress");return;}
    const auto revision=CAiEditAdapter::revision(m_window);const auto context=CAiReadAdapter::context(m_window);
    if(auto* native=m_window->findChild<CRebuild64Viewport*>())if(native->rebuildSelected()) {
        const auto empty=native->emptyViewportImage();
        if(!empty.isNull()){encodeSnapshot(id,empty,revision,context,0,"Qt Rebuild64 viewport without an open map; no world frame");return;}
        if(!native->frameIsCurrent()){fail("BUSY",QString::fromUtf8("Rebuild64 готовит кадр открытой карты; повторите снимок после загрузки."));return;}
        auto file=QSharedPointer<QTemporaryFile>::create(QDir::tempPath()+"/rebuild64-snapshot-XXXXXX.png");
        if(!file->open()){fail("SNAPSHOT_IO",file->errorString());return;}
        const auto path=file->fileName();file->close();job.phase="capturing";
        auto connection=QSharedPointer<QMetaObject::Connection>::create();QElapsedTimer timer;timer.start();
        *connection=connect(native,&CRebuild64Viewport::captureFinished,this,[this,id,path,file,connection,revision,context,timer](const QString& result,const QString& error,quint64){
            if(result!=path)return;disconnect(*connection);
            if(!m_jobs.contains(id)||m_jobs[id].complete)return;
            if(!error.isEmpty()) {
                if(error.contains(QString::fromUtf8("отмен"),Qt::CaseInsensitive)){auto& j=m_jobs[id];j.result={{"code","CANCELLED"},{"error",error}};j.markComplete();return;}
                if(CAiEditAdapter::revision(m_window)!=revision){auto& j=m_jobs[id];j.result={{"code","REVISION_CONFLICT"},{"error",error}};j.markComplete();return;}
                emit m_view->updateMsg(tr("Снимок Rebuild64 недоступен; для этого снимка используется OpenGL. %1").arg(error));
                m_view->ensureLegacyQueryContext();m_view->ensureLegacyWorldResources();CLandscape::getInstance()->ensureRenderResources();
                QString diagnostic;const auto fallback=m_view->captureTopdownPass(ETopdownPass::None,m_view->size().scaled(1024,768,Qt::KeepAspectRatio),{},&diagnostic,true);
                if(fallback.isNull()){auto& j=m_jobs[id];j.result={{"code","RENDER_UNAVAILABLE"},{"error",diagnostic}};j.markComplete();return;}
                encodeSnapshot(id,fallback,revision,context,timer.elapsed(),"OpenGL fallback for native snapshot; current owning-context FBO");return;
            }
            encodeSnapshot(id,QImage(path),revision,context,timer.elapsed(),"Rebuild64 accepted native frame and editor service; own GPU readback");
        });
        QTimer::singleShot(30000,this,[this,id,file,connection,path,native=QPointer<CRebuild64Viewport>(native)]{
            if(native)native->cancelCapture(path);disconnect(*connection);
            if(m_jobs.contains(id)&&!m_jobs[id].complete){auto& j=m_jobs[id];j.result={{"code","SNAPSHOT_TIMEOUT"},{"error","Native snapshot deadline exceeded"}};j.markComplete();}
        });
        native->capture(path);return;
    }
    if(qApp->property("agentHeadless").toBool()&&qApp->property("agentNativeOutput").toBool()) {
        QString error;QElapsedTimer elapsed;elapsed.start();EirCamera camera{};
        const auto image=nativeOutput(m_view,0,QSize(1024,768),camera,&error);
        if(image.isNull()){fail("RENDER_UNAVAILABLE",error);return;}
        encodeSnapshot(id,image,revision,context,elapsed.elapsed(),"explicit headless Rebuild64 native memory output");return;
    }
    if(!m_view||!m_view->isValid()){fail("RENDER_UNAVAILABLE","Owning viewport GL context unavailable");return;}
    job.phase="capturing";QElapsedTimer timer;timer.start();QString error;
    const auto image=m_view->captureTopdownPass(ETopdownPass::None,m_view->size().scaled(1024,768,Qt::KeepAspectRatio),{},&error);
    if(image.isNull()){fail("RENDER_UNAVAILABLE",error);return;}
    encodeSnapshot(id,image,revision,context,timer.elapsed(),"current GL viewport; fresh owning-context FBO");
}
void CAiAgentServer::encodeSnapshot(const QString& id,const QImage& image,const QString& revision,
                                  const QJsonObject& context,qint64 captureMs,const QString& kind)
{
    if(!m_jobs.contains(id)||m_jobs[id].complete)return;
    auto& job=m_jobs[id];
    const auto fail=[&](QString code,QString reason){job.result={{"code",code},{"error",reason}};job.markComplete();job.finishedAt=QDateTime::currentDateTimeUtc();};
    if(image.isNull()){fail("RENDER_UNAVAILABLE","Frame readback is empty");return;}
    if(CAiEditAdapter::revision(m_window)!=revision){fail("REVISION_CONFLICT","Document changed during snapshot");return;}
    if(job.created.msecsTo(QDateTime::currentDateTimeUtc())>=30000){fail("SNAPSHOT_TIMEOUT","Snapshot capture exceeded 30 seconds");return;}
    job.phase="encoding";const auto cancel=job.cancel;
    auto* watcher=new QFutureWatcher<QJsonObject>(this);job.watcher=watcher;
    connect(watcher,&QFutureWatcher<QJsonObject>::finished,this,[this,id,watcher,revision,context,captureMs,kind](){
        if(m_jobs.contains(id)&&!m_jobs[id].complete) {
            auto& j=m_jobs[id];auto result=watcher->result();
            if(j.created.msecsTo(QDateTime::currentDateTimeUtc())>=30000)result={{"code","SNAPSHOT_TIMEOUT"},{"error","Snapshot encoding exceeded 30 seconds"},{"phase","encoding"}};
            result["capturedRevision"]=revision;result["profile"]=context["profile"];result["map"]=context["mpr"];
            result["captureKind"]=kind;
            result["timingsMs"]=QJsonObject{{"capture",double(captureMs)},{"total",double(j.created.msecsTo(QDateTime::currentDateTimeUtc()))}};
            j.result=result;j.markComplete();j.phase="complete";j.finishedAt=QDateTime::currentDateTimeUtc();j.watcher=nullptr;
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([image,cancel](){return ai::encodeImageBounded(image,QSize(1024,768),cancel);}));
}
QJsonObject CAiAgentServer::startTopdownExport(const QJsonObject& args)
{
    prune();if(m_jobs.size()>=8)return ai::error("Operation limit 8; cancel or wait for expiry");
    int running=0;for(const auto& job:m_jobs){if(!job.complete)++running;if(job.topdown&&!job.complete)return ai::error("A topdown export is already running");}
    if(running>=2)return ai::error("Two reads already running");
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    Job job;job.deadlineMs=120000;job.topdown=QSharedPointer<CTopdownExport>::create(m_window.data(),args);
    m_jobs.insert(id,job);
    job.topdown->finished=[this,id](){
        if(!m_jobs.contains(id))return;
        auto& j=m_jobs[id];j.result=j.topdown->result();j.markComplete();j.finishedAt=QDateTime::currentDateTimeUtc();
        m_lastActivity=QDateTime::currentMSecsSinceEpoch();
    };
    job.topdown->begin();
    return {{"operation",id},{"status","running"},{"phase","resolving_resources"},{"deadlineMs",120000},{"poll","operation_status"}};
}
namespace {
class AgentCommitDialogs : public QObject {
public:
    QString error;
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Show)if(auto* box=qobject_cast<QDialog*>(object)) {
            if(qobject_cast<QProgressDialog*>(object))return false;
            box->setAttribute(Qt::WA_DontShowOnScreen);
            auto* message=qobject_cast<QMessageBox*>(object);
            error=message?message->text():"Interactive dialog required: "+box->windowTitle();QPointer<QDialog> safe=box;
            QTimer::singleShot(0,box,[safe](){if(safe)safe->reject();});
        }
        return false;
    }
};
}
QJsonObject CAiAgentServer::startCommit(const QSharedPointer<ai::EditPlan>& plan,bool snapshot)
{
    if(m_view&&m_view->property("rebuild64Selected").toBool()&&nativePlanRequiresLegacy(*plan))return nativePlanRefusal();
    prune();if(m_jobs.size()>=8)return ai::error("Operation limit 8; cancel or wait for expiry");
    for(const auto& job:m_jobs)if(job.edit&&!job.complete)return ai::error("A writer operation is already running");
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);Job job;job.edit=true;job.owner=m_currentClient;
    m_jobs[id]=job;QTimer::singleShot(0,this,[this,id,plan,snapshot](){finishCommit(id,plan,snapshot);});
    return {{"operation",id},{"status","running"},{"poll","operation_status"}};
}
void CAiAgentServer::finishCommit(const QString& id,const QSharedPointer<ai::EditPlan>& plan,bool snapshot)
{
    if(!m_jobs.contains(id)||m_jobs[id].cancel->load()||!m_jobs[id].owner||!m_view||!m_window)return;
    // The renderer can change while an immutable plan is prepared or queued.
    // Enforce the same capability again at the actual mutation boundary.
    if(m_view->property("rebuild64Selected").toBool()&&nativePlanRequiresLegacy(*plan)) {
        m_jobs[id].result=nativePlanRefusal();m_jobs[id].markComplete();
        m_lastActivity=QDateTime::currentMSecsSinceEpoch();event(m_jobs[id].result["error"].toString());return;
    }
    m_jobs[id].committing=true;
    QStringList targets;
    if(plan->method=="save")targets=m_view->m_worldTerrainSession.dirtyPaths();
    else if(plan->method!="create_map")for(const auto& map:plan->maps)targets.append(map.path);
    if(!plan->destination.isEmpty())targets.append(plan->destination);
    if(m_view->m_pLand->isMprLoad())targets.append(m_view->m_pLand->filePath().absoluteFilePath());
    targets.removeDuplicates();
    auto result=ai::error("Edit authority revoked");
    const auto authority=headlessAuthority();
    if(!authority.isEmpty()){QSignalBlocker blocker(m_allowSave);m_allowSave->setChecked(authority["allowSave"].toBool());}
    if(m_mode->currentIndex()==2&&(authority.isEmpty()||authority["mode"].toInt()==2)) {
        QScopedValueRollback<bool> lock(m_committing,true);
        m_window->setProperty("aiCommitBusy",true);
        AgentCommitDialogs dialogs;qApp->installEventFilter(&dialogs);
        if(plan->method=="save") {
            if(!m_allowSave->isChecked())result=ai::error("Save permission revoked; changes remain in memory");
            else if(plan->revision!=CAiEditAdapter::revision(m_window))result=ai::error("Document revision conflict before Save");
            else result=CAiEditAdapter::save(m_window);
        } else if(QStringList{"create_map","place_map_in_world","set_world_position","create_mob"}.contains(plan->method)&&!m_allowSave->isChecked())result=ai::error("Save permission revoked; operation unchanged");
        else result=CAiEditAdapter::apply(m_window,*plan);
        qApp->removeEventFilter(&dialogs);
        if(!dialogs.error.isEmpty())result=ai::error(dialogs.error);
        if(snapshot&&!result.contains("error")&&m_window&&m_view)result["snapshot"]=CAiReadAdapter::read(m_window,"window_snapshot",{});
        if(m_window)m_window->setProperty("aiCommitBusy",false);
    }
    if(m_jobs.contains(id)){m_jobs[id].committing=false;m_jobs[id].result=result;m_jobs[id].markComplete();}
    m_lastActivity=QDateTime::currentMSecsSinceEpoch();
    const auto context=CAiReadAdapter::context(m_window);
    event(QString::fromUtf8("Операция %1: %2; MPR=%3; MOB=%4; цели=%5").arg(plan->method,result.contains("error")?result["error"].toString():result["status"].toString(),context["mpr"].toString(),QString::fromUtf8(QJsonDocument(context["mobs"].toArray()).toJson(QJsonDocument::Compact)),targets.join(";")));
}
QJsonObject CAiAgentServer::startEdit(const QString& requested,const QJsonObject& original)
{
    prune();if(m_jobs.size()>=8)return ai::error("Operation limit 8; cancel or wait for expiry");
    for(const auto& job:m_jobs)if(job.edit&&!job.complete)return ai::error("A writer operation is already running");
    const auto method=requested=="preview"?original["tool"].toString():requested;
    auto args=requested=="preview"?original["arguments"].toObject():original;
    QSharedPointer<ai::EditPlan> unitSource;
    if(method=="edit_units"&&args.contains("sourceOperation")){
        const auto source=args["sourceOperation"].toString();
        if(args["action"]!="copy"||!m_jobs.contains(source)||!m_jobs[source].complete||m_jobs[source].owner!=m_currentClient||!m_jobs[source].unitSource)
            return ai::error("sourceOperation must be a completed list_units/get_unit snapshot owned by this connection (TTL60s)");
        unitSource=m_jobs[source].unitSource;
        if(unitSource->activePath==CLandscape::getInstance()->filePath().absoluteFilePath()&&unitSource->revision!=CAiEditAdapter::revision(m_window))
            return ai::error("Source unit revision conflict; read source units again");
    }
    if(method=="save") {
        auto plan=QSharedPointer<ai::EditPlan>::create();plan->method="save";plan->revision=args["revision"].toString();
        return startCommit(plan,false);
    }
    if(m_editWorkers->load()!=0)return ai::error("Previous cancelled writer is still releasing its snapshot; retry shortly");
    if(requested=="preview")args["preview"]=true;
    const bool deferredCapture=CAiEditAdapter::objectTool(method);
    const auto candidate=deferredCapture?ai::EditPlan():CAiEditAdapter::capture(m_window,method,args);
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
        if(!plan->error.isEmpty()){m_jobs[id].result=ai::error(plan->error);m_jobs[id].markComplete();return;}
        if(args["preview"].toBool()) {
            m_jobs[id].plan=plan;m_jobs[id].markComplete();m_jobs[id].result=plan->summary;
            m_jobs[id].result["status"]="preview";m_jobs[id].result["operation"]=id;
        } else {
            QTimer::singleShot(0,this,[this,id,plan,args](){finishCommit(id,plan,args["snapshot"].toBool());});
        }
    });
    const auto workers=m_editWorkers;
    const auto launch=[this,id,watcher,candidate,cancel,workers,method,args,deferredCapture,unitSource](){
        if(!m_jobs.contains(id)||!m_window)return;
        auto snapshot=deferredCapture?CAiEditAdapter::capture(m_window,method,args):candidate;
        if(unitSource){
            snapshot.unitSource=unitSource->changes;
            for(auto i=unitSource->fileHashes.begin();i!=unitSource->fileHashes.end();++i){
                if(snapshot.fileHashes.contains(i.key())&&snapshot.fileHashes[i.key()]!=i.value())snapshot.error="Source MOB dependency revision conflict";
                snapshot.fileHashes[i.key()]=i.value();
            }
        }
        if(!m_jobs.contains(id)||cancel->load())return;
        if(!snapshot.error.isEmpty()){
            m_jobs[id].result=ai::error(snapshot.error);m_jobs[id].markComplete();m_jobs[id].editWatcher=nullptr;watcher->deleteLater();return;
        }
        workers->fetch_add(1);
        watcher->setFuture(QtConcurrent::run([snapshot,cancel,workers](){
            struct Release {std::shared_ptr<std::atomic_int> count;~Release(){count->fetch_sub(1);}} release{workers};
            return ai::prepareEdit(snapshot,cancel);
        }));
    };
    if(deferredCapture)QTimer::singleShot(0,this,launch);else launch();
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
        text->setPlainText(codex?QString::fromUtf8("Будет выполнено после подтверждения:\ncodex mcp add ataman -- \"%1\"\n\nИзменится ~/.codex/config.toml. Мост запускает фоновый маппер по сохранённым разрешениям дока.\n").arg(exe)
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
