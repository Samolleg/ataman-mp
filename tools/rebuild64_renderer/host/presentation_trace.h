#pragma once
// Opt-in diagnostic of this host's actual Present stream. No renderer policy.
struct PresentationTrace {
 std::wstring root;FILE* log=nullptr;uint64_t sequence=0,lastGeneration=0;bool capture=false,all=false;
 PresentationTrace(){wchar_t path[32768];const auto n=GetEnvironmentVariableW(L"ATAMAN_REBUILD64_PRESENT_TRACE",path,32768);if(n&&n<32768){root=path;if(root.find(L"/work/rebuild64-tools-286-r2-")==std::wstring::npos&&root.find(L"\\work\\rebuild64-tools-286-r2-")==std::wstring::npos){root.clear();return;}log=_wfopen((root+L"/presented.jsonl").c_str(),L"ab");capture=GetEnvironmentVariableW(L"ATAMAN_REBUILD64_TRACE_PNG",path,32768)>0;all=GetEnvironmentVariableW(L"ATAMAN_REBUILD64_TRACE_ALL",path,32768)>0;}}
 ~PresentationTrace(){if(log)std::fclose(log);}
 bool presented(uint64_t generation,uint32_t neighbours,uint32_t requested,uint32_t pending,bool settled){if(!log)return false;LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);std::fprintf(log,"{\"qpc\":%lld,\"sequence\":%llu,\"generation\":%llu,\"neighbours\":%u,\"requested\":%u,\"pending\":%u,\"settled\":%s}\n",clock.QuadPart,(unsigned long long)++sequence,(unsigned long long)generation,neighbours,requested,pending,settled?"true":"false");std::fflush(log);const bool save=capture&&(all||lastGeneration!=generation);lastGeneration=generation;return save;}
};
