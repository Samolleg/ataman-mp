namespace {
int eir_error(EirError* out,int rc,const probe1b_error& in) noexcept {
 if(out){out->code=rc;std::snprintf(out->message,sizeof(out->message),"%s",in.detail);}return rc;
}
int32_t EIR_CALL host_create(const EirCreate* in,void** result,EirError* out) noexcept {
 if(result)*result=nullptr;
 probe1b_error e{};
 if(!in||!result||in->abi!=EIR_ABI||in->size!=sizeof(*in)||!in->window||!in->scratch_path||!in->game_data_path||!in->neighbour_layout_path||in->mount_count>4096||!in->mounts||(in->preview_flags&~EIR_PREVIEW_STATIC_FOOTPRINTS))
  return eir_error(out,fail(&e,E_INVALIDARG,"invalid renderer C ABI create"),e);
 return eir_error(out,guarded(&e,[&]{
  std::vector<std::wstring> roots,archives;std::vector<uint8_t> kinds;
  for(uint32_t i=0;i<in->mount_count;++i){if(!in->mounts[i].path)return fail(&e,E_INVALIDARG,"null mount");auto path=std::wstring(reinterpret_cast<const wchar_t*>(in->mounts[i].path));
   if(in->mounts[i].kind==3)roots.push_back(std::move(path));else{archives.push_back(std::move(path));kinds.push_back(in->mounts[i].kind?uint8_t(in->mounts[i].kind):uint8_t(owned::kArchiveAllKinds));}}
  // Pinned GI obtains Config/lights*.ini from the parent of the last Maps
  // root. This empty private root supplies that unchanged config context;
  // it contains no maps and cannot shadow a real resource.
  roots.push_back(std::wstring(reinterpret_cast<const wchar_t*>(in->game_data_path))+L"/Maps");
  std::vector<const wchar_t*> rootPointers,archivePointers;for(const auto& p:roots)rootPointers.push_back(p.c_str());for(const auto& p:archives)archivePointers.push_back(p.c_str());
  const auto scratch=std::wstring(reinterpret_cast<const wchar_t*>(in->scratch_path));const auto ini=scratch+L"/renderer.ini",layout=std::wstring(reinterpret_cast<const wchar_t*>(in->neighbour_layout_path));
  probe1b_create_info info{sizeof(info),PROBE1B_ABI,in->width,in->height,static_cast<HWND>(in->window),ini.c_str(),layout.c_str(),rootPointers.data(),uint32_t(rootPointers.size()),archivePointers.data(),uint32_t(archivePointers.size()),kinds.data(),in->test_failure};
  const int rc=probe1b_create(&info,result,&e);if(rc){owned::embedded_host_configuration().reset();return rc;}
  auto& session=*static_cast<Session*>(*result);session.canPresent=in->can_present;session.liveValues=in->live_values;session.presentContext=in->present_context;session.game.directory=std::wstring(reinterpret_cast<const wchar_t*>(in->game_data_path))+L"/Config";session.game.settings=scratch+L"/settings.ini";
  session.editorEmptyFootprints=in->abi>=12u&&(in->preview_flags&EIR_PREVIEW_STATIC_FOOTPRINTS);
  return 0;
 }),e);
}
int32_t EIR_CALL host_document(void* handle,const EirDocument* in,EirError* out) noexcept {
 probe1b_error e{};
 if(!handle||!in||in->size!=sizeof(*in)||!in->generation||in->terrain.bytes>UINT32_MAX||(in->terrain.bytes&&!in->terrain.data)||in->mob_count>1024||(in->mob_count&&!in->mobs)||!in->terrain_name||in->object_count>10000000||(in->object_count&&!in->objects))
  return eir_error(out,fail(&e,E_INVALIDARG,"invalid memory document"),e);
 return eir_error(out,guarded(&e,[&]{
  auto& s=*static_cast<Session*>(handle);s.trace.phase("document_begin");++s.documentUpdates;
  s.brushWorker.cancel();s.finalizePublishedSerial=0;s.edits.derived_pending=0;s.edits.finalized_serial=0;s.verticesWarmed=false;
  const auto* terrainBegin=static_cast<const uint8_t*>(in->terrain.data);
  s.holdObjectRefresh=s.terrain && s.terrainName==in->terrain_name && s.terrainBytes.size()==in->terrain.bytes && std::equal(s.terrainBytes.begin(),s.terrainBytes.end(),terrainBegin);
  if(in->terrain.bytes){s.terrainName=in->terrain_name;s.terrainBytes.assign(terrainBegin,terrainBegin+in->terrain.bytes);}else{s.terrainName.clear();s.terrainBytes.clear();}

  owned::neighbour_client_stop();
  OwnedRender::RetireTerrainPlacement(&s);
  HRESULT hr=s.renderer.retire_embedded_world();if(FAILED(hr))return fail(&e,hr,"retire previous world");
  for(const auto id:s.atlasIds)if(id)s.renderer.retire_surface(id);
  OwnedRender::E1Scene().Retire(&s,s.world);OwnedRender::E1RetireTerrain(&s);OwnedRender::E1RetireWorld(&s);owned::embedded_sky().detach(&s);
  // This host supplies a complete immutable document, not the game's same-name
  // MOB reuse transition. Retire source publications as well as positive IDs.
  OwnedRender::E1Scene().Scene().reset();
  s.terrain.reset();s.baseline.reset();s.map.reset();s.atlases.clear();s.atlasIds.clear();s.readyGeneration=0;s.completedReadyEpoch=~uint64_t(0);s.materialsPublished=false;s.generation=in->generation;s.world=++nextWorld;s.edits.applied_serial=s.edits.presented_serial=0;s.traceFrameStarted=s.traceNeighboursSettled=s.traceResourcesSettled=false;
  s.editorSeeds.clear();s.poseAuditDone=false;
  if(!in->terrain.bytes)return 0;
  if(s.world>1) {
   // A new source must not inherit stale material/terrain GPU registrations.
   // Recreate only the embedded renderer on the same host device. Keep the
   // last accepted display target until its replacement is fully ready.
   s.renderer.release();
   hr=s.renderer.adopt_host_device(s.device.Get(),s.context.Get(),s.width,s.height);
   if(FAILED(hr))return fail(&e,hr,"readopt host device for confirmed document");
   if(!owned::AtamanReadiness::load(s.renderer,s.rendererIni.c_str()))return fail(&e,E_FAIL,"reload isolated renderer configuration");
   // The immutable replacement must start with the host's current visibility,
   // before any world materials are registered. INI is not the editor toggle.
   owned::scene_core::Config visibility;owned::scene_core::ConfigOption option;
   option.key="neighbour_maps";option.type=uint8_t(owned::RendererOptionType::boolean);
   option.boolean_value=s.frameCamera.neighbours_visible&&s.neighboursConfigured;
   visibility.options.push_back(option);visibility.run(s.renderer);
   hr=s.renderer.ensure_host(s.hidden.value,s.width,s.height);
   if(FAILED(hr))return fail(&e,hr,"restart embedded renderer for confirmed document");
   s.neighboursRequested=owned::AtamanReadiness::neighboursEnabled(s.renderer);
   owned::AtamanReadiness::quality(s.renderer);
  }
  owned::ResourceFailure error;owned::ResourceBlob blob;
  if(!s.store.open(s.mounts,error)||!s.store.copy_selected_memory(in->terrain_name,in->terrain.data,uint32_t(in->terrain.bytes),blob,error))return fail(&e,E_FAIL,error.message.c_str());
  auto terrain=std::make_shared<owned::TerrainCpuAsset>();if(!owned::parse_terrain_asset(blob,in->terrain_name,*terrain,error))return fail(&e,E_FAIL,error.message.c_str());
  // parse_terrain_asset supplies geometry and records, not the source pages.
  // Complete the same retained page inputs as OwnedResourceLoader before
  // classifying swamp frames; GPU atlas upload alone cannot supply their art.
  s.trace.phase("terrain_page_bytes_begin");
  auto pages=std::make_shared<owned::ResourceStore>();if(!pages->open(s.mounts,error))return fail(&e,E_FAIL,error.message.c_str());
  terrain->pages.resize(terrain->texture_resources.size());size_t pageCount=0;
  if(!brush_load_pages(s,*terrain,in->pages,in->page_count,nullptr,error,in->terrain_profile))return fail(&e,E_INVALIDARG,error.message.c_str());
   for(const auto& page:terrain->pages)if(page.texture)++pageCount;
  for(auto& sector:terrain->sectors){sector.editor_vertices=owned::prepare_brush_vertex_streams(sector,0);sector.editor_shared_vertices=true;sector.editor_material_revisions.assign(sector.materials.size()*4,0);}
  for(uint32_t i=0;i<terrain->sectors.size();++i){terrain->sectors[i].editor_attributes=owned::prepare_brush_terrain_attributes(*terrain,i,0);if(!terrain->sectors[i].editor_attributes)return fail(&e,E_FAIL,"prepare base terrain attributes");}
  const auto swamp=owned::owned_terrain_classify_swamp_frames(*terrain);
  std::fprintf(stderr,"TERRAIN_SOURCE pages=%zu/%zu swamp_side=%u %s\n",pageCount,terrain->pages.size(),terrain->swamp_frame_side,swamp.c_str());s.terrain=terrain;s.baseline=terrain;
  if(!OwnedRender::E1PublishTerrainSnapshot(&s,s.world,s.generation,terrain))return fail(&e,E_FAIL,"publish in-memory terrain");
  std::string why;if(!upload_atlases(s,why))return fail(&e,E_FAIL,why.c_str());
  s.trace.phase("terrain_page_bytes_end");s.trace.phase("mob_resource_snapshots_begin");
  auto combined=std::make_shared<owned::OwnedMapSnapshot>();combined->generation=s.world;combined->terrain=terrain;combined->terrain_resource=in->terrain_name;
  auto terrainBytes=std::make_shared<std::vector<uint8_t>>(static_cast<const uint8_t*>(in->terrain.data),static_cast<const uint8_t*>(in->terrain.data)+in->terrain.bytes);
  std::vector<std::pair<uint32_t,uint32_t>> identities;
  std::unordered_set<uint64_t> hidden;
  for(uint32_t i=0;i<in->object_count;++i)if(!in->objects[i].visible)hidden.insert((uint64_t(in->objects[i].mob)<<32)|in->objects[i].nid);
  for(uint32_t i=0;i<in->mob_count;++i){
   const auto span=in->mobs[i];if(!span.data||!span.bytes||span.bytes>UINT32_MAX)return fail(&e,E_INVALIDARG,"invalid memory MOB span");
   owned::OwnedResourceLoader loader;owned::OwnedResourceRequestToken token;owned::OwnedMapLoadRequest request;
   request.generation=s.world;request.map_resource="ataman-memory-"+std::to_string(i)+".mob";request.terrain_resource=in->terrain_name;request.mounts=s.mounts;request.backdrop_objects=true;
   request.memory_map=std::make_shared<std::vector<uint8_t>>(static_cast<const uint8_t*>(span.data),static_cast<const uint8_t*>(span.data)+span.bytes);request.memory_terrain=terrainBytes;
   std::shared_ptr<const owned::OwnedMapSnapshot> loaded;
   if(!loader.start(4,error)||!loader.request(request,error,&token)||loader.wait_publish(token,loaded)!=owned::OwnedResourceWaitResult::ready||!loaded)return fail(&e,E_FAIL,error.message.c_str());
   loader.shutdown();
   for(auto object:loaded->objects){if(hidden.count((uint64_t(i)<<32)|object.nid))continue;identities.push_back({i,object.nid});object.nid=uint32_t(combined->objects.size()+1);combined->objects.push_back(std::move(object));}
   combined->models.insert(loaded->models.begin(),loaded->models.end());combined->textures.insert(loaded->textures.begin(),loaded->textures.end());combined->failures.insert(combined->failures.end(),loaded->failures.begin(),loaded->failures.end());
  }
  s.map=combined;s.trace.phase("mob_resource_snapshots_end");s.trace.phase("scene_visual_seed_begin");
  OwnedRender::PublishTerrainPlacement(&s,identity());
  auto& membership=OwnedRender::E1Scene();membership.Begin(&s,s.world);auto& scene=membership.Scene();auto transaction=scene.begin_source(owned::MobSourceRole::main,in->terrain_name,"ataman-memory.mob");scene.record_source(transaction,"ataman-memory.mob");
  for(const auto& object:combined->objects)scene.record_admission(transaction,object.nid,object.family,int32_t(object.nid));
  if(!scene.finish_source(transaction,true,true,why)||!scene.publish_source(transaction,combined))return fail(&e,E_FAIL,"in-memory visual scene publication rejected");
  size_t seeds=0,omitted=0;
  for(const auto& object:combined->objects){
   if(object.rejection==owned::StaticObjectRejection::malformed||object.rejection==owned::StaticObjectRejection::duplicate_nid||object.rejection==owned::StaticObjectRejection::hierarchical||object.visual_reference.empty()||!combined->models.count(object.visual_reference)){++omitted;continue;}
   std::shared_ptr<owned::RetainedStaticSourceCandidate> candidate;owned::RetainedStaticPrepareDiagnostic diag;
   if(!owned::prepare_retained_static_candidate(combined,terrain,object,candidate,&diag)||!candidate||!candidate->assembly){++omitted;continue;}
   // Generic retained assets carry texture identity but no material-class key.
   // Publish that real texture name so the renderer can build its world profile.
   auto assembly=std::make_shared<owned::VisualAssemblyAsset>(*candidate->assembly);
   for(auto& part:assembly->parts)if(part.material_name.empty())part.material_name=part.texture_source_name;
   assembly->revision^=0x4154414d41544c32ull;if(!assembly->revision)assembly->revision=1;candidate->assembly=std::move(assembly);
   owned::VisualObjectPose pose;pose.position=candidate->world_position;pose.quaternion=object.quaternion;pose.shape=object.shape;pose.source_generation=s.world;pose.appearance_revision=candidate->assembly->revision;
   if(scene.visual_seed(int32_t(object.nid),candidate->assembly,pose)) {
       ++seeds;const auto identity=identities.at(object.nid-1);
       s.editorSeeds.push_back({identity.first,identity.second,int32_t(object.nid),candidate->assembly,pose,true,object.position[2]});
       if(GetEnvironmentVariableW(L"ATAMAN_BRUSH_POSE_TRACE",nullptr,0)&&identity.second==104)
        std::fprintf(stderr,"POSE_FULL nid=%u renderer=%u world=%.9g,%.9g,%.9g relative=%.9g quat=%.9g,%.9g,%.9g,%.9g shape=%.9g,%.9g,%.9g assembly=%llu\n",identity.second,object.nid,pose.position[0],pose.position[1],pose.position[2],object.position[2],pose.quaternion[0],pose.quaternion[1],pose.quaternion[2],pose.quaternion[3],pose.shape[0],pose.shape[1],pose.shape[2],candidate->assembly->revision);
   } else ++omitted;
  }
  std::fprintf(stderr,"Ataman document generation=%llu world=%llu memory_mpr=%llu memory_mobs=%u objects=%zu seeds=%zu omitted=%zu\n",(unsigned long long)s.generation,(unsigned long long)s.world,(unsigned long long)in->terrain.bytes,in->mob_count,combined->objects.size(),seeds,omitted);
  s.trace.phase("scene_visual_seed_end");
  s.water=s.environment=s.objects=true;
  // Content feature bits must not override the file-backed renderer settings.
  const int result=probe1b_set_features(handle,7,&e);if(!result)owned::AtamanReadiness::quality(s.renderer);s.trace.phase("document_end");return result;
 }),e);
}
int32_t EIR_CALL host_camera(void* h,const EirCamera* camera,EirError* out) noexcept {
 probe1b_error e{};if(!h||!camera)return eir_error(out,fail(&e,E_INVALIDARG,"camera arguments"),e);
 return eir_error(out,guarded(&e,[&]{auto& s=*static_cast<Session*>(h);
  for(float n:camera->world_to_clip)if(!std::isfinite(n))return fail(&e,E_INVALIDARG,"nonfinite camera");
  if(camera->point_light_count>EIR_MAX_POINT_LIGHTS)return fail(&e,E_INVALIDARG,"point light count");
  if(!std::isfinite(camera->hour))return fail(&e,E_INVALIDARG,"nonfinite hour");
  const bool requested=camera->neighbours_visible!=0;
  if(requested!=s.neighboursRequested){
   s.neighboursRequested=requested;owned::scene_core::Config config;owned::scene_core::ConfigOption option;
   option.key="neighbour_maps";option.type=uint8_t(owned::RendererOptionType::boolean);option.boolean_value=requested&&s.neighboursConfigured;
   config.options.push_back(option);config.run(s.renderer);
  }
  std::copy(camera->world_to_clip,camera->world_to_clip+16,s.clip.begin());s.explicitClip=true;s.frameCamera=*camera;
  std::copy(camera->eye,camera->eye+3,s.camera.eye);std::copy(camera->target,camera->target+3,s.camera.target);s.hour=camera->hour;return 0;
 }),e);
}
int32_t EIR_CALL host_resize(void* h,uint32_t width,uint32_t height,EirError* out) noexcept {
 probe1b_error e{};if(!h||!width||!height||width>16384||height>16384)return eir_error(out,fail(&e,E_INVALIDARG,"resize arguments"),e);
 return eir_error(out,guarded(&e,[&]{auto& s=*static_cast<Session*>(h);s.readyGeneration=0;
  HRESULT hr=s.renderer.resize(width,height);
  if(FAILED(hr))return fail(&e,hr,"Rebuild64 resize/device failure");s.width=width;s.height=height;return 0;
 }),e);
}
int32_t EIR_CALL host_frame(void* h,EirStatistics* stats,EirError* out) noexcept {
 probe1b_error e{};if(!h||!stats)return eir_error(out,fail(&e,E_INVALIDARG,"frame arguments"),e);
 return eir_error(out,guarded(&e,[&]{auto& s=*static_cast<Session*>(h);*stats={};
  HRESULT hr=s.loseDevice?DXGI_ERROR_DEVICE_REMOVED:s.device->GetDeviceRemovedReason();if(FAILED(hr))return fail(&e,hr,"Rebuild64 D3D11 device lost; unsaved documents remain in OpenGL");
  if(!s.terrain)return 1;
   if(!s.traceFrameStarted){s.trace.phase("native_frame_work_begin");s.traceFrameStarted=true;}
   const auto start=std::chrono::steady_clock::now();
   if(const auto rc=brush_finalize_poll(s,&e))return rc;
   const int rc=map_frame(s,&e);trace_brush_frame(s,rc);if(rc)return rc;
   brush_finalize_frame_done(s);
   const unsigned expectedNeighbours=!s.neighboursRequested?0:_stricmp(s.terrainName.c_str(),"zone1.mpr")==0?5:_stricmp(s.terrainName.c_str(),"zone7.mpr")==0?4:0;
   const bool neighboursReady=owned::neighbour_client_settled(s.world)&&owned::AtamanReadiness::neighbours(s.renderer)==expectedNeighbours;
   if(!s.traceNeighboursSettled&&neighboursReady){s.trace.phase("neighbour_client_settled");s.traceNeighboursSettled=true;}
   if(!s.traceResourcesSettled&&neighboursReady&&!owned::AtamanReadiness::pending(s.renderer)&&s.completedReadyEpoch==owned::AtamanReadiness::epoch(s.renderer)){s.trace.phase("native_material_table_ready_epoch");s.trace.phase("native_resources_pending_clear");s.traceResourcesSettled=true;}

   if(!s.verticesWarmed&&!s.pending&&owned::neighbour_client_settled(s.world)){
    const auto hr=owned::AtamanReadiness::brushWarmVertices(s.renderer,*s.terrain);if(FAILED(hr))return fail(&e,hr,"base terrain vertex cache prewarm failed");s.verticesWarmed=true;
   }
  probe1b_statistics memory{};probe1b_get_statistics(h,&memory,&e);
  stats->service_cpu_ms=s.acceptedServiceCpuMs;stats->service_composite_ms=s.acceptedCompositeMs;stats->service_serial=s.acceptedServiceSerial;stats->document_updates=s.documentUpdates;
  stats->generation=s.generation;stats->ready_generation=s.readyGeneration;stats->private_bytes=memory.private_bytes;stats->video_bytes=memory.video_local;stats->width=s.width;stats->height=s.height;stats->pending=s.pending;
  stats->neighbours_pending=owned::neighbour_client_settled(s.world)?0u:1u;
  stats->neighbours_accepted=owned::AtamanReadiness::neighbours(s.renderer);
  const auto neighbourError=owned::neighbour_client_error(s.world);
  if(neighbourError!=s.neighbourError){s.neighbourError=neighbourError;if(!neighbourError.empty())std::fprintf(stderr,"NEIGHBOUR_FAILURE epoch=%llu %s\n",(unsigned long long)s.world,neighbourError.c_str());}
  stats->frame_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return s.readyGeneration==s.generation?0:1;
 }),e);
}
int32_t EIR_CALL host_capture(void* h,const uint16_t* path,EirError* out) noexcept {
 probe1b_error e{};if(!h||!path)return eir_error(out,fail(&e,E_INVALIDARG,"capture arguments"),e);
 return eir_error(out,guarded(&e,[&]{auto& s=*static_cast<Session*>(h);if(!s.accepted)return fail(&e,E_PENDING,"no agreed frame");
  const auto hr=read_output(&s,s.context.Get(),s.accepted.Get());if(FAILED(hr))return fail(&e,hr,"accepted target readback failed");
  return probe1b_capture_png(h,reinterpret_cast<const wchar_t*>(path),&e);
 }),e);
}
int32_t EIR_CALL host_service(void* handle,const EirService* in,EirError* out) noexcept {
 probe1b_error e{};
 if(!handle||!in||in->size!=sizeof(*in)||in->width>16384||in->height>16384||in->stride<uint64_t(in->width)*4||in->rgba.bytes!=uint64_t(in->stride)*in->height||(in->rgba.bytes&&!in->rgba.data)||in->pose_count>1000000||(in->pose_count&&!in->poses))return eir_error(out,fail(&e,E_INVALIDARG,"invalid editor service span"),e);
 return eir_error(out,guarded(&e,[&]{auto& s=*static_cast<Session*>(handle);
  if(in->generation!=s.generation)return 0;
  std::unordered_map<uint64_t,const EirObjectPose*> poses;
  for(uint32_t i=0;i<in->pose_count;++i){const auto& p=in->poses[i];poses.emplace((uint64_t(p.mob)<<32)|p.nid,&p);}
  size_t compared=0,differing=0;double maxPosition=0,maxShape=0,maxRotation=0;
  if(!s.poseAuditDone)for(const auto& seed:s.editorSeeds) {
   const auto found=poses.find((uint64_t(seed.mob)<<32)|seed.nid);
   if(found==poses.end())continue;
   const auto& p=*found->second;
   auto pose=seed.pose;
   if(!s.poseAuditDone&&!p.changed) {
    ++compared;double position=0,shape=0,rotation=0;for(int a=0;a<3;++a){position=std::max(position,std::abs(double(p.position[a])-pose.position[a]));shape=std::max(shape,std::abs(double(p.shape[a])-pose.shape[a]));}for(int a=0;a<4;++a)rotation=std::max(rotation,std::abs(double(p.quaternion[a])-pose.quaternion[a]));
    if(position>.001||shape>.001||rotation>.001)++differing;maxPosition=std::max(maxPosition,position);maxShape=std::max(maxShape,shape);maxRotation=std::max(maxRotation,rotation);
   }

  }
  if(!s.poseAuditDone&&compared){std::fprintf(stderr,"POSE_AUDIT map=%s compared=%zu differing=%zu max_position_m=%.9f max_shape=%.9f max_quaternion=%.9f (unedited native poses retained)\n",s.map?s.map->terrain_resource.c_str():"",compared,differing,maxPosition,maxShape,maxRotation);s.poseAuditDone=true;}
  auto& service=s.service;
  if(in->image_update&&(service.serial!=in->serial||service.generation!=in->generation)) {std::copy_n(in->world_to_clip,16,service.clip);service.cpuMs=in->cpu_ms;service.serial=in->serial;service.generation=in->generation;service.width=in->width;service.height=in->height;service.stride=in->stride;
   const auto* begin=static_cast<const uint8_t*>(in->rgba.data);if(in->rgba.bytes)service.pixels.assign(begin,begin+in->rgba.bytes);else service.pixels.clear();}
  return 0;
 }),e);
}
int32_t EIR_CALL host_capture_world(void* h,const uint16_t* path,EirError* out) noexcept {
 probe1b_error e{};if(!h||!path)return eir_error(out,fail(&e,E_INVALIDARG,"world capture arguments"),e);
 return eir_error(out,guarded(&e,[&]{auto& s=*static_cast<Session*>(h);if(!s.rawAccepted)return fail(&e,E_PENDING,"no accepted renderer world output");
  const auto hr=read_output(&s,s.context.Get(),s.rawAccepted.Get());if(FAILED(hr))return fail(&e,hr,"renderer world readback failed");
  return probe1b_capture_png(h,reinterpret_cast<const wchar_t*>(path),&e);
 }),e);
}
#include "offscreen_output.inc"
#include "editor_query.inc"
#include "brush_api.inc"
void EIR_CALL host_device_loss(void* h){if(h)static_cast<Session*>(h)->loseDevice=true;}
}
extern "C" __declspec(dllexport) int32_t EIR_CALL ataman_renderer_get_api(uint32_t abi,EirApi* out) noexcept {
 if(abi!=EIR_ABI||!out||out->size!=sizeof(*out))return -1;
 *out={sizeof(EirApi),abi,&host_create,&probe1b_destroy,&host_document,&host_camera,&host_resize,&host_frame,&host_capture,&host_device_loss,&host_service,&host_capture_world,&host_output,&host_query,&host_depth,&host_terrain_update,&host_poses,&host_update_state};return 0;
}
extern "C" __declspec(dllexport) int32_t EIR_CALL ataman_brush_debug(void* h,char* out,uint32_t capacity) noexcept {
 if(!h||h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load()||!out||!capacity)return -1;
 auto& s=*static_cast<Session*>(h);owned::AtamanReadiness::brushDebug(s.renderer,out,capacity);return 0;
}

extern "C" __declspec(dllexport) int32_t EIR_CALL ataman_brush_resource_counts(void* h,uint64_t* out,uint32_t count) noexcept {
 if(!h||h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load()||!out||count<13)return -1;
 auto& s=*static_cast<Session*>(h);if(!BrushD3DCounts::enabled())return -2;BrushD3DCounts::snapshot(out);return 0;
}

extern "C" __declspec(dllexport) int32_t EIR_CALL ataman_brush_frame_facts(void* h,double* out,uint32_t count) noexcept {
 if(!h||h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load()||!out||count<12)return -1;
 try{return owned::AtamanReadiness::frameFacts(static_cast<Session*>(h)->renderer,out);}catch(...){return E_FAIL;}
}
extern "C" __declspec(dllexport) int32_t EIR_CALL ataman_brush_cover_facts(void* h,uint64_t* out,uint32_t count,char* reason,uint32_t capacity) noexcept {
 if(!h||h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load()||!out||count<10||!reason||!capacity)return -1;
 owned::AtamanReadiness::coverFacts(static_cast<Session*>(h)->renderer,out,reason,capacity);return 0;
}
extern "C" __declspec(dllexport) int32_t EIR_CALL ataman_brush_terrain_facts(void* h,uint64_t* out,uint32_t count) noexcept {
 if(!h||h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load()||!out)return -1;
 auto& s=*static_cast<Session*>(h);if(!s.terrain)return -1;
 try{return owned::AtamanReadiness::terrainFacts(s.renderer,*s.terrain,out,count);}catch(...){return E_FAIL;}
}
extern "C" __declspec(dllexport) int32_t EIR_CALL ataman_brush_mesh_facts(void* h,const wchar_t* path) noexcept {
 if(!h||h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load()||!path)return -1;
 auto& s=*static_cast<Session*>(h);if(!s.terrain)return -1;
 try{return owned::AtamanReadiness::meshFacts(s.renderer,*s.terrain,path);}catch(...){return E_FAIL;}
}
extern "C" __declspec(dllexport) int32_t EIR_CALL ataman_brush_texture_facts(void* h,const wchar_t* path) noexcept {
 if(!h||h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load()||!path)return -1;
 try{return owned::AtamanReadiness::textureFacts(static_cast<Session*>(h)->renderer,path);}catch(...){return E_FAIL;}
}

extern "C" __declspec(dllexport) int32_t EIR_CALL ataman_brush_vertex_counts(void* h,uint64_t* out,uint32_t count) noexcept {
 if(!h||h!=activeHandle.load()||GetCurrentThreadId()!=activeOwner.load()||!out||count<5)return -1;
 owned::AtamanReadiness::brushVertexCounts(static_cast<Session*>(h)->renderer,out);return 0;
}
