#pragma once
// Test-only process-local NT file event observer, installed before Qt startup.
// Never installed in an ordinary editor launch; touches no other process.
#include <windows.h>
#include <winternl.h>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <string>
#include <mutex>
#include <QString>
namespace {
std::atomic<int> graphicsConfig64Attempts{0},graphicsFileEvents{0};
FILE* graphicsFileAudit=nullptr;
std::mutex graphicsFileAuditMutex;
using GraphicsNtCreate=LONG(NTAPI*)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG);
using GraphicsNtOpen=LONG(NTAPI*)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,ULONG,ULONG);
GraphicsNtCreate graphicsOriginalCreate=nullptr;GraphicsNtOpen graphicsOriginalOpen=nullptr;
using GraphicsNtTerminate=LONG(NTAPI*)(HANDLE,LONG);
GraphicsNtTerminate graphicsOriginalTerminate=nullptr;
HANDLE graphicsExitTrace=INVALID_HANDLE_VALUE;
LONG NTAPI graphicsNtTerminate(HANDLE process,LONG code){
    if(graphicsExitTrace!=INVALID_HANDLE_VALUE){
        char line[4096];DWORD written=0;int n=std::snprintf(line,sizeof(line),"terminate code=%08lx thread=%lu\n",static_cast<unsigned long>(code),GetCurrentThreadId());WriteFile(graphicsExitTrace,line,n,&written,nullptr);
        void* frames[64];const auto count=CaptureStackBackTrace(0,64,frames,nullptr);
        for(USHORT i=0;i<count;++i){HMODULE module=nullptr;char path[MAX_PATH]{};GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCSTR>(frames[i]),&module);GetModuleFileNameA(module,path,MAX_PATH);n=std::snprintf(line,sizeof(line),"%s + %llx\n",path,static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i])-reinterpret_cast<uintptr_t>(module)));WriteFile(graphicsExitTrace,line,n,&written,nullptr);}
        FlushFileBuffers(graphicsExitTrace);
    }
    return graphicsOriginalTerminate(process,code);
}
bool graphicsObserveFile(POBJECT_ATTRIBUTES attributes,ACCESS_MASK access){
    if(!attributes||!attributes->ObjectName||!attributes->ObjectName->Buffer)return false;
    const auto* name=attributes->ObjectName;std::wstring path(name->Buffer,name->Length/sizeof(wchar_t));
    if(attributes->RootDirectory&&!path.empty()&&path.front()!=L'\\'){
        wchar_t parent[32768]{};using FinalPath=DWORD(WINAPI*)(HANDLE,LPWSTR,DWORD,DWORD);
        static const auto finalPath=reinterpret_cast<FinalPath>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetFinalPathNameByHandleW"));
        const auto count=finalPath?finalPath(attributes->RootDirectory,parent,32768,0):0;
        if(count&&count<32768)path=std::wstring(parent,count)+L"\\"+path;
    }
    std::wstring lower=path;for(auto& c:lower)c=std::towlower(c);
    const bool forbidden=lower.find(L"\\config64\\")!=std::wstring::npos||lower.find(L"/config64/")!=std::wstring::npos;
    // All file-open events, including unsuccessful attempts, not handle sampling.
    ++graphicsFileEvents;
    if(graphicsFileAudit){std::lock_guard<std::mutex> lock(graphicsFileAuditMutex);std::fwprintf(graphicsFileAudit,L"%ls access=%08lx path=%ls\n",forbidden?L"DENIED_CONFIG64":L"OPEN",static_cast<unsigned long>(access),path.c_str());std::fflush(graphicsFileAudit);}
    if(forbidden)++graphicsConfig64Attempts;return forbidden;
}
LONG NTAPI graphicsNtCreate(PHANDLE h,ACCESS_MASK access,POBJECT_ATTRIBUTES a,PIO_STATUS_BLOCK io,PLARGE_INTEGER bytes,ULONG attr,ULONG share,ULONG disposition,ULONG opts,PVOID ea,ULONG eaBytes){
    if(graphicsObserveFile(a,access))return LONG(0xc0000022u);
    return graphicsOriginalCreate(h,access,a,io,bytes,attr,share,disposition,opts,ea,eaBytes);
}
LONG NTAPI graphicsNtOpen(PHANDLE h,ACCESS_MASK access,POBJECT_ATTRIBUTES a,PIO_STATUS_BLOCK io,ULONG share,ULONG opts){
    if(graphicsObserveFile(a,access))return LONG(0xc0000022u);return graphicsOriginalOpen(h,access,a,io,share,opts);
}
bool graphicsHook(const char* name,void* target,void** original){
    auto* entry=reinterpret_cast<unsigned char*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),name));
    // x64 NT syscall stub only. Refuse existing hooks or another instruction form.
    const unsigned char prefix[]={0x4c,0x8b,0xd1,0xb8};
    if(!entry||std::memcmp(entry,prefix,4)||entry[18]!=0x0f||entry[19]!=0x05)return false;
    void* trampoline=VirtualAlloc(nullptr,32,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);if(!trampoline)return false;
    std::memcpy(trampoline,entry,32);*original=trampoline;
    unsigned char jump[12]={0x48,0xb8};const auto address=reinterpret_cast<quint64>(target);std::memcpy(jump+2,&address,8);jump[10]=0xff;jump[11]=0xe0;
    DWORD old=0;if(!VirtualProtect(entry,12,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(entry,jump,12);FlushInstructionCache(GetCurrentProcess(),entry,12);DWORD ignored=0;return VirtualProtect(entry,12,old,&ignored)!=0;
}
bool installGraphicsFileAudit(const QString& path){
    graphicsFileAudit=_wfopen(reinterpret_cast<const wchar_t*>(path.utf16()),L"w, ccs=UTF-8");if(!graphicsFileAudit)return false;
    const auto trace=path+".exit.txt";graphicsExitTrace=CreateFileW(reinterpret_cast<const wchar_t*>(trace.utf16()),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    return graphicsHook("NtCreateFile",reinterpret_cast<void*>(&graphicsNtCreate),reinterpret_cast<void**>(&graphicsOriginalCreate))&&graphicsHook("NtOpenFile",reinterpret_cast<void*>(&graphicsNtOpen),reinterpret_cast<void**>(&graphicsOriginalOpen))&&graphicsHook("NtTerminateProcess",reinterpret_cast<void*>(&graphicsNtTerminate),reinterpret_cast<void**>(&graphicsOriginalTerminate));
}
}
