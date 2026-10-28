#include "malachite_bridge.h"
#include "mob.h"
#include "view.h"
#include "navigation_save_events.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QResizeEvent>
#include <QTextCodec>
#include <QUndoStack>
#include <QTimer>

QString CMalachiteBridge::executable()
{
    // The override is intended for isolated prototype checks, not persisted settings.
    const auto overridePath = qEnvironmentVariable("EI_MALACHITE_HOST");
    return overridePath.isEmpty() ? QCoreApplication::applicationDirPath()+"/tools/Malachite/Ataman.MalachiteHost.exe" : overridePath;
}
CMalachiteBridge::CMalachiteBridge(CMob* mob,CView* view,QWidget* parent):QWidget(parent),m_mob(mob),m_view(view)
{
    setAttribute(Qt::WA_NativeWindow); setMinimumSize(400,300);
    m_text=displayText();
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&CMalachiteBridge::receive);
    connect(&m_process,&QProcess::readyReadStandardError,this,[this](){const auto bytes=m_process.readAllStandardError();if(!bytes.isEmpty())m_failure+=QString::fromUtf8(bytes);});
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){if(!m_stopping)fail(m_process.errorString());});
    connect(&m_process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){if(!m_stopping)fail(tr("Malachite host stopped (%1). Local text is retained.").arg(code));});
    connect(view,&CView::mobScriptChanged,this,[this](CMob* mob,const QString&){if(mob==m_mob){++m_revision;if(!m_inCall)synchronize();}});
    connect(CNavigationSaveEvents::instance(),&CNavigationSaveEvents::mapSaved,this,[this](const QString& path,bool){
        if(!m_inCall && QFileInfo(path).absoluteFilePath()==m_mob->filePath().absoluteFilePath())synchronize();
    });
}
CMalachiteBridge::~CMalachiteBridge()
{
    m_stopping=true;
    if(m_process.state()!=QProcess::NotRunning){command("close");m_process.closeWriteChannel();if(!m_process.waitForFinished(1500)){m_process.kill();m_process.waitForFinished(1500);}}
}
QString CMalachiteBridge::displayText() const
{
    return QTextCodec::codecForName("Windows-1251")->toUnicode(m_mob->script().toLatin1()).replace("\r\n","\n").replace('\r','\n');
}
QJsonObject CMalachiteBridge::snapshot() const
{
    return {{"text",displayText()},{"revision",double(m_revision)},{"dirty",m_mob->isDirty()}};
}
void CMalachiteBridge::send(const QJsonObject& object)
{
    m_process.write(QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n');
}
int CMalachiteBridge::command(const QString& name,QJsonObject data)
{
    data["command"]=name;data["id"]=++m_serial;send(data);return m_serial;
}
void CMalachiteBridge::start(bool verification)
{
    QStringList args{QString::number(qulonglong(winId()))};if(verification)args<<"--verify";
    connect(&m_process,&QProcess::started,this,[this](){auto data=snapshot();data["name"]=m_mob->filePath().absoluteFilePath();data["width"]=width();data["height"]=height();command("open",data);});
    m_process.setWorkingDirectory(QFileInfo(executable()).absolutePath());m_process.start(executable(),args);
    QTimer::singleShot(20000,this,[this](){if(!m_ready)fail(tr("Malachite startup timed out. Local text is retained."));});
}
void CMalachiteBridge::synchronize()
{
    if(!m_ready||m_draft)return;
    m_text=displayText();command("sync",snapshot());emit draftChanged();
}
void CMalachiteBridge::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);if(m_ready)command("resize",{{"width",width()},{"height",height()}});
}
void CMalachiteBridge::fail(const QString& message)
{
    if(m_failed||m_stopping)return;
    m_failed=true;m_ready=false;m_failure=message;emit failed(message);
}
void CMalachiteBridge::receive()
{
    m_input+=m_process.readAllStandardOutput();
    while(m_input.contains('\n'))
    {
        const auto end=m_input.indexOf('\n');const auto line=m_input.left(end);m_input.remove(0,end+1);
        QJsonParseError error;const auto doc=QJsonDocument::fromJson(line,&error);
        if(error.error!=QJsonParseError::NoError||!doc.isObject()){fail("Invalid Malachite response");continue;}
        const auto message=doc.object();
        if(message.contains("fatal")){fail(message.value("fatal").toString());continue;}
        if(message.contains("call"))
        {
            const auto method=message.value("method").toString();const auto data=message.value("data").toObject();
            bool ok=true;QString problem;m_inCall=true;
            if(method=="apply")
            {
                m_text=data.value("text").toString().replace("\r\n","\n").replace('\r','\n');m_draft=true;
                auto* codec=QTextCodec::codecForName("Windows-1251");
                QString stored=m_text;
                if(m_mob->script().contains("\r\n"))stored.replace("\n","\r\n");
                const auto encoded=codec->fromUnicode(stored);
                if(qint64(data.value("revision").toDouble())!=m_revision){ok=false;problem=tr("Script changed in the mapper. Local text is retained.");}
                else if(codec->toUnicode(encoded)!=stored||stored.contains(QChar(0))){ok=false;problem=tr("Script cannot be stored in Windows-1251. Local text is retained.");}
                else{m_view->commitMobScriptEdit(m_mob,QString::fromLatin1(encoded),tr("Edit MOB script (Malachite)"));m_draft=false;m_text=displayText();}
                emit draftChanged();
            }
            else if(method=="undo"||method=="redo")
            {
                m_view->malachiteHistoryStep(method=="redo");m_text=displayText();
            }
            else if(method=="save")
            {
                ok=m_view->saveMobDocument(m_mob,m_mob->filePath());
                if(!ok)problem=m_mob->lastSaveError();
            }
            else{ok=false;problem="Unsupported Malachite request";}
            m_inCall=false;auto result=snapshot();result["reply"]=message.value("call");result["ok"]=ok;result["error"]=problem;send(result);
        }
        else
        {
            if(message.contains("embedded"))
            {
                m_ready=message.value("ok").toBool()&&message.value("embedded").toBool();
                if(m_ready){m_text=displayText();emit opened(true);}else fail(message.value("error").toString("Embedding failed"));
            }
            else if(!message.value("ok").toBool()) fail(message.value("error").toString());
            emit response(message.value("id").toInt(),message);
        }
    }
}
