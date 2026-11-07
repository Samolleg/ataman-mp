#pragma once
#include <string>
// Opt-in evidence from this DLL's own Present/readback; no desktop capture.
struct PresentationTrace {
 std::wstring root;std::FILE* log=nullptr;uint64_t sequence=0,lastGeneration=0;bool capture=false,all=false,shownCaptured=false,cameraCapture=false;float previousClip[16]{};
 std::string exeSha,dllSha;
 static std::string identity(const wchar_t* key){wchar_t value[80]{};const auto n=GetEnvironmentVariableW(key,value,80);std::string out;if(n==64)for(unsigned i=0;i<n;++i){if(!((value[i]>=L'0'&&value[i]<=L'9')||(value[i]>=L'a'&&value[i]<=L'f')))return {};out.push_back(char(value[i]));}return out;}
 PresentationTrace(){
  wchar_t path[32768];const auto n=GetEnvironmentVariableW(L"ATAMAN_REBUILD64_PRESENT_TRACE",path,32768);
  if(!n||n>=32768)return;root=path;
  if(root.find(L"/work/rebuild64-tools-286-r2-")==std::wstring::npos&&root.find(L"\\work\\rebuild64-tools-286-r2-")==std::wstring::npos
     &&root.find(L"/work/rebuild64-launch-287-")==std::wstring::npos&&root.find(L"\\work\\rebuild64-launch-287-")==std::wstring::npos
     &&root.find(L"/work/rebuild64-offscreen-288-")==std::wstring::npos&&root.find(L"\\work\\rebuild64-offscreen-288-")==std::wstring::npos
     &&root.find(L"/work/rebuild64-service-291-20261107/")==std::wstring::npos&&root.find(L"\\work\\rebuild64-service-291-20261107\\")==std::wstring::npos){root.clear();return;}
  log=_wfopen((root+L"/presented.jsonl").c_str(),L"ab");
  capture=GetEnvironmentVariableW(L"ATAMAN_REBUILD64_TRACE_PNG",path,32768)>0;
  cameraCapture=GetEnvironmentVariableW(L"ATAMAN_REBUILD64_TRACE_CAMERA",path,32768)>0;
  all=GetEnvironmentVariableW(L"ATAMAN_REBUILD64_TRACE_ALL",path,32768)>0;
  exeSha=identity(L"ATAMAN_REBUILD64_EXE_SHA");dllSha=identity(L"ATAMAN_REBUILD64_DLL_SHA");
 }
 ~PresentationTrace(){if(log)std::fclose(log);}
 void phase(const char* name){if(!log)return;LARGE_INTEGER time{};QueryPerformanceCounter(&time);std::fprintf(log,"{\"event\":\"phase\",\"phase\":\"%s\",\"qpc\":%lld}\n",name,time.QuadPart);std::fflush(log);}
 bool presented(uint64_t generation,uint32_t neighbours,uint32_t requested,uint32_t pending,bool settled,const char* map,const float* clip,const float* eye){
  if(!log)return false;LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);
  const unsigned expected=!requested?0:_stricmp(map,"zone1.mpr")==0?5:_stricmp(map,"zone7.mpr")==0?4:0;
  const bool valid=neighbours==expected&&!pending&&settled;
  if(lastGeneration!=generation)shownCaptured=false;
  const bool qtPageShown=GetFileAttributesW((root+L"/qt-page-shown-"+std::to_wstring(generation)+L".json").c_str())!=INVALID_FILE_ATTRIBUTES;
  bool cameraChanged=false;for(int i=0;i<16;++i)cameraChanged|=clip[i]!=previousClip[i];
  const bool save=capture&&(all||lastGeneration!=generation||(qtPageShown&&!shownCaptured)||(cameraCapture&&cameraChanged));
  for(int i=0;i<16;++i)previousClip[i]=clip[i];
  if(qtPageShown)shownCaptured=true;

  std::fprintf(log,"{\"event\":\"present\",\"qpc\":%lld,\"sequence\":%llu,\"generation\":%llu,\"renderer\":\"Rebuild64\",\"map\":\"%s\",\"neighbours\":%u,\"neighbours_accepted\":%u,\"expected\":%u,\"validation\":\"%s\",\"requested\":%u,\"pending\":%u,\"settled\":%s,\"qtPageShown\":%s,\"exeSha256\":\"%s\",\"dllSha256\":\"%s\"",clock.QuadPart,(unsigned long long)++sequence,(unsigned long long)generation,map,neighbours,neighbours,expected,valid?"PASS":"FAIL",requested,pending,settled?"true":"false",qtPageShown?"true":"false",exeSha.c_str(),dllSha.c_str());
  std::fprintf(log,",\"cameraClip\":[");for(int i=0;i<16;++i)std::fprintf(log,"%s%.9g",i?",":"",clip[i]);
  std::fprintf(log,"],\"eye\":[%.9g,%.9g,%.9g]}\n",eye[0],eye[1],eye[2]);
  std::fflush(log);lastGeneration=generation;return save;
 }
};
