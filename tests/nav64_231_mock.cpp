#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>
int main(int argc,char** argv){
    bool report=false;for(int i=1;i+1<argc;++i)if(!std::strcmp(argv[i],"--report-root"))report=true;
    std::printf("BAKE_ARGS ");for(int i=1;i<argc;++i)std::printf("%s ",argv[i]);std::puts("");std::fflush(stdout);
    if(!report)return 2;
    const char* mode=std::getenv("NAV64_MOCK");if(!mode)mode="success";
    if(!std::strcmp(mode,"cancel")){Sleep(30000);return 0;}
    std::puts("BAKE_BEGIN maps=1 algorithm=21");
    if(!std::strcmp(mode,"quality")){std::fprintf(stderr,"MAP_ERROR mpr=zone1.mpr mob=zone1.mob code=4 reason=Nav64: route-to-step release quality failures=4\n");std::puts("BAKE_DONE maps=1 completed=0 missing=0 errors=1 skipped=0");return 4;}
    if(!std::strcmp(mode,"map_missing")){std::fprintf(stderr,"MAP_MISSING mpr=missing.mpr mob=missing.mob code=5 reason=no_sources\n");std::puts("BAKE_DONE maps=1 completed=0 missing=1 errors=0 skipped=0");return 5;}
    if(!std::strcmp(mode,"missing_done"))return 0;
    std::puts("PROFILE type=human radius=0.2 tiles=1 ms=1 height_outliers=0");
    std::puts("MAP_DONE mpr=zone1.mpr mob=zone1.mob");std::puts("BAKE_DONE maps=1 completed=1 missing=0 errors=0 skipped=0");return 0;
}
