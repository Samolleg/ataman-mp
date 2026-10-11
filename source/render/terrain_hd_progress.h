#ifndef TERRAIN_HD_PROGRESS_H
#define TERRAIN_HD_PROGRESS_H

#include <QString>
#include <QtGlobal>
#include <atomic>
#include <chrono>
#include <mutex>
#include <functional>

// The worker owns updates; the GUI polls a bounded snapshot. No QObject or
// event queue crosses the worker boundary. Cancellation never terminates a thread.
class CTerrainHdProgress
{
public:
    enum Stage { Opening, Indexing, Matching, Recovering, Preparing, Complete };
    struct Snapshot {
        Stage stage{Opening};
        qint64 done{0}, total{0}, elapsedMs{0}, stageRemainingMs{-1};
        int percent{0};
    };
    explicit CTerrainHdProgress(std::function<void(const Snapshot&)> observer = {}) : m_observer(observer) {}
    void cancel() { m_cancelled.store(true); }
    bool cancelled() const { return m_cancelled.load(); }
    bool update(Stage stage, qint64 done, qint64 total) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if(stage < m_value.stage) return !cancelled();
        if(stage != m_value.stage) m_stageStart = Clock::now();
        m_value.stage=stage; m_value.done=done; m_value.total=total;
        const int starts[]={0,5,35,55,90,100}, ends[]={5,35,55,90,100,100};
        const int value=starts[stage]+(total>0 ? int((ends[stage]-starts[stage])*qBound<qint64>(0,done,total)/total) : 0);
        m_value.percent=qMax(m_value.percent,value);
        const auto valueCopy=m_value;lock.unlock();
        // Optional worker-side instrumentation, never a GUI callback.
        if(m_observer)m_observer(valueCopy);
        return !cancelled();
    }
    Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto s=m_value; const auto now=Clock::now();
        s.elapsedMs=std::chrono::duration_cast<std::chrono::milliseconds>(now-m_start).count();
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-m_stageStart).count();
        // Work units of different stages have different costs. Never extrapolate
        // the weighted overall percentage into a fictitious whole-job ETA.
        if((s.stage==Indexing||s.stage==Matching||s.stage==Recovering)&&elapsed>=3000&&s.done>=8&&s.total>s.done)
            s.stageRemainingMs=elapsed*(s.total-s.done)/s.done;
        return s;
    }
    static QString duration(qint64 ms) {
        const auto seconds=qMax<qint64>(0,ms/1000);
        return QStringLiteral("%1:%2:%3").arg(seconds/3600).arg((seconds/60)%60,2,10,QLatin1Char('0')).arg(seconds%60,2,10,QLatin1Char('0'));
    }
    static QString label(const Snapshot& s) {
        const QString names[]={QStringLiteral("Открытие источников"),QStringLiteral("Индексация HD-источников"),
            QStringLiteral("Сопоставление слотов"),QStringLiteral("Восстановление переходов"),
            QStringLiteral("Подготовка публикации"),QStringLiteral("Подготовка завершена")};
        QString text=names[s.stage];
        if(s.total>0)text+=QStringLiteral(" %1/%2").arg(s.done).arg(s.total);
        text+=QStringLiteral("\nОбщий прогресс: %1% · Прошло: %2").arg(s.percent).arg(duration(s.elapsedMs));
        text+=s.stageRemainingMs>=0?QStringLiteral("\nДо конца этапа: ≈ %1").arg(duration(s.stageRemainingMs))
            :QStringLiteral("\nОставшееся время: оценка уточняется");
        return text;
    }
private:
    using Clock=std::chrono::steady_clock;
    const Clock::time_point m_start=Clock::now();
    Clock::time_point m_stageStart=m_start;
    mutable std::mutex m_mutex;
    std::atomic<bool> m_cancelled{false};
    Snapshot m_value;
    const std::function<void(const Snapshot&)> m_observer;
};

#endif
