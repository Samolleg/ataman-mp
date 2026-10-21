#pragma once
#include "log.h"
#include <QElapsedTimer>
#include <QStringList>
#include <QCoreApplication>
#include <QVariant>

// A handful of completed phases plus one total; no per-file startup chatter.
class CStartupPhases
{
public:
    explicit CStartupPhases(const QString& scope) : m_scope(scope) { m_timer.start(); }
    void mark(const QString& name) {
        const qint64 now = m_timer.elapsed();
        m_parts << name + "Ms:" + QString::number(now - m_last);
        m_last = now;
    }
    ~CStartupPhases() {
        ei::log(eLogInfo, "Startup phases " + m_scope + " " + m_parts.join(' ')
            + " totalMs:" + QString::number(m_timer.elapsed()));
    }
private:
    QString m_scope;
    QStringList m_parts;
    QElapsedTimer m_timer;
    qint64 m_last{0};
};

class CStartupFrameReceipt
{
public:
    ~CStartupFrameReceipt() {
        if(!qApp || !qApp->property("startupReferenceMs").isValid()
           || qApp->property("startupFrameComplete").toBool())return;
        QElapsedTimer now;now.start();
        qApp->setProperty("startupFrameComplete",true);
        ei::log(eLogInfo,QString("Startup phases firstFrame totalMs:%1")
            .arg(now.msecsSinceReference()-qApp->property("startupReferenceMs").toLongLong()));
    }
};
