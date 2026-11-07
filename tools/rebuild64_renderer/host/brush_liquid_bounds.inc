// Liquid flow/swamp neighbours depend on authored height and tile identity,
// not on a mapper pass that re-quantized unchanged layer normals.
static std::array<int,4> brush_liquid_bounds(const owned::TerrainCpuAsset& before,const owned::TerrainCpuAsset& after,
 uint32_t x0,uint32_t y0,uint32_t x1,uint32_t y1){
 std::array<int,4> bounds{INT_MAX,INT_MAX,INT_MIN,INT_MIN};
 const auto add=[&](int x,int y,int side=0){bounds[0]=(std::min)(bounds[0],x);bounds[1]=(std::min)(bounds[1],y);bounds[2]=(std::max)(bounds[2],x+side);bounds[3]=(std::max)(bounds[3],y+side);};
 using Tile=owned::TerrainTileDescriptor;
 for(uint32_t y=y0;y<y1;++y)for(uint32_t x=x0;x<x1;++x){const auto i=size_t(y)*after.sector_count_x+x;const auto& a=before.sectors[i];const auto& b=after.sectors[i];
  if(a.has_layers&&b.has_layers)for(unsigned v=1089;v<std::min(a.vertices.size(),b.vertices.size());++v){const auto& p=a.vertices[v];const auto& q=b.vertices[v];
   if(p.z!=q.z||p.geometry_object!=q.geometry_object||p.lighting_object!=q.lighting_object)add(int(x*32+(v-1089)%33),int(y*32+(v-1089)/33));
  }
  std::array<const Tile*,256> old{},now{};
  const auto key=[](const Tile& tile){const auto v=tile.vertex_indices[0]%1089u;return v/33/2*16+v%33/2;};
  for(const auto& tile:a.tiles)if(tile.transparent){const auto k=key(tile);if(k<256)old[k]=&tile;}
  for(const auto& tile:b.tiles)if(tile.transparent){const auto k=key(tile);if(k<256)now[k]=&tile;}
  for(unsigned t=0;t<256;++t){const auto* p=old[t];const auto* q=now[t];
   const bool changed=(!p||!q)?p!=q:p->tile_index!=q->tile_index||p->material_index!=q->material_index||p->object_id!=q->object_id||p->rotation!=q->rotation||p->animation_ordinal!=q->animation_ordinal||p->animation_phases!=q->animation_phases;
   if(changed)add(int(x*32+t%16*2),int(y*32+t/16*2),2);
  }
 }
 return bounds;
}
