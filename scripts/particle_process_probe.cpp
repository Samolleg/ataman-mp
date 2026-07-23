#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace {

template<typename T>
bool readValue(HANDLE process, std::uintptr_t address, T& value)
{
    SIZE_T read = 0;
    return ReadProcessMemory(process, reinterpret_cast<const void*>(address),
                             &value, sizeof(value), &read)
        && read == sizeof(value);
}

void printBytes(HANDLE process, std::uintptr_t address, std::size_t count)
{
    std::array<std::uint8_t, 256> bytes{};
    if(count > bytes.size())
        count = bytes.size();
    SIZE_T read = 0;
    if(!ReadProcessMemory(process, reinterpret_cast<const void*>(address),
                          bytes.data(), count, &read))
    {
        std::printf(" unreadable");
        return;
    }
    for(SIZE_T index = 0; index < read; ++index)
        std::printf("%02X", bytes[index]);
}

void printRtti(HANDLE process, std::uint32_t vtable)
{
    std::uint32_t locator = 0;
    std::uint32_t typeDescriptor = 0;
    std::array<char, 128> name{};
    SIZE_T read = 0;
    if(readValue(process, vtable - 4u, locator)
            && readValue(process, locator + 12u, typeDescriptor)
            && ReadProcessMemory(process,
                                 reinterpret_cast<const void*>(typeDescriptor + 8u),
                                 name.data(), name.size() - 1, &read))
        std::printf("rtti=%s\n", name.data());
}

void printObjectSlot(HANDLE process, const char* label, std::uint32_t object,
                     std::uint32_t slot)
{
    std::uint32_t vtable = 0;
    std::uint32_t target = 0;
    if(!object || !readValue(process, object, vtable)
            || !readValue(process, vtable + slot, target))
        return;
    std::printf("%s=0x%08X vtable=0x%08X slot_%02X=0x%08X bytes=",
                label, object, vtable, slot, target);
    printBytes(process, target, 64);
    std::printf("\n");
}

void printParticlePipeline(HANDLE process)
{
    constexpr std::uint32_t particleRenderer = 0x007AF060u;
    std::uint32_t rendererState = 0;
    std::uint32_t pipeline = 0;
    printObjectSlot(process, "static_particle_renderer", particleRenderer, 0x44u);
    if(readValue(process, particleRenderer + 8u, rendererState)
            && readValue(process, rendererState + 0x0cu, pipeline))
        printObjectSlot(process, "particle_pipeline", pipeline, 0x60u);
}

bool captureParticleRenderer(HANDLE process, DWORD pid)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if(snapshot == INVALID_HANDLE_VALUE)
        return false;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    std::array<DWORD, 64> threadIds{};
    unsigned threadCount = 0;
    if(Thread32First(snapshot, &entry))
    {
        do
        {
            if(entry.th32OwnerProcessID == pid && threadCount < threadIds.size())
                threadIds[threadCount++] = entry.th32ThreadID;
        } while(Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);

    for(unsigned pass = 0; pass != 20000; ++pass)
    {
        for(unsigned index = 0; index != threadCount; ++index)
        {
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT
                                           | THREAD_QUERY_INFORMATION,
                                       FALSE, threadIds[index]);
            if(!thread)
                continue;
            if(SuspendThread(thread) != static_cast<DWORD>(-1))
            {
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if(GetThreadContext(thread, &context)
                        && context.Eip >= 0x004976B0u
                        && context.Eip < 0x00497CD5u)
                {
                    std::uint32_t renderer = 0;
                    if(readValue(process, context.Ebp + 8u, renderer))
                    {
                        std::printf("captured_eip=0x%08lX thread=%lu ",
                                    context.Eip, threadIds[index]);
                        printObjectSlot(process, "particle_renderer", renderer,
                                        0x44u);
                        ResumeThread(thread);
                        CloseHandle(thread);
                        return true;
                    }
                }
                ResumeThread(thread);
            }
            CloseHandle(thread);
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    if(argc != 2 && argc != 3)
    {
        std::fprintf(stderr, "usage: particle_process_probe GAME_PID\n");
        return 2;
    }
    const DWORD pid = std::strtoul(argv[1], nullptr, 10);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION
                                     | PROCESS_VM_READ,
                                 FALSE, pid);
    if(!process)
    {
        std::fprintf(stderr, "OpenProcess failed: %lu\n", GetLastError());
        return 3;
    }

    wchar_t path[32768]{};
    DWORD pathLength = 32768;
    if(QueryFullProcessImageNameW(process, 0, path, &pathLength))
        ::fwprintf(stdout, L"process=%ls\n", path);

    constexpr std::uintptr_t world = 0x007AF5B8u;
    std::uint32_t worldVtable = 0;
    std::uint32_t rendererGetter = 0;
    if(!readValue(process, world, worldVtable)
            || !readValue(process, worldVtable + 0x90u, rendererGetter))
    {
        std::fprintf(stderr, "failed to read world vtable\n");
        CloseHandle(process);
        return 4;
    }
    std::printf("world=0x%08X vtable=0x%08X slot_90=0x%08X bytes=",
                static_cast<unsigned>(world), worldVtable, rendererGetter);
    printBytes(process, rendererGetter, 32);
    std::printf("\n");
    printRtti(process, worldVtable);
    std::printf("world_bytes=");
    printBytes(process, world, 256);
    std::printf("\n");

    std::uint32_t renderer = 0;
    std::uint32_t rendererVtable = 0;
    std::uint32_t particleSubmit = 0;
    if(readValue(process, world + 0x19cu, renderer)
            && readValue(process, renderer, rendererVtable)
            && readValue(process, rendererVtable + 0x44u, particleSubmit))
    {
        std::printf("renderer=0x%08X vtable=0x%08X slot_44=0x%08X bytes=",
                    renderer, rendererVtable, particleSubmit);
        printBytes(process, particleSubmit, 256);
        std::printf("\n");
        printRtti(process, rendererVtable);
    }
    else
    {
        std::fprintf(stderr, "failed to read renderer at world+0x19C\n");
        CloseHandle(process);
        return 5;
    }

    std::uint32_t renderRoot = 0;
    std::uint32_t renderDevice = 0;
    if(readValue(process, 0x007AF080u, renderRoot))
    {
        printObjectSlot(process, "render_root", renderRoot, 0x44u);
        if(readValue(process, renderRoot + 0x54u, renderDevice))
            printObjectSlot(process, "render_device", renderDevice, 0x44u);
    }
    printParticlePipeline(process);
    if(argc == 3 && std::strcmp(argv[2], "capture") == 0
            && !captureParticleRenderer(process, pid))
        std::fprintf(stderr, "particle renderer was not observed\n");

    CloseHandle(process);
    return 0;
}
