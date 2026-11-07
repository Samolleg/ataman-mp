// Active-map layout transaction: retain the contact pool and neighbours, then
// rebuild native rows/families and height-dependent products under admission.
static void brushLayoutResources(Renderer& r,const std::shared_ptr<TerrainCpuAsset>& terrain,uint64_t changed,const TerrainTileFamilies* cached=nullptr){
 r.brush_ambient_prepared_.reset(); // old surf must not gate a different layout
 r.neighbours_.brush_active_rim.reset();r.neighbours_.brush_seen_revision=0;r.neighbours_.brush_rectangle_valid=false;
 TerrainTileFamilies explicit_families;
 if(!cached&&terrain->editor_profile&&terrain_tile_signatures_from_profile(std::string(terrain->editor_profile->begin(),terrain->editor_profile->end()),terrain_tile_family_space(*terrain),terrain->texture_count*terrain->tiles_per_texture,explicit_families.signatures,&explicit_families.names)){
  explicit_families.source="tile families "+terrain_tile_family_space(*terrain)+": memory profile";cached=&explicit_families;
 }
 // A baked key is reusable across different tile placements. Its material
 // semantics change only when the family signatures/names or source art do.
 if(cached){
  const auto count=std::min(r.tile_families_.signatures.size(),cached->signatures.size());
  for(size_t slot=0;slot<count;++slot){bool different=r.tile_families_.signatures[slot]!=cached->signatures[slot];
   for(const auto id:cached->signatures[slot]){const auto before=r.tile_families_.names.find(id);const auto after=cached->names.find(id);different=different||((before==r.tile_families_.names.end())!=(after==cached->names.end()))||(before!=r.tile_families_.names.end()&&after!=cached->names.end()&&before->second!=after->second);}
   if(different&&slot/64<64)changed|=uint64_t(1)<<(slot/64);
  }
 }else {r.terrain2_map_.baked.clear();}
 uint64_t used=0;r.contact_tile_rows_=owned_terrain_contact_tiles(*terrain,r.contact_tiles_x_,r.contact_tiles_y_,used);
 r.contact_maps_.ready&=~changed;r.contact_maps_.pending=(used&~r.contact_maps_.ready);
 for(auto it=r.terrain2_map_.baked.begin();it!=r.terrain2_map_.baked.end();){if((changed>>((*it>>3)/64))&1)it=r.terrain2_map_.baked.erase(it);else ++it;}
 r.tile_families_job_=TileFamiliesJob{};
 if(cached){std::promise<TerrainTileFamilies> ready;auto future=ready.get_future();ready.set_value(*cached);r.tile_families_prestart_=TileFamiliesJob(std::move(future),{});}
 else r.tile_families_prestart_=load_terrain_tile_families_async(*terrain);
 r.tile_families_prestart_terrain_=terrain;
 r.tile_families_generation_=UINT64_MAX;r.tile_families_job_generation_=UINT64_MAX;
 r.contact_map_generation_=r.ambient_map_generation_+1;
 r.contact_constants_generation_=UINT64_MAX;r.contact_update_frame_=UINT64_MAX;r.contact_retry_frame_=0;
 r.tile_blend_ready_=r.tile_blend_rows_=false;r.tile_blend_input_.reset();
 r.tile_blend_vertex_.clear();r.tile_blend_edge_.clear();r.tile_blend_drawable_.clear();
 r.tile_blend_own_edges_=false;r.terrain2_marker_logged_=false;
 // Old baked material cells retain their exact pool slices. apply_blend computes
 // the new rows/cells and adds only new material bakes as their art arrives.
 r.terrain2_map_.style_serial=0;
}
