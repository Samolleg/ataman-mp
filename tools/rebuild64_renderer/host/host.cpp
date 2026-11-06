#include "api.h"
#include "rebuild64_abi.h"
#include <chrono>
#include "retained_static_scene.h"
#include "renderer.h"
#include "embedded_host.h"
#include "E1Scene.h"
#include "E1World.h"
#include "E1Terrain.h"
#include <wincodec.h>
#include <memory>
#include <vector>
#include <cstdio>
#include <exception>
#include <cmath>
#include <cstring>
#include "sky_clouds.h"
#include "neighbour_maps.h"
#include "neighbour_client.h"
#include "NeighbourTerrain.h"

#include <psapi.h>
#include <dxgi1_4.h>
#include "E1Surface.h"
#include "E1VisualFrame.h"
#include "embedded_sky.h"
#include "retained_static_submission.h"
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
 static bool load(Renderer& r,const wchar_t* path) {
  // ensure_host registers defaults but intentionally has no game's overlay
  // bootstrap. Use the same registry and INI loader before its first apply.
  r.register_config_options();return r.debug_overlay_.config().load(path);
 }
 static HRESULT material(Renderer& r,const std::string& name,const TextureCpuAsset& texture) {
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
  if(SUCCEEDED(hr))r.note_material_texture(name,backing,false);
  return hr;
 }
 static void quality(const Renderer& r) noexcept {
  const auto& c=r.debug_overlay_.config();std::fprintf(stderr,"HOST_QUALITY draw_distance=%d smaa=%d render_scale=%d upscaler=%d anisotropy=%d fog_preset=%d sky_clouds=%d material_quality=%d\n",configured_integer(c,"draw_distance",300),configured_boolean(c,"smaa",true),configured_integer(c,"render_scale",100),configured_integer(c,"upscaler",0),configured_integer(c,"anisotropy",3),configured_integer(c,"fog_preset",2),configured_integer(c,"sky_clouds",2),configured_integer(c,"material_quality",3));
  std::fprintf(stderr,"RENDERER_CONFIG terrain_blend=%d material_terrain=%d water_simple=%d\n",configured_integer(c,"terrain_blend",1),configured_boolean(c,"material_terrain",true),configured_boolean(c,"water_simple",false));
 }
 static void settings(const Renderer& r){for(const auto& o:r.debug_overlay_.config().options()){std::string v;switch(o.type){case RendererOptionType::boolean:v=o.boolean_value?"true":"false";break;case RendererOptionType::integer:v=std::to_string(o.integer_value);break;case RendererOptionType::floating_point:v=std::to_string(o.floating_value);break;case RendererOptionType::text:v=o.text_value;break;}std::fprintf(stderr,"EFFECTIVE_CONFIG %s=%s\n",o.key.c_str(),v.c_str());}}
 static bool neighboursEnabled(const Renderer& r) {return configured_boolean(r.debug_overlay_.config(),"neighbour_maps",false);}
 static uint32_t neighbours(const Renderer& r) noexcept {return uint32_t(r.neighbours_.accepted.size());}
 static uint32_t pending(const Renderer& r) noexcept {
  // Disabled Terrain2 has no required style/bake to complete. Contact-atlas
  // publication remains required for the object contact path in either mode.
  if(configured_integer(r.debug_overlay_.config(),"terrain_blend",1)==0)
   return r.contact_maps_.pending?4u:0u;
  // The complete generic programs draw while optional specialised sets build.
  // Their optimisation residency is not a resource-readiness requirement.
  return (!r.tile_blend_ready_?1u:0u)|(!r.tile_blend_rows_?2u:0u)|(r.contact_maps_.pending?4u:0u)|
   (!r.terrain2_map_.style_serial?8u:0u)|(r.terrain2_map_.dirty?16u:0u)|
   (r.terrain2_map_.seen_ready!=r.contact_maps_.ready?32u:0u)|
   (r.terrain2_map_.baked.size()!=r.terrain2_map_.bake_cells.size()?64u:0u)|
   (r.terrain2_map_.failed?128u:0u);
 }
 static void trace(const Renderer& r) noexcept {
  std::fprintf(stderr,"READY pending=%u atlas=%llx/%llx baked=%zu/%zu table=%u sky_pending=%d sky_active=%d sky=%s\n",pending(r),
   (unsigned long long)r.contact_maps_.ready,(unsigned long long)r.contact_maps_.pending,r.terrain2_map_.baked.size(),r.terrain2_map_.bake_cells.size(),r.terrain2_table_count_,r.sky_draw_pending_,r.sky_inputs_active_,r.sky_status_);
 }
}; }
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
struct Session {ResourceRuntimeLifetime resources;GameInputs game;HiddenWindow hidden;owned::Renderer renderer;ComPtr<IDXGISwapChain> display;ComPtr<ID3D11Texture2D> accepted;uint64_t world=0,readyGeneration=0;uint32_t pending=0;bool loseDevice=false;EirCamera frameCamera{};std::array<float,16> clip{};bool explicitClip=false;bool materialsPublished=false;bool neighboursConfigured=false,neighboursRequested=false;std::string neighbourError;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;UINT width=0,height=0;std::vector<uint8_t> pixels; owned::ResourceStoreConfig mounts;owned::ResourceStore store;std::shared_ptr<owned::TerrainCpuAsset> terrain,baseline;std::vector<std::unique_ptr<OwnedRender::E1Surface>> atlases;std::vector<uint64_t> atlasIds;probe1b_camera camera{{240,110,175},{240,171,10},45};uint64_t generation=1;double hour=12.6666667;bool water=false,environment=false,objects=false;std::shared_ptr<const owned::OwnedMapSnapshot> map;};
bool active=false;
int fail(probe1b_error* e,int code,const char* text) noexcept {if(e){e->code=code;std::snprintf(e->detail,sizeof(e->detail),"%s",text);}return code;}
template<class F> int guarded(probe1b_error* e,F f) noexcept {try{return f();}catch(const std::exception& x){return fail(e,E_FAIL,x.what());}catch(...){return fail(e,E_FAIL,"C++ exception contained inside DLL");}}
HRESULT read_output(void* user,ID3D11DeviceContext* context,ID3D11Texture2D* source) {
 auto& s=*static_cast<Session*>(user);D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
 if(desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)return E_NOTIMPL;
 const bool bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM;
 desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
 ComPtr<ID3D11Texture2D> staging;HRESULT hr=s.device->CreateTexture2D(&desc,nullptr,&staging);if(FAILED(hr))return hr;
 context->CopyResource(staging.Get(),source);D3D11_MAPPED_SUBRESOURCE mapped{};hr=context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);if(FAILED(hr))return hr;
 s.width=desc.Width;s.height=desc.Height;s.pixels.resize(size_t(desc.Width)*desc.Height*4);
 for(UINT y=0;y<desc.Height;++y){const auto* src=static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch;auto* dst=s.pixels.data()+size_t(y)*desc.Width*4;
  for(UINT x=0;x<desc.Width;++x){dst[x*4]=src[x*4+(bgra?0:2)];dst[x*4+1]=src[x*4+1];dst[x*4+2]=src[x*4+(bgra?2:0)];dst[x*4+3]=255;}}
 context->Unmap(staging.Get(),0);return S_OK;
}
}
namespace {
HRESULT commit_output(void* user,ID3D11DeviceContext* context,ID3D11Texture2D* source) {
 auto& s=*static_cast<Session*>(user);s.pending=owned::AtamanReadiness::pending(s.renderer);
 if(s.pending)return S_OK;
 D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
 if(d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)return E_NOTIMPL;
 ComPtr<ID3D11Texture2D> next=s.accepted;D3D11_TEXTURE2D_DESC old{};if(next)next->GetDesc(&old);
 if(!next||old.Width!=d.Width||old.Height!=d.Height){next.Reset();d.BindFlags=0;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;HRESULT hr=s.device->CreateTexture2D(&d,nullptr,&next);if(FAILED(hr))return hr;}
 context->CopyResource(next.Get(),source);
 DXGI_SWAP_CHAIN_DESC display{};HRESULT resized=s.display->GetDesc(&display);if(FAILED(resized))return resized;
 if(display.BufferDesc.Width!=d.Width||display.BufferDesc.Height!=d.Height){resized=s.display->ResizeBuffers(0,d.Width,d.Height,DXGI_FORMAT_UNKNOWN,0);if(FAILED(resized))return resized;}
 ComPtr<ID3D11Texture2D> back;HRESULT hr=s.display->GetBuffer(0,IID_PPV_ARGS(&back));if(FAILED(hr))return hr;
 context->CopyResource(back.Get(),source);hr=s.display->Present(0,0);if(FAILED(hr))return hr;
 s.accepted=std::move(next);s.readyGeneration=s.generation;return s.device->GetDeviceRemovedReason();
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
  auto s=std::make_unique<Session>();
  s->hidden.value=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,-32000,-32000,int(in->width),int(in->height),nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(!s->hidden.value||IsWindowVisible(s->hidden.value))return fail(e,E_FAIL,"cannot create hidden renderer surface");s->width=in->width;s->height=in->height;for(uint32_t i=0;i<in->loose_count;++i)s->mounts.loose_roots.emplace_back(in->loose[i]);for(uint32_t i=0;i<in->archive_count;++i)s->mounts.archives.emplace_back(in->archives[i]);if(in->archive_kinds)s->mounts.archive_kinds.assign(in->archive_kinds,in->archive_kinds+in->archive_count);
  const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL level;
  HRESULT hr=in->test_failure==1?DXGI_ERROR_UNSUPPORTED:D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,levels,1,D3D11_SDK_VERSION,&s->device,&level,&s->context);
  if(FAILED(hr))return fail(e,hr,"D3D11CreateDevice hardware");
  ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;if(SUCCEEDED(s->device.As(&dxgi))&&SUCCEEDED(dxgi->GetAdapter(&adapter))){DXGI_ADAPTER_DESC desc{};adapter->GetDesc(&desc);std::printf("adapter=%ls\n",desc.Description);std::fflush(stdout);}
  hr=s->renderer.adopt_host_device(s->device.Get(),s->context.Get(),s->width,s->height);if(FAILED(hr))return fail(e,hr,"adopt_host_device");
  if(!owned::AtamanReadiness::load(s->renderer,in->ini))return fail(e,E_FAIL,"read copied Rebuild64 configuration");
  std::puts("before ensure_host");std::fflush(stdout);
  hr=in->test_failure==2?E_FAIL:s->renderer.ensure_host(s->hidden.value,s->width,s->height);if(FAILED(hr))return fail(e,hr,"ensure_host");
  s->neighboursConfigured=owned::AtamanReadiness::neighboursEnabled(s->renderer);s->neighboursRequested=s->neighboursConfigured;
  owned::AtamanReadiness::quality(s->renderer);owned::AtamanReadiness::settings(s->renderer);
  ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> ad;ComPtr<IDXGIFactory> factory;
  hr=s->device.As(&dx);if(SUCCEEDED(hr))hr=dx->GetAdapter(&ad);if(SUCCEEDED(hr))hr=ad->GetParent(IID_PPV_ARGS(&factory));
  DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=s->width;desc.BufferDesc.Height=s->height;desc.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.OutputWindow=in->window;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
  if(SUCCEEDED(hr))hr=factory->CreateSwapChain(s->device.Get(),&desc,&s->display);
  if(FAILED(hr))return fail(e,hr,"create native Ataman presentation surface");
  active=true;*out=s.release();return fail(e,0,"owned::Renderer initialized; adopt/ensure_host; NGX not initialized");
 });
}
#include "terrain.inc"
int32_t __cdecl probe1b_capture_png(void* h,const wchar_t* path,probe1b_error* e) noexcept {
 return guarded(e,[&]{auto& s=*static_cast<Session*>(h);if(s.pixels.empty())return fail(e,E_UNEXPECTED,"no completed frame");
  const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);ComPtr<IWICImagingFactory> factory;HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
  ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
  if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path,GENERIC_WRITE);
  if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
  if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,nullptr);if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);if(SUCCEEDED(hr))hr=frame->SetSize(s.width,s.height);
  auto format=GUID_WICPixelFormat32bppBGRA;if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);if(SUCCEEDED(hr))hr=frame->WritePixels(s.height,s.width*4,UINT(s.pixels.size()),s.pixels.data());
  if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();frame.Reset();encoder.Reset();stream.Reset();factory.Reset();if(SUCCEEDED(init))CoUninitialize();
  return fail(e,hr,SUCCEEDED(hr)?"PNG saved from renderer-owned target":"WIC capture failed");});
}
void __cdecl probe1b_destroy(void* h) noexcept {
 std::unique_ptr<Session> s(static_cast<Session*>(h));
 if(!s)return;
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
