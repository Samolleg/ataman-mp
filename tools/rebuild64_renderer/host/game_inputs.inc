// File-backed source facts. Renderer options and response stay in the renderer.
// Read only Config/Lights[Cave]<region>.ini and our isolated settings.ini copy.
#include <map>
#include <string>
#include <cwchar>
#include <cstdlib>
#include "model_lighting.h"
struct GameInputs {
 std::wstring directory,settings;
 uint32_t fogColor=0;
 struct Table {float sunlight[24][3]{},ambient[24][3]{},sky[24][3]{};};
 std::map<std::wstring,Table> tables;
 float setting(const wchar_t* section,const wchar_t* key) const {
  wchar_t text[128]{};if(!GetPrivateProfileStringW(section,key,L"",text,128,settings.c_str()))throw std::runtime_error("required settings.ini value missing");
  wchar_t* end=nullptr;const float value=std::wcstof(text,&end);if(end==text||!std::isfinite(value))throw std::runtime_error("invalid settings.ini number");return value;
 }
 bool table(const EirCamera& camera,const Table*& output,std::string& why) {
  const wchar_t* region=camera.region==1?L"gipat":camera.region==2?L"ingos":camera.region==3?L"suslanger":nullptr;
  if(!region){why="map region unavailable";return false;}
  const auto file=directory+L"/lights"+(camera.cave?std::wstring(L"cave"):std::wstring())+region+L".ini";
  auto found=tables.find(file);if(found!=tables.end()){output=&found->second;return true;}
  Table value;
  for(int section=0;section<3;++section)for(unsigned hour=0;hour<24;++hour){
   const wchar_t* name=section==0?L"sunlight":section==1?L"ambient":L"sky";
   wchar_t key[16],text[128];std::swprintf(key,16,L"time%02u",hour);
   float r,g,b;if(!GetPrivateProfileStringW(name,key,L"",text,128,file.c_str())||std::swscanf(text,L"%f , %f , %f",&r,&g,&b)!=3){why="required source Lights table value missing";return false;}
   auto* row=section==0?value.sunlight[hour]:section==1?value.ambient[hour]:value.sky[hour];row[0]=r;row[1]=g;row[2]=b;
  }
  output=&tables.emplace(file,value).first->second;std::fprintf(stderr,"GAME_LIGHT_TABLE %ls\n",file.c_str());return true;
 }
 bool frame(const EirCamera& camera,OwnedRender::E1WorldFrame& frame,std::string& why) {
  const Table* source=nullptr;if(!table(camera,source,why))return false;
  const float hour=owned::sky_wrap_hour(float(camera.hour));const unsigned first=camera.cave?0:unsigned(hour),next=camera.cave?0:(first+1)%24;const float fraction=camera.cave?0:hour-first;
  auto mix=[&](const float values[24][3],unsigned axis){return values[first][axis]+(values[next][axis]-values[first][axis])*fraction;};
  frame.environment.valid=true;frame.environment.game_hours=camera.hour;frame.environment.hour=hour;
  frame.environment.world_time=camera.world_time;frame.environment.region=int(camera.region);frame.environment.cave=camera.cave!=0;
  // The editor has no running CWeather transition. Its unloaded initial state
  // is explicit; render-owned precip_test/strength still apply in E1WorldDraw.
  owned::ModelLightingFacts light;light.sunlight=true;
  fogColor=0;for(unsigned axis=0;axis<3;++axis){light.ambient|=uint32_t(std::clamp(mix(source->ambient,axis),0.f,255.f))<<(16-8*axis);light.diffuse[axis]=mix(source->sunlight,axis)/255.f;fogColor|=uint32_t(std::clamp(mix(source->sky,axis),0.f,255.f))<<(16-8*axis);}
  // Clock track documented by the pinned sky_clouds.h adapter; toward-light
  // convention is the same as produce_model_lighting, without gamma conversion.
  const float pi=3.14159265358979323846f;
  const float elevation=(hour>=4&&hour<=20?60-3.75f*std::abs(hour-12):hour>20?30+(hour-20)*11.25f:30+(4-hour)*11.25f)*pi/180;
  const float azimuth=hour*2*pi/24;
  light.direction={std::cos(azimuth)*std::cos(elevation),std::sin(azimuth)*std::cos(elevation),-std::sin(elevation)};
  if(camera.cave)light.direction={.5f,.5f,-.707106f}; // source cave ray
  frame.lightingValid=owned::produce_model_lighting(light,frame.lighting);
  // Cave response (including removal of sunlight) is left to cave_brightness.
  frame.fog.valid=true;frame.fog.enabled=(camera.visuals&2)!=0;frame.fog.failure=nullptr;
  auto& fog=frame.fog.fog;fog.start=setting(L"Game Specific Settings",hour<4||hour>20?L"FogNightStartDistance":L"FogDayStartDistance");
  fog.end=-camera.fog_projection[2]/(camera.fog_projection[0]-camera.fog_projection[1]);
  fog.scale=255.f/(fog.end-fog.start);fog.projection_33=camera.fog_projection[0];fog.projection_34=camera.fog_projection[1];fog.projection_43=camera.fog_projection[2];
  return true;
 }
};
