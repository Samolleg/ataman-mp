#ifndef LOG_H
#define LOG_H

#include <QString>
#include <QFile>
#include <QObject>
#include <QDateTime>
#include <QMutex>
#include <QVector>

#define LOG_FATAL(msg) ei::log(eLogFatal, msg, Q_FUNC_INFO)

enum ELogMessageType
{
    eLogInit = 0
    ,eLogFatal
    ,eLogWarning
    ,eLogInfo
    ,eLogDebug
};

class CSettings;

struct SLogMessage
{
    quint64 sequence{0};
    ELogMessageType type{eLogInfo};
    QDateTime timestamp;
    QString message;
    QString formatted;
};
Q_DECLARE_METATYPE(SLogMessage)

///
/// \brief The CLogger class provides logging
///
class CLogger : public QObject
{
    Q_OBJECT
public:
    static CLogger* getInstance();
    void log(ELogMessageType type, const char* msg);
    void log(ELogMessageType type, const QString msg);
    void attachSettings(CSettings* pSet);

    CLogger(CLogger const&) = delete;
    void operator=(CLogger const&)  = delete;
    void setLogLevel(ELogMessageType type);
    QVector<SLogMessage> sessionMessages() const;
    QString logFilePath() const;

signals:
    void messageLogged(const SLogMessage& message);

private:
    CLogger();
    ~CLogger();

private:
    static CLogger* m_pLogger;
    QFile log_file;
    CSettings* m_pSettings;
    ELogMessageType m_loglvl;
    mutable QMutex m_mutex;
    QVector<SLogMessage> m_sessionMessages;
    quint64 m_nextSequence{1};
};

namespace ei {
void log(ELogMessageType type, const char* msg);
void log(ELogMessageType type, const QString msg);
void log(ELogMessageType type, const QString msg, QString func);
void setCrashContext(const QString& context);
const char* crashContext();
}

#endif // LOG_H
