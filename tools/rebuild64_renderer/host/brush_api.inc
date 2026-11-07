// ABI10. These calls run between frames on the owning renderer thread.
#include "brush_material_revision.inc"
#include "brush_liquid_bounds.inc"
// Borrowed for this synchronous decode only. Nested archive resolution owns
// every selected SEC member before the call returns; no caller span survives.
struct BrushMprSlice final : owned::ResourceSlice {
 const uint8_t* data;
 BrushMprSlice(const void* source,uint64_t bytes):data(static_cast<const uint8_t*>(source)){size=bytes;}
 bool read(uint64_t offset,uint64_t bytes,std::vector<uint8_t>& output,owned::ResourceFailure& error)const override {
  if(offset>size||bytes>size-offset||bytes>SIZE_MAX){error={owned::ResourceFailureCode::archive_bounds,"brush MPR range outside supplied span"};return false;}
  output.assign(data+size_t(offset),data+size_t(offset+bytes));error={};return true;
 }
};
int brush_identity(Session& s,uint64_t generation,uint64_t world,uint64_t serial,probe1b_error* e) {
 if(GetCurrentThreadId()!=s.ownerThread)return fail(e,RPC_E_WRONG_THREAD,"partial updates require the renderer owner thread");
 if(!s.terrain||generation!=s.generation||world!=s.world)return fail(e,HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),"stale world/generation");
 if(!serial||serial<=s.edits.applied_serial)return fail(e,HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),"stale update serial");
 return 0;
}
 #include "brush_layout.inc"
 #include "brush_attribute_jobs.inc"
int32_t EIR_CALL host_terrain_update(void* h,const EirTerrainUpdate* in,EirError* out) noexcept {
 probe1b_error e{};
 if(h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load())return eir_error(out,fail(&e,RPC_E_WRONG_THREAD,"closed/stale handle or wrong renderer owner thread"),e);
 if(!h||!in||in->size!=sizeof(*in)||!in->terrain_name||!in->terrain.data||!in->terrain.bytes||in->terrain.bytes>UINT32_MAX||!(in->flags&7)||in->flags&~15u)
  return eir_error(out,fail(&e,E_INVALIDARG,"invalid terrain update"),e);
 return eir_error(out,guarded(&e,[&]{auto& s=*static_cast<Session*>(h);
  if(const auto rc=brush_identity(s,in->generation,in->world,in->serial,&e))return rc;
  if(s.terrainName!=in->terrain_name||in->x0>=in->x1||in->y0>=in->y1||in->x1>s.terrain->sector_count_x||in->y1>s.terrain->sector_count_y)
   return fail(&e,E_INVALIDARG,"wrong terrain identity or rectangle");
  const auto begin=std::chrono::steady_clock::now();
   owned::BrushStageTimer ingressTimer;
  owned::ResourceFailure error;owned::ResourceBlob blob;
  blob.identity=s.terrain->identity;
  blob.slice=std::make_shared<BrushMprSlice>(in->terrain.data,in->terrain.bytes);
  auto next=std::make_shared<owned::TerrainCpuAsset>();
  bool layout=in->page_count!=0||in->terrain_profile.bytes!=0;
  if(layout||!owned::parse_terrain_rectangle(blob,s.terrainName,*s.terrain,{in->x0,in->y0,in->x1,in->y1},*next,error)){
   if(!owned::parse_terrain_asset(blob,s.terrainName,*next,error))return fail(&e,E_FAIL,error.message.c_str());
   if(next->sector_count_x!=s.terrain->sector_count_x||next->sector_count_y!=s.terrain->sector_count_y||next->maximum_z!=s.terrain->maximum_z)return fail(&e,E_INVALIDARG,"layout transaction changed terrain dimensions");
   layout=true;
   if(!brush_load_pages(s,*next,in->pages,in->page_count,s.terrain.get(),error,in->terrain_profile))return fail(&e,E_INVALIDARG,error.message.c_str());
  }
  if(layout)return brush_layout_update(s,in,std::move(next),begin,&e);
   ingressTimer.mark("ingress_rect_parse");
  // Memory-copy serials identify ingress bytes, not a newly loaded world.
  // The renderer's resource key stays stable; editor_revision changes content.
  next->identity=s.terrain->identity;
  next->editor_defer_shadows=true; // complete shadow/ambient solution arrives asynchronously
  for(size_t i=0;i<next->sectors.size();++i)next->sectors[i].identity=s.terrain->sectors[i].identity;
  next->pages=s.terrain->pages;
  // The classification reads unchanged real pages; no material substitute.
  if(in->flags&6)owned::owned_terrain_classify_swamp_frames(*next);
  else {next->swamp_frame_cover=s.terrain->swamp_frame_cover;next->swamp_frame_texels=s.terrain->swamp_frame_texels;next->swamp_frame_side=s.terrain->swamp_frame_side;}
   ingressTimer.mark("ingress_swamp_classify");
  next->outside_liquid=s.terrain->outside_liquid;next->welded_sides=s.terrain->welded_sides;
  if(s.terrain->liquid_outflow){auto previous=s.terrain->liquid_outflow;
   std::unique_lock<std::mutex> lock(previous->lock,std::try_to_lock);
   if(lock.owns_lock()&&previous->built)next->liquid_outflow=std::move(previous);
   next->procedural_falls=s.terrain->procedural_falls;
  }
  const auto rev=++s.editRevision;
  int gx0=INT_MAX,gy0=INT_MAX,gx1=INT_MIN,gy1=INT_MIN;
  const auto liquid_bounds=(in->flags&4)?brush_liquid_bounds(*s.terrain,*next,in->x0,in->y0,in->x1,in->y1):std::array<int,4>{INT_MAX,INT_MAX,INT_MIN,INT_MIN};
  if(in->flags&1)for(uint32_t y=in->y0;y<in->y1;++y)for(uint32_t x=in->x0;x<in->x1;++x){
   const auto index=size_t(y)*next->sector_count_x+x;const auto& before=s.terrain->sectors[index];const auto& after=next->sectors[index];
   for(size_t v=0;v<std::min(before.vertices.size(),after.vertices.size());++v){const auto& a=before.vertices[v];const auto& b=after.vertices[v];
    if(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.normal_x==b.normal_x&&a.normal_y==b.normal_y&&a.normal_z==b.normal_z)continue;
    const int X=int(x*32+(v%1089)%33),Y=int(y*32+(v%1089)/33);gx0=std::min(gx0,X);gy0=std::min(gy0,Y);gx1=std::max(gx1,X);gy1=std::max(gy1,Y);
   }
  }
  for(uint32_t y=0;y<next->sector_count_y;++y)for(uint32_t x=0;x<next->sector_count_x;++x) {
   const auto index=size_t(y)*next->sector_count_x+x;auto& sector=next->sectors[index];
   // Flow is a global drainage solution; water edits invalidate its consumers
   // across the active map. Height/paint edits invalidate a one-sector halo.
   const bool inside=x>=in->x0&&x<in->x1&&y>=in->y0&&y<in->y1;
   const bool nearHeight=gx0!=INT_MAX&&int(x*32)<=gx1+8&&int((x+1)*32)>=gx0-8&&int(y*32)<=gy1+8&&int((y+1)*32)>=gy0-8;
   const bool affected=inside||nearHeight;
   sector.editor_liquid_revision=s.terrain->sectors[index].editor_liquid_revision;
   const bool nearWater=(in->flags&4)&&(inside||(liquid_bounds[0]!=INT_MAX&&int(x*32)<=liquid_bounds[2]+8&&int((x+1)*32)>=liquid_bounds[0]-8&&int(y*32)<=liquid_bounds[3]+8&&int((y+1)*32)>=liquid_bounds[1]-8));
   if(nearWater||((in->flags&8)&&(in->flags&5)&&sector.has_layers))sector.editor_liquid_revision=rev;
   sector.editor_vertices=s.terrain->sectors[index].editor_vertices;
   sector.editor_shared_vertices=s.terrain->sectors[index].editor_shared_vertices;
   if(affected){
    sector.editor_revision=rev;sector.editor_shared_vertices=true;
    brush_material_revision(*s.terrain,*next,index,in->flags,rev);
    if(in->flags&1){sector.base_slope_normals.clear();sector.base_facet_flatness.clear();sector.editor_vertices=owned::prepare_brush_vertex_streams(sector,rev);}
    else{sector.base_slope_normals=s.terrain->sectors[index].base_slope_normals;sector.base_facet_flatness=s.terrain->sectors[index].base_facet_flatness;}
   }
  }
  const std::array<int,4> attribute_rect{gx0==INT_MAX?0:gx0-8,gy0==INT_MAX?0:gy0-8,gx1+8,gy1+8};
   ingressTimer.mark("ingress_vertex_streams");
   if((in->flags&7)==1&&gx0!=INT_MAX)brush_attribute_jobs(*next,*s.terrain,rev,attribute_rect);
  for(size_t i=0;i<next->sectors.size();++i)if(next->sectors[i].editor_revision==rev){
   auto& attributes=next->sectors[i].editor_attributes;
   if((in->flags&4)&&!(in->flags&1)&&attributes)attributes=owned::update_brush_liquid_attributes(*next,uint32_t(i),rev,attributes);
   else if(!attributes){
    const bool local=(in->flags&7)==1&&gx0!=INT_MAX;
    attributes=owned::prepare_brush_terrain_attributes(*next,uint32_t(i),rev,local?s.terrain->sectors[i].editor_attributes:nullptr,local?&attribute_rect:nullptr);
   }
   if(!next->sectors[i].editor_attributes)return fail(&e,E_FAIL,"base sector material attributes failed");
  }
   ingressTimer.mark("ingress_attributes");
   if(in->flags&1){const auto hr=owned::AtamanReadiness::brushWarmChanged(s.renderer,*next,rev);if(FAILED(hr))return fail(&e,hr,"sculpt vertex publication failed");}
   ingressTimer.mark("ingress_gpu_warm");
  auto map=std::make_shared<owned::OwnedMapSnapshot>(*s.map);map->terrain=next;
  const auto decoded=std::chrono::steady_clock::now();
  auto& scene=OwnedRender::E1Scene().Scene();
  if(!scene.editor_replace_terrain(map)||!OwnedRender::E1UpdateTerrainSnapshot(&s,next))return fail(&e,E_FAIL,"partial source publication rejected");
   ingressTimer.mark("publish_scene");
  // Native placement is MOB relative Z plus the same terrain sampler used at
  // complete load. Reselection of mesh/material is unnecessary for rigid seeds.
  size_t moved=0;
  if(in->flags&5)for(auto& seed:s.editorSeeds){
    // Rectangle decode leaves every other sector and the MP object table
    // unchanged. Only sample seeds touching this rectangle (plus one metre
    // for boundary ownership), instead of querying all map objects twice.
    if(seed.unit)continue; // UNIT support is authoritative CPU ingress, including bridges.
     const auto x=seed.pose.position[0],y=seed.pose.position[1];
    if(x<float(in->x0*32)-1||x>float(in->x1*32)+1||y<float(in->y0*32)-1||y>float(in->y1*32)+1)continue;
   float oldZ=0,newZ=0;
   if(!owned::sample_terrain_height(*s.terrain,seed.pose.position[0],seed.pose.position[1],oldZ)||!owned::sample_terrain_height(*next,seed.pose.position[0],seed.pose.position[1],newZ))continue;
   if(oldZ==newZ)continue;
   auto pose=seed.pose;pose.position[2]=newZ+seed.relativeZ;
   if(!scene.editor_seed_pose(seed.rendererId,pose,true))return fail(&e,E_FAIL,"terrain object reprojection rejected");
   seed.pose=pose;++moved;
  }
   ingressTimer.mark("publish_reproject");
  if(const auto hr=owned::AtamanReadiness::brushResources(s.renderer,next,map,in->flags&~8u,in->x0,in->y0,in->x1,in->y1);FAILED(hr))return fail(&e,hr,"partial terrain resource update rejected");
   ingressTimer.mark("publish_local_resources");
  s.terrain=next;s.baseline=next;s.map=map;
  // The optional full-document equality optimisation is no longer current.
  // Explicit future full publication must not compare against stale bytes.
  s.terrainBytes.clear();
  s.edits.decode_ms=std::chrono::duration<double,std::milli>(decoded-begin).count();
  s.edits.publish_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-decoded).count();
  s.editStart=begin;s.edits.applied_serial=in->serial;++s.edits.updates;
  s.brushWorker.cancel();s.finalizePublishedSerial=0;s.edits.derived_pending=0;
  if(in->flags&8){
   auto task=std::make_shared<BrushFinalizeTask>();task->generation=s.generation;task->world=s.world;task->serial=in->serial;
   task->flags=in->flags;task->x0=in->x0;task->y0=in->y0;task->x1=in->x1;task->y1=in->y1;task->start=begin;
   task->terrain=std::make_shared<owned::TerrainCpuAsset>(*next);task->map=map;
   task->geometry=owned::AtamanReadiness::brushGeometry(s.renderer,*next);task->families=owned::AtamanReadiness::brushFamilies(s.renderer);
   task->edges=owned::AtamanReadiness::brushEdges(s.renderer);task->own_edges=owned::AtamanReadiness::brushOwnEdges(s.renderer);
    task->surf_sea=owned::AtamanReadiness::brushSurfSea(s.renderer);
   s.edits.derived_pending=1;s.finalizeStart=begin;s.brushWorker.post(std::move(task));
  }
  std::fprintf(stderr,"TERRAIN_UPDATE world=%llu generation=%llu serial=%llu flags=%u rect=%u,%u,%u,%u decode_ms=%.3f publish_ms=%.3f reprojected=%zu\n",s.world,s.generation,in->serial,in->flags,in->x0,in->y0,in->x1,in->y1,s.edits.decode_ms,s.edits.publish_ms,moved);
  return 0;
 }),e);
}
int32_t EIR_CALL host_poses(void* h,const EirPoseUpdate* in,EirError* out) noexcept {
 probe1b_error e{};
 if(h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load())return eir_error(out,fail(&e,RPC_E_WRONG_THREAD,"closed/stale handle or wrong renderer owner thread"),e);
 if(!h||!in||in->size!=sizeof(*in)||in->count>1000000||(in->count&&!in->poses))return eir_error(out,fail(&e,E_INVALIDARG,"invalid pose update"),e);
 return eir_error(out,guarded(&e,[&]{auto& s=*static_cast<Session*>(h);
  if(const auto rc=brush_identity(s,in->generation,in->world,in->serial,&e))return rc;
  auto& scene=OwnedRender::E1Scene().Scene();
  for(uint32_t i=0;i<in->count;++i){const auto& p=in->poses[i];
   auto seed=std::find_if(s.editorSeeds.begin(),s.editorSeeds.end(),[&](const auto& x){return x.mob==p.mob&&x.nid==p.nid;});
   if(seed==s.editorSeeds.end()||!p.visible)return fail(&e,E_INVALIDARG,"pose identity/visibility requires complete membership update");
   for(float v:p.position)if(!std::isfinite(v))return fail(&e,E_INVALIDARG,"nonfinite pose");
   for(float v:p.quaternion)if(!std::isfinite(v))return fail(&e,E_INVALIDARG,"nonfinite quaternion");
   for(float v:p.shape)if(!std::isfinite(v))return fail(&e,E_INVALIDARG,"nonfinite shape");
  }
  s.editStart=std::chrono::steady_clock::now();
  auto next=std::make_shared<owned::TerrainCpuAsset>(*s.terrain);next->editor_defer_shadows=true;
  auto map=std::make_shared<owned::OwnedMapSnapshot>(*s.map);map->terrain=next;
  int x0=int(next->sector_count_x)-1,y0=int(next->sector_count_y)-1,x1=0,y1=0;
  const auto include=[&](float x,float y){const int sx=std::clamp(int(std::floor(x/32)),0,int(next->sector_count_x)-1),sy=std::clamp(int(std::floor(y/32)),0,int(next->sector_count_y)-1);x0=std::min(x0,sx);y0=std::min(y0,sy);x1=std::max(x1,sx+1);y1=std::max(y1,sy+1);};
  for(uint32_t i=0;i<in->count;++i){const auto& p=in->poses[i];auto& seed=*std::find_if(s.editorSeeds.begin(),s.editorSeeds.end(),[&](const auto& x){return x.mob==p.mob&&x.nid==p.nid;});
   auto pose=seed.pose;std::copy_n(p.position,3,pose.position.begin());std::copy_n(p.quaternion,4,pose.quaternion.begin());std::copy_n(p.shape,3,pose.shape.begin());
   include(seed.pose.position[0],seed.pose.position[1]);include(pose.position[0],pose.position[1]);
   if(!(seed.completed?scene.visual_editor_pose(seed.rendererId,{},pose,seed.unit):scene.editor_seed_pose(seed.rendererId,pose)))return fail(&e,E_FAIL,"pose publication rejected");seed.pose=pose;
   float ground=0;owned::sample_terrain_height(*next,pose.position[0],pose.position[1],ground);seed.relativeZ=pose.position[2]-ground;
   auto object=std::find_if(map->objects.begin(),map->objects.end(),[&](const auto& value){return value.nid==uint32_t(seed.rendererId);});
   if(object==map->objects.end())return fail(&e,E_FAIL,"pose CPU source identity missing");
   object->position=pose.position;object->position[2]=seed.relativeZ;object->quaternion=pose.quaternion;object->shape=pose.shape;
   if(GetEnvironmentVariableW(L"ATAMAN_BRUSH_POSE_TRACE",nullptr,0)){
    std::shared_ptr<owned::RetainedStaticSourceCandidate> candidate;owned::RetainedStaticPrepareDiagnostic diagnostic;
    if(owned::prepare_retained_static_candidate(map,next,*object,candidate,&diagnostic)&&candidate)
     std::fprintf(stderr,"POSE_SOURCE nid=%u renderer=%d world=%.9g,%.9g,%.9g relative=%.9g candidate=%.9g,%.9g,%.9g quat=%.9g,%.9g,%.9g,%.9g shape=%.9g,%.9g,%.9g assembly=%llu\n",seed.nid,seed.rendererId,pose.position[0],pose.position[1],pose.position[2],seed.relativeZ,candidate->world_position[0],candidate->world_position[1],candidate->world_position[2],pose.quaternion[0],pose.quaternion[1],pose.quaternion[2],pose.quaternion[3],pose.shape[0],pose.shape[1],pose.shape[2],seed.assembly->revision);
   }
  }
  if(!scene.editor_replace_terrain(map)||!OwnedRender::E1UpdateTerrainSnapshot(&s,next))return fail(&e,E_FAIL,"pose CPU map publication rejected");
  if(in->count&&FAILED(owned::AtamanReadiness::brushResources(s.renderer,next,map,0,uint32_t(x0),uint32_t(y0),uint32_t(x1),uint32_t(y1))))return fail(&e,E_FAIL,"pose local ambient publication rejected");
  s.terrain=next;s.baseline=next;s.map=map;s.terrainBytes.clear();
  s.brushWorker.cancel();s.finalizePublishedSerial=0;s.edits.derived_pending=1;s.finalizeStart=s.editStart;
  auto task=std::make_shared<BrushFinalizeTask>();task->generation=s.generation;task->world=s.world;task->serial=in->serial;task->flags=8;
  task->x0=uint32_t(x0);task->y0=uint32_t(y0);task->x1=uint32_t(x1);task->y1=uint32_t(y1);task->start=s.editStart;
  task->terrain=std::make_shared<owned::TerrainCpuAsset>(*next);task->map=map;
  task->geometry=owned::AtamanReadiness::brushGeometry(s.renderer,*next);task->families=owned::AtamanReadiness::brushFamilies(s.renderer);
  task->edges=owned::AtamanReadiness::brushEdges(s.renderer);task->own_edges=owned::AtamanReadiness::brushOwnEdges(s.renderer);task->surf_sea=owned::AtamanReadiness::brushSurfSea(s.renderer);
  s.brushWorker.post(std::move(task));s.edits.applied_serial=in->serial;++s.edits.updates;return 0;
 }),e);
}
int32_t EIR_CALL host_update_state(void* h,EirUpdateState* out,EirError* error) noexcept {
 if(!h||!out||h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load())return -1;auto& s=*static_cast<Session*>(h);
 Microsoft::WRL::ComPtr<ID3D11InfoQueue> queue;
 if(SUCCEEDED(s.device.As(&queue))){s.edits.debug_available=1;
  const auto count=queue->GetNumStoredMessagesAllowedByRetrievalFilter();
  for(uint64_t i=0;i<count;++i){SIZE_T bytes=0;queue->GetMessage(i,nullptr,&bytes);std::vector<uint8_t> data(bytes);auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data());
   if(SUCCEEDED(queue->GetMessage(i,message,&bytes))){if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){++s.edits.debug_errors;std::fprintf(stderr,"D3D_ERROR %u %s\n",message->ID,message->pDescription);}else if(message->Severity==D3D11_MESSAGE_SEVERITY_WARNING)++s.edits.debug_warnings;}}
  queue->ClearStoredMessages();
 }
 *out=s.edits;out->world=s.world;
 out->device_identity=reinterpret_cast<uint64_t>(s.device.Get());out->renderer_identity=reinterpret_cast<uint64_t>(&s.renderer);return 0;
}
