// Owner-thread adoption only; the CPU worker has no renderer/device context.
static int brush_finalize_poll(Session& s,probe1b_error* e){
 auto task=s.brushWorker.take();if(!task)return 0;
 if(task->world!=s.world||task->generation!=s.generation||task->serial!=s.edits.applied_serial){
  std::fprintf(stderr,"BRUSH_FINALIZE discard world=%llu serial=%llu current=%llu/%llu\n",task->world,task->serial,s.world,s.edits.applied_serial);return 0;
 }
 if(!task->failure.empty())return fail(e,E_FAIL,task->failure.c_str());
 auto next=task->terrain;next->editor_defer_shadows=false;
 const auto liquid_revision=++s.editRevision;
 for(auto& sector:next->sectors)if(sector.has_layers)sector.editor_liquid_revision=liquid_revision;
 auto map=std::make_shared<owned::OwnedMapSnapshot>(*s.map);map->terrain=next;
 auto& scene=OwnedRender::E1Scene().Scene();
 if(!scene.editor_replace_terrain(map)||!OwnedRender::E1UpdateTerrainSnapshot(&s,next))return fail(e,E_FAIL,"completed brush publication rejected");
 owned::AtamanReadiness::brushPrepared(s.renderer,task->prepared);
 const HRESULT hr=owned::AtamanReadiness::brushResources(s.renderer,next,map,task->flags,task->x0,task->y0,task->x1,task->y1);
 if(FAILED(hr))return fail(e,hr,"completed brush resources rejected");
 s.terrain=next;s.baseline=next;s.map=map;s.finalizePublishedSerial=task->serial;
 s.edits.finalize_cpu_ms=task->cpu_ms;
 std::fprintf(stderr,"BRUSH_FINALIZE publish world=%llu serial=%llu cpu_ms=%.3f\n",s.world,task->serial,task->cpu_ms);
 return 0;
}
static void brush_finalize_frame_done(Session& s){
 if(s.edits.derived_pending&&s.finalizePublishedSerial==s.edits.applied_serial&&s.edits.presented_serial==s.edits.applied_serial&&
    !s.pending&&owned::AtamanReadiness::brushNeighboursReady(s.renderer)&&owned::AtamanReadiness::brushDerivedReady(s.renderer)){
  s.edits.finalized_serial=s.edits.applied_serial;s.edits.derived_pending=0;
  s.edits.finalize_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-s.finalizeStart).count();
  std::fprintf(stderr,"BRUSH_FINALIZE present world=%llu serial=%llu elapsed_ms=%.3f presents=%llu\n",s.world,s.edits.finalized_serial,s.edits.finalize_ms,s.edits.presents);
 }
}
