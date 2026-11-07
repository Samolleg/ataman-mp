static int terrainFacts(Renderer& r,const TerrainCpuAsset& terrain,uint64_t* out,uint32_t count){
 if(count<8+terrain.sectors.size()*5)return -1;
 const auto hash=[](const void* data,std::size_t bytes){std::uint64_t h=1469598103934665603ull;const auto* value=static_cast<const uint8_t*>(data);while(bytes--){h^=*value++;h*=1099511628211ull;}return h;};
 const auto values=[&](const auto& data){return hash(data.data(),data.size()*sizeof(data[0]));};
 out[0]=terrain.sectors.size();out[1]=terrain.texture_count;out[2]=values(r.tile_families_.signatures);
 out[3]=values(r.contact_tile_rows_);out[4]=values(r.terrain2_map_.styles);out[5]=values(r.ambient_area_);out[6]=values(r.ambient_height_);out[7]=r.gc_map_generation_;
 std::size_t at=8;
 for(const auto& sector:terrain.sectors){
  std::vector<float> xyz,normals;std::vector<uint32_t> tiles,slopes,facets;
  for(const auto& v:sector.vertices){xyz.insert(xyz.end(),{v.x,v.y,v.z,v.depth,float(v.geometry_object),float(v.lighting_object)});normals.insert(normals.end(),{v.normal_x,v.normal_y,v.normal_z});}
  for(const auto& tile:sector.tiles){tiles.insert(tiles.end(),{tile.tile_index,tile.material_index,tile.object_id,tile.rotation,tile.subpart,tile.edge_mask,tile.animation_phases,tile.animation_ordinal,uint32_t(tile.transparent)});for(auto v:tile.vertex_indices)tiles.push_back(v);}
  for(uint32_t base=0;base<1089;++base){slopes.push_back(owned_terrain_slope_normal(terrain,sector,base));facets.push_back(owned_terrain_facet_flatness(terrain,sector,base));}
  out[at++]=values(xyz);out[at++]=values(normals);out[at++]=values(tiles);out[at++]=values(slopes);out[at++]=values(facets);
 }
 return int(at);
}
