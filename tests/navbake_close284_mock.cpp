#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <windows.h>
int main(){
    const char* delay=std::getenv("NAVBAKE_TEST_DELAY");const DWORD ms=delay?std::atoi(delay):700;
    std::puts("BAKE_BEGIN maps=3 algorithm=26");
    for(const char* map:{"a","b","c"}){
        std::printf("MAP_BEGIN mpr=%s mob=%s\n",map,map);std::fflush(stdout);
        if(std::getenv("NAVBAKE_TEST_LONG_LOG"))for(int i=0;i<2100;++i)std::printf("DETAIL %d\n",i);
        std::printf("stdout remainder %s",map);std::fflush(stdout);
        std::fprintf(stderr,"stderr remainder %s",map);std::fflush(stderr);
        Sleep(ms);std::puts("");std::fputs("\n",stderr);
        std::puts("PROFILE type=human radius=0.2 tiles=1 ms=1 height_outliers=0");
        std::printf("MAP_DONE mpr=%s mob=%s\n",map,map);std::fflush(stdout);
    }
    std::puts("BAKE_DONE maps=3 completed=3 missing=0 errors=0 skipped=0");return 0;
}
