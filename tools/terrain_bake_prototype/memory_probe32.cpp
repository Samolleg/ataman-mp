// Synthetic x86 address-space experiment; never loads game code or graphics DLLs.
// argv: RES slots tilePixels backgroundMiB mode [reserveMiB=512] [wrapperStagingMiB=0].
// 1024 selects the first 16 pages; 8192 repeats real 4096-kit pages twice.
// Modes: dxt, system, stream, rgba, amplified, assembled, d3d11, d3d11-rgba.
// GPU copies are conservative
// process-VA allocations, NOT measured GPU residency. Actual DXT1 mips decode.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

static_assert(sizeof(void*)==4,"Compile with x86 toolchain");
using U64=unsigned long long;
static const size_t MiB=1u<<20;
struct Block {void* p; size_t n;};
static std::vector<Block> retained;
struct Mapping {HANDLE handle;void* view;size_t n;};
static std::vector<Mapping> mappings;
static U64 decodedChecksum=0,allocatedBytes=0;
static U64 wrapperMappedBytes=0,wrapperSourceBytes=0,wrapperFixedStagingBytes=0;
static int pagesDone=0;
static bool laa=false;
static U64 ceiling=0;

static void sample(const char* event,const char* stage,size_t request=0,DWORD error=0) {
    U64 committed=0,reserved=0,freeBytes=0,largest=0,regions=0,mapped=0,image=0,privateVA=0;
    MEMORY_BASIC_INFORMATION m{};
    U64 address=0;
    while(address<ceiling&&VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(address)),&m,sizeof(m))) {
        const U64 end=std::min(ceiling,U64(reinterpret_cast<uintptr_t>(m.BaseAddress))+U64(m.RegionSize));
        const U64 n=end-address;
        if(m.State==MEM_FREE){freeBytes+=n;largest=std::max(largest,n);++regions;}
        else if(m.State==MEM_COMMIT)committed+=n;
        else if(m.State==MEM_RESERVE)reserved+=n;
        if(m.State!=MEM_FREE){if(m.Type==MEM_MAPPED)mapped+=n;else if(m.Type==MEM_IMAGE)image+=n;else if(m.Type==MEM_PRIVATE)privateVA+=n;}
        if(end<=address)break;address=end;
    }
    PROCESS_MEMORY_COUNTERS_EX pm{};pm.cb=sizeof(pm);
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm),sizeof(pm));
    std::printf("{\"event\":\"%s\",\"stage\":\"%s\",\"pagesDone\":%d,\"laa\":%s,\"pointerBits\":32,\"scanCeiling\":%llu,\"scannedBytes\":%llu,\"committedVA\":%llu,\"reservedVA\":%llu,\"occupiedVA\":%llu,\"freeVA\":%llu,\"largestFree\":%llu,\"freeRegions\":%llu,\"mappedVA\":%llu,\"imageVA\":%llu,\"privateVA\":%llu,\"privateBytes\":%llu,\"workingSet\":%llu,\"requestedBytes\":%llu,\"win32Error\":%lu,\"allocatedBytes\":%llu,\"decodeChecksum\":%llu,\"wrapperMappedBytes\":%llu,\"wrapperSourceBytes\":%llu,\"wrapperFixedStagingBytes\":%llu}\n",
        event,stage,pagesDone,laa?"true":"false",ceiling,address,committed,reserved,committed+reserved,freeBytes,largest,regions,mapped,image,privateVA,U64(pm.PrivateUsage),U64(pm.WorkingSetSize),U64(request),error,allocatedBytes,decodedChecksum,wrapperMappedBytes,wrapperSourceBytes,wrapperFixedStagingBytes);
    std::fflush(stdout);
}
static void* allocate(size_t n,bool commit,const char* stage) {
    void* p=VirtualAlloc(nullptr,n,MEM_RESERVE|(commit?MEM_COMMIT:0),commit?PAGE_READWRITE:PAGE_NOACCESS);
    if(!p){const DWORD error=GetLastError();sample("allocation_failure",stage,n,error);std::exit(10);}
    // Touch every committed 4-KiB page: this is real committed memory, not just
    // an optimistic reservation. Peak RSS remains a separate OS measurement.
    if(commit){for(size_t i=0;i<n;i+=4096)static_cast<volatile unsigned char*>(p)[i]=0xa5;}
    allocatedBytes+=n;return p;
}
static void release(void* p,size_t n) {VirtualFree(p,0,MEM_RELEASE);allocatedBytes-=n;}
static void keep(size_t n,bool commit,const char* stage) {retained.push_back({allocate(n,commit,stage),n});}
static void mapEnvelope(size_t n,const void* data) {
    // Pagefile-backed CPU mapping simulates a driver VA envelope. It is NOT
    // D3D11 allocation, VRAM, nor evidence that GPU textures map one-to-one.
    HANDLE handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,DWORD(n),nullptr);
    if(!handle){sample("allocation_failure","driver_mapping_create",n,GetLastError());std::exit(10);}
    void* view=MapViewOfFile(handle,FILE_MAP_WRITE,0,0,n);
    if(!view){DWORD error=GetLastError();CloseHandle(handle);sample("allocation_failure","driver_mapping_view",n,error);std::exit(10);}
    std::memcpy(view,data,n);mappings.push_back({handle,view,n});wrapperMappedBytes+=n;allocatedBytes+=n;
}
static uint32_t u32(const unsigned char* b) {uint32_t v;std::memcpy(&v,b,4);return v;}
static uint16_t u16(const unsigned char* b) {uint16_t v;std::memcpy(&v,b,2);return v;}
struct Entry {uint32_t offset,size;std::string name;};
static void bad(const char* text) {std::fprintf(stderr,"Invalid input: %s\n",text);std::exit(3);}

static void decode(const unsigned char* input,int width,int height,unsigned char* output) {
    const int bx=(width+3)/4,by=(height+3)/4;
    for(int y=0;y<by;++y)for(int x=0;x<bx;++x) {
        const auto b=input+(y*bx+x)*8;const uint16_t c0=u16(b),c1=u16(b+2);uint32_t bits=u32(b+4);
        unsigned char c[4][4]{};
        for(int k=0;k<2;++k){const uint16_t v=k?c1:c0;c[k][0]=static_cast<unsigned char>((v>>11)*255/31);c[k][1]=static_cast<unsigned char>(((v>>5)&63)*255/63);c[k][2]=static_cast<unsigned char>((v&31)*255/31);c[k][3]=255;}
        for(int channel=0;channel<3;++channel) {
            c[2][channel]=static_cast<unsigned char>(c0>c1?(2*int(c[0][channel])+c[1][channel])/3:(int(c[0][channel])+c[1][channel])/2);
            c[3][channel]=static_cast<unsigned char>(c0>c1?(int(c[0][channel])+2*int(c[1][channel]))/3:0);
        }
        c[2][3]=255;c[3][3]=c0>c1?255:0;
        for(int row=0;row<4;++row)for(int col=0;col<4;++col){const int index=bits&3;bits>>=2;
            if(y*4+row<height&&x*4+col<width)std::memcpy(output+((y*4+row)*width+x*4+col)*4,c[index],4);}
    }
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc<6||argc>8)return 2;
    const int slots=std::atoi(argv[2]),pixels=std::atoi(argv[3]),background=std::atoi(argv[4]);
    const std::string mode=argv[5];const int reserve=argc>=7?std::atoi(argv[6]):512;
    const int wrapperStaging=argc==8?std::atoi(argv[7]):0;
    const bool wrapper=mode=="d3d11"||mode=="d3d11-rgba";
    if((slots!=1024&&slots!=2048&&slots!=4096&&slots!=8192)||(pixels!=128&&pixels!=256)||background<0||background>2048||reserve<0||reserve>2048)return 2;
    if(mode!="dxt"&&mode!="system"&&mode!="stream"&&mode!="rgba"&&mode!="amplified"&&mode!="assembled"&&!wrapper)return 2;
    if(wrapperStaging<0||wrapperStaging>512||(!wrapper&&wrapperStaging))return 2;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const char*>(dos)+dos->e_lfanew);
    laa=(nt->FileHeader.Characteristics&IMAGE_FILE_LARGE_ADDRESS_AWARE)!=0;
    SYSTEM_INFO info{};GetSystemInfo(&info);ceiling=U64(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress))+1;
    sample("sample","baseline");
    // Fragmented private background and reserved address space are explicit
    // hypotheses; neither is a measurement of this game's baseline workload.
    std::vector<Block> holes;
    size_t left=size_t(background)*MiB;
    while(left){const size_t n=std::min(left,24*MiB);keep(n,true,"background");left-=n;holes.push_back({allocate(8*MiB,false,"background_hole"),8*MiB});keep(65536,false,"separator");}
    left=size_t(reserve)*MiB;
    while(left){const size_t n=std::min(left,16*MiB);keep(n,false,"background_reserve");left-=n;}
    sample("sample","background_with_holes");
    for(const auto& h:holes)release(h.p,h.n);
    sample("sample","background");
    if(wrapper&&wrapperStaging){keep(size_t(wrapperStaging)*MiB,true,"wrapper_fixed_staging");wrapperFixedStagingBytes=U64(wrapperStaging)*MiB;sample("sample","wrapper_fixed_staging");}
    FILE* f=nullptr;if(fopen_s(&f,argv[1],"rb")||!f)bad("RES open");
    std::fseek(f,0,SEEK_END);const long fileSize=std::ftell(f);std::rewind(f);
    unsigned char header[16];if(std::fread(header,1,16,f)!=16||u32(header)!=0x019ce23c)bad("RES signature");
    const uint32_t count=u32(header+4),table=u32(header+8),nameSize=u32(header+12);
    if(count>100000||U64(table)+U64(count)*22+nameSize>U64(fileSize))bad("RES table bounds");
    std::vector<unsigned char> entries(count*22),names(nameSize);std::fseek(f,table,SEEK_SET);
    if(std::fread(entries.data(),1,entries.size(),f)!=entries.size()||std::fread(names.data(),1,names.size(),f)!=names.size())bad("RES table read");
    std::vector<Entry> pages;
    for(uint32_t i=0;i<count;++i){const auto row=entries.data()+i*22;const auto len=u16(row+16);if(!len)continue;
        const auto noff=u32(row+18),offset=u32(row+8),size=u32(row+4);
        if(U64(noff)+len>nameSize||U64(offset)+size>U64(fileSize))bad("RES member bounds");
        std::string name(reinterpret_cast<char*>(names.data()+noff),len);if(name.size()<4||name.substr(name.size()-4)!=".mmp")bad("unexpected member");
        pages.push_back({offset,size,name});}
    std::sort(pages.begin(),pages.end(),[](const Entry&a,const Entry&b){return a.name<b.name;});
    if(pages.empty())bad("no MMP pages");
    const int targetPages=slots/64;
    std::vector<unsigned char*> decodedPages;
    if((targetPages<=64&&int(pages.size())<targetPages)||(targetPages==128&&pages.size()!=64))bad("page schedule mismatch");
    for(int page=0;page<targetPages;++page) {
        const auto& e=pages[page%pages.size()];
        auto compressed=static_cast<unsigned char*>(allocate(e.size,true,"RES_read"));
        std::fseek(f,e.offset,SEEK_SET);if(std::fread(compressed,1,e.size,f)!=e.size)bad("MMP read");
        if(e.size<76||u32(compressed)!=0x00504d4d||u32(compressed+4)!=uint32_t(pixels*8)||u32(compressed+8)!=uint32_t(pixels*8)||u32(compressed+16)!=0x31545844)bad("MMP metadata");
        int width=pixels*8,height=width;size_t dxt=0,rgba=0;const int levels=int(u32(compressed+12));
        if(levels!=(pixels==128?11:12))bad("mip count");
        for(int level=0;level<levels;++level){dxt+=size_t(std::max(1,(width+3)/4))*std::max(1,(height+3)/4)*8;rgba+=size_t(width)*height*4;width=std::max(1,width/2);height=std::max(1,height/2);}
        if(dxt+76!=e.size)bad("mip bounds");
        auto gpu=allocate(dxt,true,wrapper?"owned_compressed_source":"compressed_resident");std::memcpy(gpu,compressed+76,dxt);retained.push_back({gpu,dxt});
        void* upload=nullptr;
        if(wrapper){wrapperSourceBytes+=dxt;mapEnvelope(dxt,compressed+76);upload=allocate(dxt,true,"compressed_upload_staging");std::memcpy(upload,compressed+76,dxt);}
        else if(mode!="dxt"){auto sys=allocate(dxt,true,"compressed_system_copy");std::memcpy(sys,compressed+76,dxt);retained.push_back({sys,dxt});}
        unsigned char* expanded=nullptr;void* scratch=nullptr;
        if(mode=="stream"||mode=="rgba"||mode=="amplified"||mode=="assembled"||mode=="d3d11-rgba") {
            expanded=static_cast<unsigned char*>(allocate(rgba,true,"RGBA_decode"));
            width=height=pixels*8;size_t inputOffset=76,outputOffset=0;
            for(int level=0;level<levels;++level){decode(compressed+inputOffset,width,height,expanded+outputOffset);
                inputOffset+=size_t(std::max(1,(width+3)/4))*std::max(1,(height+3)/4)*8;outputOffset+=size_t(width)*height*4;width=std::max(1,width/2);height=std::max(1,height/2);}
            for(size_t i=0;i<rgba;i+=4096)decodedChecksum+=expanded[i];
            if(mode=="amplified"||mode=="assembled"){
                auto second=allocate(rgba,true,"RGBA_copy");std::memcpy(second,expanded,rgba);retained.push_back({second,rgba});
                scratch=allocate(rgba,true,"RGBA_temporary");std::memcpy(scratch,expanded,rgba);
            }
        }
        pagesDone=page+1;sample("sample","page_peak");
        release(compressed,e.size);
        if(upload)release(upload,dxt);
        if(scratch)release(scratch,rgba);
        if(expanded){if(mode=="stream"||mode=="d3d11-rgba")release(expanded,rgba);else retained.push_back({expanded,rgba});}
        if(mode=="assembled")decodedPages.push_back(expanded);
        sample("sample","page_resident");
    }
    if(mode=="assembled") {
        // The historical builder records this near-square layout (ceil sqrt
        // pages), including padding. Engine lifetime/copies remain hypotheses.
        const int columns=int(std::ceil(std::sqrt(double(targetPages))));
        const int rows=(targetPages+columns-1)/columns,pageWidth=pixels*8;
        const size_t bytes=size_t(columns)*rows*pageWidth*pageWidth*4;
        auto combined=static_cast<unsigned char*>(allocate(bytes,true,"combined_RGBA"));
        for(int page=0;page<targetPages;++page)for(int y=0;y<pageWidth;++y)
            std::memcpy(combined+(size_t(page/columns)*pageWidth+y)*columns*pageWidth*4+size_t(page%columns)*pageWidth*4,
                decodedPages[page]+size_t(y)*pageWidth*4,size_t(pageWidth)*4);
        retained.push_back({combined,bytes});sample("sample","combined_RGBA");
    }
    std::fclose(f);sample("complete","all_pages_loaded");
    for(const auto& b:retained)release(b.p,b.n);
    for(const auto& m:mappings){UnmapViewOfFile(m.view);CloseHandle(m.handle);allocatedBytes-=m.n;}
    return 0;
}
