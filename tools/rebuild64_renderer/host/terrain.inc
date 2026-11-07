namespace OwnedRender { bool E1PublishTerrainSnapshot(const void*,std::uint64_t,std::uint64_t,std::shared_ptr<owned::TerrainCpuAsset>) noexcept; }
namespace OwnedRender { bool E1UpdateTerrainSnapshot(const void*,std::shared_ptr<owned::TerrainCpuAsset>) noexcept; }
namespace {
std::array<float,16> identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
std::array<float,16> camera_matrix(const probe1b_camera& c,float aspect){
 auto sub=[](const float* a,const float* b){return std::array<float,3>{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
 auto norm=[](std::array<float,3> v){float l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(auto& n:v)n/=l;return v;};
 auto f=norm(sub(c.target,c.eye));auto x=norm(std::array<float,3>{f[1],-f[0],0});
 std::array<float,3> y{x[1]*f[2]-x[2]*f[1],x[2]*f[0]-x[0]*f[2],x[0]*f[1]-x[1]*f[0]};
 auto dot=[&](auto v){return v[0]*c.eye[0]+v[1]*c.eye[1]+v[2]*c.eye[2];};
 const float ys=1/std::tan(c.fov*3.14159265358979323846f/360),xs=ys/aspect,q=2000.f/1999.f;
 return {x[0]*xs,x[1]*xs,x[2]*xs,-dot(x)*xs,y[0]*ys,y[1]*ys,y[2]*ys,-dot(y)*ys,f[0]*q,f[1]*q,f[2]*q,-q*dot(f)-q,f[0],f[1],f[2],-dot(f)};
}
#include "brush_pages.inc"
bool upload_atlases(Session& s,std::string& why){
 auto previous=std::move(s.atlases);auto oldNames=std::move(s.atlasNames);auto oldDigests=std::move(s.atlasDigests);
 s.atlases.clear();s.atlasIds.clear();s.atlasNames.clear();s.atlasDigests.clear();owned::ResourceFailure error;
 s.atlases.resize(s.terrain->texture_resources.size());
 for(size_t index=0;index<s.terrain->texture_resources.size();++index){const auto& name=s.terrain->texture_resources[index];
  owned::ResourceBlob blob;owned::TextureCpuAsset tex;
  if(index<s.terrain->pages.size()&&s.terrain->pages[index].ensure_upload_bytes(error,false)){tex=*s.terrain->pages[index].texture;}
   else if(!s.store.resolve(name+".mmp",blob,error)||!owned::parse_texture_asset(blob,tex,error)){if(index>=s.terrain->texture_count){s.atlasIds.push_back(0);s.atlasNames.push_back(name);s.atlasDigests.push_back({});std::printf("optional_atlas_absent=%s\n",name.c_str());continue;}why="atlas "+name+": "+error.message;return false;}
  std::array<unsigned char,32> digest{};if(!brush_page_digest(*tex.source,digest)){why="atlas SHA-256 "+name;return false;}
  s.atlasNames.push_back(name);s.atlasDigests.push_back(digest);
  bool reused=false;for(unsigned exact=1;exact<=2&&!reused;++exact)for(size_t old=0;old<oldNames.size();++old)if(old<previous.size()&&previous[old]&&(exact==2||oldNames[old]==name)&&oldDigests[old]==digest){
   s.atlasIds.push_back(previous[old]->Id());s.atlases[index]=std::move(previous[old]);reused=true;break;}
  if(reused)continue;
  DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;unsigned block=0,pixel=0;
  switch(tex.format){case owned::MmpFormat::dxt1:format=DXGI_FORMAT_BC1_UNORM;block=8;break;case owned::MmpFormat::dxt3:format=DXGI_FORMAT_BC2_UNORM;block=16;break;case owned::MmpFormat::dxt5:format=DXGI_FORMAT_BC3_UNORM;block=16;break;case owned::MmpFormat::argb8888:format=DXGI_FORMAT_B8G8R8A8_UNORM;pixel=4;break;default:why="unsupported actual atlas format "+name;return false;}
  D3D11_TEXTURE2D_DESC d{};d.Width=tex.width;d.Height=tex.height;d.MipLevels=UINT(tex.mips.size());d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  std::vector<D3D11_SUBRESOURCE_DATA> data;
  for(const auto& mip:tex.mips){D3D11_SUBRESOURCE_DATA row{};row.pSysMem=tex.source->data()+mip.byte_offset;row.SysMemPitch=block?((mip.width+3)/4)*block:mip.width*pixel;data.push_back(row);}
  ComPtr<ID3D11Texture2D> gpu;auto hr=brush_count_device(s.device).CreateTexture2D(&d,data.data(),&gpu);if(FAILED(hr)){why="CreateTexture atlas "+name;return false;}
  auto surface=std::make_unique<OwnedRender::E1Surface>();if(!surface->Complete(gpu.Get())){why="E1Surface complete "+name;return false;}
  s.atlasIds.push_back(surface->Id());s.atlases[index]=std::move(surface);std::printf("atlas=%s %ux%u mips=%zu\n",name.c_str(),tex.width,tex.height,tex.mips.size());
 }
 for(const auto& surface:previous)if(surface)s.renderer.retire_surface(surface->Id());
 return true;
}
int map_frame(Session& s,probe1b_error* e){
 BrushFrameTimer timing(s.edits.applied_serial);
 using namespace OwnedRender;E1BeginFrame();E1Scene().Begin(&s,s.world);if(FAILED(s.renderer.begin_engine_frame()))return fail(e,E_FAIL,"begin_engine_frame");
 if(!owned::AtamanReadiness::testFrame(s.renderer,s.frameCamera.test_frame))return fail(e,E_INVALIDARG,"diagnostic native frame clock must be monotonic");
 E1WorldFrame wf;wf.camera.valid=true;wf.camera.world_to_clip=s.explicitClip?s.clip:camera_matrix(s.camera,float(s.width)/s.height);
 std::string inputFailure;
 if(!s.game.frame(s.frameCamera,wf,inputFailure))return fail(e,E_FAIL,inputFailure.c_str());
 E1WorldDraw draw(&s,s.world,wf);
 E1ClaimSky((s.frameCamera.visuals&1)!=0&&!wf.environment.cave);
 std::vector<owned::RetainedWorldPointLight> lights;
 if(s.game.setting(L"Terrain Settings",L"EnableDynamicLights")!=0)for(uint32_t index=0;index<s.frameCamera.point_light_count;++index){const auto& in=s.frameCamera.point_lights[index];owned::RetainedWorldPointLight l;l.source_id=int32_t(in.source_id);std::copy_n(in.position,3,l.center.begin());std::copy_n(in.color,3,l.color.begin());l.radius=in.radius;l.casts_shadow=in.casts_shadow!=0;lights.push_back(l);}
 const char* lightFailure=nullptr;if(!E1Scene().Scene().visual_publish_world_point_lights(std::move(lights),&lightFailure))return fail(e,E_INVALIDARG,lightFailure?lightFailure:"point light publication");
 if(!s.materialsPublished&&s.map){s.trace.phase("material_texture_ingress_begin");
  for(const auto& texture:s.map->textures)if(texture.second){const HRESULT hr=owned::AtamanReadiness::material(s.renderer,texture.first,*texture.second);if(FAILED(hr))return fail(e,hr,"world material texture ingress failed");}
  s.materialsPublished=true;s.trace.phase("material_texture_ingress_end");
 }
 auto geometry=owned::frozen_owned_terrain_geometry(*s.terrain);geometry.revision=s.generation;
 geometry.animation.precise_time=s.frameCamera.world_time;
 std::copy_n(s.frameCamera.wind_direction,2,geometry.animation.wind_direction);
 geometry.animation.wind_force=s.frameCamera.wind_force;
 // Normalized wind/phase are game terrain runtime state, absent from MOB.
 // Do not label the raw map force as that already-normalized value.
 geometry.animation.waves_enabled=s.game.setting(L"Terrain Settings",L"EnableWaterWaves")!=0;
 if(!E1PublishTerrainFrame(&s,geometry,identity()))return fail(e,E_FAIL,"publish geometry");
 owned::RetainedTerrainSourceSnapshot completed;E1CurrentTerrainSource(completed);auto& retained=E1Scene().Scene();const auto rid=retained.terrain_source_identity();if(rid.source_generation){retained.publish_terrain_geometry(rid.source_generation,rid.source_revision,completed.geometry.get());for(unsigned i=0;i<s.atlasIds.size();++i)if(s.atlasIds[i])retained.publish_terrain_texture(rid.source_generation,rid.source_revision,i,E1SurfaceValue(s.atlasIds[i]));}

 std::vector<E1TerrainSectorValues> sectors;for(unsigned i=0;i<s.terrain->sectors.size();++i){E1TerrainSectorValues v;v.sector=i;v.phases.resize(s.terrain->sectors[i].animation_count,0);sectors.push_back(std::move(v));}
 if(!E1PublishTerrainSectors(&s,sectors))return fail(e,E_FAIL,"publish sectors");
 E1PublishTerrainAtlases(&s,s.atlasIds);owned::OwnedTerrainFrameInputs view;std::copy(s.camera.eye,s.camera.eye+3,view.camera_position.begin());view.object_to_clip=wf.camera.world_to_clip;view.runtime_texture_count=s.terrain->texture_count;view.lod_distances={s.game.setting(L"Terrain Settings",L"LOD1"),s.game.setting(L"Terrain Settings",L"LOD2")};
 if(!E1PublishTerrainView(&s,view))return fail(e,E_FAIL,"publish terrain view");
 owned::NativeOwnedFixedState material;material.enabled=true;material.replay_frame_state=true;material.captured_viewport={0,0,s.width,s.height};material.captured_fog_enable=wf.fog.enabled;material.captured_fog_color=s.game.fogColor;
 material.fill_mode=D3DFILL_SOLID;material.cull_mode=D3DCULL_NONE;material.z_enable=1;material.z_write_enable=1;material.z_func=D3DCMP_LESSEQUAL;material.source_blend=D3DBLEND_ONE;material.destination_blend=D3DBLEND_ZERO;material.alpha_func=D3DCMP_ALWAYS;material.texture_factor=0xffffffff;material.color_op=D3DTOP_MODULATE;material.color_arg1=D3DTA_TEXTURE;material.color_arg2=D3DTA_DIFFUSE;material.alpha_op=D3DTOP_SELECTARG1;material.alpha_arg1=D3DTA_TEXTURE;material.alpha_arg2=D3DTA_DIFFUSE;material.address_u=material.address_v=D3DTADDRESS_WRAP;material.min_filter=material.mag_filter=D3DTFN_LINEAR;material.mip_filter=D3DTFP_LINEAR;
 if(!E1PublishTerrainMaterial(&s,false,material))return fail(e,E_FAIL,"publish base material");
 s.renderer.begin_scene();s.renderer.visit_embedded_device(&s,[](void* context,const owned::EmbeddedDeviceView& v)->HRESULT{auto& session=*static_cast<Session*>(context);float c[]={float((session.game.fogColor>>16)&255)/255.f,float((session.game.fogColor>>8)&255)/255.f,float(session.game.fogColor&255)/255.f,1};v.context->ClearRenderTargetView(v.color,c);v.context->ClearDepthStencilView(v.depth,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);return S_OK;});
 E1TerrainSubmission result;HRESULT hr=S_OK;
 if(s.editorEmptyFootprints&&!E1PublishGroundCoverFootprints({},false))return fail(e,E_FAIL,"static editor footprint snapshot unavailable");
 if(s.objects&&(!s.explicitClip||(s.frameCamera.visuals&16))){owned::FixedState fixed=owned::native_owned_submission_state({},material);E1VisualInputs inputs;inputs.time=wf.environment.world_time;inputs.sceneFlags=0xc0u|((!s.explicitClip||(s.frameCamera.visuals&4))?0x100u:0u);inputs.worldToClip=wf.camera.world_to_clip;inputs.cameraAnchorZ=s.camera.target[2];inputs.ambient=wf.lighting.ambient;inputs.sunDiffuse=wf.lighting.sunlight_diffuse;inputs.sunDirection=wf.lighting.sunlight_direction;inputs.lightingValid=true;inputs.fog=[](void*,const float*)->unsigned char{return 255;};
 if(!E1RunOwnedVisualFrame(s.renderer,fixed,inputs))hr=E_FAIL;
 size_t drawn=0,omitted=0;unsigned shown=0;for(const auto& o:s.map->objects){const auto* why=E1OwnedVisualOmission(int32_t(o.nid));if(why&&shown++<12)std::fprintf(stderr,"omission id=%u reason=%s\n",o.nid,why);}for(const auto& o:s.map->objects){if(E1OwnedVisualDrawn(int32_t(o.nid)))++drawn;else ++omitted;}std::printf("retained drawn=%zu omitted=%zu\n",drawn,omitted);
 }else if(!s.explicitClip||(s.frameCamera.visuals&4))hr=E1SubmitTerrainPass(s.renderer,false,result);std::printf("base queued=%zu completed=%d hr=%08lx failure=%s\n",result.queued,result.completed,hr,result.failure?result.failure:"");std::fflush(stdout);
 if(FAILED(hr))return fail(e,hr,result.failure?result.failure:"base submission");
 if(s.water&&(!s.explicitClip||(s.frameCamera.visuals&8))){material.alpha_blend_enable=1;material.source_blend=D3DBLEND_SRCALPHA;material.destination_blend=D3DBLEND_INVSRCALPHA;material.alpha_op=D3DTOP_SELECTARG2;E1PublishTerrainMaterial(&s,true,material);hr=E1SubmitTerrainPass(s.renderer,true,result);std::printf("water queued=%zu completed=%d hr=%08lx failure=%s\n",result.queued,result.completed,hr,result.failure?result.failure:"");if(FAILED(hr))return fail(e,hr,result.failure?result.failure:"water submission");}
 draw.Complete();s.renderer.end_scene();timing.mark("submit");hr=s.renderer.composite_world();timing.mark("composite");if(FAILED(hr))return fail(e,hr,"composite_world");hr=s.renderer.visit_embedded_output(&s,&commit_output);timing.mark("accepted_present");if(FAILED(hr))return fail(e,hr,"readback failed");hr=s.renderer.present();timing.mark("native_tail");if(FAILED(hr))return fail(e,hr,"hidden HWND present/housekeeping");s.completedReadyEpoch=(!s.pending&&(!s.neighboursRequested||owned::neighbour_client_settled(s.world)))?owned::AtamanReadiness::epoch(s.renderer):~uint64_t(0);return fail(e,0,SUCCEEDED(hr)?"actual owned renderer terrain frame completed":"readback failed");
}
}
int32_t __cdecl probe1b_set_features(void* h,uint32_t bits,probe1b_error* e) noexcept {return guarded(e,[&]{auto& s=*static_cast<Session*>(h);s.water=bits&1;s.environment=bits&2;s.objects=bits&4;if(!s.environment)owned::embedded_sky().configure(false);return fail(e,0,"features");});}
