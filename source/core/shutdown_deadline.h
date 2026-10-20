#ifndef SHUTDOWN_DEADLINE_H
#define SHUTDOWN_DEADLINE_H
#include <condition_variable>
#include <chrono>
#include <thread>
#include <mutex>
#include <cstdlib>
#include <QFile>
#include <QString>

// Never arm while a save is active. This is the final teardown safety net,
// including driver destruction and Qt/static worker joins after the event loop.
class CShutdownDeadline
{
public:
    static CShutdownDeadline& instance() { static CShutdownDeadline deadline; return deadline; }
    void arm(const QString& journal) {
        if(worker.joinable())return;
        worker=std::thread([this,journal]() {
            std::unique_lock<std::mutex> lock(mutex);
            if(done.wait_for(lock,std::chrono::seconds(30),[this](){return finished;}))return;
            QFile file(journal);
            if(file.open(QIODevice::Append)) {
                file.write("\nShutdown complete status:teardown-timeout exitCode:2\n");file.flush();
            }
            std::_Exit(2);
        });
    }
    ~CShutdownDeadline() {
        {std::lock_guard<std::mutex> lock(mutex);finished=true;}
        done.notify_all();if(worker.joinable())worker.join();
    }
private:
    std::mutex mutex;std::condition_variable done;std::thread worker;bool finished{false};
};
#endif
