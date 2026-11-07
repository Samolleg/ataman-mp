// Atlas/material publication shares the validated same-world layout transaction.
int32_t EIR_CALL host_atlas_update(void* h,const EirTerrainUpdate* in,EirError* out) noexcept {
 if(!in || in->size!=sizeof(*in) || !in->page_count || !in->pages) {
  if(out){out->code=E_INVALIDARG;std::snprintf(out->message,sizeof(out->message),"atlas transaction requires complete memory pages");}return E_INVALIDARG;
 }
 return host_terrain_update(h,in,out);
}
