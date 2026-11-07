// A sector is a publication unit; its material/subpart cohorts have separate
// content revisions so a small overlay does not decode every opaque cohort.
static void brush_material_revision(const owned::TerrainCpuAsset& before,owned::TerrainCpuAsset& after,size_t index,uint32_t flags,uint64_t revision){
 const auto& a=before.sectors[index];auto& b=after.sectors[index];
 b.editor_attributes=!(flags&1)?a.editor_attributes:nullptr;
 b.editor_material_revisions=a.editor_material_revisions;
 b.editor_material_revisions.resize(b.materials.size()*4,0);
 if(flags&1){std::fill(b.editor_material_revisions.begin(),b.editor_material_revisions.end(),revision);return;}
 using Tile=owned::TerrainTileDescriptor;
 std::array<const Tile*,512> old{},now{};
 const auto key=[](const Tile& tile){unsigned x=33,y=33;
  for(const auto v:tile.vertex_indices){const auto i=v%1089u;x=(std::min)(x,unsigned(i%33u));y=(std::min)(y,unsigned(i/33u));}
  return (tile.transparent?256u:0u)+(y/2u)*16u+x/2u;
 };
 for(const auto& tile:a.tiles){const auto k=key(tile);if(k<512)old[k]=&tile;}
 for(const auto& tile:b.tiles){const auto k=key(tile);if(k<512)now[k]=&tile;}
 if(b.editor_attributes)for(unsigned i=0;i<256;++i){const auto* x=old[i];const auto* y=now[i];
  const auto material_class=[&](const Tile* tile){return !tile?0:tile->animation_ordinal!=UINT_MAX?1:tile->material_index<after.texture_count?2:3;};
  if(!x||!y||material_class(x)!=material_class(y)||x->animation_ordinal!=y->animation_ordinal||x->animation_phases!=y->animation_phases||
     (x->animation_ordinal!=UINT_MAX&&x->tile_index!=y->tile_index)){b.editor_attributes.reset();break;}
 }
 const auto mark=[&](const Tile* tile){if(tile&&size_t(tile->material_index)*4+tile->subpart<b.editor_material_revisions.size())b.editor_material_revisions[size_t(tile->material_index)*4+tile->subpart]=revision;};
 const auto equal=[](const Tile* x,const Tile* y){if(!x||!y)return x==y;
  return x->tile_index==y->tile_index&&x->material_index==y->material_index&&x->object_id==y->object_id&&x->rotation==y->rotation&&x->subpart==y->subpart&&x->edge_mask==y->edge_mask&&x->transparent==y->transparent&&x->animation_ordinal==y->animation_ordinal&&x->animation_phases==y->animation_phases&&x->vertex_indices==y->vertex_indices;
 };
 for(unsigned i=0;i<512;++i)if(!equal(old[i],now[i])){
  mark(old[i]);mark(now[i]);
  if(i>=256){const int x=int(i%256%16),y=int(i%256/16);
   for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(x+dx>=0&&x+dx<16&&y+dy>=0&&y+dy<16){const auto k=(y+dy)*16+x+dx;mark(old[k]);mark(now[k]);}
  }
 }
 if(flags&4)for(unsigned i=0;i<256;++i){const auto* tile=now[i]?now[i]:old[i];if(!tile)continue;
  bool changed=false;
  for(const auto base:tile->vertex_indices){const size_t top=size_t(base)+1089;
   const bool has_a=a.has_layers&&top<a.vertices.size(),has_b=b.has_layers&&top<b.vertices.size();
   if(has_a!=has_b){changed=true;break;}
   if(has_a){const auto& x=a.vertices[top];const auto& y=b.vertices[top];if(x.z!=y.z||x.lighting_object!=y.lighting_object||x.geometry_object!=y.geometry_object){changed=true;break;}}
  }
  if(changed){mark(old[i]);mark(now[i]);}
 }
}
