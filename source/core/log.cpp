#include "log.h"
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
    if(nullptr == m_pLogger)
        m_pLogger = new CLogger();
    return m_pLogger;
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
        if(m_loglvl<type)
            return;

        if(!log_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            qWarning() << "Cannot open workflow log:" << log_file.fileName() << log_file.errorString();
            return;
        }

        entry.sequence = m_nextSequence++;
        entry.type = type;
        entry.timestamp = QDateTime::currentDateTime();
        entry.message = msg;
        QString txt = QString("[%1] ").arg(entry.timestamp.toString("dd.MM.yyyy hh:mm:ss"));

        QTextStream textStream(&log_file);
        textStream.setCodec("UTF-8");
        switch (type)
        {
        case eLogInfo: txt += QString("{INFO} \t %1").arg(msg); break;
        case eLogDebug: txt += QString("{DEBUG} \t %1").arg(msg); break;
        case eLogWarning: txt += QString("{WARNING} \t %1").arg(msg); break;
        case eLogFatal: txt += QString("{ERROR} \t %1").arg(msg); break;
        case eLogInit:
            txt = QString("[   %1\t  %2  ] {%3} \t %4").arg("DATE", "TIME", "TYPE", "MESSAGE"); break;
        }

        entry.formatted = txt;
        textStream << txt << endl;
        textStream.flush();
        log_file.flush();
        log_file.close();
        m_sessionMessages.append(entry);
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
    };

    rotateLog(logDir.filePath("workflow.log"));
    rotateLog(current.filePath("workflow.log")); // migrate the legacy root-level log once
    log_file.setFileName(logDir.filePath("workflow.log"));
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
