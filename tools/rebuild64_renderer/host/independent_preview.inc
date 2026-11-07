// 288: an independent native renderer and immutable model, never E1Scene/world.
// This block consumes the same public FIG/BON/MMP decoder and native geometry
// submission as Rebuild64. It neither parses engine objects nor changes shaders.
namespace preview288 {
using V=std::array<float,3>;using M=std::array<float,16>;
V subtract(V a,V b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
float dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
V cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
V normal(V a){const float n=std::sqrt(dot(a,a));for(auto& x:a)x/=std::max(.0001f,n);return a;}
M camera(V eye,V target,float aspect,float radius){
 const auto f=normal(subtract(target,eye)),r=normal(cross(f,{0,0,1})),u=cross(r,f);
 const M view{r[0],r[1],r[2],-dot(r,eye),u[0],u[1],u[2],-dot(u,eye),-f[0],-f[1],-f[2],dot(f,eye),0,0,0,1};
 const float nearZ=std::max(.001f,radius*.001f),farZ=std::max(100.f,radius*100),q=1/std::tan(45.f*3.14159265f/360);
 const M projection{q/aspect,0,0,0,0,q,0,0,0,0,farZ/(nearZ-farZ),farZ*nearZ/(nearZ-farZ),0,0,-1,0};
 return owned::retained_multiply_matrix(projection,view);
}
}
int ensure_independent_renderer(Session& s,UINT width,UINT height,probe1b_error* error) {
 if(!s.previewRenderer){
  s.previewWindow.value=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,-32000,-32000,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(!s.previewWindow.value||IsWindowVisible(s.previewWindow.value))return fail(error,E_FAIL,"cannot create private hidden preview target");
  ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;HRESULT hr=s.device.As(&dxgi);if(SUCCEEDED(hr))hr=dxgi->GetAdapter(&adapter);
  const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL actual;
  if(SUCCEEDED(hr))hr=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,levels,1,D3D11_SDK_VERSION,&s.previewDevice,&actual,&s.previewContext);
  if(FAILED(hr))return fail(error,hr,"independent output device on the displayed adapter");
  auto renderer=std::make_unique<owned::Renderer>();hr=renderer->adopt_host_device(s.previewDevice.Get(),s.previewContext.Get(),width,height);
  if(FAILED(hr)||!owned::AtamanReadiness::load(*renderer,s.rendererIni.c_str()))return fail(error,E_FAIL,"preview renderer configuration");
  hr=renderer->ensure_embedded_output(s.previewWindow.value,width,height);if(FAILED(hr))return fail(error,hr,"preview renderer initialization");s.previewRenderer=std::move(renderer);
 }
 return 0;
}
int model_output(Session& s,const EirOutputRequest& request,EirOutput& output,probe1b_error* error) {
 if(!request.model||!request.texture||!request.width||!request.height||request.width>4096||request.height>4096||
    uint64_t(request.width)*request.height>4194304)return fail(error,E_INVALIDARG,"invalid independent preview extent/model/texture");
 if(request.model_bytes.bytes>67108864||request.bone_bytes.bytes>67108864||request.texture_bytes.bytes>67108864||
    (request.model_bytes.bytes&&!request.model_bytes.data)||(request.bone_bytes.bytes&&!request.bone_bytes.data)||(request.texture_bytes.bytes&&!request.texture_bytes.data))return fail(error,E_INVALIDARG,"invalid auxiliary preview bytes");
 uint64_t bytesKey=1469598103934665603ull;for(const auto span:{request.model_bytes,request.bone_bytes,request.texture_bytes,request.visuals}){const auto* b=static_cast<const uint8_t*>(span.data);for(uint64_t i=0;i<span.bytes;++i){bytesKey^=b[i];bytesKey*=1099511628211ull;}}
 const std::string secondary=request.secondary_texture&&*request.secondary_texture?request.secondary_texture:request.texture;
 const std::string key=std::string(request.model)+"/"+request.texture+"/"+secondary+"/"+(request.part_names?request.part_names:"")+"/"+std::to_string(bytesKey)+"/"+std::to_string(request.shape[0])+"/"+std::to_string(request.shape[1])+"/"+std::to_string(request.shape[2]);
 if(!s.previewSource||s.previewKey!=key) {
  owned::ResourceFailure failure;if(!s.store.open(s.mounts,failure))return fail(error,E_FAIL,failure.message.c_str());
  if(request.visuals.bytes) {
   auto plans=units289::read(request.visuals);if(plans.size()!=1)return fail(error,E_INVALIDARG,"preview needs one completed appearance");
   auto plan=plans.begin()->second;plan.action.clear();plan.length=0;
   auto snapshot=std::make_shared<owned::OwnedMapSnapshot>();owned::StaticObjectSource object;object.nid=1;object.family=54;object.quaternion={1,0,0,0};std::copy_n(request.shape,3,object.shape.begin());std::string why;
   if(!units289::prepare(s,*snapshot,object,plan,why))return fail(error,E_FAIL,why.c_str());
   const auto model=snapshot->models.at(plan.model);auto selected=object;selected.part_names.clear();
   for(size_t i=0;i<model->nodes.size();++i)if(owned::retained_static_node_selected(object,*model,i)&&model->nodes[i].transform_available&&!model->nodes[i].figure.indices.empty())selected.part_names.push_back(model->nodes[i].name);
   snapshot->objects.push_back(selected);auto plane=std::make_shared<owned::TerrainCpuAsset>();plane->sector_count_x=plane->sector_count_y=1;plane->sectors.resize(1);plane->sectors[0].vertices.resize(1089);owned::RetainedStaticPrepareDiagnostic diagnostic;
   if(!owned::prepare_retained_static_candidate(snapshot,plane,snapshot->objects.front(),s.previewSource,&diagnostic))return fail(error,E_FAIL,owned::retained_static_prepare_result_name(diagnostic.result));
   auto assembly=std::make_shared<owned::VisualAssemblyAsset>(*s.previewSource->assembly);owned::VisualObjectPose pose;
   if(!units289::complete(s,plan,assembly,pose,why))return fail(error,E_FAIL,why.c_str());s.previewSource->assembly=assembly;
   for(size_t i=0;i<s.previewSource->nodes.size();++i){auto& node=s.previewSource->nodes[i];const auto& texture=assembly->parts[i].texture;node.texture=texture.get();node.texture_mips.clear();
    for(const auto& mip:texture->mips)node.texture_mips.push_back({texture->source->data()+mip.byte_offset,mip.width,mip.height,mip.bytes});
    node.texture_identity=std::hash<std::string>{}(texture->identity.key());
    switch(texture->format){case owned::MmpFormat::dxt1:node.texture_format=owned::NativeOwnedTextureFormat::dxt1;break;case owned::MmpFormat::dxt3:node.texture_format=owned::NativeOwnedTextureFormat::dxt3;break;case owned::MmpFormat::dxt5:node.texture_format=owned::NativeOwnedTextureFormat::dxt5;break;default:node.texture_format=owned::NativeOwnedTextureFormat::argb8888;break;}
   }
  } else {
  auto snapshot=std::make_shared<owned::OwnedMapSnapshot>();auto model=std::make_shared<owned::ModelCpuAsset>();
  if(request.model_bytes.bytes) {
   owned::ResourceBlob modelBlob,boneBlob;
   if(!s.store.copy_selected_memory(std::string(request.model)+".mod",request.model_bytes.data,uint32_t(request.model_bytes.bytes),modelBlob,failure)||
      !s.store.copy_selected_memory(std::string(request.model)+".bon",request.bone_bytes.data,uint32_t(request.bone_bytes.bytes),boneBlob,failure)||
      !owned::decode_model_resources(modelBlob,boneBlob,request.model,*model,failure))return fail(error,E_FAIL,failure.message.c_str());
  } else {
   owned::ResourceBlob mod,bone,fig;const std::string name=request.model;
   if(s.store.resolve(name+".mod",mod,failure)&&s.store.resolve(name+".bon",bone,failure)) {
    // Independent rest-pose view does not traverse/sample an animation archive.
    if(!owned::decode_model_resources(mod,bone,name,*model,failure))return fail(error,E_FAIL,failure.message.c_str());
   } else {
    if(!s.store.resolve(name+".fig",fig,failure))return fail(error,E_FAIL,failure.message.c_str());
    owned::ModelNodeCpuAsset node;node.name=name;node.bone_positions={0,0,0};
    if(!owned::parse_figure_asset(fig,node.figure,failure))return fail(error,E_FAIL,failure.message.c_str());
    model->visual_reference=name;model->model_container=fig.identity;model->bone_container=fig.identity;model->source_bytes=fig.identity.bytes;
    model->static_shape_parameterized=node.figure.shape_count==8;model->nodes.push_back(std::move(node));model->links.push_back({name,"",UINT_MAX});
   }
  }
  snapshot->models.emplace(request.model,model);owned::ResourceBlob blob;auto texture=std::make_shared<owned::TextureCpuAsset>();
  std::string name=request.texture;if(name.size()<4||name.substr(name.size()-4)!=".mmp")name+=".mmp";
  if(request.texture_bytes.bytes) {
   if(!s.store.copy_selected_memory(name,request.texture_bytes.data,uint32_t(request.texture_bytes.bytes),blob,failure))return fail(error,E_FAIL,failure.message.c_str());
  } else if(!s.store.resolve(name,blob,failure))return fail(error,E_FAIL,failure.message.c_str());
  if(!owned::parse_texture_asset(blob,*texture,failure))return fail(error,E_FAIL,failure.message.c_str());
  snapshot->textures.emplace(request.texture,texture);
  if(secondary!=request.texture){owned::ResourceBlob selected;auto asset=std::make_shared<owned::TextureCpuAsset>();std::string member=secondary;if(member.size()<4||member.substr(member.size()-4)!=".mmp")member+=".mmp";
   if(!s.store.resolve(member,selected,failure)||!owned::parse_texture_asset(selected,*asset,failure))return fail(error,E_FAIL,failure.message.c_str());snapshot->textures.emplace(secondary,asset);}
  owned::StaticObjectSource object;object.nid=1;object.family=54;object.visual_reference=request.model;object.primary_texture=request.texture;object.secondary_texture=secondary;object.quaternion={1,0,0,0};
  if(request.part_names){std::string parts=request.part_names;size_t begin=0;while(begin<parts.size()){const auto end=parts.find('\n',begin);object.part_names.push_back(parts.substr(begin,end==std::string::npos?end:end-begin));if(end==std::string::npos)break;begin=end+1;}}
  std::copy(request.shape,request.shape+3,object.shape.begin());snapshot->objects.push_back(object);
  // The source compiler asks for a height even for a detached preview. Supply
  // an explicit zero-height CPU plane; it never becomes a displayed terrain.
  auto plane=std::make_shared<owned::TerrainCpuAsset>();plane->sector_count_x=plane->sector_count_y=1;plane->sectors.resize(1);plane->sectors[0].vertices.resize(1089);
  owned::RetainedStaticPrepareDiagnostic diagnostic;
  if(!owned::prepare_retained_static_candidate(snapshot,plane,snapshot->objects.front(),s.previewSource,&diagnostic))
   return fail(error,E_FAIL,owned::retained_static_prepare_result_name(diagnostic.result));
  }
  s.previewKey=key;
 }
 if(const auto rc=pump_output_display(s,error))return rc;
 if(const auto init=ensure_independent_renderer(s,request.width,request.height,error))return init;
 const UINT savedWidth=s.width,savedHeight=s.height;struct ExtentRestore{Session& s;UINT w,h;~ExtentRestore(){s.width=w;s.height=h;}} extentRestore{s,savedWidth,savedHeight};
 auto& renderer=*s.previewRenderer;HRESULT hr=renderer.resize(request.width,request.height);if(FAILED(hr))return fail(error,hr,"preview resize");
 std::array<float,4> baseQuaternion{request.preview[7],request.preview[8],request.preview[9],request.preview[10]};
 float quaternionLength=0;for(float value:baseQuaternion)quaternionLength+=value*value;
 if(quaternionLength<1.e-10f)baseQuaternion={1,0,0,0};else for(float& value:baseQuaternion)value/=std::sqrt(quaternionLength);
 preview288::M baseRotation;owned::retained_object_matrix({0,0,0},baseQuaternion,baseRotation);
 preview288::V minimum{INFINITY,INFINITY,INFINITY},maximum{-INFINITY,-INFINITY,-INFINITY};
 for(const auto& node:s.previewSource->nodes)for(unsigned corner=0;corner<8;++corner){
  float point[3];for(unsigned a=0;a<3;++a)point[a]=node.bounds.center[a]+((corner&(1u<<a))?node.bounds.maximum[a]:node.bounds.minimum[a])+node.object_local[a*4+3];
  for(unsigned a=0;a<3;++a){float value=baseRotation[a*4+3];for(unsigned b=0;b<3;++b)value+=baseRotation[a*4+b]*point[b];minimum[a]=std::min(minimum[a],value);maximum[a]=std::max(maximum[a],value);}
 }
 preview288::V center{};for(unsigned a=0;a<3;++a)center[a]=(minimum[a]+maximum[a])*.5f;
 const float radius=std::max(.1f,std::sqrt(preview288::dot(preview288::subtract(maximum,minimum),preview288::subtract(maximum,minimum)))*.5f);
 const float azimuth=(45+request.preview[2])*3.14159265f/180,elevation=(25+request.preview[3])*3.14159265f/180,distance=radius*3.2f/std::max(.1f,request.preview[4]);
 center[0]+=request.preview[5]*radius;center[2]+=request.preview[6]*radius;
 const preview288::V eye{center[0]+distance*std::cos(azimuth)*std::cos(elevation),center[1]+distance*std::sin(azimuth)*std::cos(elevation),center[2]+distance*std::sin(elevation)};
 const auto clip=preview288::camera(eye,center,float(request.width)/request.height,radius);
 OwnedRender::E1WorldFrame frame;frame.camera.valid=true;frame.camera.world_to_clip=clip;std::string why;auto environment=request.camera;if(!environment.region){environment.region=1;environment.hour=12;}environment.visuals=0;
 if(!s.game.frame(environment,frame,why))return fail(error,E_FAIL,why.c_str());
 hr=renderer.begin_embedded_output_frame();if(FAILED(hr))return fail(error,hr,"independent frame begin");renderer.set_native_world_lighting(frame.lighting,&frame.environment);
 renderer.visit_embedded_device(nullptr,[](void*,const owned::EmbeddedDeviceView& v)->HRESULT {const float clear[]={.16f,.16f,.16f,1};v.context->ClearRenderTargetView(v.color,clear);v.context->ClearDepthStencilView(v.depth,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);return S_OK;});
 hr=renderer.composite_world();if(FAILED(hr))return fail(error,hr,"preview background composite");
 const float pitch=request.preview[0]*3.14159265f/360,yaw=request.preview[1]*3.14159265f/360;
 const std::array<float,4> quaternion{std::cos(yaw)*std::cos(pitch),std::cos(yaw)*std::sin(pitch),std::sin(yaw)*std::sin(pitch),std::sin(yaw)*std::cos(pitch)};
 preview288::M rotation;owned::retained_object_matrix({0,0,0},quaternion,rotation);
 for(unsigned a=0;a<3;++a){const float pivot=(minimum[a]+maximum[a])*.5f;rotation[a*4+3]=pivot;
  for(unsigned b=0;b<3;++b)rotation[a*4+3]-=rotation[a*4+b]*(minimum[b]+maximum[b])*.5f;}

 std::vector<owned::NativePreparedMeshHandle> meshes(s.previewSource->nodes.size());std::vector<owned::NativePreparedMaterialHandle> materials(s.previewSource->nodes.size());
 std::weak_ptr<const void> geometryOwner=s.previewSource;unsigned partOrdinal=0;
 for(const auto& node:s.previewSource->nodes){
  if(const auto rc=pump_output_display(s,error))return rc;
  const auto& figure=*node.figure;owned::NativeGeometryView view;view.interface_preview=true;view.contribution={true,true,0};
  view.positions=node.positions.data();view.position_groups=figure.position_groups;view.normals=node.normals.data();view.normal_groups=figure.normal_groups;
  view.texture_coordinates=figure.texture_coordinates.data();view.texture_coordinate_count=UINT(figure.texture_coordinates.size());view.components=figure.components.data();view.component_count=UINT(figure.components.size());view.indices=figure.indices.data();view.index_count=UINT(figure.indices.size());
  view.semantic_kind=owned::SemanticKind::figure;view.world.enabled=false;view.world.object_space=true;view.world.identity=&figure.identity;view.world.owner=&geometryOwner;view.world.part=partOrdinal++;view.prepared_mesh=&meshes[partOrdinal-1];view.prepared_material=&materials[partOrdinal-1];view.prepared_identity={1,1,1,1,partOrdinal-1,0,request.serial?request.serial:1,true,false};view.world.object_to_world=owned::retained_multiply_matrix(rotation,owned::retained_multiply_matrix(baseRotation,node.object_local));view.world.world_to_clip=clip;view.object_to_clip=owned::retained_multiply_matrix(clip,view.world.object_to_world);view.lighting.enabled=1;view.lighting.object_to_world=view.world.object_to_world;
  view.cache_identity=node.geometry_identity;view.geometry_revision=node.geometry_revision;view.geometry_revision_valid=true;
  view.owned_texture.identity=node.texture_identity;view.owned_texture.generation=1;view.owned_texture.value_source=1;view.owned_texture.resource=&node.texture->identity;view.owned_texture.format=node.texture_format;view.owned_texture.mips=node.texture_mips.data();view.owned_texture.mip_count=UINT(node.texture_mips.size());view.owned_texture.opaque_mips=node.texture->opaque_mips;view.owned_texture.material_name=&s.previewSource->assembly->parts[partOrdinal-1].texture_source_name;
  auto& fixed=view.owned_fixed_state;fixed.enabled=true;fixed.replay_frame_state=true;fixed.captured_viewport={0,0,request.width,request.height};fixed.fill_mode=D3DFILL_SOLID;fixed.cull_mode=D3DCULL_NONE;fixed.z_enable=fixed.z_write_enable=1;fixed.z_func=D3DCMP_LESSEQUAL;fixed.alpha_func=D3DCMP_ALWAYS;fixed.source_blend=D3DBLEND_SRCALPHA;fixed.destination_blend=D3DBLEND_INVSRCALPHA;fixed.alpha_blend_enable=1;fixed.texture_factor=0xffffffff;fixed.color_op=D3DTOP_MODULATE;fixed.color_arg1=D3DTA_TEXTURE;fixed.color_arg2=D3DTA_DIFFUSE;fixed.alpha_op=D3DTOP_SELECTARG1;fixed.alpha_arg1=D3DTA_TEXTURE;fixed.address_u=fixed.address_v=D3DTADDRESS_WRAP;fixed.min_filter=fixed.mag_filter=D3DTFN_LINEAR;fixed.mip_filter=D3DTFP_LINEAR;
  owned::NativeGeometryResult result;hr=renderer.queue_native_owned(view,owned::native_owned_submission_state({},fixed),&result);if(FAILED(hr)){std::fprintf(stderr,"PREVIEW_QUEUE hr=%08lx operation=%s\n",hr,result.failure_operation?result.failure_operation:"unknown");return fail(error,hr,result.failure_operation?result.failure_operation:"independent native figure submission");}
 }
 renderer.end_scene();hr=renderer.flush_native();if(FAILED(hr))return fail(error,hr,"preview composite");
 hr=renderer.visit_embedded_output(&s,[](void* context,ID3D11DeviceContext* dc,ID3D11Texture2D* texture)->HRESULT {return read_output(context,dc,texture);});
 if(FAILED(hr))return fail(error,hr,"preview memory readback");
 std::fprintf(stderr,"PREVIEW_EXTENT requested=%ux%u actual=%ux%u bytes=%zu capacity=%llu\n",request.width,request.height,s.pixelWidth,s.pixelHeight,s.pixels.size(),(unsigned long long)output.capacity);
 if(s.pixelWidth!=request.width||s.pixelHeight!=request.height||s.pixels.size()!=uint64_t(request.width)*request.height*4||s.pixels.size()>output.capacity)return fail(error,E_UNEXPECTED,"preview GPU target extent differs from requested output; copy refused");
 output.width=request.width;output.height=request.height;output.stride=request.width*4;
 if(!output.bgra||output.capacity<uint64_t(output.stride)*output.height)return fail(error,E_INVALIDARG,"preview output buffer too small");
 std::memcpy(output.bgra,s.pixels.data(),s.pixels.size());output.generation=request.generation;output.serial=request.serial;output.neighbours_accepted=s.map?owned::AtamanReadiness::neighbours(s.renderer):0;output.pending=0;std::snprintf(output.map,sizeof(output.map),"%s",s.terrainName.c_str());

 return 0;
}
