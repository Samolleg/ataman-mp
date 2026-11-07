static HRESULT meshFacts(Renderer& r,const TerrainCpuAsset& terrain,const wchar_t* path){
 FILE* file=_wfopen(path,L"wb");if(!file)return E_FAIL;
 struct Close{FILE* file;~Close(){std::fclose(file);}}close{file};
 std::unordered_map<std::string,std::size_t> sectors;for(std::size_t i=0;i<terrain.sectors.size();++i)sectors.emplace(terrain.sectors[i].identity.key(),i);
 for(size_t n=0;n<r.neighbours_.brush_pieces.size();++n){const auto& piece=*r.neighbours_.brush_pieces[n];
  if(!piece.brush||!piece.brush->original)continue;
  for(size_t i=0;i<piece.brush->original->sectors.size();++i)sectors.emplace(piece.brush->original->sectors[i].identity.key(),10000+n*1000+i);
 }
 std::fputs("[",file);bool first=true;
 std::map<std::pair<std::string,UINT>,std::vector<uint8_t>> parents;
 for(const auto& entry:r.native_meshes_){const auto& key=entry.first;const auto& mesh=entry.second;
  const auto found=sectors.find(key.resource_key);if(found==sectors.end()||mesh.allocation.page>=r.native_geometry_pages_.size())continue;
  const auto& page=r.native_geometry_pages_[mesh.allocation.page];if(!page.buffer)continue;
  D3D11_BUFFER_DESC desc{};desc.ByteWidth=mesh.allocation.bytes;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Buffer> readback;auto hr=brush_count_device(r.device_).CreateBuffer(&desc,nullptr,&readback);if(FAILED(hr))return hr;
  D3D11_BOX box{};box.left=mesh.allocation.offset;box.right=box.left+mesh.allocation.bytes;box.bottom=box.back=1;
  r.context_->CopySubresourceRegion(readback.Get(),0,0,0,0,page.buffer.Get(),0,&box);
  D3D11_MAPPED_SUBRESOURCE data{};hr=r.context_->Map(readback.Get(),0,D3D11_MAP_READ,0,&data);if(FAILED(hr))return hr;
  const auto hash=[&](UINT offset,std::size_t bytes){
   uint64_t h=1469598103934665603ull;const uint8_t* p=nullptr;
   if(offset>=mesh.allocation.offset&&uint64_t(offset-mesh.allocation.offset)+bytes<=mesh.allocation.bytes)p=static_cast<const uint8_t*>(data.pData)+offset-mesh.allocation.offset;
   else {
    const auto parent_key=std::make_pair(key.resource_key,mesh.allocation.page);const auto found_parent=r.brush_vertex_gpu_.find(parent_key);
    if(found_parent==r.brush_vertex_gpu_.end())return uint64_t(0);const auto& span=found_parent->second.span;
    if(offset<span.offset||uint64_t(offset-span.offset)+bytes>span.bytes)return uint64_t(0);
    auto& saved=parents[parent_key];
    if(saved.empty()){
     D3D11_BUFFER_DESC d{};d.ByteWidth=span.bytes;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
     Microsoft::WRL::ComPtr<ID3D11Buffer> staging;if(FAILED(brush_count_device(r.device_).CreateBuffer(&d,nullptr,&staging)))return uint64_t(0);
     const D3D11_BOX b{span.offset,0,0,span.offset+span.bytes,1,1};r.context_->CopySubresourceRegion(staging.Get(),0,0,0,0,page.buffer.Get(),0,&b);
     D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(r.context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)))return uint64_t(0);
     saved.assign(static_cast<const uint8_t*>(m.pData),static_cast<const uint8_t*>(m.pData)+span.bytes);r.context_->Unmap(staging.Get(),0);
    }
    p=saved.data()+offset-span.offset;
   }
   while(bytes--){h^=*p++;h*=1099511628211ull;}return h;
  };
  const uint64_t xyz=hash(mesh.position_offset,key.position_groups*sizeof(NativePackedGroup));
  const uint64_t normals=hash(mesh.normal_offset,key.normal_groups*sizeof(NativePackedGroup));
  const uint64_t uv=hash(mesh.texture_coordinate_offset,key.texture_coordinate_count*sizeof(NativeTextureCoordinate));
  const auto parent=r.brush_vertex_gpu_.find(std::make_pair(key.resource_key,mesh.allocation.page));
  const bool shared=parent!=r.brush_vertex_gpu_.end()&&mesh.position_offset==parent->second.span.offset;
  const uint64_t material=hash(mesh.terrain_vertex_offset,(shared?2304u:key.component_count)*sizeof(NativeTerrainVertex));
  const uint64_t indices=hash(mesh.index_offset,key.index_count*4);
  const uint64_t diffuse=hash(mesh.diffuse_storage_offset,key.normal_groups*4u*4u);
  const uint64_t specular=hash(mesh.specular_storage_offset,key.normal_groups*4u*4u);
  const uint64_t components=hash(mesh.component_offset,key.component_count*sizeof(NativeGpuComponent));
  r.context_->Unmap(readback.Get(),0);
  std::fprintf(file,"%s{\"sector\":%zu,\"material\":%u,\"variant\":%u,\"groups\":%u,\"components\":%u,\"indices\":%u,\"positionHash\":%llu,\"normalHash\":%llu,\"uvHash\":%llu,\"materialHash\":%llu,\"indexHash\":%llu,\"diffuseHash\":%llu,\"specularHash\":%llu,\"componentHash\":%llu}",first?"":",",found->second,key.world_part,mesh.geometry_revision[4],key.position_groups,key.component_count,key.index_count,xyz,normals,uv,material,indices,diffuse,specular,components);first=false;
 }
 std::fputs("]",file);
 const std::wstring recordPath=std::wstring(path)+L"-records.json";FILE* records=_wfopen(recordPath.c_str(),L"wb");if(!records)return E_FAIL;Close recordClose{records};
 std::fputs("[",records);first=true;
 for(std::size_t layout=0;layout<r.native_submission_layouts_.size();++layout)for(auto id:r.native_submission_layouts_[layout].source_records){
  if(id>=r.native_instance_records_.size())continue;const auto& record=r.native_instance_records_[id];const auto& command=record.command;
  for(const auto& entry:r.native_meshes_){if(&entry.second!=command.mesh)continue;const auto& key=entry.first;const auto found=sectors.find(key.resource_key);if(found==sectors.end())break;
   auto instance=command.instance;instance.geometry_offsets={};instance.color_offsets={};instance.morph_offset=0;instance.terrain_vertex_offset=0;instance.terrain_phase_offset=0;instance.second_morph_offset=0;
   const auto p0=instance.padding[0],p1=instance.padding[1];instance.padding[0]=instance.padding[1]=0;
   uint64_t h=1469598103934665603ull;const auto* b=reinterpret_cast<const uint8_t*>(&instance);for(std::size_t k=0;k<sizeof(instance);++k){h^=b[k];h*=1099511628211ull;}
   std::fprintf(records,"%s{\"layout\":%zu,\"sector\":%zu,\"material\":%u,\"variant\":%u,\"components\":%u,\"meshIndices\":%u,\"drawIndices\":%u,\"age\":%llu,\"instanceHash\":%llu,\"profile\":%u,\"padding1\":%u,\"bucket\":%u}",first?"":",",layout,found->second,key.world_part,entry.second.geometry_revision[4],key.component_count,key.index_count,command.index_count,r.native_frame_index_-entry.second.last_used_frame,h,p0,p1,command.state_bucket);first=false;break;
  }
 }
 std::fputs("]",records);return S_OK;
}
