// Crop the unchanged native grid builder, preserving its equations and tree
// census. Only a one-sector halo is decoded into the retained full-map grids.
static bool brushGrids(Renderer& r,const TerrainCpuAsset& terrain,const OwnedMapSnapshot* map,
 uint32_t x0,uint32_t y0,uint32_t x1,uint32_t y1,ambient::TerrainGrids& out,std::array<UINT,4>& rect) {
 const UINT w=terrain.sector_count_x*16,h=terrain.sector_count_y*16,vw=w*2+1,vh=h*2+1;
 if(r.ambient_liquid_level_.ground.size()!=size_t(vw)*vh||r.ambient_area_.size()!=size_t(w)*h)return false;
 const UINT sx=x0?x0-1:0,sy=y0?y0-1:0,ex=std::min(terrain.sector_count_x,x1+1),ey=std::min(terrain.sector_count_y,y1+1);
 TerrainCpuAsset selected=terrain;selected.sector_count_x=ex-sx;selected.sector_count_y=ey-sy;selected.sectors.clear();
 for(UINT y=sy;y<ey;++y)for(UINT x=sx;x<ex;++x){auto sector=terrain.sectors[size_t(y)*terrain.sector_count_x+x];sector.map_x-=sx;sector.map_y-=sy;selected.sectors.push_back(std::move(sector));}
 OwnedMapSnapshot shifted;
 if(map)for(const auto& original:map->objects){
  if(!ambient::leafy_tree_model(original.visual_reference.c_str()))continue;
  auto object=original;object.position[0]-=sx*32.f;object.position[1]-=sy*32.f;
  if(object.position[0]<-8||object.position[1]<-8||object.position[0]>(ex-sx)*32+8||object.position[1]>(ey-sy)*32+8)continue;
  shifted.objects.push_back(std::move(object));
 }
 ambient::TerrainGrids local;
 if(!ambient::build_terrain_grids(selected,map?&shifted:nullptr,local,r.ambient_geometry_.get()))return false;
 const UINT l=sx*16,t=sy*16,rr=ex*16,b=ey*16,lvw=local.tiles_x*2+1;
 rect={l,t,rr,b};out.tiles_x=w;out.tiles_y=h;
 out.level=r.ambient_liquid_level_;out.height=r.ambient_height_;out.ground=out.level.ground;out.area=r.ambient_area_;out.types.resize(size_t(w)*h);
 for(UINT y=0;y<local.tiles_y;++y)for(UINT x=0;x<local.tiles_x;++x){const size_t src=size_t(y)*local.tiles_x+x,dst=size_t(t+y)*w+l+x;
  out.level.dry_area[dst]=local.level.dry_area[src];out.level.liquid_area[dst]=local.level.liquid_area[src];out.types[dst]=local.types[src];
 }
 for(UINT y=0;y<=local.tiles_y*2;++y)for(UINT x=0;x<=local.tiles_x*2;++x){const size_t src=size_t(y)*lvw+x,dst=size_t(t*2+y)*vw+l*2+x;
  out.level.ground[dst]=local.level.ground[src];out.ground[dst]=local.ground[src];out.height[dst]=local.height[src];
 }
 const auto in_vertex=[&](UINT cell){const auto x=cell%vw,y=cell/vw;return x>=l*2&&x<=rr*2&&y>=t*2&&y<=b*2;};
 auto& vertices=out.level.vertices;vertices.erase(std::remove_if(vertices.begin(),vertices.end(),[&](const auto& v){return in_vertex(v.cell);}),vertices.end());
 for(auto v:local.level.vertices){v.cell=(t*2+v.cell/lvw)*vw+l*2+v.cell%lvw;vertices.push_back(v);}
 const auto in_tile=[&](UINT tile){const auto x=tile%w,y=tile/w;return x>=l&&x<rr&&y>=t&&y<b;};
 auto& tiles=out.level.tiles;tiles.erase(std::remove_if(tiles.begin(),tiles.end(),[&](const auto& v){return in_tile(v.tile);}),tiles.end());
 for(auto tile:local.level.tiles){tile.tile=(t+tile.tile/local.tiles_x)*w+l+tile.tile%local.tiles_x;tiles.push_back(tile);}
 std::sort(tiles.begin(),tiles.end(),[](const auto& a,const auto& b){return a.tile<b.tile;});
 out.area=out.level.dry_area;
 return ambient::apply_liquid_level(out.level,&r.ambient_geometry_->layer_shifts,out.height,out.area,out.emissive);
}
