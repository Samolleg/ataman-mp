#include "log.h"
#include "log_rotation.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <cstring>
#include <QMutexLocker>

namespace {
char g_crashContext[2048] = "startup";
}

CLogger* CLogger::m_pLogger = nullptr;

CLogger *CLogger::getInstance()
{
    static CLogger* instance = new CLogger();
    return instance;
}

void CLogger::log(ELogMessageType type, const char *msg)
{
    log(type, QString(msg));
}

void CLogger::log(ELogMessageType type, const QString msg)
{
    SLogMessage entry;
    {
        QMutexLocker locker(&m_mutex);
        // Lifecycle receipts must survive a user's warning-only log level.
        if(m_loglvl<type && !msg.startsWith(QStringLiteral("Shutdown "))
           && !msg.startsWith(QStringLiteral("Startup phases ")))
            return;

        entry.sequence = m_nextSequence++;
        entry.type = type;
        entry.timestamp = QDateTime::currentDateTime();
        entry.message = msg;
        QString txt = QString("[%1] ").arg(entry.timestamp.toString("dd.MM.yyyy hh:mm:ss"));

        switch (type)
        {
        case eLogInfo: txt += QString("{INFO} \t %1").arg(msg); break;
        case eLogDebug: txt += QString("{DEBUG} \t %1").arg(msg); break;
        case eLogWarning: txt += QString("{WARNING} \t %1").arg(msg); break;
        case eLogFatal: txt += QString("{ERROR} \t %1").arg(msg); break;
        case eLogInit:
            txt = QString("[   %1\t  %2  ] {%3} \t %4").arg("DATE", "TIME", "TYPE", "MESSAGE"); break;
        }

        if(!ei::rotateLog(log_file.fileName(), txt.toUtf8().size()+8)
           || !log_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
        QTextStream textStream(&log_file);
        textStream.setCodec("UTF-8");
        textStream.setGenerateByteOrderMark(log_file.size()==0);
        entry.formatted = txt;
        textStream << txt << endl;
        textStream.flush();
        log_file.flush();
        log_file.close();
        m_sessionMessages.append(entry);
        if(m_sessionMessages.size()>4000)m_sessionMessages.remove(0,m_sessionMessages.size()-4000);
    }

    emit messageLogged(entry);

#ifdef QT_DEBUG
  qDebug() << txt;
#endif

//    if(type == eLogFatal)
    //        QCoreApplication::exit(-1);
}

void CLogger::attachSettings(CSettings *pSet)
{
    m_pSettings = pSet;
}

CLogger::CLogger()
    :QObject(nullptr)
    ,log_file()
    ,m_pSettings(nullptr)
    ,m_loglvl(eLogDebug)
{
    qRegisterMetaType<SLogMessage>("SLogMessage");
    QDir current = QDir::current();
    current.mkpath("logs");
    QDir logDir(current.filePath("logs"));
    if(qApp && qApp->property("agentHeadless").toBool()) {
        const auto path=logDir.filePath("headless-"+QString::number(QCoreApplication::applicationPid()));
        QDir().mkpath(path);logDir=QDir(path);
    }

    auto rotateLog = [&logDir](const QString& sourcePath)
    {
        QFileInfo source(sourcePath);
        if(!source.exists() || source.size() == 0)
            return;

        const QString timestamp = source.lastModified().toString("yyyyMMdd-HHmmss");
        QString destination = logDir.filePath(QString("workflow-%1.log").arg(timestamp));
        int suffix = 1;
        while(QFile::exists(destination))
            destination = logDir.filePath(QString("workflow-%1-%2.log").arg(timestamp).arg(suffix++));

        if(!QFile::rename(source.absoluteFilePath(), destination))
            qWarning() << "Cannot rotate workflow log" << source.absoluteFilePath() << "to" << destination;
        else for(int i=1;i<5;++i) {
            const auto suffix="."+QString::number(i);
            if(QFile::exists(source.absoluteFilePath()+suffix))QFile::rename(source.absoluteFilePath()+suffix,destination+suffix);
        }
    };

    rotateLog(logDir.filePath("workflow.log"));
    rotateLog(current.filePath("workflow.log")); // migrate the legacy root-level log once
    QString currentPath=logDir.filePath("workflow.log");
    if(QFileInfo(currentPath).size()>0){
        // A locked legacy log may not rotate. Start a separate UTF-8 file
        // instead of mixing encodings in that existing journal.
        int suffix=0;
        do {currentPath=logDir.filePath(QString("workflow-utf8-%1-%2.log")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")).arg(suffix++));}
        while(QFileInfo::exists(currentPath));
    }
    ei::pruneWorkflowLogs(logDir);
    log_file.setFileName(currentPath);
    log(eLogInit, "Log started");
}

void CLogger::setLogLevel(ELogMessageType type)
{
    QMutexLocker locker(&m_mutex);
    m_loglvl = type;
}

QVector<SLogMessage> CLogger::sessionMessages() const
{
    QMutexLocker locker(&m_mutex);
    return m_sessionMessages;
}

QString CLogger::logFilePath() const
{
    QMutexLocker locker(&m_mutex);
    return log_file.fileName();
}

CLogger::~CLogger()
{//this method never used bcs singleton is a singleton!!!
    if(nullptr != m_pLogger)
    {
        delete m_pLogger;
    }
}

void ei::log(ELogMessageType type, const QString msg)
{
    CLogger::getInstance()->log(type, msg);
}

void ei::log(ELogMessageType type, const char *msg)
{
    CLogger::getInstance()->log(type, msg);
}

void ei::log(ELogMessageType type, const QString msg, QString func)
{
    CLogger::getInstance()->log(type, msg +"\t|" + func);
}

void ei::setCrashContext(const QString& context)
{
    const QByteArray encoded = context.toUtf8();
    const int length = qMin(encoded.size(), int(sizeof(g_crashContext) - 1));
    std::memcpy(g_crashContext, encoded.constData(), size_t(length));
    g_crashContext[length] = '\0';
}

const char* ei::crashContext()
{
    return g_crashContext;
}
