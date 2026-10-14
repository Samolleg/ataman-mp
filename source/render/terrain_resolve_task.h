#ifndef TERRAIN_RESOLVE_TASK_H
#define TERRAIN_RESOLVE_TASK_H

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <atomic>

// The GUI owns this modal transaction. Workers only update atomics and mutate
// isolated document/atlas stages; run() always joins before their owners leave.
class CTerrainResolveTask : public QDialog
{
public:
    explicit CTerrainResolveTask(QWidget* parent) : QDialog(parent) {
        setObjectName(QStringLiteral("terrainResolveProgressDialog"));
        setWindowTitle(QStringLiteral("Исправить переходы"));
        setWindowModality(Qt::ApplicationModal);setMinimumWidth(500);
        auto* layout=new QVBoxLayout(this);
        m_label=new QLabel(this);layout->addWidget(m_label);
        m_bar=new QProgressBar(this);layout->addWidget(m_bar);
        m_cancel=new QPushButton(QStringLiteral("Отмена"),this);
        m_cancel->setObjectName(QStringLiteral("terrainResolveCancel"));
        layout->addWidget(m_cancel,0,Qt::AlignRight);
        connect(m_cancel,&QPushButton::clicked,this,[this](){reject();});
        connect(&m_timer,&QTimer::timeout,this,[this](){refresh();});
        m_timer.start(100);m_pump.start();show();
    }
    void reject() override {
        if(m_committing)return;
        m_cancelled.store(true);m_cancel->setEnabled(false);refresh();
    }
    bool cancelled() const {return m_cancelled.load();}
    bool updateWork(int done,int total) {
        m_done.store(done);m_total.store(total);return !cancelled();
    }
    bool checkpoint(const QString& operation) {
        m_operation=operation;
        if(m_pump.elapsed()>=40) {
            refresh();QApplication::processEvents();m_pump.restart();
        }
        return !cancelled();
    }
    template<class Job> bool run(const QString& operation,Job job) {
        if(!checkpoint(operation))return false;
        updateWork(0,0);refresh();
        auto future=QtConcurrent::run(job);
        QFutureWatcher<bool> watcher;QEventLoop loop;
        connect(&watcher,&QFutureWatcherBase::finished,&loop,&QEventLoop::quit);
        watcher.setFuture(future);
        if(!future.isFinished())loop.exec();
        return future.result()&&!cancelled();
    }
    void lockForCommit() {
        m_committing=true;m_cancel->setEnabled(false);
        m_operation=QStringLiteral("Применение подготовленных изменений");refresh();
    }
private:
    void refresh() {
        const int total=m_total.load(),done=m_done.load();
        m_label->setText(cancelled()?QStringLiteral("Отмена подготовки… Изменения не применяются."):m_operation);
        m_bar->setRange(0,qMax(0,total));m_bar->setValue(done);
    }
    std::atomic<bool> m_cancelled{false};
    std::atomic<int> m_done{0},m_total{0};
    bool m_committing{false};
    QString m_operation;
    QLabel* m_label;
    QProgressBar* m_bar;
    QPushButton* m_cancel;
    QTimer m_timer;
    QElapsedTimer m_pump;
};
#endif
