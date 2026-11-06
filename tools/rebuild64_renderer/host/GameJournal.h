#pragma once
// Probe-owned diagnostic sink. Only inherited stderr of our isolated client.
#include <cstdio>
#include <cstring>
namespace GameJournal {
template<class... Args> inline void Event(const char* level,const char* name,const char* format,Args... args) noexcept {
 std::fprintf(stderr,"[%s] %s ",level?level:"",name?name:"");
 std::fprintf(stderr,format?format:"",args...);std::fputc('\n',stderr);
}
// The game's Count aggregator owns native neighbour diagnostics. Preserve
// that bounded stream (piece summaries and failure reasons) in the host log.
inline void Count(const char* level,const char* name,const char* reason) noexcept {
 if(name&&std::strcmp(name,"neighbour_maps")==0)
  std::fprintf(stderr,"[%s] %s %s\n",level?level:"",name,reason?reason:"unspecified");
}
template<class... Args> inline void Count(Args...) noexcept {}
}
