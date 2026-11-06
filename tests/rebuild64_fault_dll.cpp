// Test-only forwarding DLL. The real native backend runs until the armed file
// requests an access violation. This code is never in the distributable DLL.
#include "../source/render/rebuild64_abi.h"
#include <windows.h>
namespace {
EirApi realApi{};HMODULE realModule=nullptr;
int32_t EIR_CALL frame(void* handle,EirStatistics* stats,EirError* error) {
 wchar_t path[32768];const auto n=GetEnvironmentVariableW(L"EI_TOOLS_FAULT_FILE",path,32768);
 if(n&&n<32768&&GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES) {
  *static_cast<volatile int*>(nullptr)=286;
 }
 return realApi.frame(handle,stats,error);
}
}
extern "C" __declspec(dllexport) int32_t EIR_CALL ataman_renderer_get_api(uint32_t abi,EirApi* out) {
 realModule=LoadLibraryW(L"ataman_rebuild64_real.dll");if(!realModule)return -2;
 const auto get=reinterpret_cast<EirGetApi>(GetProcAddress(realModule,"ataman_renderer_get_api"));
 realApi.size=sizeof(realApi);if(!get||get(abi,&realApi))return -3;
 if(!out||out->size!=sizeof(*out))return -4;*out=realApi;out->frame=&frame;return 0;
}
