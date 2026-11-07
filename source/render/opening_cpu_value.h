#pragma once
#include <QtConcurrent>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <utility>
namespace ei {
// CPU values only: callers must not capture widgets, mutable caches or GL state.
// The canonical OpenGL path remains synchronous and executes the same function.
template<class F> auto openingCpuValue(bool native,F function)->decltype(function()) {
    if(!native)return function();
    auto future=QtConcurrent::run(std::move(function));
    while(!future.isFinished()){QCoreApplication::processEvents(QEventLoop::AllEvents,4);QThread::msleep(1);}
    return future.result();
}
}
