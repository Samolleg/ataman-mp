#pragma once
#include "brush_ambient_prepared.h"
#include <thread>
#include <mutex>
#include <condition_variable>

struct BrushFinalizeTask {
 uint64_t ticket=0,generation=0,world=0,serial=0;
 uint32_t flags=0,x0=0,y0=0,x1=0,y1=0;
 std::shared_ptr<owned::TerrainCpuAsset> terrain;
 std::shared_ptr<const owned::OwnedMapSnapshot> map;
 owned::OwnedTerrainGeometryValues geometry;
 owned::TerrainTileFamilies families;
 std::vector<uint32_t> edges;
 bool own_edges=true;
 bool surf_sea=false;
 std::chrono::steady_clock::time_point start;
 std::shared_ptr<owned::BrushAmbientPrepared> prepared;
 std::string failure;
 double cpu_ms=0,thread_ms=0;
};
// One worker, one coalesced request, one result. New edits retire an older
// ticket; no per-stamp thread and no future destructor can block a frame.
class BrushFinalizeWorker {
 std::mutex mutex;std::condition_variable wake;std::thread thread;
 std::shared_ptr<BrushFinalizeTask> pending,completed;
 uint64_t ticket=0;bool stopping=false;
 void run(){
  SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_NORMAL);
  for(;;){std::shared_ptr<BrushFinalizeTask> task;
   {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||bool(pending);});if(stopping)return;task=std::move(pending);}
   const auto begin=std::chrono::steady_clock::now();
   FILETIME created{},exit{},kernel0{},user0{},kernel1{},user1{};
   GetThreadTimes(GetCurrentThread(),&created,&exit,&kernel0,&user0);
   try{
    owned::BrushStageTimer timer;
    owned::owned_terrain_reset_liquid_outflow(*task->terrain);task->terrain->procedural_falls.reset();
    task->prepared=owned::prepare_brush_ambient(task->terrain,task->map.get(),task->geometry,task->families,task->edges,task->own_edges);
    timer.mark("worker_ambient_total");
    if(!task->prepared)task->failure="background terrain derivatives failed";
    else {
     const auto& g=task->prepared->grids;
     task->prepared->surf=std::make_shared<owned::LiquidSurfField>(owned::build_liquid_surf_field(
      g.area,g.level.liquid_area,g.height,g.level.ground,g.tiles_x,g.tiles_y,task->surf_sea));
     timer.mark("worker_surf");
     if(GetEnvironmentVariableW(L"ATAMAN_BRUSH_ATTRIBUTE_COMPARE",nullptr,0))for(uint32_t i=0;i<task->terrain->sectors.size();++i){
      const auto& sector=task->terrain->sectors[i];if(!sector.editor_revision||!sector.editor_attributes)continue;
      auto complete=owned::prepare_brush_terrain_attributes(*task->terrain,i,sector.editor_attributes->revision);
      if(!complete)throw std::runtime_error("complete attribute comparison decode failed");
      size_t differing=0;for(size_t v=0;v<complete->values.size();++v){
       const auto& a=sector.editor_attributes->values[v];const auto& b=complete->values[v];
       if(!std::memcmp(&a,&b,sizeof(a)))continue;
       if(differing++<8){const auto* x=reinterpret_cast<const uint8_t*>(&a);const auto* y=reinterpret_cast<const uint8_t*>(&b);size_t byte=0;while(byte<sizeof(a)&&x[byte]==y[byte])++byte;
        std::fprintf(stderr,"BRUSH_ATTRIBUTE_MISMATCH serial=%llu flags=%u sector=%u corner=%zu first_byte=%zu old=%u full=%u bed_depth=%.9g/%.9g emission=%.9g/%.9g objects=%08x/%08x flags=%08x/%08x\n",task->serial,task->flags,i,v,byte,unsigned(x[byte]),unsigned(y[byte]),a.liquid_depth,b.liquid_depth,a.liquid_bed_emission,b.liquid_bed_emission,a.shift_objects,b.shift_objects,a.flags,b.flags);
       }
      }
      if(differing)std::fprintf(stderr,"BRUSH_ATTRIBUTE_TOTAL serial=%llu sector=%u corners=%zu\n",task->serial,i,differing);
     }
    }
   }catch(const std::exception& e){task->failure=e.what();}catch(...){task->failure="background terrain derivatives exception";}
   task->cpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
   GetThreadTimes(GetCurrentThread(),&created,&exit,&kernel1,&user1);
   const auto ticks=[](const FILETIME& v){return (uint64_t(v.dwHighDateTime)<<32)|v.dwLowDateTime;};
   task->thread_ms=double(ticks(kernel1)+ticks(user1)-ticks(kernel0)-ticks(user0))/10000.;
   std::fprintf(stderr,"BRUSH_WORK_CPU world=%llu serial=%llu wall_ms=%.3f thread_ms=%.3f\n",task->world,task->serial,task->cpu_ms,task->thread_ms);
   {std::lock_guard<std::mutex> lock(mutex);if(!stopping&&task->ticket==ticket)completed=std::move(task);}
  }
 }
public:
 ~BrushFinalizeWorker(){shutdown();}
 void cancel(){std::lock_guard<std::mutex> lock(mutex);++ticket;pending.reset();completed.reset();}
 void post(std::shared_ptr<BrushFinalizeTask> task){std::lock_guard<std::mutex> lock(mutex);
  task->ticket=++ticket;pending=std::move(task);completed.reset();if(!thread.joinable())thread=std::thread([this]{run();});wake.notify_one();
 }
 std::shared_ptr<BrushFinalizeTask> take(){std::lock_guard<std::mutex> lock(mutex);return std::move(completed);}
 void shutdown(){
  {std::lock_guard<std::mutex> lock(mutex);stopping=true;++ticket;pending.reset();completed.reset();wake.notify_all();}
  if(thread.joinable())thread.join();
 }
};
