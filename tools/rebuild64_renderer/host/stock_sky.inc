// Ataman's authored nask0sky/Sky00 path. Uses the renderer's shipped fixed
// function shaders; no frame postprocessing, new shader or game-engine code.
// The bounded six-morph format matches Ataman's existing sky-only decoder.
struct SkyVertex {float x,y,z,u,v;};
struct StockSky {
 std::vector<SkyVertex> vertices;
 ComPtr<ID3D11ShaderResourceView> texture;
};
namespace {
struct SkyReader {
 const std::vector<uint8_t>& bytes;size_t offset=0;
 bool skip(size_t n){if(n>bytes.size()-offset)return false;offset+=n;return true;}
 bool u16(uint16_t& v){if(bytes.size()-offset<2)return false;v=bytes[offset]|(uint16_t(bytes[offset+1])<<8);offset+=2;return true;}
 bool u32(uint32_t& v){if(bytes.size()-offset<4)return false;v=bytes[offset]|(uint32_t(bytes[offset+1])<<8)|(uint32_t(bytes[offset+2])<<16)|(uint32_t(bytes[offset+3])<<24);offset+=4;return true;}
 bool f32(float& f){uint32_t bits;if(!u32(bits))return false;std::memcpy(&f,&bits,4);return std::isfinite(f);}
};
bool decode_sky(const std::vector<uint8_t>& bytes,std::vector<SkyVertex>& out){
 SkyReader r{bytes};uint32_t group,texture,blocks;
 if(!r.u32(group)||!r.u32(texture)||!r.skip(240)||!r.u32(blocks)||!blocks||blocks>65536)return false;
 std::vector<std::array<float,3>> xyz(size_t(blocks)*4);
 for(uint32_t b=0;b<blocks;++b)for(unsigned axis=0;axis<3;++axis)for(unsigned morph=0;morph<6;++morph)for(unsigned lane=0;lane<4;++lane){float f;if(!r.f32(f))return false;if(!morph)xyz[b*4+lane][axis]=f;}
 uint32_t n,uvs;if(!r.u32(n)||n>65536||!r.skip(size_t(n)*64)||!r.u32(uvs)||!uvs||uvs>1048576)return false;
 std::vector<std::array<float,2>> uv(uvs);for(auto& v:uv)if(!r.f32(v[0])||!r.f32(v[1]))return false;
 if(!r.u32(n)||n>1048576||!r.skip(size_t(n)*52)||!r.u32(n)||!n||n>1048576)return false;
 struct Component{uint16_t v,n,uv;};std::vector<Component> components(n);
 for(auto& c:components)if(!r.u16(c.v)||!r.u16(c.n)||!r.u16(c.uv))return false;
 if(!r.u32(n)||n>1048576||!r.skip(size_t(n)*4)||!r.u32(n)||!n||n>65535||n%3)return false;
 std::vector<SkyVertex> decoded;decoded.reserve(n);
 for(uint32_t i=0;i<n;++i){uint16_t index;if(!r.u16(index)||index>=components.size())return false;const auto c=components[index];if(c.v>=xyz.size()||c.uv>=uv.size())return false;const auto p=xyz[c.v];const auto t=uv[c.uv];decoded.push_back({p[0],p[1],p[2],t[0],t[1]});}
 if(r.offset!=bytes.size())return false;out=std::move(decoded);return true;
}
HRESULT load_stock_sky(StockSky& sky,owned::ResourceStore& store,ID3D11Device* device){
 if(sky.texture)return S_OK;owned::ResourceFailure error;owned::ResourceBlob figure,blob;owned::TextureCpuAsset tex;
 if(!store.resolve("nask0sky.fig",figure,error)||!figure.bytes||!decode_sky(*figure.bytes,sky.vertices)||!store.resolve("sky00.mmp",blob,error)||!owned::parse_texture_asset(blob,tex,error))return E_FAIL;
 DXGI_FORMAT format;unsigned block=0,pixel=0;
 switch(tex.format){case owned::MmpFormat::dxt1:format=DXGI_FORMAT_BC1_UNORM;block=8;break;case owned::MmpFormat::dxt3:format=DXGI_FORMAT_BC2_UNORM;block=16;break;case owned::MmpFormat::dxt5:format=DXGI_FORMAT_BC3_UNORM;block=16;break;case owned::MmpFormat::argb8888:format=DXGI_FORMAT_B8G8R8A8_UNORM;pixel=4;break;default:return E_NOTIMPL;}
 D3D11_TEXTURE2D_DESC desc{};desc.Width=tex.width;desc.Height=tex.height;desc.MipLevels=UINT(tex.mips.size());desc.ArraySize=1;desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 std::vector<D3D11_SUBRESOURCE_DATA> data;for(const auto& mip:tex.mips){if(mip.byte_offset>tex.source->size()||mip.bytes>tex.source->size()-mip.byte_offset)return E_INVALIDARG;D3D11_SUBRESOURCE_DATA row{};row.pSysMem=tex.source->data()+mip.byte_offset;row.SysMemPitch=block?((mip.width+3)/4)*block:mip.width*pixel;data.push_back(row);}
 ComPtr<ID3D11Texture2D> texture;HRESULT hr=device->CreateTexture2D(&desc,data.data(),&texture);if(SUCCEEDED(hr))hr=device->CreateShaderResourceView(texture.Get(),nullptr,&sky.texture);return hr;
}
HRESULT draw_stock_sky(StockSky& sky,owned::Renderer& renderer,const EirCamera& camera,UINT width,UINT height){
 if(!(camera.visuals&1)||camera.cave)return S_OK;
 auto byte=[](float x){return uint32_t(std::clamp(x,0.f,1.f)*255.f+.5f);};
 uint32_t tint=0xff000000,fog=0;for(unsigned i=0;i<3;++i){tint|=byte(camera.sky_tint[i])<<(16-8*i);fog|=byte(camera.fog_color[i])<<(16-8*i);}
 std::vector<owned::FixedFunctionVertex> vertices;
 // Subdivide only to approximate the existing GL per-pixel smoothstep in the
 // two lower authored rings with the shipped fixed-function interpolator.
 auto emit=[&](const SkyVertex& v){float p[4]={v.x,v.y,v.z,1},clip[4]={};for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)clip[a]+=camera.sky_to_clip[a*4+b]*p[b];float t=std::clamp(v.z/13.91404f,0.f,1.f);t=t*t*(3-2*t);vertices.push_back({clip[0],clip[1],clip[3],clip[3],tint,byte(t)<<24,v.u,v.v});};
 auto mix=[](SkyVertex a,SkyVertex b,SkyVertex c,float u,float v){const float w=1-u-v;return SkyVertex{a.x*w+b.x*u+c.x*v,a.y*w+b.y*u+c.y*v,a.z*w+b.z*u+c.z*v,a.u*w+b.u*u+c.u*v,a.v*w+b.v*u+c.v*v};};
 constexpr unsigned steps=4;
 for(size_t i=0;i<sky.vertices.size();i+=3)for(unsigned a=0;a<steps;++a)for(unsigned b=0;b<steps-a;++b){auto at=[&](unsigned x,unsigned y){return mix(sky.vertices[i],sky.vertices[i+1],sky.vertices[i+2],float(x)/steps,float(y)/steps);};emit(at(a,b));emit(at(a+1,b));emit(at(a,b+1));if(a+b+1<steps){emit(at(a+1,b));emit(at(a+1,b+1));emit(at(a,b+1));}}
 if(vertices.size()>65536)return E_INVALIDARG;
 owned::FixedState state;state.viewport={0,0,width,height,0,1};state.render[D3DRENDERSTATE_ZENABLE]=1;state.render[D3DRENDERSTATE_ZWRITEENABLE]=0;state.render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;state.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_NONE;state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=0;state.render[D3DRENDERSTATE_ALPHATESTENABLE]=0;state.render[D3DRENDERSTATE_SPECULARENABLE]=0;state.render[D3DRENDERSTATE_FOGENABLE]=(camera.visuals&2)!=0;state.render[D3DRENDERSTATE_FOGCOLOR]=fog;
 state.stage[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;state.stage[0][D3DTSS_COLORARG1]=D3DTA_TEXTURE;state.stage[0][D3DTSS_COLORARG2]=D3DTA_DIFFUSE;state.stage[0][D3DTSS_ALPHAOP]=D3DTOP_SELECTARG2;state.stage[0][D3DTSS_ALPHAARG2]=D3DTA_DIFFUSE;state.stage[0][D3DTSS_ADDRESSU]=state.stage[0][D3DTSS_ADDRESSV]=D3DTADDRESS_WRAP;state.stage[0][D3DTSS_MINFILTER]=state.stage[0][D3DTSS_MAGFILTER]=D3DTFN_LINEAR;state.stage[0][D3DTSS_MIPFILTER]=D3DTFP_LINEAR;
 return renderer.draw_embedded_fixed_function(D3DPT_TRIANGLELIST,vertices.data(),DWORD(vertices.size()),nullptr,0,state,sky.texture.Get());
}
}
