#ifndef FILE_WRITE_ACTIVITY_H
#define FILE_WRITE_ACTIVITY_H
#include <atomic>

// Includes nested synchronous saves: a Close event dispatched by a save's
// progress dialog must return to that save before destroying its viewport.
class CFileWriteActivity
{
public:
    CFileWriteActivity() { counter().fetch_add(1); }
    ~CFileWriteActivity() { counter().fetch_sub(1); }
    static int active() { return counter().load(); }
private:
    static std::atomic<int>& counter() { static std::atomic<int> value{0}; return value; }
};
#endif
