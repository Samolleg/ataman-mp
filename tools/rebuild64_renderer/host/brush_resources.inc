// Functional invalidation only. Uses unchanged renderer equations/resources.
static HRESULT brushResources(Renderer& r,const std::shared_ptr<TerrainCpuAsset>& terrain,
 const std::shared_ptr<const OwnedMapSnapshot>& map,uint32_t flags,uint32_t x0,uint32_t y0,uint32_t x1,uint32_t y1) {
 const bool boundary=!x0||!y0||x1==terrain->sector_count_x||y1==terrain->sector_count_y;
  if(flags&5){
    auto& n=r.neighbours_;if(n.brush_rectangle_valid){n.brush_rectangle[0]=std::min(n.brush_rectangle[0],x0);n.brush_rectangle[1]=std::min(n.brush_rectangle[1],y0);n.brush_rectangle[2]=std::max(n.brush_rectangle[2],x1);n.brush_rectangle[3]=std::max(n.brush_rectangle[3],y1);}
    else {n.brush_rectangle={x0,y0,x1,y1};n.brush_rectangle_valid=true;}}
  if((flags&5)&&((flags&8)||boundary)) {if(r.neighbours_.brush_work)r.neighbours_.brush_work->cancel->store(true);r.neighbours_.brush_terrain=terrain;r.neighbours_.brush_flags=flags;}
 // Ambient generation is a height-derived lifetime. The tile art, atlas
 // slices and baked per-material heights are unchanged by sculpt/water.
  if((flags&8)&&r.contact_maps_.tiles&&r.tile_blend_ready_&&
    r.contact_map_generation_==r.ambient_map_generation_&&r.tile_families_generation_==r.ambient_map_generation_){
  const auto next_generation=r.ambient_map_generation_+1;
  r.contact_map_generation_=r.tile_families_generation_=next_generation;
  r.contact_constants_generation_=UINT64_MAX;r.contact_update_frame_=UINT64_MAX;
   r.tile_blend_ready_=r.tile_blend_rows_=false; // new input invalidates the rows even when the perimeter did not change
  r.tile_families_prestart_=TileFamiliesJob{};r.tile_families_prestart_terrain_=terrain;
  std::fprintf(stderr,"BRUSH_CONTACT_RETAIN ambient=%llu next=%llu atlas=%p material_height=%p baked=%zu\n",r.ambient_map_generation_,next_generation,r.contact_maps_.atlas.Get(),r.terrain2_map_.height_srv.Get(),r.terrain2_map_.baked.size());
  return S_OK;
 }
 // Heights preserve every atlas slot, family signature and placement.
 // A completed stroke still rebuilds height-dependent maps, but must not
 // put their publication behind a new background read of unchanged families.
 if((flags&8)&&(flags&7)==1 && r.tile_blend_ready_ &&
    r.tile_families_generation_==r.ambient_map_generation_ &&
    r.tile_families_.signatures.size()==size_t(terrain->texture_count)*terrain->tiles_per_texture) {
  std::promise<TerrainTileFamilies> cached;
  auto result=cached.get_future();cached.set_value(r.tile_families_);
  r.tile_families_prestart_=TileFamiliesJob(std::move(result),{});
  r.tile_families_prestart_terrain_=terrain;
  std::fprintf(stderr,"BRUSH_FAMILIES reuse slots=%zu ambient=%llu flags=%u\n",r.tile_families_.signatures.size(),r.ambient_map_generation_,flags);
 }
 if(flags&8)return S_OK; // stroke end: complete height-derived publication
 if(r.ambient_terrain_.expired()||!r.ambient_geometry_)return S_OK;
 ambient::TerrainGrids grids;
  std::array<UINT,4> dirty{0,0,terrain->sector_count_x*16,terrain->sector_count_y*16};
  const bool local_grids=brushGrids(r,*terrain,map.get(),x0,y0,x1,y1,grids,dirty);
  if(!local_grids&&!ambient::build_terrain_grids(*terrain,map.get(),grids,r.ambient_geometry_.get()))return E_FAIL;
 const auto upload=[&](ComPtr<ID3D11ShaderResourceView>& view,UINT w,UINT h,DXGI_FORMAT format,const void* data,UINT pitch)->HRESULT {
   const bool vertices=w==terrain->sector_count_x*32+1;
   const UINT pixel=vertices?4:1;
   const D3D11_BOX box{dirty[0]*(vertices?2u:1u),dirty[1]*(vertices?2u:1u),0,dirty[2]*(vertices?2u:1u)+(vertices?1u:0u),dirty[3]*(vertices?2u:1u)+(vertices?1u:0u),1};
   const auto update=[&](ID3D11Texture2D* texture){r.context_->UpdateSubresource(texture,0,local_grids?&box:nullptr,static_cast<const uint8_t*>(data)+(local_grids?size_t(box.top)*pitch+box.left*pixel:0),pitch,0);};
  if(view){ComPtr<ID3D11Resource> resource;view->GetResource(&resource);ComPtr<ID3D11Texture2D> existing;
   if(SUCCEEDED(resource.As(&existing))){D3D11_TEXTURE2D_DESC desc{};existing->GetDesc(&desc);
     if(desc.Width==w&&desc.Height==h&&desc.Format==format){
      if(desc.Usage==D3D11_USAGE_DEFAULT){update(existing.Get());return S_OK;}
      desc.Usage=D3D11_USAGE_DEFAULT;ComPtr<ID3D11Texture2D> next;ComPtr<ID3D11ShaderResourceView> srv;
      auto hr=brush_count_device(r.device_).CreateTexture2D(&desc,nullptr,&next);if(FAILED(hr))return hr;
      if(FAILED(hr=brush_count_device(r.device_).CreateShaderResourceView(next.Get(),nullptr,&srv)))return hr;
      r.context_->CopyResource(next.Get(),existing.Get());update(next.Get());view=std::move(srv);return S_OK;
     }
   }
  }
  D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;
  d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA bytes{data,pitch,0};ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> next;
  auto hr=brush_count_device(r.device_).CreateTexture2D(&d,&bytes,&texture);if(FAILED(hr))return hr;
  hr=brush_count_device(r.device_).CreateShaderResourceView(texture.Get(),nullptr,&next);if(SUCCEEDED(hr))view=std::move(next);return hr;
 };
 const UINT tx=grids.tiles_x,ty=grids.tiles_y,vx=tx*2+1,vy=ty*2+1;HRESULT hr=S_OK;
 if(flags&5){
  if(FAILED(hr=upload(r.ambient_height_srv_,vx,vy,DXGI_FORMAT_R32_FLOAT,grids.height.data(),vx*4)))return hr;
 }
 if(flags&1){
  if(FAILED(hr=upload(r.ambient_ground_srv_,vx,vy,DXGI_FORMAT_R32_FLOAT,grids.ground.data(),vx*4)))return hr;
  r.build_native_contact_cells(grids.ground,vx,vy);
 }
 if(FAILED(hr=upload(r.ambient_area_srv_,tx,ty,DXGI_FORMAT_R8_UINT,grids.area.data(),tx)))return hr;
 if(flags&2){
  if(FAILED(hr=upload(r.ambient_type_srv_,tx,ty,DXGI_FORMAT_R8_UINT,grids.types.data(),tx)))return hr;
 }
 r.ambient_area_=std::move(grids.area);r.ambient_height_=std::move(grids.height);r.ambient_liquid_level_=std::move(grids.level);
 r.emissive_tiles_=std::move(grids.emissive);++r.ambient_liquid_level_updates_;
 r.ambient_terrain_=terrain;r.ambient_source_map_=map;
 if(flags&2){
   const auto local=brushMaterials(r,*terrain,x0,y0,x1,y1);
   if(local!=S_FALSE)return local;
  uint64_t used=0;r.contact_tile_rows_=owned_terrain_contact_tiles(*terrain,tx,ty,used);
  r.contact_maps_.pending|=used&~r.contact_maps_.ready;
  r.tile_blend_rows_=false; // input changed even when perimeter families match
  hr=r.apply_terrain_tile_blend(r.tile_families_);if(FAILED(hr))return hr;
 }
 return S_OK;
}
