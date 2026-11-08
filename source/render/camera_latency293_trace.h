#pragma once
// Opt-in camera regression counters. No hot-path file, Qt mutex or GUI access.
#include <atomic>
#include <array>
#include <mutex>
#include <windows.h>
#include <QJsonObject>
#include <QApplication>
#include <QEvent>
namespace latency293 {
enum Site {Tick,Service,Snapshot,ReuseWait,PublishWait,TickCameraWait,LiveCameraWait,
           Raster,WorkerSnapshotWait,PresentLease,Dispatch,ProcessEvents,Sleep,Input,ExternalPoll,Count};
struct Value {std::atomic<qint64> maximum{0},total{0},calls{0};};
struct State {std::atomic<int> phase{0},legacyStops{0},legacyInterval{-1};std::array<std::array<Value,Count>,2> values;QString slowReceiver[2];int slowEvent[2]{};};
inline State& state(){static State s;return s;}
inline qint64 clock(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
inline void add(Site site,int phase,qint64 start){
    if(!phase||state().phase.load()!=phase)return;
    const auto ticks=clock()-start;auto& v=state().values[phase-1][site];
    auto previous=v.maximum.load();while(previous<ticks&&!v.maximum.compare_exchange_weak(previous,ticks)){}
    v.total.fetch_add(ticks);v.calls.fetch_add(1);
}
struct Scope {
    Site site;int phase; qint64 start;
    explicit Scope(Site s):site(s),phase(state().phase.load()),start(phase?clock():0){}
    ~Scope(){add(site,phase,start);}
};
struct Lock {
    std::unique_lock<std::mutex> lock;
    Lock(std::mutex& mutex,Site site):lock(mutex,std::defer_lock){Scope timing(site);lock.lock();}
};
inline QJsonObject results(int phase){
    static const char* names[]={"Qt.tick","Qt.serviceInput","Qt.serviceSnapshot","Qt.service.reuse.mutex",
        "Qt.service.publish.mutex","Qt.tick.camera.mutex","Qt.liveCamera.mutex","worker.raster",
        "worker.snapshot.mutex","worker.Present.lease","Qt.notify","harness.processEvents","harness.Sleep1","Qt.editorInput","Qt.externalWorldRefresh"};
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);QJsonObject result;
    for(int i=0;i<Count;++i){const auto& v=state().values[phase-1][i];result[names[i]]=QJsonObject{
        {"maxMs",double(v.maximum.load())*1000/frequency.QuadPart},{"calls",double(v.calls.load())},
        {"totalMs",double(v.total.load())*1000/frequency.QuadPart}};}
    result["slowReceiver"]=state().slowReceiver[phase-1];result["slowEvent"]=state().slowEvent[phase-1];
    result["legacyTimerStops"]=state().legacyStops.load();result["legacyTimerInterval"]=state().legacyInterval.load();return result;
}
class Application : public QApplication {
public:
    Application(int& argc,char** argv):QApplication(argc,argv){}
    bool notify(QObject* receiver,QEvent* event) override {
        const int phase=state().phase.load();if(!phase)return QApplication::notify(receiver,event);
        const QString name=QString::fromLatin1(receiver->metaObject()->className())+":"+receiver->objectName();
        const int type=int(event->type());const auto begin=clock();
        const bool handled=QApplication::notify(receiver,event);const auto ticks=clock()-begin;
        if(state().phase.load()==phase&&ticks>state().values[phase-1][Dispatch].maximum.load()){
            state().slowReceiver[phase-1]=name;state().slowEvent[phase-1]=type;
        }
        add(Dispatch,phase,begin);return handled;
    }
};
}
