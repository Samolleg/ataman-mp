#pragma once
#include <functional>
#include <utility>
// Only a native-opening caller installs this hook, on its own GUI thread.
// Legacy OpenGL callers keep an empty hook and their existing execution order.
namespace ei {
inline std::function<void()>& legacyProjectionPulse(){static thread_local std::function<void()> callback;return callback;}
inline void pulseLegacyProjection(){auto callback=legacyProjectionPulse();if(callback)callback();}
struct LegacyProjectionPulseScope {
    std::function<void()> previous;
    explicit LegacyProjectionPulseScope(std::function<void()> callback):previous(std::move(legacyProjectionPulse())){legacyProjectionPulse()=std::move(callback);}
    ~LegacyProjectionPulseScope(){legacyProjectionPulse()=std::move(previous);}
};
}
