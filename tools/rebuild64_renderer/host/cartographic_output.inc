// 288: cartographic geometry uses existing renderer entry points/programs.
// Beauty terrain/water pixels remain the normal Rebuild64 world output. The
// coverage/relief targets use the existing homogeneous fixed-function shader;
// no shader is added or modified and no GL geometry/context enters this path.
int32_t EIR_CALL host_output(void*,const EirOutputRequest*,EirOutput*,EirError*) noexcept;
namespace carto288 {
owned::FixedFunctionVertex vertex(const std::array<float,16>& clip,float x,float y,float z,
                                 uint32_t color=0xffffffff,float u=0,float v=0) {
 owned::FixedFunctionVertex result{};const float p[]={x,y,z,1};float* out=&result.x;
 for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)out[row]+=clip[row*4+col]*p[col];
 result.diffuse=color;result.specular=0xff000000;result.u=u;result.v=v;return result;
}
owned::FixedState state(UINT width,UINT height,bool color,bool textured) {
 owned::FixedState result;result.viewport={0,0,width,height,0,1};
 result.render[D3DRENDERSTATE_ZENABLE]=TRUE;result.render[D3DRENDERSTATE_ZWRITEENABLE]=TRUE;
 result.render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;result.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_NONE;
 result.render[D3DRENDERSTATE_LIGHTING]=FALSE;result.render[D3DRENDERSTATE_FOGENABLE]=FALSE;
 result.render[D3DRENDERSTATE_ALPHABLENDENABLE]=FALSE;result.render[D3DRENDERSTATE_ALPHATESTENABLE]=textured;
 result.render[D3DRENDERSTATE_ALPHAFUNC]=D3DCMP_GREATER;result.render[D3DRENDERSTATE_ALPHAREF]=25;
 result.render[168 /* D3DRS_COLORWRITEENABLE, retained by the D3D7 state adapter */]=color?15:0;
 result.stage[0][D3DTSS_COLOROP]=D3DTOP_SELECTARG2;result.stage[0][D3DTSS_COLORARG2]=D3DTA_DIFFUSE;
 result.stage[0][D3DTSS_ALPHAOP]=D3DTOP_SELECTARG1;result.stage[0][D3DTSS_ALPHAARG1]=textured?D3DTA_TEXTURE:D3DTA_DIFFUSE;
 result.stage[0][D3DTSS_MINFILTER]=D3DTFN_LINEAR;result.stage[0][D3DTSS_MAGFILTER]=D3DTFG_LINEAR;
 result.stage[0][D3DTSS_MIPFILTER]=D3DTFP_NONE;result.stage[0][D3DTSS_ADDRESSU]=D3DTADDRESS_WRAP;result.stage[0][D3DTSS_ADDRESSV]=D3DTADDRESS_WRAP;
 result.stage[1][D3DTSS_COLOROP]=D3DTOP_DISABLE;return result;
}
HRESULT terrain(Session& session,owned::Renderer& renderer,const owned::TerrainCpuAsset& land,
                const std::array<float,16>& clip,UINT width,UINT height,int kind) {
 // kind 0=base depth, 1=base relief, 2=liquid coverage. The decoded tile's
 // nine exact vertex indices supply full-detail topology, independent of LOD.
 const auto fixed=state(width,height,kind!=0,false);
 constexpr unsigned rim[]={0,1,2,5,8,7,6,3,0};
 std::vector<owned::FixedFunctionVertex> triangles;triangles.reserve(256*24);
 for(const auto& sector:land.sectors) {
  triangles.clear();
  for(const auto& tile:sector.tiles) {
   const bool liquid=tile.transparent&&tile.object_id<land.objects.size()&&owned::terrain_liquid_object(land.objects[tile.object_id].type);
   if(kind==2?!liquid:tile.transparent)continue;
   for(unsigned edge=0;edge<8;++edge)for(unsigned index:{4u,rim[edge],rim[edge+1]}) {
    const auto id=tile.vertex_indices[index];if(id>=sector.vertices.size())return E_INVALIDARG;
    const auto& v=sector.vertices[id];uint32_t color=0xffffffff;
    if(kind==1){const float length=std::max(.001f,std::sqrt(v.normal_x*v.normal_x+v.normal_y*v.normal_y+v.normal_z*v.normal_z));
     const float shade=.35f+.6f*std::max(0.f,(v.normal_x*.35f+v.normal_y*.45f+v.normal_z*.82f)/(length*std::sqrt(.35f*.35f+.45f*.45f+.82f*.82f)));
     const unsigned c=unsigned(std::clamp(shade,0.f,1.f)*255+.5f);color=0xff000000u|(c<<16)|(c<<8)|c;}
    triangles.push_back(vertex(clip,v.x,v.y,v.z,color));
   }
  }
  if(const auto rc=pump_output_display(session,nullptr))return rc;
  if(!triangles.empty()){const auto hr=renderer.draw_embedded_fixed_function(D3DPT_TRIANGLELIST,triangles.data(),DWORD(triangles.size()),nullptr,0,fixed,nullptr);if(FAILED(hr))return hr;}
 }
 return S_OK;
}
HRESULT object_mask(Session& session,owned::Renderer& renderer,const owned::RetainedStaticSourceCandidate& source,
                    const std::array<float,16>& clip,UINT width,UINT height) {
 const auto fixed=state(width,height,true,true);
 for(const auto& node:source.nodes) {
  if(const auto rc=pump_output_display(session,nullptr))return rc;
  const auto& figure=*node.figure;ComPtr<ID3D11ShaderResourceView> texture;
  const auto& name=source.assembly->parts[&node-source.nodes.data()].texture_source_name;
  HRESULT hr=owned::AtamanReadiness::material(renderer,name,*node.texture,&texture);if(FAILED(hr))return hr;
  const auto transform=owned::retained_multiply_matrix(clip,node.object_to_world);
  std::vector<owned::FixedFunctionVertex> vertices;vertices.reserve(figure.components.size());
  for(const auto& component:figure.components) {
   const auto& p=node.positions[component.position>>2];const unsigned lane=component.position&3;
   const auto& uv=figure.texture_coordinates[component.texture];
   vertices.push_back(vertex(transform,p.x[lane],p.y[lane],p.z[lane],0xffffffff,uv.u,uv.v));
  }
  hr=renderer.draw_embedded_fixed_function(D3DPT_TRIANGLELIST,vertices.data(),DWORD(vertices.size()),figure.indices.data(),DWORD(figure.indices.size()),fixed,texture.Get());if(FAILED(hr))return hr;
 }
 return S_OK;
}
HRESULT object_color(owned::Renderer& renderer,const std::shared_ptr<owned::RetainedStaticSourceCandidate>& source,
                     const std::array<float,16>& clip,UINT width,UINT height) {
 std::weak_ptr<const void> owner=source;unsigned ordinal=0;
 for(const auto& node:source->nodes) {
  const auto& figure=*node.figure;owned::NativePreparedMeshHandle mesh;owned::NativePreparedMaterialHandle material;owned::NativeGeometryView view;view.semantic_kind=owned::SemanticKind::figure;view.contribution={true,true,0};view.object_token=source->nid;
  view.positions=node.positions.data();view.position_groups=figure.position_groups;view.normals=node.normals.data();view.normal_groups=figure.normal_groups;
  view.texture_coordinates=figure.texture_coordinates.data();view.texture_coordinate_count=UINT(figure.texture_coordinates.size());view.components=figure.components.data();view.component_count=UINT(figure.components.size());view.indices=figure.indices.data();view.index_count=UINT(figure.indices.size());
  view.interface_preview=true;view.world.enabled=false;view.world.object_space=true;view.world.identity=&figure.identity;view.world.owner=&owner;view.world.part=ordinal++;view.prepared_mesh=&mesh;view.prepared_material=&material;view.prepared_identity={int32_t(source->nid),1,1,1,ordinal-1,0,1,true,false};
  view.world.object_to_world=node.object_to_world;view.world.world_to_clip=clip;view.object_to_clip=owned::retained_multiply_matrix(clip,node.object_to_world);view.lighting.enabled=1;view.lighting.object_to_world=node.object_to_world;
  view.cache_identity=node.geometry_identity;view.geometry_revision=node.geometry_revision;view.geometry_revision_valid=true;
  view.owned_texture.identity=node.texture_identity;view.owned_texture.generation=1;view.owned_texture.value_source=1;view.owned_texture.resource=&node.texture->identity;view.owned_texture.format=node.texture_format;view.owned_texture.mips=node.texture_mips.data();view.owned_texture.mip_count=UINT(node.texture_mips.size());view.owned_texture.opaque_mips=node.texture->opaque_mips;view.owned_texture.material_name=&source->assembly->parts[ordinal-1].texture_source_name;
  auto& fixed=view.owned_fixed_state;fixed.enabled=true;fixed.replay_frame_state=true;fixed.captured_viewport={0,0,width,height};fixed.fill_mode=D3DFILL_SOLID;fixed.cull_mode=D3DCULL_NONE;fixed.z_enable=fixed.z_write_enable=1;fixed.z_func=D3DCMP_LESSEQUAL;fixed.alpha_func=D3DCMP_ALWAYS;fixed.source_blend=D3DBLEND_SRCALPHA;fixed.destination_blend=D3DBLEND_INVSRCALPHA;fixed.alpha_blend_enable=1;fixed.texture_factor=0xffffffff;fixed.color_op=D3DTOP_MODULATE;fixed.color_arg1=D3DTA_TEXTURE;fixed.color_arg2=D3DTA_DIFFUSE;fixed.alpha_op=D3DTOP_SELECTARG1;fixed.alpha_arg1=D3DTA_TEXTURE;fixed.address_u=fixed.address_v=D3DTADDRESS_WRAP;fixed.min_filter=fixed.mag_filter=D3DTFN_LINEAR;fixed.mip_filter=D3DTFP_LINEAR;
  owned::NativeGeometryResult result;const auto hr=renderer.queue_native_owned(view,owned::native_owned_submission_state({},fixed),&result);if(FAILED(hr)){std::fprintf(stderr,"CARTOGRAPHIC failure=%s\n",result.failure_operation?result.failure_operation:"unknown");return hr;}
 }
 return renderer.flush_native();
}
}
int cartographic_output(Session& s,const EirOutputRequest& request,EirOutput& output,probe1b_error* error) {
 const int pass=int(request.preview[7]);
 if(pass<3||pass>8||!s.terrain||!s.map||!request.width||!request.height||request.width>8192||request.height>8192||uint64_t(request.width)*request.height>8388608||request.object_keys.bytes%8||request.object_keys.bytes>8000000||(request.object_keys.bytes&&!request.object_keys.data))return fail(error,E_INVALIDARG,"invalid cartographic output request");
 const UINT savedWidth=s.width,savedHeight=s.height;struct Restore {Session& s;UINT w,h;~Restore(){s.width=w;s.height=h;}} restore{s,savedWidth,savedHeight};
 if(const auto init=ensure_independent_renderer(s,request.width,request.height,error))return init;
 auto& renderer=*s.previewRenderer;auto hr=renderer.resize(request.width,request.height);if(FAILED(hr))return fail(error,hr,"cartographic resize");
 std::array<float,16> clip;std::copy(request.camera.world_to_clip,request.camera.world_to_clip+16,clip.begin());
 std::unordered_set<uint64_t> selected;const auto* keys=static_cast<const uint64_t*>(request.object_keys.data);
 for(uint64_t i=0;i<request.object_keys.bytes/8;++i)selected.insert(keys[i]);
 std::vector<std::shared_ptr<owned::RetainedStaticSourceCandidate>> objects;
 if(pass>=6)for(const auto& seed:s.editorSeeds)if(selected.count((uint64_t(seed.mob)<<32)|seed.nid)) {
  if(const auto rc=pump_output_display(s,error))return rc;
  std::shared_ptr<owned::RetainedStaticSourceCandidate> candidate;owned::RetainedStaticPrepareDiagnostic diagnostic;
  const auto& source=s.map->objects.at(size_t(seed.rendererId-1));
  if(!owned::prepare_retained_static_candidate(s.map,s.terrain,source,candidate,&diagnostic))return fail(error,E_FAIL,owned::retained_static_prepare_result_name(diagnostic.result));
  objects.push_back(std::move(candidate));
 }
 // Coverage is an independent target. Existing fixed-function alpha testing
 // retains authored cutouts; the renderer's zone-ID shader would ignore them.
 if(FAILED(hr=renderer.begin_embedded_output_frame()))return fail(error,hr,"independent cartographic frame begin");renderer.hit_test_frame(true);
 struct MaskEnd {owned::Renderer& r;~MaskEnd(){r.hit_test_frame(false);}} maskEnd{renderer};
 hr=carto288::terrain(s,renderer,*s.terrain,clip,request.width,request.height,pass==3?1:0);if(FAILED(hr))return fail(error,hr,"cartographic terrain depth/relief");
 if(pass!=3)renderer.visit_embedded_device(nullptr,[](void*,const owned::EmbeddedDeviceView& v)->HRESULT {const float black[]={0,0,0,0};v.context->ClearRenderTargetView(v.color,black);return S_OK;});
 if(pass==4||pass==5)hr=carto288::terrain(s,renderer,*s.terrain,clip,request.width,request.height,2);
 else if(pass>=6)for(const auto& object:objects){hr=carto288::object_mask(s,renderer,*object,clip,request.width,request.height);if(FAILED(hr))break;}
 if(FAILED(hr))return fail(error,hr,"cartographic coverage draw");
 hr=renderer.visit_embedded_output(&s,[](void* context,ID3D11DeviceContext* dc,ID3D11Texture2D* texture)->HRESULT{return read_output(context,dc,texture);});
 if(FAILED(hr))return fail(error,hr,"cartographic coverage readback");
 if(s.pixelWidth!=request.width||s.pixelHeight!=request.height||s.pixels.size()!=uint64_t(request.width)*request.height*4)return fail(error,E_UNEXPECTED,"coverage GPU target extent mismatch");
 const auto coverage=s.pixels;renderer.hit_test_frame(false);renderer.end_scene();
 const bool mask=pass==5||pass==7;
 if(pass==4) {
  EirOutputRequest beauty=request;beauty.mode=2;beauty.camera.visuals=4|8;EirError detail{};
  const auto rc=host_output(&s,&beauty,&output,&detail);if(rc)return fail(error,rc,detail.message);
  s.pixels.assign(static_cast<const uint8_t*>(output.bgra),static_cast<const uint8_t*>(output.bgra)+uint64_t(request.width)*request.height*4);
 } else if(pass==6||pass==8) {
  if(FAILED(hr=renderer.begin_embedded_output_frame()))return fail(error,hr,"independent cartographic frame begin");
  renderer.visit_embedded_device(nullptr,[](void*,const owned::EmbeddedDeviceView& v)->HRESULT{const float clear[]={0,0,0,0};v.context->ClearRenderTargetView(v.color,clear);v.context->ClearDepthStencilView(v.depth,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);return S_OK;});
  OwnedRender::E1WorldFrame frame;std::string why;if(!s.game.frame(request.camera,frame,why))return fail(error,E_FAIL,why.c_str());
  renderer.set_native_world_lighting(frame.lighting,&frame.environment);
  hr=carto288::terrain(s,renderer,*s.terrain,clip,request.width,request.height,0);
  renderer.visit_embedded_device(nullptr,[](void*,const owned::EmbeddedDeviceView& v)->HRESULT {const float black[]={0,0,0,0};v.context->ClearRenderTargetView(v.color,black);return S_OK;});

  if(FAILED(hr=renderer.composite_world()))return fail(error,hr,"cartographic background composite");
  for(const auto& object:objects){if(FAILED(hr))break;if(const auto rc=pump_output_display(s,error))return rc;hr=carto288::object_color(renderer,object,clip,request.width,request.height);}
  if(FAILED(hr))return fail(error,hr,"cartographic native object color");renderer.end_scene();hr=renderer.flush_native();if(FAILED(hr))return fail(error,hr,"cartographic native composite");
  hr=renderer.visit_embedded_output(&s,[](void* context,ID3D11DeviceContext* dc,ID3D11Texture2D* texture)->HRESULT{return read_output(context,dc,texture);});if(FAILED(hr))return fail(error,hr,"cartographic native color readback");
 }
 if((pass==6||pass==8)&&(s.pixelWidth!=request.width||s.pixelHeight!=request.height||s.pixels.size()!=uint64_t(request.width)*request.height*4))return fail(error,E_UNEXPECTED,"color GPU target extent mismatch");
 const size_t bytes=size_t(request.width)*request.height*4;
 if(!output.bgra||output.capacity<bytes)return fail(error,E_INVALIDARG,"cartographic output buffer too small");
 auto* destination=static_cast<uint8_t*>(output.bgra);
 for(size_t i=0;i<bytes;i+=4) {
  const bool covered=coverage[i]||coverage[i+1]||coverage[i+2];
  for(unsigned c=0;c<3;++c)destination[i+c]=mask?(covered?255:0):(pass==3?coverage[i+c]:covered?s.pixels[i+c]:0);
  destination[i+3]=(pass==3||mask||covered)?255:0;
 }
 output.width=request.width;output.height=request.height;output.stride=request.width*4;output.generation=s.generation;output.serial=request.serial;output.neighbours_accepted=owned::AtamanReadiness::neighbours(s.renderer);output.pending=0;std::snprintf(output.map,sizeof(output.map),"%s",s.terrainName.c_str());
 return 0;
}
