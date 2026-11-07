// Local contact publication uses the preceding complete global donor/fold
// solution. Stroke end resolves those global statistics with native code.
static HRESULT brushMaterials(Renderer& r,const TerrainCpuAsset& terrain,
 uint32_t x0,uint32_t y0,uint32_t x1,uint32_t y1) {
 if(!r.tile_blend_input_||!r.tile_blend_rows_||!r.tile_blend_own_edges_||!r.contact_maps_.tiles)return S_FALSE;
 const UINT w=r.contact_tiles_x_,h=r.contact_tiles_y_;
 TerrainCpuAsset selected=terrain;selected.sectors.clear();
 for(uint32_t y=y0;y<y1;++y)for(uint32_t x=x0;x<x1;++x)selected.sectors.push_back(terrain.sectors[size_t(y)*terrain.sector_count_x+x]);
 auto update=owned_terrain_tile_blend_input(selected,w,h);
 auto merged=std::make_shared<TerrainTileBlendInput>(*r.tile_blend_input_);
 for(UINT y=y0*16;y<y1*16;++y)for(UINT x=x0*16;x<x1*16;++x){const size_t i=size_t(y)*w+x;
  merged->slot[i]=update.slot[i];merged->lanes[i]=update.lanes[i];merged->wet[i]=update.wet[i];
  merged->steep[i]=update.steep[i];merged->heights[i]=update.heights[i];merged->underwater[i]=update.underwater[i];
 }
 merged->usage.assign(merged->slots,0);
 for(const auto slot:merged->slot)if(slot<merged->slots)++merged->usage[slot];
 for(UINT s=0;s<merged->slots;++s)merged->usable[s]=merged->usage[s]>0&&s/64u<contact_blend_max_materials&&!terrain.tile_animation_phases[s];
 const UINT left=x0*16>16?x0*16-16:0,top=y0*16>16?y0*16-16:0;
 const UINT right=std::min(w,x1*16+16),bottom=std::min(h,y1*16+16);
 TerrainTileBlendInput crop;crop.tiles_x=right-left;crop.tiles_y=bottom-top;crop.slots=merged->slots;
 crop.usage=merged->usage;crop.usable=merged->usable;
 for(UINT y=top;y<bottom;++y)for(UINT x=left;x<right;++x){const size_t i=size_t(y)*w+x;
  crop.slot.push_back(merged->slot[i]);crop.lanes.push_back(merged->lanes[i]);crop.wet.push_back(merged->wet[i]);
  crop.steep.push_back(merged->steep[i]);crop.heights.push_back(merged->heights[i]);crop.underwater.push_back(merged->underwater[i]);
 }
 size_t blended=0,plain=0,bed=0;std::set<uint32_t> unmatched,borrowed;std::vector<uint32_t> fold;
 const auto rows=owned_terrain_tile_blend_rows(crop,r.tile_families_,{},blended,plain,bed,unmatched,fold,borrowed,nullptr,true,&r.contact_maps_.cells);
 uint64_t used=0;const auto contacts=owned_terrain_contact_tiles(selected,w,h,used);
 for(UINT y=y0*16;y<y1*16;++y)for(UINT x=x0*16;x<x1*16;++x)r.contact_tile_rows_[size_t(y)*w+x]=contacts[size_t(y)*w+x];
 r.contact_maps_.pending|=used&~r.contact_maps_.ready;
 ComPtr<ID3D11Resource> resource;r.contact_maps_.tiles->GetResource(&resource);
 ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return E_FAIL;
 D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
 if(desc.Usage!=D3D11_USAGE_DEFAULT){
  desc.Usage=D3D11_USAGE_DEFAULT;ComPtr<ID3D11Texture2D> next;ComPtr<ID3D11ShaderResourceView> srv;
  HRESULT hr=brush_count_device(r.device_).CreateTexture2D(&desc,nullptr,&next);if(FAILED(hr))return hr;
  if(FAILED(hr=brush_count_device(r.device_).CreateShaderResourceView(next.Get(),nullptr,&srv)))return hr;
  r.context_->CopyResource(next.Get(),texture.Get());texture=std::move(next);r.contact_maps_.tiles=std::move(srv);
 }
 D3D11_BOX base{x0*16,y0*16,0,x1*16,y1*16,1};
 r.context_->UpdateSubresource(texture.Get(),0,&base,r.contact_tile_rows_.data()+size_t(base.top)*w+base.left,w*16,0);
 const UINT xl=x0*16>8?x0*16-8:0,yt=y0*16>8?y0*16-8:0,xr=std::min(w,x1*16+8),yb=std::min(h,y1*16+8);
 D3D11_BOX blend{xl,h+yt,0,xr,h+yb,1};
 r.context_->UpdateSubresource(texture.Get(),0,&blend,rows.data()+size_t(yt-top)*crop.tiles_x+xl-left,crop.tiles_x*16,0);
 r.contact_maps_.tiles_serial=++r.terrain2_tiles_serials_;
 auto old_bakes=r.terrain2_map_.bake_cells;const auto old_styles=r.terrain2_map_.styles;
 r.update_terrain2_styles(r.terrain2_map_,r.tile_families_,r.contact_maps_.cells,rows,{});
 for(const auto& cell:old_bakes)if(std::none_of(r.terrain2_map_.bake_cells.begin(),r.terrain2_map_.bake_cells.end(),[&](const auto& n){return n.key==cell.key;}))r.terrain2_map_.bake_cells.push_back(cell);
 for(size_t i=0;i<old_styles.size();++i)if(r.terrain2_map_.styles[i]==kTerrain2NoStyle)r.terrain2_map_.styles[i]=old_styles[i];
 r.tile_blend_input_=std::move(merged);
 // No seam/donor or cover rebuild here: the existing complete publication
 // remains valid outside this halo, including all neighbouring map slices.
 std::fprintf(stderr,"BRUSH_MATERIAL_RECT tiles=%u,%u,%u,%u halo=%u,%u,%u,%u position_upload_bytes=legacy_pending\n",x0*16,y0*16,x1*16,y1*16,xl,yt,xr,yb);
 return S_OK;
}
