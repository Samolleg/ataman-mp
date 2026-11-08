// 291: isolated native ownership/depth block. It uses the existing native
// shaders and their ordinary LOD/cutout/neighbour clipping, never flat hit-test.
struct EditorDepthReadback {UINT width=0,height=0;std::vector<uint32_t> words;};
HRESULT read_editor_depth(Session& s,EditorDepthReadback& result) {
 auto* source=owned::AtamanReadiness::editorDepth(s.renderer);if(!source)return E_PENDING;
 D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
 if(d.SampleDesc.Count!=1||(d.Format!=DXGI_FORMAT_R24G8_TYPELESS&&d.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT))return E_NOTIMPL;
 d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.BindFlags=0;d.MiscFlags=0;
 ComPtr<ID3D11Texture2D> copy;auto hr=s.device->CreateTexture2D(&d,nullptr,&copy);if(FAILED(hr))return hr;
 s.context->CopyResource(copy.Get(),source);D3D11_MAPPED_SUBRESOURCE mapped{};
 if(FAILED(hr=s.context->Map(copy.Get(),0,D3D11_MAP_READ,0,&mapped)))return hr;
 result.width=d.Width;result.height=d.Height;result.words.resize(size_t(d.Width)*d.Height);
 for(UINT y=0;y<d.Height;++y)std::memcpy(result.words.data()+size_t(y)*d.Width,static_cast<const char*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(d.Width)*4);
 s.context->Unmap(copy.Get(),0);return S_OK;
}
static uint32_t editor_sample(const EditorDepthReadback& depth,UINT x,UINT y,UINT width,UINT height) {
 const auto px=(std::min)(depth.width-1,UINT((uint64_t(2*x+1)*depth.width)/(2*width)));
 const auto py=(std::min)(depth.height-1,UINT((uint64_t(2*y+1)*depth.height)/(2*height)));
 return depth.words[size_t(py)*depth.width+px];
}
// Called only after this frame's native color/depth are complete, before Present.
HRESULT prepare_service293(Session& s,UINT width,UINT height) {
 if(!s.rasterService293)return S_OK;
 EditorDepthReadback depth;auto hr=read_editor_depth(s,depth);if(FAILED(hr))return hr;
 std::vector<float> pixels(size_t(width)*height);
 // Most frames have identical color/depth extents. Avoid two 64-bit divides
 // per pixel (millions per frame); resampling computes each coordinate once.
 constexpr float inverseDepth=1.f/16777215.f;
 if(width==depth.width&&height==depth.height) {
  for(size_t i=0;i<pixels.size();++i)pixels[i]=float(depth.words[i]&0xffffffu)*inverseDepth;
 }else {
  std::vector<UINT> columns(width);
  for(UINT x=0;x<width;++x)columns[x]=(std::min)(depth.width-1,UINT((uint64_t(2*x+1)*depth.width)/(2*width)));
  for(UINT y=0;y<height;++y){const auto py=(std::min)(depth.height-1,UINT((uint64_t(2*y+1)*depth.height)/(2*height)));
   for(UINT x=0;x<width;++x)pixels[size_t(y)*width+x]=float(depth.words[size_t(py)*depth.width+columns[x]]&0xffffffu)*inverseDepth;
  }
 }
 EirService service{};
 if(!s.rasterService293(s.presentContext,s.generation,&s.frameCamera,width,height,pixels.data(),&service))return S_OK;
 // Reuse the existing validation/copy boundary; poses were applied before draw.
 EirError error{};const auto rc=host_service(&s,&service,&error);return rc?HRESULT(rc):S_OK;
}
int32_t EIR_CALL host_depth(void* handle,EirDepth* out,EirError* error) noexcept {
 probe1b_error e{};
 return eir_error(error,guarded(&e,[&]{
  if(!handle||!out||out->size!=sizeof(*out)||!out->width||!out->height||out->capacity<uint64_t(out->width)*out->height||!out->pixels)return fail(&e,E_INVALIDARG,"native depth span");
  auto& s=*static_cast<Session*>(handle);if(out->generation!=s.readyGeneration)return fail(&e,E_PENDING,"native depth generation");
  EditorDepthReadback depth;auto hr=read_editor_depth(s,depth);if(FAILED(hr))return fail(&e,hr,"native depth readback");
  for(UINT y=0;y<out->height;++y)for(UINT x=0;x<out->width;++x)out->pixels[size_t(y)*out->width+x]=float(editor_sample(depth,x,y,out->width,out->height)&0xffffffu)/16777215.f;
  return 0;
 }),e);
}
int32_t EIR_CALL host_query(void* handle,EirQuery* out,EirError* error) noexcept {
 probe1b_error e{};
 return eir_error(error,guarded(&e,[&]{
  if(!handle||!out||out->size!=sizeof(*out)||!out->width||!out->height||out->width>16384||out->height>16384||out->capacity<uint64_t(out->width)*out->height||!out->pixels)return fail(&e,E_INVALIDARG,"native query span");
  auto& s=*static_cast<Session*>(handle);if(out->generation!=s.readyGeneration)return fail(&e,E_PENDING,"native query generation");
  const auto camera=s.frameCamera;
  struct Restore {Session& s;EirCamera camera;bool water;~Restore(){s.water=water;owned::AtamanReadiness::editorQuery(s.renderer,-1);s.querying=false;EirError ignored{};host_camera(&s,&camera,&ignored);}} restore{s,camera,s.water};
  EirError cameraError{};if(host_camera(&s,&out->camera,&cameraError))return fail(&e,cameraError.code,cameraError.message);
  s.querying=true;
  std::fill_n(out->pixels,size_t(out->width)*out->height,EirPickPixel{0,UINT32_MAX,0,1,1,1});
  // Seven bits are free; the high bit remains the renderer's shadow mark.
  // Five identical native frames retain all 32 ID bits without a new shader.
  for(int byte=0;byte<5;++byte){
   owned::AtamanReadiness::editorQuery(s.renderer,byte);
   const auto rc=map_frame(s,&e);if(rc)return rc;
   EditorDepthReadback depth;auto hr=read_editor_depth(s,depth);if(FAILED(hr))return fail(&e,hr,"native ownership stencil");
   for(UINT y=0;y<out->height;++y)for(UINT x=0;x<out->width;++x){const auto word=editor_sample(depth,x,y,out->width,out->height);auto& p=out->pixels[size_t(y)*out->width+x];p.renderer_id|=((word>>24)&0x7fu)<<(7*byte);p.depth=float(word&0xffffffu)/16777215.f;}
  }
  const auto visuals=s.frameCamera.visuals;const auto water=s.water;
  s.frameCamera.visuals=(visuals|4u)&~8u;s.water=false;
  owned::AtamanReadiness::editorQuery(s.renderer,-1,true);
  const auto rc=map_frame(s,&e);if(rc)return rc;
  EditorDepthReadback terrain;auto hr=read_editor_depth(s,terrain);if(FAILED(hr))return fail(&e,hr,"native terrain depth");
  s.frameCamera.visuals=(visuals|8u)&~4u;s.water=true;
  const auto overlayRc=map_frame(s,&e);s.water=water;s.frameCamera.visuals=visuals;if(overlayRc)return overlayRc;
  EditorDepthReadback overlay;hr=read_editor_depth(s,overlay);if(FAILED(hr))return fail(&e,hr,"native overlay depth");
  std::unordered_map<unsigned,std::pair<unsigned,unsigned>> identities;
  for(const auto& seed:s.editorSeeds)identities[unsigned(seed.rendererId)]={seed.mob,seed.nid};
  for(UINT y=0;y<out->height;++y)for(UINT x=0;x<out->width;++x){auto& p=out->pixels[size_t(y)*out->width+x];p.terrain_depth=float(editor_sample(terrain,x,y,out->width,out->height)&0xffffffu)/16777215.f;p.overlay_depth=float(editor_sample(overlay,x,y,out->width,out->height)&0xffffffu)/16777215.f;
   const auto found=identities.find(p.renderer_id);if(found!=identities.end()){p.mob=found->second.first;p.nid=found->second.second;}
   else if(p.renderer_id)p.nid=owned::AtamanReadiness::editorNeighbour(s.renderer,p.renderer_id).first;
  }
  size_t zero=0,ground=0,overlayHits=0,objects=0,neighbourHits=0;float terrainMin=1,terrainMax=0;
  for(size_t i=0;i<size_t(out->width)*out->height;++i){const auto& p=out->pixels[i];zero+=!p.renderer_id;ground+=p.terrain_depth<1;overlayHits+=p.overlay_depth<1;objects+=p.mob!=UINT32_MAX;neighbourHits+=p.mob==UINT32_MAX&&p.nid;terrainMin=(std::min)(terrainMin,p.terrain_depth);terrainMax=(std::max)(terrainMax,p.terrain_depth);}
  std::fprintf(stderr,"NATIVE_QUERY_STATS zero=%zu terrain=%zu overlay=%zu objects=%zu neighbour=%zu terrain_min=%.9f terrain_max=%.9f\n",zero,ground,overlayHits,objects,neighbourHits,terrainMin,terrainMax);
  std::fprintf(stderr,"NATIVE_QUERY generation=%llu size=%ux%u five_stencil_planes=1 terrain_depth=1 shaders_unchanged=1\n",static_cast<unsigned long long>(s.generation),out->width,out->height);
  return 0;
 }),e);
}
