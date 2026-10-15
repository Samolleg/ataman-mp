#pragma once
#include "log.h"
#include <QElapsedTimer>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// A slow-frame record must distinguish running code from driver/OS waits.
// No GL queries, files, allocations or logging on the normal frame path.
class CRenderStallTrace
{
public:
    explicit CRenderStallTrace(const char* operation) : m_operation(operation)
    { m_timer.start(); m_cpuStart = threadCpuNsecs(); }
    void mark(const char* label)
    {
        if(m_count == 16) return;
        m_samples[m_count++] = {label, m_timer.nsecsElapsed(), threadCpuNsecs()};
    }
    ~CRenderStallTrace()
    {
        const qint64 total = m_timer.nsecsElapsed();
        if(total < 250000000) return;
        mark("tail");
        QString stages;
        qint64 prior = 0, priorCpu = m_cpuStart;
        for(int i = 0; i < m_count; ++i)
        {
            const auto& sample = m_samples[i];
            stages += QString(" %1:%2/%3") .arg(sample.label)
                .arg(double(sample.wall - prior) / 1000000.0, 0, 'f', 2)
                .arg(double(sample.cpu - priorCpu) / 1000000.0, 0, 'f', 2);
            prior = sample.wall; priorCpu = sample.cpu;
        }
        ei::log(eLogWarning, QString("Slow operation:%1 wallMs:%2 stagesWallCpuMs:%3")
            .arg(m_operation).arg(double(total) / 1000000.0, 0, 'f', 2).arg(stages));
    }
private:
    static qint64 threadCpuNsecs()
    {
#ifdef Q_OS_WIN
        FILETIME created, exited, kernel, user;
        if(GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
            return (qint64(kernel.dwHighDateTime) * 4294967296LL + kernel.dwLowDateTime
                  + qint64(user.dwHighDateTime) * 4294967296LL + user.dwLowDateTime) * 100;
#endif
        return 0;
    }
    struct Sample { const char* label; qint64 wall; qint64 cpu; };
    Sample m_samples[16];
    int m_count{0};
    const char* m_operation;
    QElapsedTimer m_timer;
    qint64 m_cpuStart{0};
};
