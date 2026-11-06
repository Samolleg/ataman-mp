#pragma once
// Probe-owned diagnostic sink. Only inherited stderr of our isolated client.
#include <cstdio>
namespace GameJournal {
template<class... Args> inline void Event(const char* level,const char* name,const char* format,Args... args) noexcept {
 std::fprintf(stderr,"[%s] %s ",level?level:"",name?name:"");
 std::fprintf(stderr,format?format:"",args...);std::fputc('\n',stderr);
}
template<class... Args> inline void Count(Args...) noexcept {}
}
