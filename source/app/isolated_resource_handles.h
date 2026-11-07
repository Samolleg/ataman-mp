#pragma once
#include <windows.h>
#include <QVector>
#include <QStringList>
// Observe handles of this process only. Never duplicate or alter another process handle.
inline QStringList isolatedOpenResourceHandles(bool& observed) {
    struct Entry {void* object;ULONG_PTR pid,handle;ULONG access;USHORT creator,type;ULONG attributes,reserved;};
    struct Table {ULONG_PTR count,reserved;Entry entries[1];};
    using Query=LONG(WINAPI*)(ULONG,PVOID,ULONG,PULONG);
    auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQuerySystemInformation"));
    observed=false;if(!query)return {};
    QVector<char> bytes(1<<20);ULONG needed=0;LONG result;
    while((result=query(64,bytes.data(),ULONG(bytes.size()),&needed))==LONG(0xC0000004L)&&bytes.size()<(1<<27))bytes.resize(qMax(int(needed)+65536,bytes.size()*2));
    if(result<0)return {};observed=true;QStringList paths;auto* table=reinterpret_cast<const Table*>(bytes.constData());
    using FinalName=DWORD(WINAPI*)(HANDLE,LPWSTR,DWORD,DWORD);
    auto finalName=reinterpret_cast<FinalName>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetFinalPathNameByHandleW"));if(!finalName){observed=false;return {};}
    for(ULONG_PTR i=0;i<table->count;++i){const auto& e=table->entries[i];if(e.pid!=GetCurrentProcessId())continue;
        HANDLE h=reinterpret_cast<HANDLE>(e.handle);if(GetFileType(h)!=FILE_TYPE_DISK)continue;
        wchar_t name[32768];DWORD n=finalName(h,name,32768,0);
        if(n&&n<32768){auto path=QString::fromWCharArray(name);path.replace('\\','/');if(path.startsWith("//?/"))path.remove(0,4);if(path.endsWith(".res",Qt::CaseInsensitive)||path.endsWith(".mpr",Qt::CaseInsensitive)||path.endsWith(".mob",Qt::CaseInsensitive))paths.append(path);}
    }
    paths.removeDuplicates();return paths;
}
