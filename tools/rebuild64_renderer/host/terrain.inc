namespace OwnedRender { bool E1PublishTerrainSnapshot(const void*,std::uint64_t,std::uint64_t,std::shared_ptr<owned::TerrainCpuAsset>) noexcept; }
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
bool upload_atlases(Session& s,std::string& why){
 s.atlases.clear();s.atlasIds.clear();owned::ResourceFailure error;
 for(size_t index=0;index<s.terrain->texture_resources.size();++index){const auto& name=s.terrain->texture_resources[index];
  owned::ResourceBlob blob;owned::TextureCpuAsset tex;
  if(!s.store.resolve(name+".mmp",blob,error)||!owned::parse_texture_asset(blob,tex,error)){if(index>=s.terrain->texture_count){s.atlasIds.push_back(0);std::printf("optional_atlas_absent=%s\n",name.c_str());continue;}why="atlas "+name+": "+error.message;return false;}
  DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;unsigned block=0,pixel=0;
  switch(tex.format){case owned::MmpFormat::dxt1:format=DXGI_FORMAT_BC1_UNORM;block=8;break;case owned::MmpFormat::dxt3:format=DXGI_FORMAT_BC2_UNORM;block=16;break;case owned::MmpFormat::dxt5:format=DXGI_FORMAT_BC3_UNORM;block=16;break;case owned::MmpFormat::argb8888:format=DXGI_FORMAT_B8G8R8A8_UNORM;pixel=4;break;default:why="unsupported actual atlas format "+name;return false;}
  D3D11_TEXTURE2D_DESC d{};d.Width=tex.width;d.Height=tex.height;d.MipLevels=UINT(tex.mips.size());d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  std::vector<D3D11_SUBRESOURCE_DATA> data;
  for(const auto& mip:tex.mips){D3D11_SUBRESOURCE_DATA row{};row.pSysMem=tex.source->data()+mip.byte_offset;row.SysMemPitch=block?((mip.width+3)/4)*block:mip.width*pixel;data.push_back(row);}
  ComPtr<ID3D11Texture2D> gpu;auto hr=s.device->CreateTexture2D(&d,data.data(),&gpu);if(FAILED(hr)){why="CreateTexture atlas "+name;return false;}
  auto surface=std::make_unique<OwnedRender::E1Surface>();if(!surface->Complete(gpu.Get())){why="E1Surface complete "+name;return false;}
  s.atlasIds.push_back(surface->Id());s.atlases.push_back(std::move(surface));std::printf("atlas=%s %ux%u mips=%zu\n",name.c_str(),tex.width,tex.height,tex.mips.size());
 }
 return true;
}
int map_frame(Session& s,probe1b_error* e){
 using namespace OwnedRender;E1BeginFrame();E1Scene().Begin(&s,s.world);if(FAILED(s.renderer.begin_engine_frame()))return fail(e,E_FAIL,"begin_engine_frame");
 E1WorldFrame wf;wf.camera.valid=true;wf.camera.world_to_clip=s.explicitClip?s.clip:camera_matrix(s.camera,float(s.width)/s.height);
 wf.environment.valid=true;wf.environment.game_hours=s.hour;wf.environment.hour=float(s.hour);wf.environment.world_time=s.hour*3600;wf.environment.region=owned::fog_region_gipat;
 wf.lightingValid=true;wf.lighting.ambient={129/255.f,135/255.f,125/255.f,1};wf.lighting.sunlight_diffuse={1,1,1,1};const float az=float(s.hour)*6.28318530718f/24,el=(60.f-3.75f*std::abs(float(s.hour)-12.f))*3.14159265359f/180;wf.lighting.sunlight_direction={-std::cos(az)*std::cos(el),-std::sin(az)*std::cos(el),std::sin(el),0};
 wf.fog.valid=true;wf.fog.enabled=s.environment&&(!s.explicitClip||(s.frameCamera.visuals&2));wf.fog.failure=nullptr;wf.fog.fog={90,100,25.5f,2000.f/1999.f,1,-2000.f/1999.f};
 if(s.explicitClip&&s.frameCamera.fog_end>s.frameCamera.fog_start){
  wf.environment.region=int(s.frameCamera.region);wf.environment.cave=s.frameCamera.cave!=0;
  for(unsigned i=0;i<3;++i){wf.lighting.ambient[i]=s.frameCamera.ambient[i];wf.lighting.sunlight_diffuse[i]=s.frameCamera.sun_diffuse[i];wf.lighting.sunlight_direction[i]=s.frameCamera.sun_direction[i];}
  wf.fog.fog.start=s.frameCamera.fog_start;wf.fog.fog.end=s.frameCamera.fog_end;wf.fog.fog.scale=255.f/(wf.fog.fog.end-wf.fog.fog.start);
 }
 E1WorldDraw draw(&s,s.world,wf);
 if(!s.materialsPublished&&s.map){
  for(const auto& texture:s.map->textures)if(texture.second){const HRESULT hr=owned::AtamanReadiness::material(s.renderer,texture.first,*texture.second);if(FAILED(hr))return fail(e,hr,"world material texture ingress failed");}
  s.materialsPublished=true;
 }
 auto geometry=owned::frozen_owned_terrain_geometry(*s.terrain);geometry.revision=s.generation;
 if(!E1PublishTerrainFrame(&s,geometry,identity()))return fail(e,E_FAIL,"publish geometry");
 owned::RetainedTerrainSourceSnapshot completed;E1CurrentTerrainSource(completed);auto& retained=E1Scene().Scene();const auto rid=retained.terrain_source_identity();if(rid.source_generation){retained.publish_terrain_geometry(rid.source_generation,rid.source_revision,completed.geometry.get());for(unsigned i=0;i<s.atlasIds.size();++i)if(s.atlasIds[i])retained.publish_terrain_texture(rid.source_generation,rid.source_revision,i,E1SurfaceValue(s.atlasIds[i]));}

 std::vector<E1TerrainSectorValues> sectors;for(unsigned i=0;i<s.terrain->sectors.size();++i){E1TerrainSectorValues v;v.sector=i;v.phases.resize(s.terrain->sectors[i].animation_count,0);sectors.push_back(std::move(v));}
 if(!E1PublishTerrainSectors(&s,sectors))return fail(e,E_FAIL,"publish sectors");
 E1PublishTerrainAtlases(&s,s.atlasIds);owned::OwnedTerrainFrameInputs view;std::copy(s.camera.eye,s.camera.eye+3,view.camera_position.begin());view.object_to_clip=wf.camera.world_to_clip;view.runtime_texture_count=s.terrain->texture_count;view.lod_distances={10000,20000};view.force_full_detail=true;
 if(!E1PublishTerrainView(&s,view))return fail(e,E_FAIL,"publish terrain view");
 owned::NativeOwnedFixedState material;material.enabled=true;material.replay_frame_state=true;material.captured_viewport={0,0,s.width,s.height};material.captured_fog_enable=wf.fog.enabled;material.captured_fog_color=0x2eb5d9;
 if(s.explicitClip&&s.frameCamera.fog_end>s.frameCamera.fog_start){material.captured_fog_color=0;for(unsigned i=0;i<3;++i)material.captured_fog_color|=uint32_t(std::clamp(s.frameCamera.fog_color[i],0.f,1.f)*255.f)<<(16-8*i);}material.fill_mode=D3DFILL_SOLID;material.cull_mode=D3DCULL_NONE;material.z_enable=1;material.z_write_enable=1;material.z_func=D3DCMP_LESSEQUAL;material.source_blend=D3DBLEND_ONE;material.destination_blend=D3DBLEND_ZERO;material.alpha_func=D3DCMP_ALWAYS;material.texture_factor=0xffffffff;material.color_op=D3DTOP_MODULATE;material.color_arg1=D3DTA_TEXTURE;material.color_arg2=D3DTA_DIFFUSE;material.alpha_op=D3DTOP_SELECTARG1;material.alpha_arg1=D3DTA_TEXTURE;material.alpha_arg2=D3DTA_DIFFUSE;material.address_u=material.address_v=D3DTADDRESS_WRAP;material.min_filter=material.mag_filter=D3DTFN_LINEAR;material.mip_filter=D3DTFP_LINEAR;
 if(!E1PublishTerrainMaterial(&s,false,material))return fail(e,E_FAIL,"publish base material");
 s.renderer.begin_scene();s.renderer.visit_embedded_device(&s,[](void* context,const owned::EmbeddedDeviceView& v)->HRESULT{auto& session=*static_cast<Session*>(context);float c[]={0,0,0,1};if(session.explicitClip)for(unsigned i=0;i<3;++i)c[i]=session.frameCamera.fog_color[i];v.context->ClearRenderTargetView(v.color,c);v.context->ClearDepthStencilView(v.depth,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);return S_OK;});
 E1TerrainSubmission result;HRESULT hr=S_OK;
 if(s.explicitClip&&(s.frameCamera.visuals&1)&&!s.frameCamera.cave){
  hr=load_stock_sky(s.sky,s.store,s.device.Get());if(FAILED(hr))return fail(e,hr,"authored nask0sky/Sky00 unavailable");
  hr=draw_stock_sky(s.sky,s.renderer,s.frameCamera,s.width,s.height);if(FAILED(hr))return fail(e,hr,"authored sky submission");
 }
 if(s.objects&&(!s.explicitClip||(s.frameCamera.visuals&16))){owned::FixedState fixed=owned::native_owned_submission_state({},material);E1VisualInputs inputs;inputs.time=wf.environment.world_time;inputs.sceneFlags=0xc0u|((!s.explicitClip||(s.frameCamera.visuals&4))?0x100u:0u);inputs.worldToClip=wf.camera.world_to_clip;inputs.cameraAnchorZ=s.camera.target[2];inputs.ambient=wf.lighting.ambient;inputs.sunDiffuse=wf.lighting.sunlight_diffuse;inputs.sunDirection=wf.lighting.sunlight_direction;inputs.lightingValid=true;inputs.fog=[](void*,const float*)->unsigned char{return 255;};
 if(!E1RunOwnedVisualFrame(s.renderer,fixed,inputs))hr=E_FAIL;
 size_t drawn=0,omitted=0;unsigned shown=0;for(const auto& o:s.map->objects){const auto* why=E1OwnedVisualOmission(int32_t(o.nid));if(why&&shown++<12)std::fprintf(stderr,"omission id=%u reason=%s\n",o.nid,why);}for(const auto& o:s.map->objects){if(E1OwnedVisualDrawn(int32_t(o.nid)))++drawn;else ++omitted;}std::printf("retained drawn=%zu omitted=%zu\n",drawn,omitted);
 }else if(!s.explicitClip||(s.frameCamera.visuals&4))hr=E1SubmitTerrainPass(s.renderer,false,result);std::printf("base queued=%zu completed=%d hr=%08lx failure=%s\n",result.queued,result.completed,hr,result.failure?result.failure:"");std::fflush(stdout);
 if(FAILED(hr))return fail(e,hr,result.failure?result.failure:"base submission");
 if(s.water&&(!s.explicitClip||(s.frameCamera.visuals&8))){material.alpha_blend_enable=1;material.source_blend=D3DBLEND_SRCALPHA;material.destination_blend=D3DBLEND_INVSRCALPHA;material.alpha_op=D3DTOP_SELECTARG2;E1PublishTerrainMaterial(&s,true,material);hr=E1SubmitTerrainPass(s.renderer,true,result);std::printf("water queued=%zu completed=%d hr=%08lx failure=%s\n",result.queued,result.completed,hr,result.failure?result.failure:"");if(FAILED(hr))return fail(e,hr,result.failure?result.failure:"water submission");}
 draw.Complete();s.renderer.end_scene();hr=s.renderer.composite_world();if(FAILED(hr))return fail(e,hr,"composite_world");hr=s.renderer.visit_embedded_output(&s,&commit_output);if(FAILED(hr))return fail(e,hr,"readback failed");hr=s.renderer.present();if(FAILED(hr))return fail(e,hr,"hidden HWND present/housekeeping");return fail(e,0,SUCCEEDED(hr)?"actual owned renderer terrain frame completed":"readback failed");
}
}
int32_t __cdecl probe1b_set_features(void* h,uint32_t bits,probe1b_error* e) noexcept {return guarded(e,[&]{auto& s=*static_cast<Session*>(h);s.water=bits&1;s.environment=bits&2;s.objects=bits&4;if(!s.environment)owned::embedded_sky().configure(false);owned::scene_core::Config cfg;owned::scene_core::ConfigOption blend;blend.key="terrain_blend";blend.type=uint8_t(owned::RendererOptionType::integer);blend.integer_value=(bits&16)?1:0;cfg.options.push_back(blend);cfg.run(s.renderer);return fail(e,0,"features");});}
