#include "ai_agent_server.h"
#include "main_window.h"
#include "view.h"
#include <QtConcurrent>

CAiAgentServer::CAiAgentServer(MainWindow* w):QObject(w),m_window(w),m_view(w->view())
{
    m_dock=new QDockWidget(QString::fromUtf8("ИИ-агент"),w);m_dock->setObjectName("aiAgentDock");
    auto* body=new QWidget(m_dock);auto* layout=new QVBoxLayout(body);
    m_mode=new QComboBox(body);m_mode->setObjectName("aiAgentMode");
    m_mode->addItems({QString::fromUtf8("Выкл"),QString::fromUtf8("Только чтение"),QString::fromUtf8("Правка — скоро")});
    if(auto* model=qobject_cast<QStandardItemModel*>(m_mode->model()))model->item(2)->setEnabled(false);
    layout->addWidget(m_mode);m_status=new QLabel(QString::fromUtf8("Отключено"),body);layout->addWidget(m_status);
    m_events=new QPlainTextEdit(body);m_events->setReadOnly(true);m_events->setMaximumBlockCount(200);layout->addWidget(m_events);
    auto* setup=new QPushButton(QString::fromUtf8("Подключить Codex/Claude Code"),body);layout->addWidget(setup);
    auto* off=new QPushButton(QString::fromUtf8("Отключить"),body);off->setObjectName("aiAgentDisconnect");layout->addWidget(off);
    m_dock->setWidget(body);
    connect(m_mode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int n){setEnabled(n==1);});
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
    m_status->setText(enabled()?QString::fromUtf8("Только чтение; подключений: %1").arg(m_clients.size()):QString::fromUtf8("Отключено"));
}
void CAiAgentServer::cancelJob(Job& job)
{
    job.cancel->store(true);
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
    m_server.close();
    const auto sockets=m_clients.keys();m_clients.clear();
    for(auto* socket:sockets){socket->disconnect(this);socket->abort();socket->deleteLater();}
    for(auto& job:m_jobs)cancelJob(job);m_jobs.clear();
    if(!m_descriptor.isEmpty())QFile::remove(m_descriptor);
    m_descriptor.clear();m_secret.clear();m_id.clear();
    {QSignalBlocker blocker(m_mode);m_mode->setCurrentIndex(0);}
    event(QString::fromUtf8("Доступ выключен; операции отменены"));
}
void CAiAgentServer::setEnabled(bool enabled)
{
    stop();if(!enabled||!m_view||!m_window)return;
    if(!ai::secureDirectory(ai::discoveryDirectory())){event(QString::fromUtf8("Не удалось защитить каталог обнаружения"));return;}
    m_id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_secret=QUuid::createUuid().toString(QUuid::WithoutBraces)+QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if(!m_server.listen("ei-ataman-"+m_id)){stop();event(QString::fromUtf8("Не удалось открыть локальный канал"));return;}
    m_descriptor=QDir(ai::discoveryDirectory()).filePath(m_id+".json");
    QSaveFile file(m_descriptor);
    QJsonObject descriptor{{"instance",m_id},{"pipe",m_server.serverName()},{"secret",m_secret},
        {"pid",double(QCoreApplication::applicationPid())},{"version",ENDURANCE_VERSION},
        {"mode","read-only"},{"started",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(descriptor).toJson(QJsonDocument::Compact))<0||!file.commit()) {
        stop();event(QString::fromUtf8("Не удалось записать обнаружение"));return;
    }
    {QSignalBlocker blocker(m_mode);m_mode->setCurrentIndex(1);}
    event(QString::fromUtf8("Чтение разрешено пользователем"));
}
void CAiAgentServer::accept()
{
    while(m_server.hasPendingConnections()) {
        auto* s=m_server.nextPendingConnection();
        if(m_clients.size()>=4){s->abort();s->deleteLater();continue;}
        s->setReadBufferSize(ai::MaxFrame+1);m_clients.insert(s,{});
        s->setProperty("authenticated",false);
        connect(s,&QLocalSocket::readyRead,this,[this,s](){receive(s);});
        connect(s,&QLocalSocket::disconnected,this,[this,s](){m_clients.remove(s);s->deleteLater();event(QString::fromUtf8("Агент отключён"));});
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
        else result=dispatch(method,args);
        QByteArray out=QJsonDocument(QJsonObject{{"id",id},{"result",result}}).toJson(QJsonDocument::Compact);
        if(out.size()>ai::MaxResult)out=QJsonDocument(QJsonObject{{"id",id},{"result",ai::error("Response limit; use a smaller region/page")}}).toJson(QJsonDocument::Compact);
        if(s->bytesToWrite()>ai::MaxFrame){s->abort();return;}
        s->write(out+'\n');
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
        if(!it->complete&&age>30000){cancelJob(*it);it->complete=true;it->result=ai::error("Operation deadline exceeded");}
        if(age>60000){cancelJob(*it);it=m_jobs.erase(it);}else ++it;
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
    if(QApplication::activeModalWidget())return ai::error("Editor modal interaction in progress; retry later");
    if(method=="read_terrain"||method=="search_database"||method=="read_profile_map"||method=="list_profile_maps")return startJob(method,args);
    if(method=="operation_status"||method=="cancel_operation") {
        prune();const auto id=args["operation"].toString();if(!m_jobs.contains(id))return ai::error("Unknown or expired operation");
        auto& job=m_jobs[id];
        if(method=="cancel_operation"){cancelJob(job);m_jobs.remove(id);return {{"status","cancelled"}};}
        QJsonObject out{{"operation",id},{"status",job.complete?"complete":"running"}};
        if(job.complete)out.insert("result",job.result);return out;
    }
    return CAiReadAdapter::read(m_window,method,args);
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
