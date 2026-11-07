#include "api.h"
#include "rebuild64_abi.h"
#include <chrono>
#include "retained_static_scene.h"
#include "renderer.h"
#include "embedded_host.h"
#include "E1Scene.h"
#include "E1World.h"
#include "E1Terrain.h"
#include "E1AmbientEffects.h"
#include <wincodec.h>
#include <memory>
#include <limits>
#include <vector>
#include <unordered_set>
#include <cstdio>
#include <exception>
#include <cmath>
#include <cstring>
#include "sky_clouds.h"
#include "neighbour_maps.h"
#include "native_prebuilt.h"
#include "neighbour_client.h"
#include "NeighbourTerrain.h"

#include <psapi.h>
#include <d3d11sdklayers.h>
#include <atomic>
#include <dxgi1_4.h>
#include "E1Surface.h"
#include "E1VisualFrame.h"
#include "embedded_sky.h"
#include "retained_static_submission.h"
#include "ambient_terrain_grids.h"
#include "owned_terrain_decode.h"
#include "terrain2.h"
#include <bcrypt.h>
#include "contact_blend.h"
#include "brush_ambient_prepared.h"
#include "brush_neighbour_work.h"
#include "brush_vertex_streams.h"
#include "E1TerrainSubmission.h"
#include "scene_core_types.h"
// Independent provider for the renderer's existing typed host config command.
// No saving, game UI or remote transport. Only our caller-created options.
namespace owned::scene_core {
void Config::run(Renderer& renderer) {
 auto& config=renderer.debug_overlay_.config();
 for(const auto& option:options){switch(static_cast<RendererOptionType>(option.type)){
 case RendererOptionType::boolean:config.set_boolean(option.key.c_str(),option.boolean_value);break;
 case RendererOptionType::integer:config.set_integer(option.key.c_str(),option.integer_value);break;
 case RendererOptionType::floating_point:config.set_floating_point(option.key.c_str(),option.floating_value);break;
 case RendererOptionType::text:config.set_text(option.key.c_str(),option.text_value.c_str());break;
 }}
}
}
namespace owned {
struct AtamanReadiness {
 static void editorQuery(Renderer& r,int byte,bool terrain=false){
  if((r.embedded_editor_byte_>=0)!=(byte>=0)){
   r.native_static_set_.dirty=r.native_terrain_set_.dirty=r.neighbours_.opaque.dirty=true;
   for(auto& layout:r.native_submission_layouts_){layout.source_revision=0;layout.static_revision=0;}
  }
  r.embedded_editor_byte_=byte;r.embedded_editor_terrain_=terrain;
 }
 static ID3D11Texture2D* editorDepth(Renderer& r){return r.depth_.Get();}
 static std::pair<unsigned,std::string> editorNeighbour(const Renderer& r,unsigned id){
  const auto found=r.embedded_editor_neighbours_.find(id);if(found==r.embedded_editor_neighbours_.end())return {};
  return {found->second.nid,found->second.map};
 }
 static HRESULT brushWarmVertices(Renderer& r,const TerrainCpuAsset& terrain){
  for(const auto& sector:terrain.sectors)if(sector.editor_vertices&&sector.editor_attributes){
   bool resident=false;for(const auto& entry:r.brush_vertex_gpu_)if(entry.first.first==sector.editor_vertices->resource&&entry.second.revision==sector.editor_vertices->revision&&entry.second.attribute_revision==sector.editor_attributes->revision){resident=true;break;}
   if(resident)continue;
   UINT page=UINT_MAX;for(UINT i=0;i<r.native_geometry_pages_.size();++i)if(!r.native_geometry_pages_[i].dedicated&&r.native_geometry_pages_[i].buffer)page=i;
   if(page==UINT_MAX)return E_FAIL;UINT position=0,normal=0,attributes=0;
   const auto hr=r.brush_vertex_gpu(page,*sector.editor_vertices,sector.editor_attributes.get(),position,normal,attributes);if(FAILED(hr))return hr;
  }return S_OK;
 }
 static void brushVertexCounts(const Renderer& r,uint64_t* values){values[0]=r.brush_vertex_creates_;values[1]=r.brush_vertex_uploads_;values[2]=r.brush_vertex_upload_bytes_;values[3]=r.brush_material_refreshes_;values[4]=r.brush_vertex_gpu_.size();}
 static HRESULT brushWarmChanged(Renderer& r,const TerrainCpuAsset& terrain,uint64_t revision){
  for(const auto& sector:terrain.sectors)if(sector.editor_vertices&&sector.editor_vertices->revision==revision){
   UINT page=UINT_MAX;for(const auto& entry:r.brush_vertex_gpu_)if(entry.first.first==sector.editor_vertices->resource){page=entry.first.second;break;}
   if(page==UINT_MAX)for(UINT i=0;i<r.native_geometry_pages_.size();++i)if(!r.native_geometry_pages_[i].dedicated&&r.native_geometry_pages_[i].buffer)page=i;
   if(page==UINT_MAX)return E_FAIL;UINT position=0,normal=0,attributes=0;
   const auto hr=r.brush_vertex_gpu(page,*sector.editor_vertices,sector.editor_attributes.get(),position,normal,attributes);if(FAILED(hr))return hr;
  }return S_OK;
 }
 static void brushPrepared(Renderer& r,std::shared_ptr<BrushAmbientPrepared> ready){r.contact_tile_rows_=ready->contact;r.brush_ambient_prepared_=std::move(ready);}
 static bool brushNeighboursReady(const Renderer& r){return !r.neighbours_.brush_terrain&&!r.neighbours_.brush_work;}
 static bool brushSurfSea(const Renderer& r){return liquid_surf_map_takes_sea(r.ambient_map_name_,r.liquid_surf_no_sea_maps());}
 static bool brushDerivedReady(const Renderer& r){
  if(r.liquid_surf_job_.valid()||r.liquid_maps_job_.valid())return false;
  if(r.brush_ambient_prepared_&&r.brush_ambient_prepared_->surf&&
    (r.liquid_surf_active_!=r.brush_ambient_prepared_->surf||r.liquid_surf_built_serial_!=r.liquid_surf_serial_))return false;
  if(r.gc_map_generation_==r.ambient_map_generation_&&!r.gc_material_state_.empty())return false;
  return true;
 }
 static OwnedTerrainGeometryValues brushGeometry(const Renderer& r,const TerrainCpuAsset& terrain){return r.ambient_geometry_?*r.ambient_geometry_:frozen_owned_terrain_geometry(terrain);}
 static TerrainTileFamilies brushFamilies(const Renderer& r){return r.tile_families_;}
 static std::vector<uint32_t> brushEdges(const Renderer& r){return r.tile_blend_edge_;}
 static bool brushOwnEdges(const Renderer& r){return r.tile_blend_own_edges_;}
 #include "terrain_facts.inc"
 #include "mesh_facts.inc"
 #include "texture_facts.inc"
 static bool testFrame(Renderer& r,uint64_t frame){
  if(frame&&frame<r.native_frame_index_){std::fprintf(stderr,"TEST_CLOCK rejected current=%llu requested=%llu\n",r.native_frame_index_,frame);return false;}
  if(frame)r.native_frame_index_=frame;
  if(GetEnvironmentVariableW(L"ATAMAN_TEST_DISABLE_DOF_HISTORY",nullptr,0))r.depth_of_field_.diagnostic_reset_history();
  if(GetEnvironmentVariableW(L"ATAMAN_TEST_DISABLE_SHADOW_CACHE",nullptr,0)){r.native_shadow_valid_.fill(false);r.native_shadow_static_valid_.fill(false);}

  return true;
 }
 static void coverFacts(Renderer& r,uint64_t* out,char* reason,uint32_t capacity) {
  const uint64_t facts[]{r.native_frame_index_,r.ambient_terrain_frame_,r.ambient_map_generation_,r.gc_map_generation_,r.gc_drawn_frame_,r.gc_pass_count_,r.gc_material_state_.size(),r.gc_failed_,r.gc_shadow_ready_,r.gc_tree_epoch_};
  std::copy(std::begin(facts),std::end(facts),out);
  std::snprintf(reason,capacity,"%s",r.gc_skip_reason_?r.gc_skip_reason_:"");
 }
 static HRESULT frameFacts(Renderer& r,double* out) {
  out[0]=double(r.native_frame_index_);out[1]=double(r.presentation_frame_index_);out[2]=double(r.world_state_epoch_);
  out[3]=r.depth_of_field_.diagnostic_seconds();out[4]=r.depth_of_field_.diagnostic_history();out[5]=r.depth_of_field_.diagnostic_focus_index();
  out[6]=std::numeric_limits<double>::quiet_NaN();out[7]=double(r.ambient_map_generation_);out[8]=double(r.tile_families_generation_);out[9]=double(r.terrain2_map_.style_serial);out[10]=double(r.contact_maps_.tiles_serial);out[11]=double(r.gc_tree_epoch_);
  auto* source=r.depth_of_field_.diagnostic_focus_target();if(!source)return S_FALSE;
  D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);if(d.Width!=1||d.Height!=1||d.Format!=DXGI_FORMAT_R32_FLOAT)return E_UNEXPECTED;
  d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;HRESULT hr=brush_count_device(r.device_).CreateTexture2D(&d,nullptr,&staging);if(FAILED(hr))return hr;
  r.context_->CopyResource(staging.Get(),source);D3D11_MAPPED_SUBRESOURCE read{};hr=r.context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&read);if(FAILED(hr))return hr;
  out[6]=*static_cast<const float*>(read.pData);r.context_->Unmap(staging.Get(),0);return S_OK;
 }
 #include "brush_grids.inc"
 #include "brush_materials.inc"
  #include "brush_resources.inc"
  #include "brush_layout_resources.inc"
 static bool load(Renderer& r,const wchar_t* path) {
  // ensure_host registers defaults but intentionally has no game's overlay
  // bootstrap. Use the same registry and INI loader before its first apply.
  r.register_config_options();return r.debug_overlay_.config().load(path);
 }
 static HRESULT material(Renderer& r,const std::string& name,const TextureCpuAsset& texture,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>* output=nullptr) {
  static_assert(int(MmpFormat::dxt5)==int(NativeOwnedTextureFormat::dxt5),"pinned MMP format order");
  NativeOwnedTextureView view;view.value_source=static_cast<UINT>(texture.value_source)+1;
  view.identity=texture.value_source==TextureValueSource::archive?1:texture.value_serial;view.generation=1;
  view.resource=texture.value_source==TextureValueSource::archive?&texture.identity:nullptr;
  view.format=static_cast<NativeOwnedTextureFormat>(texture.format);view.opaque_mips=texture.opaque_mips;
  std::vector<NativeOwnedTextureMip> levels;
  for(const auto& mip:texture.mips){if(mip.byte_offset>texture.source->size()||mip.bytes>texture.source->size()-mip.byte_offset)return E_INVALIDARG;levels.push_back({texture.source->data()+mip.byte_offset,mip.width,mip.height,mip.bytes});}
  view.mips=levels.data();view.mip_count=UINT(levels.size());view.material_name=&name;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;uint64_t backing=0;NativeGeometryResult result;
  const HRESULT hr=r.get_native_owned_texture(view,srv,backing,&result,true,true);
  if(SUCCEEDED(hr)){r.note_material_texture(name,backing,false);if(output)*output=srv;}
  return hr;
 }
 static void quality(const Renderer& r) noexcept {
  const auto& c=r.debug_overlay_.config();std::fprintf(stderr,"HOST_QUALITY draw_distance=%d smaa=%d render_scale=%d upscaler=%d anisotropy=%d fog_preset=%d sky_clouds=%d material_quality=%d\n",configured_integer(c,"draw_distance",300),configured_boolean(c,"smaa",true),configured_integer(c,"render_scale",100),configured_integer(c,"upscaler",0),configured_integer(c,"anisotropy",3),configured_integer(c,"fog_preset",2),configured_integer(c,"sky_clouds",2),configured_integer(c,"material_quality",3));
  std::fprintf(stderr,"RENDERER_CONFIG terrain_blend=%d material_terrain=%d water_simple=%d\n",configured_integer(c,"terrain_blend",1),configured_boolean(c,"material_terrain",true),configured_boolean(c,"water_simple",false));
 }
 static void settings(const Renderer& r){for(const auto& o:r.debug_overlay_.config().options()){std::string v;switch(o.type){case RendererOptionType::boolean:v=o.boolean_value?"true":"false";break;case RendererOptionType::integer:v=std::to_string(o.integer_value);break;case RendererOptionType::floating_point:v=std::to_string(o.floating_value);break;case RendererOptionType::text:v=o.text_value;break;}std::fprintf(stderr,"EFFECTIVE_CONFIG %s=%s\n",o.key.c_str(),v.c_str());}}
 static bool neighboursEnabled(const Renderer& r) {return configured_boolean(r.debug_overlay_.config(),"neighbour_maps",false);}
 static uint64_t epoch(const Renderer& r) noexcept {return r.world_state_epoch_;}
 static void brushDebug(const Renderer& r,char* out,uint32_t size) {
  std::snprintf(out,size,"pending=%u frame=%llu epoch=%llu ambient=%llu contact=%llu ready=%llu pending_atlas=%llu baked=%zu cells=%zu style=%llu families=%llu families_job=%llu tile_ready=%d rows=%d failed=%d family_valid=%d family_ready=%d prestart_valid=%d prestart_ready=%d retry_frame=%llu",
   pending(r),r.native_frame_index_,r.world_state_epoch_,r.ambient_map_generation_,r.contact_map_generation_,r.contact_maps_.ready,r.contact_maps_.pending,r.terrain2_map_.baked.size(),r.terrain2_map_.bake_cells.size(),r.terrain2_map_.style_serial,r.tile_families_generation_,r.tile_families_job_generation_,r.tile_blend_ready_,r.tile_blend_rows_,r.terrain2_map_.failed,r.tile_families_job_.valid(),r.tile_families_job_.ready(),r.tile_families_prestart_.valid(),r.tile_families_prestart_.ready(),r.contact_retry_frame_);
 }
 static uint32_t neighbours(const Renderer& r) noexcept {return uint32_t(r.neighbours_.accepted.size());}
 static uint32_t pending(const Renderer& r) noexcept {
  // Disabled Terrain2 has no required style/bake to complete. Contact-atlas
  // publication remains required for the object contact path in either mode.
  if(configured_integer(r.debug_overlay_.config(),"terrain_blend",1)==0)
   return r.contact_maps_.pending?4u:0u;
  // The complete generic programs draw while optional specialised sets build.
  // Their optimisation residency is not a resource-readiness requirement.
  bool missing_bake=false;for(const auto& cell:r.terrain2_map_.bake_cells)if(!r.terrain2_map_.baked.count(cell.key)){missing_bake=true;break;}
  return (!r.tile_blend_ready_?1u:0u)|(!r.tile_blend_rows_?2u:0u)|(r.contact_maps_.pending?4u:0u)|
   (!r.terrain2_map_.style_serial?8u:0u)|(r.terrain2_map_.dirty?16u:0u)|
   (r.terrain2_map_.seen_ready!=r.contact_maps_.ready?32u:0u)|
   (missing_bake?64u:0u)|
   (r.terrain2_map_.failed?128u:0u);
 }
 static void trace(const Renderer& r) noexcept {
  std::fprintf(stderr,"READY pending=%u atlas=%llx/%llx baked=%zu/%zu table=%u sky_pending=%d sky_active=%d sky=%s\n",pending(r),
   (unsigned long long)r.contact_maps_.ready,(unsigned long long)r.contact_maps_.pending,r.terrain2_map_.baked.size(),r.terrain2_map_.bake_cells.size(),r.terrain2_table_count_,r.sky_draw_pending_,r.sky_inputs_active_,r.sky_status_);
 }
}; }
void capture_presented_trace(void*,ID3D11Texture2D*);
using Microsoft::WRL::ComPtr;
#include "game_inputs.inc"
namespace {
struct HiddenWindow {
 HWND value=nullptr;
 ~HiddenWindow(){if(value)DestroyWindow(value);}
};
// Match the public resource runtime's acquire/release lifetime. Merely adopting
// mounts (the remote-core path) never released selected RES handles on close.
// This member is destroyed last, after renderer/world/snapshot retirement.
struct ResourceRuntimeLifetime {
 ResourceRuntimeLifetime(){owned::owned_resource_runtime_acquire();}
 ~ResourceRuntimeLifetime(){owned::owned_resource_runtime_release();}
};
#include "brush_d3d_counts.h"
#include "editor_service.h"
#include "presentation_trace.h"
struct EditorSeed {uint32_t mob=0,nid=0;int32_t rendererId=0;std::shared_ptr<const owned::VisualAssemblyAsset> assembly;owned::VisualObjectPose pose;bool visible=true;float relativeZ=0;};
// The worker is explicitly joined before any world/device release.
#include "brush_worker.h"
struct Session {bool querying=false;uint32_t(EIR_CALL *liveValues)(void*,uint64_t,uint32_t,EirCamera*,EirService*)=nullptr;std::chrono::steady_clock::time_point livePump=std::chrono::steady_clock::now();UINT pixelWidth=0,pixelHeight=0;bool independentOutput=false;ComPtr<ID3D11Texture2D> outputTarget;bool editorEmptyFootprints=false;bool verticesWarmed=false;BrushFinalizeWorker brushWorker;uint64_t finalizePublishedSerial=0;std::chrono::steady_clock::time_point finalizeStart{};uint64_t diagnosticFrame=0;uint32_t presentHold=0,leaseProbe=~0u,leaseAcquire=~0u;DWORD ownerThread=GetCurrentThreadId();EirUpdateState edits{};uint64_t editRevision=0;std::chrono::steady_clock::time_point editStart{};ResourceRuntimeLifetime resources;PresentationTrace trace;uint32_t (EIR_CALL *canPresent)(void*,uint64_t,uint32_t,uint32_t)=nullptr;void* presentContext=nullptr;double acceptedServiceCpuMs=0,acceptedCompositeMs=0;bool poseAuditDone=false,holdObjectRefresh=false;bool traceFrameStarted=false,traceNeighboursSettled=false,traceResourcesSettled=false;uint64_t completedReadyEpoch=~uint64_t(0),documentUpdates=0,acceptedServiceSerial=0;EditorService service;std::vector<EditorSeed> editorSeeds;std::vector<uint8_t> terrainBytes;std::string terrainName;std::wstring rendererIni;GameInputs game;HiddenWindow hidden;owned::Renderer renderer;ComPtr<IDXGISwapChain> display;ComPtr<ID3D11Texture2D> accepted,rawAccepted;uint64_t world=0,readyGeneration=0;uint32_t pending=0;bool loseDevice=false;EirCamera frameCamera{};std::array<float,16> clip{};bool explicitClip=false;bool materialsPublished=false;bool neighboursConfigured=false,neighboursRequested=false;std::string neighbourError;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;UINT width=0,height=0;std::vector<uint8_t> pixels; owned::ResourceStoreConfig mounts;owned::ResourceStore store;std::shared_ptr<owned::TerrainCpuAsset> terrain,baseline;std::vector<std::unique_ptr<OwnedRender::E1Surface>> atlases;std::vector<uint64_t> atlasIds;std::vector<std::string> atlasNames;std::vector<std::array<unsigned char,32>> atlasDigests;std::map<std::array<unsigned char,32>,owned::TerrainTileFamilies> brushFamilyCache;probe1b_camera camera{{240,110,175},{240,171,10},45};uint64_t generation=1;double hour=12.6666667;bool water=false,environment=false,objects=false;std::shared_ptr<const owned::OwnedMapSnapshot> map;HiddenWindow previewWindow;ComPtr<ID3D11Device> previewDevice;ComPtr<ID3D11DeviceContext> previewContext;std::unique_ptr<owned::Renderer> previewRenderer;std::shared_ptr<owned::RetainedStaticSourceCandidate> previewSource;std::string previewKey;};
bool active=false;std::atomic<void*> activeHandle{nullptr};std::atomic<DWORD> activeOwner{0};uint64_t nextWorld=0;
int fail(probe1b_error* e,int code,const char* text) noexcept {if(e){e->code=code;std::snprintf(e->detail,sizeof(e->detail),"%s",text);}return code;}
template<class F> int guarded(probe1b_error* e,F f) noexcept {try{return f();}catch(const std::exception& x){return fail(e,E_FAIL,x.what());}catch(...){return fail(e,E_FAIL,"C++ exception contained inside DLL");}}
HRESULT read_output(void* user,ID3D11DeviceContext* context,ID3D11Texture2D* source) {
 auto& s=*static_cast<Session*>(user);D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
 if(desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)return E_NOTIMPL;
 const bool bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM;
 desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
 ComPtr<ID3D11Device> sourceDevice;source->GetDevice(&sourceDevice);ComPtr<ID3D11Texture2D> staging;HRESULT hr=sourceDevice->CreateTexture2D(&desc,nullptr,&staging);if(FAILED(hr))return hr;
 context->CopyResource(staging.Get(),source);D3D11_MAPPED_SUBRESOURCE mapped{};hr=context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);if(FAILED(hr))return hr;
 s.pixelWidth=desc.Width;s.pixelHeight=desc.Height;s.pixels.resize(size_t(desc.Width)*desc.Height*4);
 for(UINT y=0;y<desc.Height;++y){const auto* src=static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch;auto* dst=s.pixels.data()+size_t(y)*desc.Width*4;
  for(UINT x=0;x<desc.Width;++x){dst[x*4]=src[x*4+(bgra?0:2)];dst[x*4+1]=src[x*4+1];dst[x*4+2]=src[x*4+(bgra?2:0)];dst[x*4+3]=255;}}
 context->Unmap(staging.Get(),0);return S_OK;
}
}
namespace {
HRESULT commit_output(void* user,ID3D11DeviceContext* context,ID3D11Texture2D* source) {
 auto& s=*static_cast<Session*>(user);if(s.querying)return S_OK;s.presentHold=0;s.leaseProbe=s.leaseAcquire=~0u;s.pending=owned::AtamanReadiness::pending(s.renderer);
 if(s.pending){s.presentHold=1;return S_OK;}if(s.neighboursRequested&&!owned::neighbour_client_settled(s.world)){s.presentHold=2;return S_OK;}
  if(s.readyGeneration!=s.generation&&!owned::AtamanReadiness::brushNeighboursReady(s.renderer)){s.presentHold=8;return S_OK;}
 if(s.trace.capture){
  wchar_t expected[16]{};const auto n=GetEnvironmentVariableW(L"ATAMAN_BRUSH_EXPECT_NEIGHBOURS",expected,16);
  if(n&&n<16&&std::wcstoul(expected,nullptr,10)!=owned::AtamanReadiness::neighbours(s.renderer)){
   std::fprintf(stderr,"PRESENT_NEIGHBOUR_COUNT_FAIL expected=%ls actual=%u generation=%llu\n",expected,owned::AtamanReadiness::neighbours(s.renderer),s.generation);return E_FAIL;
  }
 }
 // Preserve287: native readiness does not wait for the optional editor service layer.
 // Readiness must hold across a completed render/present/housekeeping cycle.
 // The first pending==0 draw can still reference the previous material table;
 // admit only the next draw in the same ready epoch.
 if(s.completedReadyEpoch!=owned::AtamanReadiness::epoch(s.renderer)){s.presentHold=5;return S_OK;}
 s.leaseProbe=s.canPresent?s.canPresent(s.presentContext,s.generation,s.frameCamera.neighbours_visible,2):1;if(!s.leaseProbe){s.presentHold=6;return S_OK;}
 // Offscreen 288: same readiness gate, separate GPU target, no display Present,
 // no accepted-frame/service/camera publication and no world replacement.
 if(s.independentOutput) {
  D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);d.BindFlags=0;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;
  s.outputTarget.Reset();HRESULT hr=s.device->CreateTexture2D(&d,nullptr,&s.outputTarget);
  if(SUCCEEDED(hr))context->CopyResource(s.outputTarget.Get(),source);return hr;
 }

 D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
 if(d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)return E_NOTIMPL;
 ComPtr<ID3D11Texture2D> next=s.accepted;D3D11_TEXTURE2D_DESC old{};if(next)next->GetDesc(&old);
 if(!next||old.Width!=d.Width||old.Height!=d.Height){next.Reset();d.BindFlags=0;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;HRESULT hr=brush_count_device(s.device).CreateTexture2D(&d,nullptr,&next);if(FAILED(hr))return hr;}
 D3D11_TEXTURE2D_DESC rawDesc{};if(s.rawAccepted)s.rawAccepted->GetDesc(&rawDesc);
 if(!s.rawAccepted||rawDesc.Width!=d.Width||rawDesc.Height!=d.Height){s.rawAccepted.Reset();d.BindFlags=0;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;const auto hr=brush_count_device(s.device).CreateTexture2D(&d,nullptr,&s.rawAccepted);if(FAILED(hr))return hr;}
 DXGI_SWAP_CHAIN_DESC display{};HRESULT resized=s.display->GetDesc(&display);if(FAILED(resized))return resized;
 if(display.BufferDesc.Width!=d.Width||display.BufferDesc.Height!=d.Height){resized=s.display->ResizeBuffers(0,d.Width,d.Height,DXGI_FORMAT_UNKNOWN,0);if(FAILED(resized))return resized;}
 ComPtr<ID3D11Texture2D> back;HRESULT hr=s.display->GetBuffer(0,IID_PPV_ARGS(&back));if(FAILED(hr))return hr;
 context->CopyResource(back.Get(),source);
 const auto compositeStart=std::chrono::steady_clock::now();hr=std::memcmp(s.service.clip,s.frameCamera.world_to_clip,sizeof(s.service.clip))?S_OK:s.service.draw(s.device.Get(),context,back.Get(),s.generation);if(FAILED(hr))return hr;
 const auto compositeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-compositeStart).count();
 bool tracePixels=false;
 {
  s.leaseAcquire=s.canPresent?s.canPresent(s.presentContext,s.generation,s.frameCamera.neighbours_visible,0):1;if(!s.leaseAcquire){s.presentHold=7;return S_OK;}
  struct Admission {Session& s;~Admission(){if(s.canPresent)s.canPresent(s.presentContext,0,0,1);}} admission{s};
  context->CopyResource(s.rawAccepted.Get(),source);
  context->CopyResource(next.Get(),back.Get());
  hr=s.display->Present(0,0);if(FAILED(hr))return hr;
  ++s.edits.presents;if(s.edits.presented_serial!=s.edits.applied_serial){s.edits.presented_serial=s.edits.applied_serial;s.edits.last_update_to_present_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-s.editStart).count();}
  tracePixels=s.trace.presented(s.generation,owned::AtamanReadiness::neighbours(s.renderer),s.frameCamera.neighbours_visible,s.pending,owned::neighbour_client_settled(s.world),s.terrainName.c_str(),s.frameCamera.world_to_clip,s.frameCamera.eye,s.edits.presented_serial);
 }
 if(tracePixels)capture_presented_trace(&s,next.Get());
 s.accepted=std::move(next);s.readyGeneration=s.generation;s.acceptedServiceSerial=s.service.generation==s.generation?s.service.serial:0;s.acceptedServiceCpuMs=s.service.cpuMs;s.acceptedCompositeMs=compositeMs;return s.device->GetDeviceRemovedReason();
}
}
void trace_brush_frame(Session& s,int rc) {
 if(!GetEnvironmentVariableW(L"ATAMAN_BRUSH_FRAME_TRACE",nullptr,0))return;
 char state[2048];owned::AtamanReadiness::brushDebug(s.renderer,state,sizeof(state));
 if(FILE* f=std::fopen("brush-frames.jsonl","ab")){
  std::fprintf(f,"{\"tick\":%llu,\"frame\":%llu,\"generation\":%llu,\"world\":%llu,\"applied\":%llu,\"presented\":%llu,\"presents\":%llu,\"readyGeneration\":%llu,\"serviceGeneration\":%llu,\"serviceSerial\":%llu,\"serviceBytes\":%zu,\"hold\":%u,\"leaseProbe\":%u,\"leaseAcquire\":%u,\"neighboursSettled\":%u,\"neighboursAccepted\":%u,\"completedEpoch\":%llu,\"epoch\":%llu,\"rc\":%d,\"renderer\":\"%s\"}\n",GetTickCount64(),++s.diagnosticFrame,s.generation,s.world,s.edits.applied_serial,s.edits.presented_serial,s.edits.presents,s.readyGeneration,s.service.generation,s.service.serial,s.service.pixels.size(),s.presentHold,s.leaseProbe,s.leaseAcquire,unsigned(owned::neighbour_client_settled(s.world)),owned::AtamanReadiness::neighbours(s.renderer),s.completedReadyEpoch,owned::AtamanReadiness::epoch(s.renderer),rc,state);std::fclose(f);
 }
}
int32_t __cdecl probe1b_create(const probe1b_create_info* in,void** out,probe1b_error* e) noexcept {
 if(out)*out=nullptr;if(!in||!out||in->size!=sizeof(*in)||in->abi!=PROBE1B_ABI||!in->window||!in->width||!in->height||active)return fail(e,E_INVALIDARG,"invalid create/ABI or session already active");
 return guarded(e,[&]{
  owned::EmbeddedHostPaths paths;paths.renderer_config=in->ini;paths.world_map_layouts=in->layouts;
  for(uint32_t i=0;i<in->loose_count;++i)paths.mounts.loose_roots.emplace_back(in->loose[i]);
  for(uint32_t i=0;i<in->archive_count;++i)paths.mounts.archives.emplace_back(in->archives[i]);
  if(in->archive_kinds)paths.mounts.archive_kinds.assign(in->archive_kinds,in->archive_kinds+in->archive_count);
  paths.data_mounts=paths.mounts;paths.data_mounts.archive_kinds.clear();
  paths.text_mounts=paths.data_mounts;
  owned::embedded_host_configuration().configure(std::move(paths));
  auto s=std::make_unique<Session>();s->rendererIni=in->ini;
  s->hidden.value=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,-32000,-32000,int(in->width),int(in->height),nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(!s->hidden.value||IsWindowVisible(s->hidden.value))return fail(e,E_FAIL,"cannot create hidden renderer surface");s->width=in->width;s->height=in->height;for(uint32_t i=0;i<in->loose_count;++i)s->mounts.loose_roots.emplace_back(in->loose[i]);for(uint32_t i=0;i<in->archive_count;++i)s->mounts.archives.emplace_back(in->archives[i]);if(in->archive_kinds)s->mounts.archive_kinds.assign(in->archive_kinds,in->archive_kinds+in->archive_count);
  s->trace.phase("device_begin");
  const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL level;
  HRESULT hr=in->test_failure==1?DXGI_ERROR_UNSUPPORTED:D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS|(GetEnvironmentVariableW(L"ATAMAN_REBUILD64_D3D_DEBUG",nullptr,0)?D3D11_CREATE_DEVICE_DEBUG:0),levels,1,D3D11_SDK_VERSION,&s->device,&level,&s->context);
  if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING){std::fprintf(stderr,"D3D_DEBUG unavailable SDK component; retry without layer\n");hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,levels,1,D3D11_SDK_VERSION,&s->device,&level,&s->context);}
  if(FAILED(hr))return fail(e,hr,"D3D11CreateDevice hardware");

  ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;if(SUCCEEDED(s->device.As(&dxgi))&&SUCCEEDED(dxgi->GetAdapter(&adapter))){DXGI_ADAPTER_DESC desc{};adapter->GetDesc(&desc);std::printf("adapter=%ls\n",desc.Description);std::fflush(stdout);}
  hr=s->renderer.adopt_host_device(s->device.Get(),s->context.Get(),s->width,s->height);if(FAILED(hr))return fail(e,hr,"adopt_host_device");
  if(!owned::AtamanReadiness::load(s->renderer,in->ini))return fail(e,E_FAIL,"read copied Rebuild64 configuration");
  s->trace.phase("shader_prepare_begin");
#ifdef EID3D11_EMBEDDED
  if(in->test_failure==3)Sleep(3000); // isolated responsiveness injection, never a shader timing sample
#endif
  std::puts("before ensure_host");std::fflush(stdout);
  hr=in->test_failure==2?E_FAIL:s->renderer.ensure_host(s->hidden.value,s->width,s->height);if(FAILED(hr))return fail(e,hr,"ensure_host");
  s->trace.phase("shader_prepare_end");
  s->neighboursConfigured=owned::AtamanReadiness::neighboursEnabled(s->renderer);s->neighboursRequested=s->neighboursConfigured;
  owned::AtamanReadiness::quality(s->renderer);owned::AtamanReadiness::settings(s->renderer);
  ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> ad;ComPtr<IDXGIFactory> factory;
  hr=s->device.As(&dx);if(SUCCEEDED(hr))hr=dx->GetAdapter(&ad);if(SUCCEEDED(hr))hr=ad->GetParent(IID_PPV_ARGS(&factory));
  DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=s->width;desc.BufferDesc.Height=s->height;desc.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.OutputWindow=in->window;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
  if(SUCCEEDED(hr))hr=factory->CreateSwapChain(s->device.Get(),&desc,&s->display);
  if(FAILED(hr))return fail(e,hr,"create native Ataman presentation surface");
  active=true;activeOwner.store(GetCurrentThreadId());*out=s.release();activeHandle.store(*out);return fail(e,0,"owned::Renderer initialized; adopt/ensure_host; NGX not initialized");
 });
}
#include "brush_frame_timer.h"
#include "terrain.inc"
#include "brush_finalize.inc"
int32_t __cdecl probe1b_capture_png(void* h,const wchar_t* path,probe1b_error* e) noexcept {
 return guarded(e,[&]{auto& s=*static_cast<Session*>(h);if(s.pixels.empty())return fail(e,E_UNEXPECTED,"no completed frame");
  const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);ComPtr<IWICImagingFactory> factory;HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
  ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
  if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path,GENERIC_WRITE);
  if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
  if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,nullptr);if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);if(SUCCEEDED(hr))hr=frame->SetSize(s.pixelWidth,s.pixelHeight);
  auto format=GUID_WICPixelFormat32bppBGRA;if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);if(SUCCEEDED(hr))hr=frame->WritePixels(s.pixelHeight,s.pixelWidth*4,UINT(s.pixels.size()),s.pixels.data());
  if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();frame.Reset();encoder.Reset();stream.Reset();factory.Reset();if(SUCCEEDED(init))CoUninitialize();
  return fail(e,hr,SUCCEEDED(hr)?"PNG saved from renderer-owned target":"WIC capture failed");});
}
void capture_presented_trace(void* handle,ID3D11Texture2D* texture) {
 auto& s=*static_cast<Session*>(handle);if(FAILED(read_output(&s,s.context.Get(),texture)))return;probe1b_error error{};
 const auto path=s.trace.root+L"/present-"+std::to_wstring(s.trace.sequence)+L"-gen-"+std::to_wstring(s.generation)+L".png";
 probe1b_capture_png(handle,path.c_str(),&error);
}
void __cdecl probe1b_destroy(void* h) noexcept {
 activeHandle.store(nullptr);activeOwner.store(0);
 std::unique_ptr<Session> s(static_cast<Session*>(h));
  if(!s)return;
  s->brushWorker.shutdown();s->previewSource.reset();s->previewRenderer.reset();
  owned::neighbour_client_stop();OwnedRender::RetireTerrainPlacement(s.get());
 try{s->renderer.retire_embedded_world();}catch(...){}
 for(const auto id:s->atlasIds)if(id)s->renderer.retire_surface(id);
 OwnedRender::E1Scene().Retire(s.get(),s->world);OwnedRender::E1RetireTerrain(s.get());OwnedRender::E1RetireWorld(s.get());owned::embedded_sky().detach(s.get());
 s->map.reset();s->terrain.reset();s->baseline.reset();s->atlases.clear();s->accepted.Reset();s->display.Reset();
 try{s->renderer.release();}catch(...){}
 s.reset();
 try{owned::embedded_host_configuration().reset();}catch(...){}
 active=false;
}

#include "statistics.inc"
#include "embed_api.inc"
