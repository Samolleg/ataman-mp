#include "brush_layout_reuse.inc"
std::array<unsigned char,32> brush_layout_key(const Session& s){
 std::vector<uint8_t> bytes;
 for(const auto& name:s.terrain->texture_resources){bytes.insert(bytes.end(),name.begin(),name.end());bytes.push_back(0);}
 for(const auto& digest:s.atlasDigests)bytes.insert(bytes.end(),digest.begin(),digest.end());
 for(auto value:s.terrain->tile_types)for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(uint8_t(value>>shift));
 if(s.terrain->editor_profile)bytes.insert(bytes.end(),s.terrain->editor_profile->begin(),s.terrain->editor_profile->end());
 std::array<unsigned char,32> key{};if(!brush_page_digest(bytes,key))throw std::runtime_error("layout digest");return key;
}
int brush_layout_update(Session& s,const EirTerrainUpdate* in,std::shared_ptr<owned::TerrainCpuAsset> next,
 std::chrono::steady_clock::time_point begin,probe1b_error* e){
 next->identity=s.terrain->identity;next->outside_liquid=s.terrain->outside_liquid;next->welded_sides=s.terrain->welded_sides;
 const auto revision=++s.editRevision;size_t vertex_reuse=0,attribute_reuse=0;
 std::vector<bool> changed(next->sectors.size()),halo(next->sectors.size());
 for(size_t i=0;i<next->sectors.size();++i)changed[i]=!brush_layout_same_vertices(s.terrain->sectors[i],next->sectors[i])||!brush_layout_same_tiles(*s.terrain,*next,i);
 for(int y=0;y<int(next->sector_count_y);++y)for(int x=0;x<int(next->sector_count_x);++x)if(changed[size_t(y)*next->sector_count_x+x])
  for(int yy=std::max(0,y-1);yy<=std::min(int(next->sector_count_y)-1,y+1);++yy)for(int xx=std::max(0,x-1);xx<=std::min(int(next->sector_count_x)-1,x+1);++xx)halo[size_t(yy)*next->sector_count_x+xx]=true;
 for(size_t i=0;i<next->sectors.size();++i){auto& sector=next->sectors[i];sector.identity=s.terrain->sectors[i].identity;
  sector.editor_revision=sector.editor_liquid_revision=revision;sector.editor_shared_vertices=true;
  if(brush_layout_same_vertices(s.terrain->sectors[i],sector)&&s.terrain->sectors[i].editor_vertices){sector.editor_vertices=s.terrain->sectors[i].editor_vertices;++vertex_reuse;}
  else sector.editor_vertices=owned::prepare_brush_vertex_streams(sector,revision);
  sector.editor_material_revisions.assign(sector.materials.size()*4,revision);
 }
 owned::owned_terrain_classify_swamp_frames(*next);
 const bool reuse_attributes=brush_layout_same_attributes(*s.terrain,*next);
 for(uint32_t i=0;i<next->sectors.size();++i){
  if(reuse_attributes&&!halo[i]&&s.terrain->sectors[i].editor_attributes){next->sectors[i].editor_attributes=s.terrain->sectors[i].editor_attributes;++attribute_reuse;}
  else next->sectors[i].editor_attributes=owned::prepare_brush_terrain_attributes(*next,i,revision);
  if(!next->sectors[i].editor_attributes)return fail(e,E_FAIL,"layout attribute preparation failed");
 }
 std::fprintf(stderr,"BRUSH_LAYOUT_REUSE sectors=%zu vertices=%zu attributes=%zu globals=%d\n",next->sectors.size(),vertex_reuse,attribute_reuse,reuse_attributes);
 if(GetEnvironmentVariableW(L"ATAMAN_BRUSH_ATTRIBUTE_COMPARE",nullptr,0)){
  size_t different=0;
  for(uint32_t i=0;i<next->sectors.size();++i){const auto& selected=next->sectors[i].editor_attributes;const auto full=owned::prepare_brush_terrain_attributes(*next,i,revision);
   if(!full||full->values.size()!=selected->values.size())return fail(e,E_FAIL,"layout attribute comparison decode failed");
   different+=full->steep!=selected->steep;
   for(size_t corner=0;corner<full->values.size();++corner)if(std::memcmp(&full->values[corner],&selected->values[corner],sizeof(full->values[corner]))){if(different<8)std::fprintf(stderr,"BRUSH_LAYOUT_ATTRIBUTE sector=%u corner=%zu\n",i,corner);++different;}
  }
  std::fprintf(stderr,"BRUSH_LAYOUT_ATTRIBUTE_COMPARE different=%zu\n",different);if(different)return fail(e,E_FAIL,"layout attribute reuse differs from complete decode");
 }
 auto map=std::make_shared<owned::OwnedMapSnapshot>(*s.map);map->terrain=next;
 const auto decoded=std::chrono::steady_clock::now();
 uint64_t changedPages=0;
 for(size_t i=0;i<next->texture_count&&i<64;++i){bool changed=i>=s.terrain->texture_count;
  if(!changed){owned::ResourceFailure failure;const auto& before=s.terrain->pages[i];const auto& after=next->pages[i];
   if(!before.ensure_upload_bytes(failure,false)||!after.ensure_upload_bytes(failure,false))return fail(e,E_FAIL,"layout previous page unavailable");
   changed=*before.texture->source!=*after.texture->source;
  }if(changed)changedPages|=uint64_t(1)<<i;
 }
 // No Present can occur during this owner-thread transaction. Upload the full
 // package before publishing its new selectors and keep the accepted display
 // until native admission reports all new rows, cells and atlases ready.
 const auto family=owned::AtamanReadiness::brushFamilies(s.renderer);
 if(family.signatures.size()==s.terrain->texture_count*s.terrain->tiles_per_texture&&family.source.find("profile")!=std::string::npos){
  if(s.brushFamilyCache.size()>=8)s.brushFamilyCache.erase(s.brushFamilyCache.begin());s.brushFamilyCache[brush_layout_key(s)]=family;
 }
 const auto previous=s.terrain;s.terrain=next;std::string why;
 if(!upload_atlases(s,why)){s.terrain=previous;return fail(e,E_FAIL,why.c_str());}
 auto& scene=OwnedRender::E1Scene().Scene();
 if(!scene.editor_replace_terrain(map)||!OwnedRender::E1UpdateTerrainSnapshot(&s,next))return fail(e,E_FAIL,"active layout publication rejected");
 const auto cached=s.brushFamilyCache.find(brush_layout_key(s));
 owned::TerrainTileFamilies supplied;const owned::TerrainTileFamilies* prepared_families=cached==s.brushFamilyCache.end()?nullptr:&cached->second;
 if(!prepared_families&&next->editor_profile&&owned::terrain_tile_signatures_from_profile(std::string(next->editor_profile->begin(),next->editor_profile->end()),owned::terrain_tile_family_space(*next),next->texture_count*next->tiles_per_texture,supplied.signatures,&supplied.names)){
  supplied.source="tile families "+owned::terrain_tile_family_space(*next)+": memory profile";prepared_families=&supplied;
 }
 std::shared_ptr<owned::BrushAmbientPrepared> prepared;
 const auto derived_begin=std::chrono::steady_clock::now();
 if(prepared_families){
  auto geometry=owned::AtamanReadiness::brushGeometry(s.renderer,*next);if(geometry.layer_shifts.size()!=next->objects.size())geometry=owned::frozen_owned_terrain_geometry(*next);
  prepared=owned::prepare_brush_ambient(next,map.get(),geometry,*prepared_families,owned::AtamanReadiness::brushEdges(s.renderer),owned::AtamanReadiness::brushOwnEdges(s.renderer));
  if(!prepared)return fail(e,E_FAIL,"active layout derivative preparation failed");
  const auto& grids=prepared->grids;
  prepared->surf=std::make_shared<owned::LiquidSurfField>(owned::build_liquid_surf_field(grids.area,grids.level.liquid_area,grids.height,grids.level.ground,grids.tiles_x,grids.tiles_y,owned::AtamanReadiness::brushSurfSea(s.renderer)));
 }
 owned::AtamanReadiness::brushLayoutResources(s.renderer,next,changedPages,cached==s.brushFamilyCache.end()?nullptr:&cached->second);
 if(prepared)owned::AtamanReadiness::brushPrepared(s.renderer,std::move(prepared));
 s.brushWorker.cancel();s.terrain=next;s.baseline=next;s.map=map;s.terrainBytes.clear();
 s.edits.applied_serial=in->serial;++s.edits.updates;s.editStart=begin;
 s.edits.decode_ms=std::chrono::duration<double,std::milli>(decoded-begin).count();
 s.edits.publish_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-decoded).count();
 s.finalizeStart=begin;s.finalizePublishedSerial=in->serial;s.edits.derived_pending=1;s.edits.finalize_ms=0;s.edits.finalize_cpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-derived_begin).count();
 std::fprintf(stderr,"TERRAIN_LAYOUT world=%llu generation=%llu serial=%llu pages=%u changed=%llx decode_ms=%.3f publish_ms=%.3f\n",s.world,s.generation,in->serial,next->texture_count,changedPages,s.edits.decode_ms,s.edits.publish_ms);
 return 0;
}
