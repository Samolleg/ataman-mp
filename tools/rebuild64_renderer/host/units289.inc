// 289: semantic editor appearance ingress, separate from brush/service hosts.
// Owned FIG/BON/ANM decoding, native materials and native pose evaluation.
namespace units289 {
std::string fold(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
struct Reader {
 const uint8_t* p;size_t left;
 uint32_t number(){if(left<4)throw std::runtime_error("short units289 record");uint32_t n;std::memcpy(&n,p,4);p+=4;left-=4;return n;}
 std::string text(size_t limit=1048576){const auto n=number();if(n>limit||n>left)throw std::runtime_error("units289 field bounds");std::string v(reinterpret_cast<const char*>(p),n);p+=n;left-=n;return v;}
 std::vector<std::string> list(){const auto n=number();if(n>4096)throw std::runtime_error("units289 part bounds");std::vector<std::string> v;for(uint32_t i=0;i<n;++i)v.push_back(text());return v;}
};
struct Plan {
 uint32_t mob,nid;bool unit;std::string model,primary,secondary,action;uint32_t length;
 std::vector<std::string> parts,layers,emissive;std::map<std::string,std::string> textures;
 std::shared_ptr<const std::string> composite;
};
using Plans=std::map<uint64_t,Plan>;
Plans read(EirSpan bytes) {
 if(bytes.bytes>268435456 || (bytes.bytes&&!bytes.data))throw std::runtime_error("units289 span bounds");
 Reader r{static_cast<const uint8_t*>(bytes.data),size_t(bytes.bytes)};Plans result;
 std::map<std::string,std::shared_ptr<const std::string>> atlases;
 while(r.left){Plan p{};p.mob=r.number();p.nid=r.number();p.unit=r.number()!=0;
  p.model=fold(r.text());p.primary=fold(r.text());p.secondary=fold(r.text());p.action=r.text();p.length=r.number();
  p.parts=r.list();const auto n=r.number();if(n>4096)throw std::runtime_error("units289 texture bounds");
  for(uint32_t i=0;i<n;++i){const auto name=fold(r.text());p.textures[name]=fold(r.text());}
  p.layers=r.list();p.emissive=r.list();auto pixels=std::make_shared<const std::string>(r.text(67108864));
  std::string atlasKey=p.model;for(const auto& layer:p.layers)atlasKey+='\n'+layer;
  if(!pixels->empty()){atlases[atlasKey]=pixels;p.composite=std::move(pixels);}else if(atlases.count(atlasKey))p.composite=atlases.at(atlasKey);
  if(!result.emplace((uint64_t(p.mob)<<32)|p.nid,std::move(p)).second)throw std::runtime_error("duplicate units289 identity");
 }return result;
}
std::shared_ptr<const owned::TextureCpuAsset> texture(Session& s,const std::string& name,owned::ResourceFailure& failure) {
 owned::ResourceBlob blob;auto result=std::make_shared<owned::TextureCpuAsset>();
 const auto member=name.size()>4&&name.substr(name.size()-4)==".mmp"?name:name+".mmp";
 if(!s.store.resolve(member,blob,failure)||!owned::parse_texture_asset(blob,*result,failure))return {};
 return result;
}
bool prepare(Session& s,owned::OwnedMapSnapshot& map,owned::StaticObjectSource& object,const Plan& plan,std::string& why) {
 owned::ResourceFailure failure;owned::ResourceBlob mod,bone;auto model=std::make_shared<owned::ModelCpuAsset>();
 if(!map.models.count(plan.model)) {
  if(!s.store.resolve(plan.model+".mod",mod,failure)||!s.store.resolve(plan.model+".bon",bone,failure)||
     !owned::decode_model_resources(mod,bone,plan.model,*model,failure)){why="model "+plan.model+": "+failure.message;return false;}
  map.models[plan.model]=model;
 }
 object.visual_reference=plan.model;object.primary_texture=plan.primary;object.secondary_texture=plan.secondary.empty()?plan.primary:plan.secondary;
 object.part_names=plan.parts;for(auto& part:object.part_names)part=fold(part);object.rejection=owned::StaticObjectRejection::none;
 if(!map.textures.count(object.primary_texture)){
  const auto value=texture(s,object.primary_texture,failure);if(!value){why="texture "+object.primary_texture+": "+failure.message;return false;}map.textures[object.primary_texture]=value;
 }
 if(!map.textures.count(object.secondary_texture)){
  const auto value=texture(s,object.secondary_texture,failure);
  // Construction palette only: completed per-part bindings replace it below.
  // Never display a missing secondary on an unbound selected part.
  map.textures[object.secondary_texture]=value?value:map.textures.at(object.primary_texture);
 }
 return true;
}
bool candidate(const std::shared_ptr<owned::OwnedMapSnapshot>& map,const std::shared_ptr<owned::TerrainCpuAsset>& terrain,
 const owned::StaticObjectSource& object,std::shared_ptr<owned::RetainedStaticSourceCandidate>& result,std::string& why) {
 const auto found=map->models.find(object.visual_reference);if(found==map->models.end()){why="missing completed model";return false;}
 const auto& model=found->second;auto assembly=std::make_shared<owned::VisualAssemblyAsset>();
 assembly->revision=0x4154414d554e4954ull;assembly->skeleton=model;assembly->shape=object.shape;
 auto material=std::make_shared<owned::MaterialCpuAsset>();material->diffuse={1,1,1,1};
 for(size_t i=0;i<model->nodes.size();++i){const auto& node=model->nodes[i];
  if(!owned::retained_static_node_selected(object,*model,i)||!node.transform_available||node.figure.indices.empty())continue;
  // Joints remain in the complete skeleton, but do not invent a drawable FIG.
  owned::VisualPartAsset part;part.name=node.name;part.geometry=std::shared_ptr<const owned::FigureCpuAsset>(model,&node.figure);
  part.skeleton_node=uint32_t(i);part.texture_type=node.figure.texture_number;part.material_type=node.figure.material_group;part.material=material;
  part.texture_source_name=owned::figure_primary_texture(part.texture_type)?object.primary_texture:object.secondary_texture;
  part.texture=map->textures.at(part.texture_source_name);part.material_name=part.texture_source_name;assembly->parts.push_back(std::move(part));
 }
 if(assembly->parts.empty()){why="completed appearance has no drawable selected parts";return false;}
 float height=0;if(!owned::sample_terrain_height(*terrain,object.position[0],object.position[1],height)){why="completed appearance outside terrain";return false;}
 result=std::make_shared<owned::RetainedStaticSourceCandidate>();result->assembly=assembly;result->world_position=object.position;result->world_position[2]+=height;
 return true;
}
bool complete(Session& s,const Plan& plan,std::shared_ptr<owned::VisualAssemblyAsset>& assembly,owned::VisualObjectPose& pose,std::string& why) {
 owned::ResourceFailure failure;
 std::shared_ptr<const owned::TextureCpuAsset> composite;
 std::string compositeName;
 if(!plan.layers.empty()){
  if(!plan.composite||plan.composite->empty()){why="resolved body atlas has no completed CPU bytes";return false;}
  // A composed identity must include all equipment layers, not only the skin.
  uint64_t key=1469598103934665603ull;for(unsigned char c:plan.model){key^=c;key*=1099511628211ull;}for(const auto& layer:plan.layers){key^=0xff;key*=1099511628211ull;for(unsigned char c:layer){key^=c;key*=1099511628211ull;}}
  compositeName="ataman-body-"+std::to_string(key);
  const auto cached=s.unitAtlases289.find(compositeName);
  if(cached!=s.unitAtlases289.end())composite=cached->second;
  else {owned::ResourceBlob blob;auto value=std::make_shared<owned::TextureCpuAsset>();
   if(!s.store.copy_selected_memory(compositeName+".mmp",plan.composite->data(),uint32_t(plan.composite->size()),blob,failure)||
      !owned::parse_texture_asset(blob,*value,failure)){why=failure.message;return false;}composite=value;s.unitAtlases289[compositeName]=value;
  }
  owned::MaterialComposition composition;composition.layers=plan.layers;composition.dimension=composite->width;composition.serial=key;
  OwnedRender::E1Scene().Scene().material_publish_composition(compositeName,std::move(composition));
 }
 for(auto& part:assembly->parts){
  const auto override=plan.textures.find(fold(part.name));
  if(override!=plan.textures.end()){
   auto selected=texture(s,override->second,failure);if(!selected){why="part texture "+override->second+": "+failure.message;return false;}
   part.texture=std::move(selected);part.texture_source_name=override->second;
  }else if(composite&&owned::figure_primary_texture(part.texture_type)){
   part.texture=composite;part.texture_source_name=compositeName;
  }else if(!owned::figure_primary_texture(part.texture_type)&&!plan.secondary.empty()){
   auto selected=texture(s,plan.secondary,failure);if(!selected){why="unbound secondary "+plan.secondary+" part "+part.name+": "+failure.message;return false;}
   part.texture=std::move(selected);part.texture_source_name=plan.secondary;
  }
  part.material_name=part.texture_source_name;
  if(std::any_of(plan.emissive.begin(),plan.emissive.end(),[&](const auto& name){return fold(name)==fold(part.name);})){auto material=std::make_shared<owned::MaterialCpuAsset>(*part.material);material->emissive={1,1,1,0};part.material=std::move(material);}
 }
 if(!plan.action.empty()&&plan.length){
  owned::ResourceBlob outer,adb;
  if(!s.store.resolve(plan.model+".anm",outer,failure)){why="animation "+plan.model+": "+failure.message;return false;}
  // ADB and named clips are members of the model's ANM resource archive.
  owned::MemoryResourceArchive archive;
  if(!archive.open(outer,failure)||!archive.resolve(plan.model+".adb",adb,failure)){
   if(!s.store.resolve(plan.model+".adb",adb,failure)){why="ADB "+plan.model+": "+failure.message;return false;}
  }
  auto database=owned::retained_visual_database_asset(adb,failure);if(!database){why=failure.message;return false;}
  uint32_t ordinal=0;for(;ordinal<database->entries.size();++ordinal)if(fold(database->entries[ordinal].name)==fold(plan.action))break;
  if(ordinal==database->entries.size()){why="resolved idle absent from native ADB: "+plan.action;return false;}
  auto clip=owned::retained_visual_clip_asset(outer,plan.action,failure,database,s.world);
  if(!clip){why=failure.message;return false;}
  pose.selection={database,clip,ordinal,false};
 }
 return true;
}
void advance(Session& s) {
 auto& scene=OwnedRender::E1Scene().Scene();
 owned::RetainedHighlightState highlight;highlight.show_selected=true;scene.visual_publish_highlight(highlight);
 for(auto& seed:s.editorSeeds)if(seed.completed&&seed.visible){
  auto pose=seed.pose;
  if(!pose.selection.neutral){const auto length=pose.selection.database->entries[pose.selection.ordinal].length;
   pose.phase=length?std::fmod(s.frameCamera.world_time*.055*15.0+seed.nid%length,double(length)):0;}
  if(!scene.visual_editor_pose(seed.rendererId,{},pose,seed.unit))throw std::runtime_error("native completed pose update rejected");
 }
}
} // namespace units289
