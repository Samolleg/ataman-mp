#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>
int main(int argc,char** argv){
    std::printf("BAKE_ARGS ");for(int i=1;i<argc;++i)std::printf("%s ",argv[i]);std::puts("");std::fflush(stdout);
    const char* p=std::getenv("NAV64_MOCK");if(!p)p="missing";
    if(!std::strcmp(p,"cancel")){Sleep(30000);return 0;}
    std::puts("PROFILE type=human radius=0.2 tiles=1 wall_ms=1 bytes=100 fingerprint=test");
    std::puts("MAP_DONE map=test total_ms=1 bytes=100");
    if(!std::strcmp(p,"missing"))return 0;
    if(!std::strcmp(p,"map_missing")){std::fprintf(stderr,"MAP_MISSING mpr=absent.mpr mob=absent.mob code=5 mpr_present=0 mob_present=0 reason=no_sources\n");std::puts("BAKE_DONE maps=1 completed=0 missing=1 errors=0 total_ms=1 bytes=0");return 5;}
    std::puts("BAKE_DONE maps=1 completed=1 missing=0 errors=0 total_ms=1 bytes=100");
    return !std::strcmp(p,"exit")?4:0;
}
