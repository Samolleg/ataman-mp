static HRESULT textureFacts(Renderer& r,const wchar_t* path){
 FILE* file=_wfopen(path,L"wb");if(!file)return E_FAIL;struct Close{FILE* f;~Close(){std::fclose(f);}}close{file};
 const auto hash=[](const uint8_t* data,std::size_t pitch,std::size_t bytes,std::size_t rows){uint64_t h=1469598103934665603ull;for(std::size_t y=0;y<rows;++y)for(std::size_t x=0;x<bytes;++x){h^=data[y*pitch+x];h*=1099511628211ull;}return h;};
 const std::pair<const char*,ID3D11ShaderResourceView*> views[]{
  {"contactRows",r.contact_maps_.tiles.Get()},{"contactAtlas",r.contact_maps_.atlas.Get()},{"materialHeights",r.terrain2_map_.height_srv.Get()},
  {"terrainGrids",r.terrain2_grids_srv_.Get()},{"terrainStyles",r.terrain2_style_srv_.Get()},
  {"cachedColorFine",r.terrain2_cache_fine_srv_.Get()},{"cachedColorCoarse",r.terrain2_cache_coarse_srv_.Get()},
  {"cachedHeightFine",r.terrain2_cache_height_fine_srv_.Get()},{"cachedHeightCoarse",r.terrain2_cache_height_coarse_srv_.Get()},
  {"coverColor",r.gc_color_srv_.Get()},{"coverFields",r.gc_fields_srv_.Get()},{"materialCoverColor",r.gc_t2_color_srv_.Get()},{"materialCoverClasses",r.gc_t2_classes_srv_.Get()},
  {"ambientArea",r.ambient_area_srv_.Get()},{"ambientHeight",r.ambient_height_srv_.Get()},{"ambientGround",r.ambient_ground_srv_.Get()}};
 std::fprintf(file,"{\"terrainCacheOn\":%s,\"terrainCacheMode\":%d,\"fields\":[",r.terrain2_cache_on_?"true":"false",configured_integer(r.debug_overlay_.config(),"terrain2_cache",0));bool first=true;
 for(const auto& entry:views){auto* view=entry.second;if(!view)continue;D3D11_SHADER_RESOURCE_VIEW_DESC vd{};view->GetDesc(&vd);Microsoft::WRL::ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;if(SUCCEEDED(resource.As(&buffer))){D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.StructureByteStride=0;Microsoft::WRL::ComPtr<ID3D11Buffer> staging;auto hr=brush_count_device(r.device_).CreateBuffer(&desc,nullptr,&staging);if(FAILED(hr))return hr;r.context_->CopyResource(staging.Get(),resource.Get());D3D11_MAPPED_SUBRESOURCE data{};hr=r.context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&data);if(FAILED(hr))return hr;const auto h=hash(static_cast<const uint8_t*>(data.pData),desc.ByteWidth,desc.ByteWidth,1);r.context_->Unmap(staging.Get(),0);std::fprintf(file,"%s{\"name\":\"%s\",\"slice\":0,\"mip\":0,\"width\":%u,\"height\":1,\"hash\":%llu}",first?"":",",entry.first,desc.ByteWidth,h);first=false;continue;}
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))continue;D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
  UINT start=0,slices=1,firstMip=0,mips=desc.MipLevels;
  if(vd.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2DARRAY){start=vd.Texture2DArray.FirstArraySlice;slices=vd.Texture2DArray.ArraySize;firstMip=vd.Texture2DArray.MostDetailedMip;mips=vd.Texture2DArray.MipLevels;}
  else if(vd.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D){firstMip=vd.Texture2D.MostDetailedMip;mips=vd.Texture2D.MipLevels;}
  else continue;
  slices=(std::min)(slices,desc.ArraySize-start);mips=(std::min)(mips,desc.MipLevels-firstMip);
  UINT pixel=0,block=0;switch(desc.Format){case DXGI_FORMAT_BC1_UNORM:case DXGI_FORMAT_BC1_UNORM_SRGB:block=8;break;case DXGI_FORMAT_BC2_UNORM:case DXGI_FORMAT_BC3_UNORM:block=16;break;case DXGI_FORMAT_R8_UNORM:case DXGI_FORMAT_R8_UINT:pixel=1;break;case DXGI_FORMAT_R16_UINT:case DXGI_FORMAT_R16_UNORM:pixel=2;break;case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R32_UINT:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:case DXGI_FORMAT_B8G8R8A8_UNORM:pixel=4;break;case DXGI_FORMAT_R16G16B16A16_FLOAT:case DXGI_FORMAT_R32G32_FLOAT:pixel=8;break;case DXGI_FORMAT_R32G32B32A32_UINT:case DXGI_FORMAT_R32G32B32A32_FLOAT:pixel=16;break;default:continue;}
  D3D11_TEXTURE2D_DESC copy=desc;copy.ArraySize=1;copy.Usage=D3D11_USAGE_STAGING;copy.BindFlags=copy.MiscFlags=0;copy.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;auto hr=brush_count_device(r.device_).CreateTexture2D(&copy,nullptr,&staging);if(FAILED(hr))return hr;
  for(UINT mip=0;mip<mips;++mip)for(UINT slice=0;slice<slices;++slice){
   const auto level=mip+firstMip;const UINT width=(std::max)(1u,desc.Width>>level),height=(std::max)(1u,desc.Height>>level);
   r.context_->CopySubresourceRegion(staging.Get(),level,0,0,0,texture.Get(),D3D11CalcSubresource(level,slice+start,desc.MipLevels),nullptr);
   D3D11_MAPPED_SUBRESOURCE data{};hr=r.context_->Map(staging.Get(),level,D3D11_MAP_READ,0,&data);if(FAILED(hr))return hr;
   const auto h=hash(static_cast<const uint8_t*>(data.pData),data.RowPitch,block?(std::max)(1u,(width+3)/4)*block:width*pixel,block?(std::max)(1u,(height+3)/4):height);r.context_->Unmap(staging.Get(),level);
   std::fprintf(file,"%s{\"name\":\"%s\",\"slice\":%u,\"mip\":%u,\"width\":%u,\"height\":%u,\"hash\":%llu}",first?"":",",entry.first,slice,mip,width,height,h);first=false;
  }
 }
 std::fprintf(file,"],\"terrainStaticEntries\":%zu,\"terrainStaticDraws\":%zu}",r.native_terrain_set_.objects.size(),r.native_terrain_set_.draws.size());return S_OK;
}
