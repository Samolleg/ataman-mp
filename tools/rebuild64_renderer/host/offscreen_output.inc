#include "output_live.inc"
#include "independent_preview.inc"
#include "cartographic_output.inc"
// EI-REBUILD64-OFFSCREEN-288. This include is intentionally independent of terrain/brush updates.
int32_t EIR_CALL host_output(void* h,const EirOutputRequest* request,EirOutput* output,EirError* out) noexcept {
 probe1b_error error{};
 if(!h||!request||request->size!=sizeof(*request)||!output||output->size!=sizeof(*output)||request->mode>4)
  return eir_error(out,fail(&error,E_INVALIDARG,"invalid memory output arguments"),error);
 return eir_error(out,guarded(&error,[&]()->int {
  auto& s=*static_cast<Session*>(h);
  if(s.canPresent&&!s.canPresent(s.presentContext,request->generation,s.frameCamera.neighbours_visible,2))return fail(&error,E_ABORT,"offscreen cancelled: document changed");
  if(request->mode==3)return model_output(s,*request,*output,&error);
  if(request->generation!=s.generation||!s.readyGeneration||s.readyGeneration!=s.generation||s.pending||
     (s.neighboursRequested&&!owned::neighbour_client_settled(s.world)))
   return fail(&error,E_PENDING,"Rebuild64: world/document/materials/neighbours are not ready; retry output");
  if(request->mode==4)return cartographic_output(s,*request,*output,&error);
  const UINT oldWidth=s.width,oldHeight=s.height;const auto oldCamera=s.camera;const auto oldFrame=s.frameCamera;
  const auto oldClip=s.clip;const bool oldExplicit=s.explicitClip;const auto oldEpoch=s.completedReadyEpoch;
  const auto oldPending=s.pending;const double oldHour=s.hour;
  struct Restore {
   Session& s;UINT width,height;probe1b_camera camera;EirCamera frame;std::array<float,16> clip;bool explicitClip;
   uint64_t epoch;uint32_t pending;double hour;bool resized=false;
   ~Restore(){s.independentOutput=false;s.outputTarget.Reset();s.camera=camera;s.frameCamera=frame;s.clip=clip;s.explicitClip=explicitClip;s.hour=hour;
    if(resized)s.renderer.resize(width,height);s.width=width;s.height=height;s.completedReadyEpoch=epoch;s.pending=pending;}
  } restore{s,oldWidth,oldHeight,oldCamera,oldFrame,oldClip,oldExplicit,oldEpoch,oldPending,oldHour};
  ID3D11Texture2D* target=request->mode==0?s.accepted.Get():s.rawAccepted.Get();
  if(request->mode==2) {
   if(!request->width||!request->height||request->width>8192||request->height>8192||uint64_t(request->width)*request->height>16777216)
    return fail(&error,E_INVALIDARG,"offscreen extent exceeds 16M pixels/8192");
   // Visibility is a world setting, not an offscreen camera setting. Changing it
   // would retire the live neighbour cohort, so an independent request cannot do so.
   if(request->camera.neighbours_visible!=s.frameCamera.neighbours_visible)
    return fail(&error,E_INVALIDARG,"offscreen neighbour visibility must match the accepted world");
   s.independentOutput=true;s.frameCamera=request->camera;s.explicitClip=true;
   std::copy(request->camera.world_to_clip,request->camera.world_to_clip+16,s.clip.begin());
   std::copy(request->camera.eye,request->camera.eye+3,s.camera.eye);std::copy(request->camera.target,request->camera.target+3,s.camera.target);
   s.hour=request->camera.hour;s.width=request->width;s.height=request->height;
   restore.resized=oldWidth!=s.width||oldHeight!=s.height;
   if(restore.resized)s.renderer.resize(s.width,s.height);
   // A single render turn; callers interleave a pending request with live frames.
   const int rc=map_frame(s,&error);if(rc)return rc;
   target=s.outputTarget.Get();if(!target)return fail(&error,E_PENDING,"offscreen materials are preparing; retry output");
  }
  if(!target)return fail(&error,E_PENDING,"no accepted output");
  D3D11_TEXTURE2D_DESC desc{};target->GetDesc(&desc);
  output->width=desc.Width;output->height=desc.Height;output->stride=desc.Width*4;
  if(!output->bgra||output->capacity<uint64_t(output->stride)*desc.Height)return fail(&error,E_INVALIDARG,"memory output buffer too small");
  const auto hr=read_output(&s,s.context.Get(),target);if(FAILED(hr))return fail(&error,hr,"native memory readback failed");
  std::memcpy(output->bgra,s.pixels.data(),s.pixels.size());output->generation=s.generation;output->serial=request->serial;
  output->neighbours_accepted=owned::AtamanReadiness::neighbours(s.renderer);output->pending=0;
  std::snprintf(output->map,sizeof(output->map),"%s",s.terrainName.c_str());
  return 0;
 }),error);
}
