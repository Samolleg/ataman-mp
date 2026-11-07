#pragma once
#include <QStringList>
#include <functional>
#include <memory>
#include <mutex>

// The document writer owns the transaction. A display may temporarily release
// read handles before an atomic RES replacement; unit/document tests need no host.
class CResourceWriteLease {
public:
    using Acquire=std::function<std::shared_ptr<void>(const QStringList&,QString*)>;
    static Acquire setAcquirer(Acquire callback){std::lock_guard<std::mutex> lock(mutex());auto previous=std::move(acquirer());acquirer()=std::move(callback);return previous;}
    static std::shared_ptr<void> acquire(const QStringList& paths,QString* error){
        Acquire callback;{std::lock_guard<std::mutex> lock(mutex());callback=acquirer();}
        return callback?callback(paths,error):std::shared_ptr<void>();
    }
private:
    static Acquire& acquirer(){static Acquire value;return value;}
    static std::mutex& mutex(){static std::mutex value;return value;}
};
