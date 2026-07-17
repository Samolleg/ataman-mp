#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

#include <cstdio>
#include <cstring>

namespace {
constexpr unsigned char kExpectedGameSha256[32] = {
    0xBE, 0x0F, 0xC3, 0x30, 0x03, 0x5E, 0x03, 0x98,
    0x3C, 0xCE, 0x59, 0x00, 0x38, 0xCC, 0x7F, 0x88,
    0x1F, 0x1F, 0x5D, 0x1B, 0xCF, 0x08, 0x27, 0x17,
    0xF3, 0x17, 0x76, 0xDD, 0x4D, 0xEE, 0x94, 0xE2
};
constexpr uintptr_t kConsoleExecuteAddress = 0x005DC620;
constexpr uintptr_t kGameStringCtorAddress = 0x007009DE;
constexpr uintptr_t kGameStringDtorAddress = 0x00700970;
constexpr uintptr_t kServerHandlerAddress = 0x005DF490;
constexpr unsigned char kConsoleExecutePrologue[7] = {
    0x6A, 0xFF, 0x68, 0x80, 0x89, 0x72, 0x00
};
constexpr char kMarkerPrefix[] = "endurance_launch ";

void* g_consoleTrampoline = nullptr;
volatile LONG g_launchHandled = 0;
wchar_t g_expectedMap[32] = {};
wchar_t g_resultPath[MAX_PATH * 4] = {};

void Log(const wchar_t* message)
{
    if(!g_resultPath[0])
        return;
    HANDLE file = CreateFileW(g_resultPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(file == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME time{};
    GetSystemTime(&time);
    wchar_t line[1024]{};
    const int count = _snwprintf_s(line, sizeof(line) / sizeof(line[0]), _TRUNCATE,
        L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ %s\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds, message);
    if(count > 0)
    {
        char utf8[4096]{};
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, count, utf8,
                                               int(sizeof(utf8)), nullptr, nullptr);
        DWORD written = 0;
        if(bytes > 0)
            WriteFile(file, utf8, DWORD(bytes), &written, nullptr);
    }
    CloseHandle(file);
}

bool HashFileSha256(const wchar_t* path, unsigned char output[32])
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(file == INVALID_HANDLE_VALUE)
        return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool success = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES,
                                       CRYPT_VERIFYCONTEXT) != FALSE &&
                   CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    unsigned char buffer[64 * 1024];
    while(success)
    {
        DWORD read = 0;
        if(!ReadFile(file, buffer, sizeof(buffer), &read, nullptr))
        {
            success = false;
            break;
        }
        if(read == 0)
            break;
        success = CryptHashData(hash, buffer, read, 0) != FALSE;
    }
    if(success)
    {
        DWORD size = 32;
        success = CryptGetHashParam(hash, HP_HASHVAL, output, &size, 0) != FALSE && size == 32;
    }
    if(hash)
        CryptDestroyHash(hash);
    if(provider)
        CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return success;
}

bool VerifyRuntime()
{
    wchar_t gamePath[MAX_PATH * 4]{};
    if(!GetModuleFileNameW(nullptr, gamePath, DWORD(sizeof(gamePath) / sizeof(gamePath[0]))))
        return false;
    unsigned char hash[32]{};
    if(!HashFileSha256(gamePath, hash) ||
       std::memcmp(hash, kExpectedGameSha256, sizeof(hash)) != 0)
        return false;
    return std::memcmp(reinterpret_cast<const void*>(kConsoleExecuteAddress),
                       kConsoleExecutePrologue, sizeof(kConsoleExecutePrologue)) == 0;
}

bool IsValidTemporaryMap(const char* map)
{
    if(!map || std::strlen(map) != 10 || map[0] != 'e' || map[1] != 'd')
        return false;
    for(int index = 2; index < 10; ++index)
    {
        const char value = map[index];
        if(!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
            return false;
    }
    return true;
}

struct GameString { char* value; };
struct FakeCommandArguments
{
    unsigned long unused = 0;
    GameString* arguments = nullptr;
    unsigned long count = 0;
};

bool QueueServerLaunch(const char* map)
{
    using GameStringCtor = GameString* (__thiscall*)(GameString*, const char*);
    using GameStringDtor = void (__thiscall*)(GameString*);
    using ServerHandler = void (__thiscall*)(FakeCommandArguments*);
    auto ctor = reinterpret_cast<GameStringCtor>(kGameStringCtorAddress);
    auto dtor = reinterpret_cast<GameStringDtor>(kGameStringDtorAddress);
    auto handler = reinterpret_cast<ServerHandler>(kServerHandlerAddress);

    GameString argument{};
    ctor(&argument, map);
    FakeCommandArguments command;
    command.arguments = &argument;
    command.count = 1;
    handler(&command);
    dtor(&argument);
    return true;
}

extern "C" int __stdcall TryHandleCommand(const char* command)
{
    if(!command || std::strncmp(command, kMarkerPrefix, sizeof(kMarkerPrefix) - 1) != 0)
        return 0;
    const char* map = command + sizeof(kMarkerPrefix) - 1;
    if(!IsValidTemporaryMap(map))
    {
        Log(L"Rejected malformed Endurance launch marker");
        return 1;
    }
    char expected[32]{};
    WideCharToMultiByte(CP_ACP, 0, g_expectedMap, -1, expected, int(sizeof(expected)), nullptr, nullptr);
    if(std::strcmp(map, expected) != 0)
    {
        Log(L"Rejected Endurance launch marker that did not match the inherited manifest map");
        return 1;
    }
    if(InterlockedCompareExchange(&g_launchHandled, 1, 0) != 0)
        return 1;
    Log(L"Accepted Endurance launch marker; invoking the fingerprinted engine server handler");
    QueueServerLaunch(map);
    Log(L"Engine server launch request queued");
    return 1;
}

__declspec(naked) void ConsoleExecuteHook()
{
    __asm {
        pushfd
        pushad
        push ecx
        call TryHandleCommand
        test eax, eax
        jz pass_through
        popad
        popfd
        ret
    pass_through:
        popad
        popfd
        jmp dword ptr [g_consoleTrampoline]
    }
}

bool InstallHook()
{
    auto* target = reinterpret_cast<unsigned char*>(kConsoleExecuteAddress);
    constexpr SIZE_T overwritten = sizeof(kConsoleExecutePrologue);
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, overwritten + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if(!trampoline)
        return false;
    std::memcpy(trampoline, target, overwritten);
    trampoline[overwritten] = 0xE9;
    *reinterpret_cast<long*>(trampoline + overwritten + 1) =
        long((target + overwritten) - (trampoline + overwritten + 5));
    g_consoleTrampoline = trampoline;

    DWORD oldProtect = 0;
    if(!VirtualProtect(target, overwritten, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    target[0] = 0xE9;
    *reinterpret_cast<long*>(target + 1) =
        long(reinterpret_cast<unsigned char*>(&ConsoleExecuteHook) - (target + 5));
    target[5] = 0x90;
    target[6] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, overwritten);
    DWORD ignored = 0;
    VirtualProtect(target, overwritten, oldProtect, &ignored);
    return true;
}

DWORD WINAPI InitializeBridge(void*)
{
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_MAP", g_expectedMap,
                            DWORD(sizeof(g_expectedMap) / sizeof(g_expectedMap[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_BRIDGE_RESULT", g_resultPath,
                            DWORD(sizeof(g_resultPath) / sizeof(g_resultPath[0])));
    if(!g_expectedMap[0] || !g_resultPath[0])
        return 0;
    Log(L"EI Mapper Endurance JMV bridge initializing");
    if(!VerifyRuntime())
    {
        Log(L"Bridge disabled: game hash or hook signature mismatch");
        return 0;
    }
    if(!InstallHook())
    {
        Log(L"Bridge disabled: console transition hook installation failed");
        return 0;
    }
    Log(L"Bridge ready; waiting for the private Endurance launch marker");
    return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if(reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, InitializeBridge, nullptr, 0, nullptr);
        if(thread)
            CloseHandle(thread);
    }
    return TRUE;
}
