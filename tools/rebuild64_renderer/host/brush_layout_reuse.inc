// These attributes depend on native vertex/tile values and terrain material
// semantics, not on the length of the atlas table. Detect their exact changed
// inputs before rebuilding the active layout; a one-sector halo covers the
// unchanged native terrain sampling kernels.
static bool brush_layout_same_tiles(const owned::TerrainCpuAsset& a,const owned::TerrainCpuAsset& b,size_t sector){
 const auto& x=a.sectors[sector].tiles;const auto& y=b.sectors[sector].tiles;if(x.size()!=y.size())return false;
 const auto material=[](uint32_t index,uint32_t count){return index<count?uint64_t(index):uint64_t(1)<<32|(index-count);};
 for(size_t i=0;i<x.size();++i){const auto& p=x[i];const auto& q=y[i];
  if(p.tile_index!=q.tile_index||material(p.material_index,a.texture_count)!=material(q.material_index,b.texture_count)||p.object_id!=q.object_id||p.rotation!=q.rotation||p.subpart!=q.subpart||p.edge_mask!=q.edge_mask||p.transparent!=q.transparent||p.animation_ordinal!=q.animation_ordinal||p.animation_phases!=q.animation_phases||p.vertex_indices!=q.vertex_indices)return false;
 }return true;
}
static bool brush_layout_same_vertices(const owned::TerrainSectorCpuAsset& a,const owned::TerrainSectorCpuAsset& b){
 return a.vertices.size()==b.vertices.size()&&(a.vertices.empty()||!std::memcmp(a.vertices.data(),b.vertices.data(),a.vertices.size()*sizeof(owned::TerrainVertex)));
}
static bool brush_layout_same_attributes(const owned::TerrainCpuAsset& a,const owned::TerrainCpuAsset& b){
 const size_t abase=size_t(a.texture_count)*a.tiles_per_texture,bbase=size_t(b.texture_count)*b.tiles_per_texture;
 const bool types=abase<=a.tile_types.size()&&bbase<=b.tile_types.size()&&a.tile_types.size()-abase==b.tile_types.size()-bbase&&
  std::equal(a.tile_types.begin(),a.tile_types.begin()+std::min(abase,bbase),b.tile_types.begin())&&
  std::equal(a.tile_types.begin()+abase,a.tile_types.end(),b.tile_types.begin()+bbase);
 if(GetEnvironmentVariableW(L"ATAMAN_BRUSH_FRAME_TIMING",nullptr,0)){
  std::fprintf(stderr,"BRUSH_LAYOUT_GLOBALS objects=%zu/%zu types=%zu/%zu base=%zu/%zu same=%d\n",a.objects.size(),b.objects.size(),a.tile_types.size(),b.tile_types.size(),abase,bbase,types);
  for(size_t i=0;i<std::min(a.objects.size(),b.objects.size());++i)if(std::memcmp(&a.objects[i],&b.objects[i],sizeof(a.objects[i]))){const auto& x=a.objects[i];const auto& y=b.objects[i];std::fprintf(stderr,"BRUSH_LAYOUT_OBJECT index=%zu type=%u/%u color=%.9g,%.9g,%.9g/%.9g,%.9g,%.9g opacity=%.9g/%.9g emission=%.9g/%.9g wave=%.9g/%.9g warp=%.9g/%.9g\n",i,x.type,y.type,x.color[0],x.color[1],x.color[2],y.color[0],y.color[1],y.color[2],x.opacity,y.opacity,x.self_illumination,y.self_illumination,x.wave_multiplier,y.wave_multiplier,x.warp_speed,y.warp_speed);break;}
  for(size_t i=0;i<std::min(a.tile_types.size(),b.tile_types.size());++i)if(a.tile_types[i]!=b.tile_types[i]){std::fprintf(stderr,"BRUSH_LAYOUT_TYPE index=%zu before=%u after=%u\n",i,a.tile_types[i],b.tile_types[i]);break;}
 }
 // This cache contains base/long vertices only (brush_attribute_decode.inc
 // iterates material < texture_count+2). Swamp art/cover is consumed by the
 // separate liquid-layer decode and shader texture, never by these values.
  return a.objects.size()==b.objects.size()&&(a.objects.empty()||!std::memcmp(a.objects.data(),b.objects.data(),a.objects.size()*sizeof(owned::TerrainObjectDescriptor)))&&types;
}
