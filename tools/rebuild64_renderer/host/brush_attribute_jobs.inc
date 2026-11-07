// CPU-only canonical attribute preparation. Each worker owns its descriptor
// copy, including mutable decoder memo tables. It shares immutable payloads
// and the native mutex-protected liquid solution, never a device/context.
static bool brush_attribute_jobs(owned::TerrainCpuAsset& next,const owned::TerrainCpuAsset& previous,
 uint64_t revision,const std::array<int,4>& rectangle) {
 std::vector<uint32_t> indices;
 for(uint32_t i=0;i<next.sectors.size();++i)
  if(next.sectors[i].editor_revision==revision&&!next.sectors[i].editor_attributes)indices.push_back(i);
 if(indices.size()<8)return false;
 using Result=std::vector<std::pair<uint32_t,std::shared_ptr<const owned::BrushTerrainAttributes>>>;
 std::vector<std::future<Result>> jobs;
 constexpr uint32_t workers=4;
 for(uint32_t worker=0;worker<workers;++worker){
  auto source=std::make_shared<owned::TerrainCpuAsset>(next);
  jobs.push_back(std::async(std::launch::async,[source,&previous,indices,rectangle,revision,worker,workers](){
   Result result;
   for(size_t n=worker;n<indices.size();n+=workers){const auto i=indices[n];
    auto attributes=owned::prepare_brush_terrain_attributes(*source,i,revision,previous.sectors[i].editor_attributes,&rectangle);
    if(!attributes)throw std::runtime_error("brush attribute job failed");
    result.emplace_back(i,std::move(attributes));
   }
   return result;
  }));
 }
 for(auto& job:jobs)for(auto& value:job.get())next.sectors[value.first].editor_attributes=std::move(value.second);
 if(GetEnvironmentVariableW(L"ATAMAN_BRUSH_ATTRIBUTE_COMPARE",nullptr,0)){
  size_t different=0;
  for(auto i:indices){const auto serial=owned::prepare_brush_terrain_attributes(next,i,revision,previous.sectors[i].editor_attributes,&rectangle);
   const auto& parallel=next.sectors[i].editor_attributes;
   if(!serial||serial->values.size()!=parallel->values.size()||serial->steep!=parallel->steep||
      std::memcmp(serial->values.data(),parallel->values.data(),serial->values.size()*sizeof(serial->values[0])))++different;
  }
  std::fprintf(stderr,"BRUSH_ATTRIBUTE_JOBS_COMPARE sectors=%zu different=%zu workers=%u\n",indices.size(),different,workers);
  if(different)throw std::runtime_error("parallel brush attributes differ from serial native decode");
 }
 return true;
}
