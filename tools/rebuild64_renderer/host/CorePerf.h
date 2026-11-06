#pragma once
// Probe-owned diagnostic substitute. No engine code or gameplay state.
#include <windows.h>
#include <cstdint>
#include "GameJournal.h"
namespace CorePerf {
enum Partition { VisualFrameRun, RenderTerrain, SceneSyncWait, LimiterWait, PresentSlotWait };
struct Scope { explicit Scope(Partition) noexcept {} };
using AnyThreadScope = Scope;
inline unsigned BeginFrameDetail(const char*, LONGLONG) noexcept { return 0; }
inline void EndFrameDetail(unsigned, LONGLONG) noexcept {}
struct DiagnosticFrame { std::uint64_t frameNumber = 0; };
inline DiagnosticFrame& Get() noexcept { static DiagnosticFrame value; return value; }
}
