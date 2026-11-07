// Document-scoped immutable page ingress. A failed package is rejected before
// changing the live source; no disk writes, global mount or missing-page art.
bool brush_page_digest(const std::vector<uint8_t>& bytes,std::array<unsigned char,32>& digest){
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 bool ok=bytes.size()<=ULONG_MAX&&BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0&&
  BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)==0&&
  BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0)==0&&
  BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)==0;
 if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
bool brush_load_pages(Session& s,owned::TerrainCpuAsset& terrain,const EirTexturePage* input,uint32_t count,
 const owned::TerrainCpuAsset* previous,owned::ResourceFailure& failure,EirSpan profile={}){
 const auto bad=[&](const char* why){failure={owned::ResourceFailureCode::parse,why};return false;};
 if(terrain.texture_count>owned::terrain_tile_fold_page)return bad("terrain exceeds the unchanged shader capacity (63 authored pages; page63 is the folded-cell sentinel)");
 if(count>256||(count&&!input))return bad("invalid memory texture page package");
 std::vector<std::string> names;names.reserve(count);
 for(uint32_t i=0;i<count;++i){
  if(!input[i].name||!input[i].mmp.data||!input[i].mmp.bytes||input[i].mmp.bytes>UINT32_MAX)return bad("invalid memory MMP span");
  std::string name=input[i].name;
  if(name.size()>255||name.find_first_of("/\\:")!=std::string::npos)return bad("memory page name must be a resource leaf");
  if(name.size()>4&&name.substr(name.size()-4)==".mmp")name.resize(name.size()-4);
  if(std::find(terrain.texture_resources.begin(),terrain.texture_resources.end(),name)==terrain.texture_resources.end()||
     std::find(names.begin(),names.end(),name)!=names.end())return bad("memory page is duplicate or outside this terrain table");
  names.push_back(std::move(name));
 }
 if(profile.bytes){
  if(!profile.data||profile.bytes>16*1024*1024)return bad("invalid atlas profile span");
  auto bytes=std::make_shared<std::vector<uint8_t>>(static_cast<const uint8_t*>(profile.data),static_cast<const uint8_t*>(profile.data)+profile.bytes);
  owned::TerrainTileFamilies validated;
  if(!owned::terrain_tile_signatures_from_profile(std::string(bytes->begin(),bytes->end()),owned::terrain_tile_family_space(terrain),terrain.texture_count*terrain.tiles_per_texture,validated.signatures,&validated.names))return bad("profile does not match this terrain namespace and slot table");
  terrain.editor_profile=std::move(bytes);
 }else if(previous&&previous->texture_resources==terrain.texture_resources)terrain.editor_profile=previous->editor_profile;
 auto store=std::make_shared<owned::ResourceStore>();if(!store->open(s.mounts,failure))return false;
 terrain.pages.clear();terrain.pages.resize(terrain.texture_resources.size());
 for(size_t index=0;index<terrain.pages.size();++index){
  auto& page=terrain.pages[index];const auto& name=terrain.texture_resources[index];
  const auto supplied=std::find(names.begin(),names.end(),name);
  if(supplied!=names.end()){
   const auto& span=input[size_t(supplied-names.begin())].mmp;owned::ResourceBlob blob;
   if(!s.store.copy_selected_memory(name+".mmp",span.data,uint32_t(span.bytes),blob,failure))return false;
   page.memory_source=blob.bytes;page.memory_identity=blob.identity;
  }else if(previous){
   const auto old=std::find(previous->texture_resources.begin(),previous->texture_resources.end(),name);
   if(old!=previous->texture_resources.end()&&size_t(old-previous->texture_resources.begin())<previous->pages.size())page=previous->pages[size_t(old-previous->texture_resources.begin())];
  }
  if(!page.memory_source&&!page.store){page.store=store;if(!store->locate(name+".mmp",page.location,failure)){page={};if(index<terrain.texture_count)return false;continue;}}
  if(!page.ensure_upload_bytes(failure,false)){if(index<terrain.texture_count)return false;page={};continue;}
  if(index<terrain.texture_count&&(page.texture->format!=owned::MmpFormat::dxt1||page.texture->mips.size()<2))return bad("terrain contact page requires complete BC1 mip chain");
 }
 failure={};return true;
}
