#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <cctype>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

namespace {
constexpr unsigned char kExpectedGameSha256[32] = {
    0xBE, 0x0F, 0xC3, 0x30, 0x03, 0x5E, 0x03, 0x98,
    0x3C, 0xCE, 0x59, 0x00, 0x38, 0xCC, 0x7F, 0x88,
    0x1F, 0x1F, 0x5D, 0x1B, 0xCF, 0x08, 0x27, 0x17,
    0xF3, 0x17, 0x76, 0xDD, 0x4D, 0xEE, 0x94, 0xE2
};
constexpr unsigned char kExpectedBaseGameSha256[32] = {
    0xB8, 0x71, 0xD7, 0x0D, 0x38, 0x76, 0xB2, 0xA5,
    0x63, 0x4F, 0x09, 0x20, 0xC9, 0x21, 0x18, 0xBC,
    0x49, 0x05, 0x8B, 0x50, 0xC7, 0xD8, 0xCB, 0xE6,
    0x83, 0x76, 0x34, 0x9F, 0x51, 0xC2, 0xE5, 0x70
};

using CreateFileAFn = HANDLE (WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                      DWORD, DWORD, HANDLE);
using CreateFileWFn = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                      DWORD, DWORD, HANDLE);
using GetFileAttributesAFn = DWORD (WINAPI*)(LPCSTR);
using GetFileAttributesWFn = DWORD (WINAPI*)(LPCWSTR);
using SetFileAttributesAFn = BOOL (WINAPI*)(LPCSTR, DWORD);
using SetFileAttributesWFn = BOOL (WINAPI*)(LPCWSTR, DWORD);
using CreateDirectoryAFn = BOOL (WINAPI*)(LPCSTR, LPSECURITY_ATTRIBUTES);
using CreateDirectoryWFn = BOOL (WINAPI*)(LPCWSTR, LPSECURITY_ATTRIBUTES);
using RemoveDirectoryAFn = BOOL (WINAPI*)(LPCSTR);
using RemoveDirectoryWFn = BOOL (WINAPI*)(LPCWSTR);
using DeleteFileAFn = BOOL (WINAPI*)(LPCSTR);
using DeleteFileWFn = BOOL (WINAPI*)(LPCWSTR);
using FindFirstFileAFn = HANDLE (WINAPI*)(LPCSTR, LPWIN32_FIND_DATAA);
using FindFirstFileWFn = HANDLE (WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
using MoveFileAFn = BOOL (WINAPI*)(LPCSTR, LPCSTR);
using MoveFileWFn = BOOL (WINAPI*)(LPCWSTR, LPCWSTR);
using MoveFileExAFn = BOOL (WINAPI*)(LPCSTR, LPCSTR, DWORD);
using MoveFileExWFn = BOOL (WINAPI*)(LPCWSTR, LPCWSTR, DWORD);
using CopyFileAFn = BOOL (WINAPI*)(LPCSTR, LPCSTR, BOOL);
using CopyFileWFn = BOOL (WINAPI*)(LPCWSTR, LPCWSTR, BOOL);
using CaptureStackFn = USHORT (WINAPI*)(ULONG, ULONG, PVOID*, PULONG);
using MapLoadFn = int (__thiscall*)(void*, const char*, const char*, void*);
using ZoneTransitionFn = int (__thiscall*)(void*, const char*, int);
using NewGameBootstrapFn = void (__thiscall*)(void*);
using PlayerDefaultsResetFn = void (__thiscall*)(void*);
using GameStringAssignFn = void (__thiscall*)(void*, const char*);

HMODULE g_instance = nullptr;
volatile LONG g_initialized = 0;
volatile LONG g_observationCount = 0;
wchar_t g_resultPath[MAX_PATH * 4] = {};
CreateFileAFn g_originalCreateFileA = nullptr;
CreateFileWFn g_originalCreateFileW = nullptr;
GetFileAttributesAFn g_originalGetFileAttributesA = nullptr;
GetFileAttributesWFn g_originalGetFileAttributesW = nullptr;
SetFileAttributesAFn g_originalSetFileAttributesA = nullptr;
SetFileAttributesWFn g_originalSetFileAttributesW = nullptr;
CreateDirectoryAFn g_originalCreateDirectoryA = nullptr;
CreateDirectoryWFn g_originalCreateDirectoryW = nullptr;
RemoveDirectoryAFn g_originalRemoveDirectoryA = nullptr;
RemoveDirectoryWFn g_originalRemoveDirectoryW = nullptr;
DeleteFileAFn g_originalDeleteFileA = nullptr;
DeleteFileWFn g_originalDeleteFileW = nullptr;
FindFirstFileAFn g_originalFindFirstFileA = nullptr;
FindFirstFileWFn g_originalFindFirstFileW = nullptr;
MoveFileAFn g_originalMoveFileA = nullptr;
MoveFileWFn g_originalMoveFileW = nullptr;
MoveFileExAFn g_originalMoveFileExA = nullptr;
MoveFileExWFn g_originalMoveFileExW = nullptr;
CopyFileAFn g_originalCopyFileA = nullptr;
CopyFileWFn g_originalCopyFileW = nullptr;
CaptureStackFn g_captureStack = nullptr;

constexpr DWORD kMapLoadEntryRva = 0x00195480;
constexpr DWORD kStartZoneTransitionRva = 0x0017C460;
constexpr DWORD kStartZoneMapLoadReturnRva = 0x0017A61B;
constexpr DWORD kNewGameBootstrapRva = 0x0017BD10;
constexpr DWORD kPlayerDefaultsResetRva = 0x000A5BD0;
constexpr DWORD kPlayerDefaultsManagerRva = 0x003AA5C4;
constexpr DWORD kGameStringAssignRva = 0x00300AF9;
constexpr UINT kDirectLaunchMessage = WM_APP + 0x45D;
constexpr UINT kHotReloadMessage = WM_APP + 0x45E;
constexpr unsigned char kMapLoadEntryBytes[6] = {
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00
};
constexpr unsigned char kStartZoneTransitionBytes[7] = {
    0x6A, 0xFF, 0x68, 0x28, 0x39, 0x72, 0x00
};
constexpr unsigned char kNewGameBootstrapBytes[7] = {
    0x6A, 0xFF, 0x68, 0xF0, 0x37, 0x72, 0x00
};
constexpr unsigned char kPlayerDefaultsResetBytes[6] = {
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00
};
enum class RuntimeState : LONG
{
    Unknown = 0,
    Initializing = 1,
    MainMenuReady = 2,
    GameplayReady = 3,
    InventoryOpen = 4,
    StoreOpen = 5,
    WorldMapReady = 6,
    Transitioning = 7
};

MapLoadFn g_originalMapLoad = nullptr;
ZoneTransitionFn g_originalZoneTransition = nullptr;
PlayerDefaultsResetFn g_originalPlayerDefaultsReset = nullptr;
HMODULE g_gameModule = nullptr;
HHOOK g_directLaunchHook = nullptr;
volatile LONG g_directLaunchState = 0;
volatile LONG g_pendingMapLoadProbeCount = 0;
DWORD g_gameThreadId = 0;
void* g_zoneTransitionController = nullptr;
char g_temporaryMpr[MAX_PATH * 4] = {};
char g_temporaryMob[MAX_PATH * 4] = {};
char g_sourceZone[128] = {};
int g_targetExit = -1;
int g_targetExitId = 0;
wchar_t g_liveSaves[MAX_PATH * 4] = {};
wchar_t g_testSaves[MAX_PATH * 4] = {};
CRITICAL_SECTION g_savePathLock{};
volatile LONG g_savePathLockReady = 0;
wchar_t g_godmodeMob[MAX_PATH * 4] = {};
wchar_t g_supplementalMobDirectory[MAX_PATH * 4] = {};
wchar_t g_hotReloadRequestPath[MAX_PATH * 4] = {};
wchar_t g_hotReloadThreadPath[MAX_PATH * 4] = {};
wchar_t g_hotReloadRoot[MAX_PATH * 4] = {};
ULONGLONG g_hotReloadSequence = 0;
ULONGLONG g_saveGeneration = 0;
volatile LONG g_runtimeState = LONG(RuntimeState::Initializing);
volatile LONG g_runtimeStateSequence = 0;
volatile LONG g_runtimeStateTick = 0;
volatile LONG g_pendingStateAwareReload = 0;
volatile LONG g_saveRedirectLogCount = 0;
volatile LONG g_godmodeRedirectLogged = 0;
volatile LONG g_introSkipMask = 0;
int g_baseProbeLevel = 0;

int __fastcall MapLoadDetour(void* self, void*, const char* mprPath,
                             const char* mobPath, void* context);
int __fastcall ZoneTransitionDetour(void* self, void*, const char* zoneName,
                                    int requestedMode);
void __fastcall PlayerDefaultsResetDetour(void* self, void*);
LRESULT CALLBACK DirectLaunchMessageHook(int code, WPARAM wParam, LPARAM lParam);

struct HotReloadControl
{
    int protocol = 0;
    ULONGLONG sequence = 0;
    ULONGLONG generation = 0;
    ULONGLONG runtimeStateSequence = 0;
    int entranceId = 0;
    wchar_t mprPath[MAX_PATH * 4] = {};
    wchar_t mobPath[MAX_PATH * 4] = {};
    wchar_t bootstrapMobPath[MAX_PATH * 4] = {};
    wchar_t savePath[MAX_PATH * 4] = {};
    unsigned char mprSha256[32] = {};
    unsigned char mobSha256[32] = {};
    unsigned char bootstrapMobSha256[32] = {};
    bool hasBootstrap = false;
    char sourceZone[128] = {};
    char zone[128] = {};
    char bootstrapMode[32] = {};
};

const wchar_t* FileNamePart(const wchar_t* path)
{
    if(!path)
        return nullptr;
    const wchar_t* slash = std::wcsrchr(path, L'\\');
    const wchar_t* forward = std::wcsrchr(path, L'/');
    if(!slash || (forward && forward > slash))
        slash = forward;
    return slash ? slash + 1 : path;
}

bool HasObservedExtension(const wchar_t* path)
{
    const wchar_t* name = FileNamePart(path);
    const wchar_t* extension = name ? std::wcsrchr(name, L'.') : nullptr;
    return extension &&
        (_wcsicmp(extension, L".mpr") == 0 ||
         _wcsicmp(extension, L".mob") == 0 ||
         _wcsicmp(extension, L".mq") == 0);
}

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
    wchar_t line[4096]{};
    const int count = _snwprintf_s(line, sizeof(line) / sizeof(line[0]), _TRUNCATE,
        L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ %s\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds, message);
    if(count > 0)
    {
        char utf8[16384]{};
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, count, utf8,
                                               int(sizeof(utf8)), nullptr, nullptr);
        DWORD written = 0;
        if(bytes > 0)
            WriteFile(file, utf8, DWORD(bytes), &written, nullptr);
    }
    CloseHandle(file);
}

const wchar_t* RuntimeStateName(RuntimeState state)
{
    switch(state)
    {
    case RuntimeState::Initializing: return L"Initializing";
    case RuntimeState::MainMenuReady: return L"MainMenuReady";
    case RuntimeState::GameplayReady: return L"GameplayReady";
    case RuntimeState::InventoryOpen: return L"InventoryOpen";
    case RuntimeState::StoreOpen: return L"StoreOpen";
    case RuntimeState::WorldMapReady: return L"WorldMapReady";
    case RuntimeState::Transitioning: return L"Transitioning";
    default: return L"Unknown";
    }
}

void SetRuntimeState(RuntimeState state, const wchar_t* evidence)
{
    InterlockedExchange(&g_runtimeStateTick, LONG(GetTickCount()));
    const LONG previous = InterlockedExchange(&g_runtimeState, LONG(state));
    if(previous == LONG(state))
        return;
    const LONG sequence = InterlockedIncrement(&g_runtimeStateSequence);
    wchar_t message[512]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"RUNTIME_STATE sequence=%ld from=%s to=%s evidence=%s",
                 sequence, RuntimeStateName(RuntimeState(previous)),
                 RuntimeStateName(state), evidence ? evidence : L"<none>");
    Log(message);
}

RuntimeState CurrentRuntimeState()
{
    return RuntimeState(InterlockedCompareExchange(&g_runtimeState, 0, 0));
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

bool ParseSha256(const char* text, unsigned char output[32])
{
    if(!text || std::strlen(text) != 64)
        return false;
    for(int index = 0; index < 32; ++index)
    {
        const auto nibble = [](char value) -> int
        {
            if(value >= '0' && value <= '9') return value - '0';
            value = char(std::tolower(static_cast<unsigned char>(value)));
            return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
        };
        const int high = nibble(text[index * 2]);
        const int low = nibble(text[index * 2 + 1]);
        if(high < 0 || low < 0)
            return false;
        output[index] = static_cast<unsigned char>((high << 4) | low);
    }
    return true;
}

bool HashMatches(const wchar_t* path, const unsigned char expected[32])
{
    unsigned char actual[32]{};
    return HashFileSha256(path, actual)
        && std::memcmp(actual, expected, sizeof(actual)) == 0;
}

bool Utf8ToWide(const char* source, wchar_t* output, int outputCount)
{
    return source && output && outputCount > 0
        && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1,
                               output, outputCount) > 0;
}

bool IsPathInsideHotReloadRoot(const wchar_t* path)
{
    if(!path || !path[0] || !g_hotReloadRoot[0])
        return false;
    wchar_t fullPath[MAX_PATH * 4]{};
    wchar_t fullRoot[MAX_PATH * 4]{};
    if(!GetFullPathNameW(path, DWORD(sizeof(fullPath) / sizeof(fullPath[0])), fullPath, nullptr)
       || !GetFullPathNameW(g_hotReloadRoot,
                           DWORD(sizeof(fullRoot) / sizeof(fullRoot[0])), fullRoot, nullptr))
        return false;
    size_t rootLength = std::wcslen(fullRoot);
    while(rootLength > 3 && (fullRoot[rootLength - 1] == L'\\'
                            || fullRoot[rootLength - 1] == L'/'))
        fullRoot[--rootLength] = L'\0';
    return _wcsnicmp(fullPath, fullRoot, rootLength) == 0
        && (fullPath[rootLength] == L'\\' || fullPath[rootLength] == L'/');
}

bool ReadHotReloadControl(HotReloadControl* output)
{
    if(!output || !g_hotReloadRequestPath[0]
       || !IsPathInsideHotReloadRoot(g_hotReloadRequestPath))
        return false;
    HANDLE file = CreateFileW(g_hotReloadRequestPath, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    bool success = GetFileSizeEx(file, &size) && size.QuadPart > 0
        && size.QuadPart < 64 * 1024;
    char bytes[64 * 1024]{};
    DWORD read = 0;
    if(success)
        success = ReadFile(file, bytes, DWORD(size.QuadPart), &read, nullptr)
            && read == DWORD(size.QuadPart);
    CloseHandle(file);
    if(!success)
        return false;
    bytes[read] = '\0';

    HotReloadControl parsed;
    bool haveMprHash = false;
    bool haveMobHash = false;
    bool haveProfile = false;
    bool haveRunId = false;
    char* context = nullptr;
    for(char* line = strtok_s(bytes, "\r\n", &context); line;
        line = strtok_s(nullptr, "\r\n", &context))
    {
        char* separator = std::strchr(line, '=');
        if(!separator) continue;
        *separator = '\0';
        const char* value = separator + 1;
        if(std::strcmp(line, "protocol") == 0) parsed.protocol = std::atoi(value);
        else if(std::strcmp(line, "sequence") == 0) parsed.sequence = _strtoui64(value, nullptr, 10);
        else if(std::strcmp(line, "generation") == 0) parsed.generation = _strtoui64(value, nullptr, 10);
        else if(std::strcmp(line, "runtimeStateSequence") == 0)
            parsed.runtimeStateSequence = _strtoui64(value, nullptr, 10);
        else if(std::strcmp(line, "runId") == 0) haveRunId = value[0] != '\0';
        else if(std::strcmp(line, "profile") == 0)
        {
            haveProfile = _stricmp(value, "jamais-vu") == 0;
            if(!haveProfile) return false;
        }
        else if(std::strcmp(line, "zone") == 0)
        {
            if(strcpy_s(parsed.zone, value) != 0) return false;
        }
        else if(std::strcmp(line, "sourceZone") == 0)
        {
            if(strcpy_s(parsed.sourceZone, value) != 0) return false;
        }
        else if(std::strcmp(line, "bootstrapMode") == 0)
        {
            if(strcpy_s(parsed.bootstrapMode, value) != 0) return false;
        }
        else if(std::strcmp(line, "entrance") == 0) parsed.entranceId = std::atoi(value);
        else if(std::strcmp(line, "mpr") == 0)
        {
            if(!Utf8ToWide(value, parsed.mprPath,
                           int(sizeof(parsed.mprPath) / sizeof(parsed.mprPath[0])))) return false;
        }
        else if(std::strcmp(line, "mob") == 0)
        {
            if(!Utf8ToWide(value, parsed.mobPath,
                           int(sizeof(parsed.mobPath) / sizeof(parsed.mobPath[0])))) return false;
        }
        else if(std::strcmp(line, "savePath") == 0)
        {
            if(!Utf8ToWide(value, parsed.savePath,
                           int(sizeof(parsed.savePath) / sizeof(parsed.savePath[0])))) return false;
        }
        else if(std::strcmp(line, "bootstrapMob") == 0 && value[0])
        {
            parsed.hasBootstrap = Utf8ToWide(value, parsed.bootstrapMobPath,
                int(sizeof(parsed.bootstrapMobPath) / sizeof(parsed.bootstrapMobPath[0])));
            if(!parsed.hasBootstrap) return false;
        }
        else if(std::strcmp(line, "mprSha256") == 0)
            haveMprHash = ParseSha256(value, parsed.mprSha256);
        else if(std::strcmp(line, "mobSha256") == 0)
            haveMobHash = ParseSha256(value, parsed.mobSha256);
        else if(std::strcmp(line, "bootstrapMobSha256") == 0 && value[0])
        {
            if(!ParseSha256(value, parsed.bootstrapMobSha256)) return false;
        }
    }
    for(const char* cursor = parsed.zone; *cursor; ++cursor)
        if(!std::isalnum(static_cast<unsigned char>(*cursor))
           && *cursor != '_' && *cursor != '-')
            return false;
    for(const char* cursor = parsed.sourceZone; *cursor; ++cursor)
        if(!std::isalnum(static_cast<unsigned char>(*cursor))
           && *cursor != '_' && *cursor != '-')
            return false;
    if(parsed.bootstrapMode[0]
       && _stricmp(parsed.bootstrapMode, "automatic") != 0
       && _stricmp(parsed.bootstrapMode, "transition") != 0
       && _stricmp(parsed.bootstrapMode, "new-game") != 0)
        return false;
    if(parsed.protocol != 2 || parsed.sequence == 0 || parsed.generation == 0
       || !haveProfile || !haveRunId
       || parsed.sequence <= g_hotReloadSequence || parsed.entranceId <= 0
       || parsed.entranceId > 1024
       || !parsed.zone[0] || !parsed.mprPath[0] || !parsed.mobPath[0]
       || !parsed.savePath[0]
       || !haveMprHash || !haveMobHash
       || !IsPathInsideHotReloadRoot(parsed.mprPath)
       || !IsPathInsideHotReloadRoot(parsed.mobPath)
       || !IsPathInsideHotReloadRoot(parsed.savePath)
       || !HashMatches(parsed.mprPath, parsed.mprSha256)
       || !HashMatches(parsed.mobPath, parsed.mobSha256))
        return false;
    const DWORD saveAttributes = GetFileAttributesW(parsed.savePath);
    if(saveAttributes == INVALID_FILE_ATTRIBUTES || !(saveAttributes & FILE_ATTRIBUTE_DIRECTORY)
       || (saveAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return false;
    if(parsed.hasBootstrap
       && (!IsPathInsideHotReloadRoot(parsed.bootstrapMobPath)
           || !HashMatches(parsed.bootstrapMobPath, parsed.bootstrapMobSha256)))
        return false;
    *output = parsed;
    return true;
}

void PublishHotReloadThreadId()
{
    if(!g_hotReloadThreadPath[0] || !IsPathInsideHotReloadRoot(g_hotReloadThreadPath))
        return;
    HANDLE file = CreateFileW(g_hotReloadThreadPath, GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(file == INVALID_HANDLE_VALUE)
        return;
    char value[32]{};
    const int length = sprintf_s(value, "%lu\r\n", g_gameThreadId);
    DWORD written = 0;
    if(length > 0)
        WriteFile(file, value, DWORD(length), &written, nullptr);
    CloseHandle(file);
}

enum class RuntimeBuild { Unsupported, JmvStarter, BaseEi };

RuntimeBuild VerifyRuntime()
{
    wchar_t gamePath[MAX_PATH * 4]{};
    if(!GetModuleFileNameW(nullptr, gamePath, DWORD(sizeof(gamePath) / sizeof(gamePath[0]))))
        return RuntimeBuild::Unsupported;
    unsigned char hash[32]{};
    if(!HashFileSha256(gamePath, hash))
        return RuntimeBuild::Unsupported;
    if(std::memcmp(hash, kExpectedGameSha256, sizeof(hash)) == 0)
        return RuntimeBuild::JmvStarter;
    if(std::memcmp(hash, kExpectedBaseGameSha256, sizeof(hash)) == 0)
        return RuntimeBuild::BaseEi;
    return RuntimeBuild::Unsupported;
}

void AppendText(wchar_t* destination, size_t capacity, const wchar_t* text)
{
    if(!destination || !capacity || !text)
        return;
    const size_t used = std::wcslen(destination);
    if(used + 1 >= capacity)
        return;
    wcsncat_s(destination, capacity, text, capacity - used - 1);
}

void FormatStack(wchar_t* output, size_t capacity)
{
    if(!output || !capacity)
        return;
    output[0] = L'\0';
    if(!g_captureStack)
    {
        AppendText(output, capacity, L"unavailable");
        return;
    }
    void* frames[16]{};
    const USHORT count = g_captureStack(2, 16, frames, nullptr);
    for(USHORT index = 0; index < count; ++index)
    {
        MEMORY_BASIC_INFORMATION memory{};
        wchar_t modulePath[MAX_PATH * 4]{};
        const HMODULE module = VirtualQuery(frames[index], &memory, sizeof(memory)) == sizeof(memory)
            ? static_cast<HMODULE>(memory.AllocationBase) : nullptr;
        const wchar_t* moduleName = L"unknown";
        if(module && GetModuleFileNameW(module, modulePath,
                                       DWORD(sizeof(modulePath) / sizeof(modulePath[0]))))
            moduleName = FileNamePart(modulePath);
        wchar_t frame[192]{};
        _snwprintf_s(frame, sizeof(frame) / sizeof(frame[0]), _TRUNCATE,
                     index == 0 ? L"%s+0x%08lX" : L">%s+0x%08lX",
                     moduleName,
                     module ? DWORD(reinterpret_cast<uintptr_t>(frames[index]) -
                                    reinterpret_cast<uintptr_t>(module))
                            : DWORD(reinterpret_cast<uintptr_t>(frames[index])));
        AppendText(output, capacity, frame);
    }
}

void FormatAddress(void* address, wchar_t* output, size_t capacity)
{
    if(!output || !capacity)
        return;
    output[0] = L'\0';
    if(!address)
    {
        AppendText(output, capacity, L"unavailable");
        return;
    }

    MEMORY_BASIC_INFORMATION memory{};
    wchar_t modulePath[MAX_PATH * 4]{};
    const HMODULE module = VirtualQuery(address, &memory, sizeof(memory)) == sizeof(memory)
        ? static_cast<HMODULE>(memory.AllocationBase) : nullptr;
    const wchar_t* moduleName = L"unknown";
    if(module && GetModuleFileNameW(module, modulePath,
                                   DWORD(sizeof(modulePath) / sizeof(modulePath[0]))))
        moduleName = FileNamePart(modulePath);
    _snwprintf_s(output, capacity, _TRUNCATE, L"%s+0x%08lX@0x%08lX",
                 moduleName,
                 module ? DWORD(reinterpret_cast<uintptr_t>(address) -
                                reinterpret_cast<uintptr_t>(module))
                        : DWORD(reinterpret_cast<uintptr_t>(address)),
                 DWORD(reinterpret_cast<uintptr_t>(address)));
}

void Observe(const wchar_t* operation, const wchar_t* path, ULONG_PTR result,
             DWORD error, void* caller, DWORD access = 0, DWORD creation = 0)
{
    if(!HasObservedExtension(path))
        return;
    const LONG sequence = InterlockedIncrement(&g_observationCount);
    if(sequence > 512)
        return;
    wchar_t stack[2500]{};
    FormatStack(stack, sizeof(stack) / sizeof(stack[0]));
    wchar_t callerText[256]{};
    FormatAddress(caller, callerText, sizeof(callerText) / sizeof(callerText[0]));
    wchar_t message[3900]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"TRANSITION_PROBE seq=%ld thread=%lu op=%s path=\"%s\" result=0x%08lX error=%lu access=0x%08lX creation=%lu caller=%s stack=%s",
                 sequence, GetCurrentThreadId(), operation, path ? path : L"<null>",
                 DWORD(result), error, access, creation, callerText, stack);
    Log(message);
}

bool IsPathBoundary(wchar_t value)
{
    return value == L'\0' || value == L'\\' || value == L'/';
}

void NormalizeSlashes(wchar_t* path)
{
    if(!path)
        return;
    for(wchar_t* cursor = path; *cursor; ++cursor)
        if(*cursor == L'/')
            *cursor = L'\\';
}

bool RewriteSavePathW(LPCWSTR path, wchar_t* output, size_t capacity)
{
    if(!path || !output || !capacity || !g_liveSaves[0] || !g_testSaves[0])
        return false;
    wchar_t activeSaves[MAX_PATH * 4]{};
    if(InterlockedCompareExchange(&g_savePathLockReady, 0, 0))
    {
        EnterCriticalSection(&g_savePathLock);
        wcscpy_s(activeSaves, g_testSaves);
        LeaveCriticalSection(&g_savePathLock);
    }
    else
        wcscpy_s(activeSaves, g_testSaves);
    wchar_t full[MAX_PATH * 4]{};
    const DWORD length = GetFullPathNameW(path, DWORD(sizeof(full) / sizeof(full[0])), full, nullptr);
    if(!length || length >= sizeof(full) / sizeof(full[0]))
        return false;
    NormalizeSlashes(full);
    const size_t liveLength = std::wcslen(g_liveSaves);
    if(_wcsnicmp(full, g_liveSaves, liveLength) != 0 || !IsPathBoundary(full[liveLength]))
        return false;
    const wchar_t* suffix = full + liveLength;
    if(_snwprintf_s(output, capacity, _TRUNCATE, L"%s%s", activeSaves, suffix) < 0)
        return false;
    const LONG sequence = InterlockedIncrement(&g_saveRedirectLogCount);
    if(sequence <= 64)
    {
        wchar_t message[1900]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"SAVE_REDIRECT seq=%ld from=\"%s\" to=\"%s\"", sequence, full, output);
        Log(message);
    }
    return true;
}

bool RewriteSavePathA(LPCSTR path, char* output, size_t capacity)
{
    if(!path || !output || !capacity)
        return false;
    wchar_t wide[MAX_PATH * 4]{};
    wchar_t rewritten[MAX_PATH * 4]{};
    if(!MultiByteToWideChar(CP_ACP, 0, path, -1, wide,
                            int(sizeof(wide) / sizeof(wide[0]))) ||
       !RewriteSavePathW(wide, rewritten, sizeof(rewritten) / sizeof(rewritten[0])))
        return false;
    BOOL substituted = FALSE;
    return WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, rewritten, -1,
                               output, int(capacity), nullptr, &substituted) != 0 && !substituted;
}

bool CommitSaveGeneration(const HotReloadControl& request)
{
    if(request.generation == g_saveGeneration)
        return _wcsicmp(g_testSaves, request.savePath) == 0;
    if(request.generation < g_saveGeneration || !IsPathInsideHotReloadRoot(request.savePath))
        return false;
    wchar_t pattern[MAX_PATH * 4]{};
    if(_snwprintf_s(pattern, sizeof(pattern) / sizeof(pattern[0]), _TRUNCATE,
                    L"%s\\*", request.savePath) < 0)
        return false;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern, &data);
    if(find != INVALID_HANDLE_VALUE)
    {
        bool nonEmpty = false;
        do
        {
            if(wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0)
            {
                nonEmpty = true;
                break;
            }
        } while(FindNextFileW(find, &data));
        FindClose(find);
        if(nonEmpty)
            return false;
    }
    EnterCriticalSection(&g_savePathLock);
    const bool copied = wcscpy_s(g_testSaves, request.savePath) == 0;
    if(copied)
        g_saveGeneration = request.generation;
    LeaveCriticalSection(&g_savePathLock);
    if(!copied)
        return false;
    wchar_t message[1800]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"SAVE_GENERATION committed generation=%llu path=\"%s\"",
                 request.generation, request.savePath);
    Log(message);
    return true;
}

bool ShouldSkipIntroW(LPCWSTR path)
{
    wchar_t enabled[8]{};
    if(!path || !GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_SKIP_INTROS", enabled,
                                          DWORD(sizeof(enabled) / sizeof(enabled[0]))) ||
       enabled[0] != L'1')
        return false;
    const wchar_t* name = FileNamePart(path);
    LONG bit = 0;
    if(name && _wcsicmp(name, L"1c.bik") == 0)
        bit = 1;
    else if(name && _wcsicmp(name, L"nival.bik") == 0)
        bit = 2;
    if(!bit)
        return false;
    if(!(InterlockedOr(&g_introSkipMask, bit) & bit))
    {
        wchar_t message[512]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"INTRO_SKIP hid test-run movie path=\"%s\"", path);
        Log(message);
    }
    SetLastError(ERROR_FILE_NOT_FOUND);
    return true;
}

bool ShouldSkipIntroA(LPCSTR path)
{
    if(!path)
        return false;
    wchar_t wide[MAX_PATH * 4]{};
    return MultiByteToWideChar(CP_ACP, 0, path, -1, wide,
                               int(sizeof(wide) / sizeof(wide[0]))) && ShouldSkipIntroW(wide);
}

bool RewriteGodmodeMobW(LPCWSTR path, wchar_t* output, size_t capacity)
{
    if(!path || !output || !capacity || !g_godmodeMob[0])
        return false;
    const wchar_t* name = FileNamePart(path);
    if(!name || _wcsicmp(name, L"AutoRunSpellAddon123.mob") != 0)
        return false;
    if(wcsncpy_s(output, capacity, g_godmodeMob, _TRUNCATE) != 0)
        return false;
    if(InterlockedCompareExchange(&g_godmodeRedirectLogged, 1, 0) == 0)
    {
        wchar_t message[1800]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"RUNTIME_BOOTSTRAP redirect from=\"%s\" to=\"%s\"",
                     path, output);
        Log(message);
    }
    return true;
}

bool RewriteGodmodeMobA(LPCSTR path, char* output, size_t capacity)
{
    if(!path || !output || !capacity)
        return false;
    wchar_t wide[MAX_PATH * 4]{};
    wchar_t rewritten[MAX_PATH * 4]{};
    if(!MultiByteToWideChar(CP_ACP, 0, path, -1, wide,
                            int(sizeof(wide) / sizeof(wide[0]))) ||
       !RewriteGodmodeMobW(wide, rewritten,
                           sizeof(rewritten) / sizeof(rewritten[0])))
        return false;
    BOOL substituted = FALSE;
    return WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, rewritten, -1,
                               output, int(capacity), nullptr, &substituted) != 0 && !substituted;
}

bool RewriteSupplementalMobW(LPCWSTR path, wchar_t* output, size_t capacity)
{
    if(!path || !output || !capacity || !g_supplementalMobDirectory[0])
        return false;
    const wchar_t* name = FileNamePart(path);
    const wchar_t* extension = name ? std::wcsrchr(name, L'.') : nullptr;
    if(!name || !extension || _wcsicmp(extension, L".mob") != 0
       || _wcsicmp(name, L"AutoRunSpellAddon123.mob") == 0)
        return false;
    if(_snwprintf_s(output, capacity, _TRUNCATE, L"%s\\%s",
                    g_supplementalMobDirectory, name) < 0)
        return false;
    const DWORD attributes = g_originalGetFileAttributesW
        ? g_originalGetFileAttributesW(output) : GetFileAttributesW(output);
    if(attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    wchar_t message[1800]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"SUPPLEMENTAL_MOB redirect from=\"%s\" to=\"%s\"", path, output);
    Log(message);
    return true;
}

bool RewriteSupplementalMobA(LPCSTR path, char* output, size_t capacity)
{
    if(!path || !output || !capacity)
        return false;
    wchar_t wide[MAX_PATH * 4]{};
    wchar_t rewritten[MAX_PATH * 4]{};
    if(!MultiByteToWideChar(CP_ACP, 0, path, -1, wide,
                            int(sizeof(wide) / sizeof(wide[0])))
       || !RewriteSupplementalMobW(wide, rewritten,
                                   sizeof(rewritten) / sizeof(rewritten[0])))
        return false;
    BOOL substituted = FALSE;
    return WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, rewritten, -1,
                               output, int(capacity), nullptr, &substituted) != 0 && !substituted;
}

HANDLE WINAPI HookCreateFileW(LPCWSTR path, DWORD access, DWORD share,
                              LPSECURITY_ATTRIBUTES security, DWORD creation,
                              DWORD flags, HANDLE templateFile)
{
    void* caller = _ReturnAddress();
    if(ShouldSkipIntroW(path))
        return INVALID_HANDLE_VALUE;
    wchar_t redirected[MAX_PATH * 4]{};
    LPCWSTR selected = (RewriteGodmodeMobW(path, redirected, sizeof(redirected) / sizeof(redirected[0]))
        || RewriteSupplementalMobW(path, redirected, sizeof(redirected) / sizeof(redirected[0]))
        || RewriteSavePathW(path, redirected, sizeof(redirected) / sizeof(redirected[0]))) ? redirected : path;
    HANDLE result = g_originalCreateFileW(selected, access, share, security, creation, flags, templateFile);
    const DWORD error = GetLastError();
    Observe(L"CreateFileW", path, reinterpret_cast<ULONG_PTR>(result), error, caller, access, creation);
    SetLastError(error);
    return result;
}

HANDLE WINAPI HookCreateFileA(LPCSTR path, DWORD access, DWORD share,
                              LPSECURITY_ATTRIBUTES security, DWORD creation,
                              DWORD flags, HANDLE templateFile)
{
    void* caller = _ReturnAddress();
    if(ShouldSkipIntroA(path))
        return INVALID_HANDLE_VALUE;
    char redirected[MAX_PATH * 4]{};
    LPCSTR selected = (RewriteGodmodeMobA(path, redirected, sizeof(redirected))
        || RewriteSupplementalMobA(path, redirected, sizeof(redirected))
        || RewriteSavePathA(path, redirected, sizeof(redirected))) ? redirected : path;
    HANDLE result = g_originalCreateFileA(selected, access, share, security, creation, flags, templateFile);
    const DWORD error = GetLastError();
    if(path)
    {
        wchar_t pathW[MAX_PATH * 4]{};
        MultiByteToWideChar(CP_ACP, 0, path, -1, pathW,
                            int(sizeof(pathW) / sizeof(pathW[0])));
        Observe(L"CreateFileA", pathW, reinterpret_cast<ULONG_PTR>(result), error, caller, access, creation);
    }
    SetLastError(error);
    return result;
}

DWORD WINAPI HookGetFileAttributesW(LPCWSTR path)
{
    void* caller = _ReturnAddress();
    if(ShouldSkipIntroW(path))
        return INVALID_FILE_ATTRIBUTES;
    wchar_t redirected[MAX_PATH * 4]{};
    LPCWSTR selected = (RewriteGodmodeMobW(path, redirected, sizeof(redirected) / sizeof(redirected[0]))
        || RewriteSupplementalMobW(path, redirected, sizeof(redirected) / sizeof(redirected[0]))
        || RewriteSavePathW(path, redirected, sizeof(redirected) / sizeof(redirected[0]))) ? redirected : path;
    const DWORD result = g_originalGetFileAttributesW(selected);
    const DWORD error = GetLastError();
    Observe(L"GetFileAttributesW", path, result, error, caller);
    SetLastError(error);
    return result;
}

DWORD WINAPI HookGetFileAttributesA(LPCSTR path)
{
    void* caller = _ReturnAddress();
    if(ShouldSkipIntroA(path))
        return INVALID_FILE_ATTRIBUTES;
    char redirected[MAX_PATH * 4]{};
    LPCSTR selected = (RewriteGodmodeMobA(path, redirected, sizeof(redirected))
        || RewriteSupplementalMobA(path, redirected, sizeof(redirected))
        || RewriteSavePathA(path, redirected, sizeof(redirected))) ? redirected : path;
    const DWORD result = g_originalGetFileAttributesA(selected);
    const DWORD error = GetLastError();
    if(path)
    {
        wchar_t pathW[MAX_PATH * 4]{};
        MultiByteToWideChar(CP_ACP, 0, path, -1, pathW,
                            int(sizeof(pathW) / sizeof(pathW[0])));
        Observe(L"GetFileAttributesA", pathW, result, error, caller);
    }
    SetLastError(error);
    return result;
}

#define REDIRECT_ONE_W(name, original, resultType, failure, parameters, arguments) \
resultType WINAPI name parameters { wchar_t redirected[MAX_PATH * 4]{}; \
LPCWSTR selected = RewriteSavePathW(path, redirected, sizeof(redirected) / sizeof(redirected[0])) ? redirected : path; \
return original arguments; }
#define REDIRECT_ONE_A(name, original, resultType, failure, parameters, arguments) \
resultType WINAPI name parameters { char redirected[MAX_PATH * 4]{}; \
LPCSTR selected = RewriteSavePathA(path, redirected, sizeof(redirected)) ? redirected : path; \
return original arguments; }

REDIRECT_ONE_W(HookSetFileAttributesW, g_originalSetFileAttributesW, BOOL, FALSE,
               (LPCWSTR path, DWORD attributes), (selected, attributes))
REDIRECT_ONE_A(HookSetFileAttributesA, g_originalSetFileAttributesA, BOOL, FALSE,
               (LPCSTR path, DWORD attributes), (selected, attributes))
REDIRECT_ONE_W(HookCreateDirectoryW, g_originalCreateDirectoryW, BOOL, FALSE,
               (LPCWSTR path, LPSECURITY_ATTRIBUTES security), (selected, security))
REDIRECT_ONE_A(HookCreateDirectoryA, g_originalCreateDirectoryA, BOOL, FALSE,
               (LPCSTR path, LPSECURITY_ATTRIBUTES security), (selected, security))
REDIRECT_ONE_W(HookRemoveDirectoryW, g_originalRemoveDirectoryW, BOOL, FALSE,
               (LPCWSTR path), (selected))
REDIRECT_ONE_A(HookRemoveDirectoryA, g_originalRemoveDirectoryA, BOOL, FALSE,
               (LPCSTR path), (selected))
REDIRECT_ONE_W(HookDeleteFileW, g_originalDeleteFileW, BOOL, FALSE,
               (LPCWSTR path), (selected))
REDIRECT_ONE_A(HookDeleteFileA, g_originalDeleteFileA, BOOL, FALSE,
               (LPCSTR path), (selected))
REDIRECT_ONE_W(HookFindFirstFileW, g_originalFindFirstFileW, HANDLE, INVALID_HANDLE_VALUE,
               (LPCWSTR path, LPWIN32_FIND_DATAW data), (selected, data))
REDIRECT_ONE_A(HookFindFirstFileA, g_originalFindFirstFileA, HANDLE, INVALID_HANDLE_VALUE,
               (LPCSTR path, LPWIN32_FIND_DATAA data), (selected, data))

BOOL WINAPI HookMoveFileW(LPCWSTR from, LPCWSTR to)
{
    wchar_t redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCWSTR selectedFrom = RewriteSavePathW(from, redirectedFrom, sizeof(redirectedFrom) / sizeof(redirectedFrom[0])) ? redirectedFrom : from;
    LPCWSTR selectedTo = RewriteSavePathW(to, redirectedTo, sizeof(redirectedTo) / sizeof(redirectedTo[0])) ? redirectedTo : to;
    return g_originalMoveFileW(selectedFrom, selectedTo);
}
BOOL WINAPI HookMoveFileA(LPCSTR from, LPCSTR to)
{
    char redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCSTR selectedFrom = RewriteSavePathA(from, redirectedFrom, sizeof(redirectedFrom)) ? redirectedFrom : from;
    LPCSTR selectedTo = RewriteSavePathA(to, redirectedTo, sizeof(redirectedTo)) ? redirectedTo : to;
    return g_originalMoveFileA(selectedFrom, selectedTo);
}
BOOL WINAPI HookMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags)
{
    wchar_t redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCWSTR selectedFrom = RewriteSavePathW(from, redirectedFrom, sizeof(redirectedFrom) / sizeof(redirectedFrom[0])) ? redirectedFrom : from;
    LPCWSTR selectedTo = RewriteSavePathW(to, redirectedTo, sizeof(redirectedTo) / sizeof(redirectedTo[0])) ? redirectedTo : to;
    return g_originalMoveFileExW(selectedFrom, selectedTo, flags);
}
BOOL WINAPI HookMoveFileExA(LPCSTR from, LPCSTR to, DWORD flags)
{
    char redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCSTR selectedFrom = RewriteSavePathA(from, redirectedFrom, sizeof(redirectedFrom)) ? redirectedFrom : from;
    LPCSTR selectedTo = RewriteSavePathA(to, redirectedTo, sizeof(redirectedTo)) ? redirectedTo : to;
    return g_originalMoveFileExA(selectedFrom, selectedTo, flags);
}
BOOL WINAPI HookCopyFileW(LPCWSTR from, LPCWSTR to, BOOL failIfExists)
{
    wchar_t redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCWSTR selectedFrom = RewriteSavePathW(from, redirectedFrom, sizeof(redirectedFrom) / sizeof(redirectedFrom[0])) ? redirectedFrom : from;
    LPCWSTR selectedTo = RewriteSavePathW(to, redirectedTo, sizeof(redirectedTo) / sizeof(redirectedTo[0])) ? redirectedTo : to;
    return g_originalCopyFileW(selectedFrom, selectedTo, failIfExists);
}
BOOL WINAPI HookCopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists)
{
    char redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCSTR selectedFrom = RewriteSavePathA(from, redirectedFrom, sizeof(redirectedFrom)) ? redirectedFrom : from;
    LPCSTR selectedTo = RewriteSavePathA(to, redirectedTo, sizeof(redirectedTo)) ? redirectedTo : to;
    return g_originalCopyFileA(selectedFrom, selectedTo, failIfExists);
}

void* ReplacementFor(const char* name)
{
    if(std::strcmp(name, "CreateFileA") == 0)
        return reinterpret_cast<void*>(&HookCreateFileA);
    if(std::strcmp(name, "CreateFileW") == 0)
        return reinterpret_cast<void*>(&HookCreateFileW);
    if(std::strcmp(name, "GetFileAttributesA") == 0)
        return reinterpret_cast<void*>(&HookGetFileAttributesA);
    if(std::strcmp(name, "GetFileAttributesW") == 0)
        return reinterpret_cast<void*>(&HookGetFileAttributesW);
#define REPLACE(api) if(std::strcmp(name, #api) == 0) return reinterpret_cast<void*>(&Hook##api)
    REPLACE(SetFileAttributesA);
    REPLACE(SetFileAttributesW);
    REPLACE(CreateDirectoryA);
    REPLACE(CreateDirectoryW);
    REPLACE(RemoveDirectoryA);
    REPLACE(RemoveDirectoryW);
    REPLACE(DeleteFileA);
    REPLACE(DeleteFileW);
    REPLACE(FindFirstFileA);
    REPLACE(FindFirstFileW);
    REPLACE(MoveFileA);
    REPLACE(MoveFileW);
    REPLACE(MoveFileExA);
    REPLACE(MoveFileExW);
    REPLACE(CopyFileA);
    REPLACE(CopyFileW);
#undef REPLACE
    return nullptr;
}

int PatchModuleImports(HMODULE module)
{
    if(!module || module == g_instance)
        return 0;
    auto* base = reinterpret_cast<unsigned char*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if(nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    const IMAGE_DATA_DIRECTORY directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!directory.VirtualAddress || !directory.Size)
        return 0;
    int patched = 0;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for(; descriptor->Name; ++descriptor)
    {
        const char* library = reinterpret_cast<const char*>(base + descriptor->Name);
        if(_stricmp(library, "KERNEL32.dll") != 0 &&
           _stricmp(library, "KERNELBASE.dll") != 0)
            continue;
        if(!descriptor->OriginalFirstThunk)
            continue;
        auto* original = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* resolved = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for(; original->u1.AddressOfData; ++original, ++resolved)
        {
            if(IMAGE_SNAP_BY_ORDINAL(original->u1.Ordinal))
                continue;
            auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + original->u1.AddressOfData);
            void* replacement = ReplacementFor(reinterpret_cast<const char*>(import->Name));
            if(!replacement || resolved->u1.Function == reinterpret_cast<ULONG_PTR>(replacement))
                continue;
            DWORD oldProtect = 0;
            if(!VirtualProtect(&resolved->u1.Function, sizeof(resolved->u1.Function),
                               PAGE_READWRITE, &oldProtect))
                continue;
            resolved->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            DWORD ignored = 0;
            VirtualProtect(&resolved->u1.Function, sizeof(resolved->u1.Function),
                           oldProtect, &ignored);
            ++patched;
        }
    }
    return patched;
}

int InstallFileObservationHooks()
{
    HMODULE kernel = GetModuleHandleW(L"KERNEL32.dll");
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if(!kernel)
        return 0;
    g_originalCreateFileA = reinterpret_cast<CreateFileAFn>(GetProcAddress(kernel, "CreateFileA"));
    g_originalCreateFileW = reinterpret_cast<CreateFileWFn>(GetProcAddress(kernel, "CreateFileW"));
    g_originalGetFileAttributesA = reinterpret_cast<GetFileAttributesAFn>(
        GetProcAddress(kernel, "GetFileAttributesA"));
    g_originalGetFileAttributesW = reinterpret_cast<GetFileAttributesWFn>(
        GetProcAddress(kernel, "GetFileAttributesW"));
#define LOAD_ORIGINAL(api) g_original##api = reinterpret_cast<api##Fn>(GetProcAddress(kernel, #api))
    LOAD_ORIGINAL(SetFileAttributesA);
    LOAD_ORIGINAL(SetFileAttributesW);
    LOAD_ORIGINAL(CreateDirectoryA);
    LOAD_ORIGINAL(CreateDirectoryW);
    LOAD_ORIGINAL(RemoveDirectoryA);
    LOAD_ORIGINAL(RemoveDirectoryW);
    LOAD_ORIGINAL(DeleteFileA);
    LOAD_ORIGINAL(DeleteFileW);
    LOAD_ORIGINAL(FindFirstFileA);
    LOAD_ORIGINAL(FindFirstFileW);
    LOAD_ORIGINAL(MoveFileA);
    LOAD_ORIGINAL(MoveFileW);
    LOAD_ORIGINAL(MoveFileExA);
    LOAD_ORIGINAL(MoveFileExW);
    LOAD_ORIGINAL(CopyFileA);
    LOAD_ORIGINAL(CopyFileW);
#undef LOAD_ORIGINAL
    g_captureStack = ntdll ? reinterpret_cast<CaptureStackFn>(
        GetProcAddress(ntdll, "RtlCaptureStackBackTrace")) : nullptr;
    if(!g_originalCreateFileA || !g_originalCreateFileW ||
       !g_originalGetFileAttributesA || !g_originalGetFileAttributesW ||
       !g_originalSetFileAttributesA || !g_originalSetFileAttributesW ||
       !g_originalCreateDirectoryA || !g_originalCreateDirectoryW ||
       !g_originalRemoveDirectoryA || !g_originalRemoveDirectoryW ||
       !g_originalDeleteFileA || !g_originalDeleteFileW ||
       !g_originalFindFirstFileA || !g_originalFindFirstFileW ||
       !g_originalMoveFileA || !g_originalMoveFileW ||
       !g_originalMoveFileExA || !g_originalMoveFileExW ||
       !g_originalCopyFileA || !g_originalCopyFileW)
        return 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                               GetCurrentProcessId());
    if(snapshot == INVALID_HANDLE_VALUE)
        return 0;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int patched = 0;
    if(Module32FirstW(snapshot, &entry))
    {
        do
        {
            wchar_t message[900]{};
            _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                         L"TRANSITION_PROBE module=%s base=0x%08lX size=0x%08lX",
                         entry.szExePath, DWORD(reinterpret_cast<uintptr_t>(entry.modBaseAddr)),
                         entry.modBaseSize);
            Log(message);
            patched += PatchModuleImports(entry.hModule);
        }
        while(Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return patched;
}

const char* FileNamePartA(const char* path)
{
    if(!path)
        return nullptr;
    const char* slash = std::strrchr(path, '\\');
    const char* forward = std::strrchr(path, '/');
    if(!slash || (forward && forward > slash))
        slash = forward;
    return slash ? slash + 1 : path;
}

bool IsMainMenuMpr(const char* path)
{
    const char* name = FileNamePartA(path);
    return name && _stricmp(name, "ZoneMainMenuNew.mpr") == 0;
}

bool ReadDirectLaunchPaths()
{
    wchar_t mpr[MAX_PATH * 4]{};
    wchar_t mob[MAX_PATH * 4]{};
    wchar_t sourceZone[128]{};
    wchar_t targetExit[16]{};
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_GODMODE_MOB", g_godmodeMob,
                            DWORD(sizeof(g_godmodeMob) / sizeof(g_godmodeMob[0])));
    if(g_godmodeMob[0])
        NormalizeSlashes(g_godmodeMob);
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_SUPPLEMENTAL_MOB_DIR",
                            g_supplementalMobDirectory,
                            DWORD(sizeof(g_supplementalMobDirectory)
                                  / sizeof(g_supplementalMobDirectory[0])));
    if(g_supplementalMobDirectory[0])
    {
        NormalizeSlashes(g_supplementalMobDirectory);
        while(std::wcslen(g_supplementalMobDirectory) > 3
              && IsPathBoundary(g_supplementalMobDirectory[
                     std::wcslen(g_supplementalMobDirectory) - 1]))
            g_supplementalMobDirectory[
                std::wcslen(g_supplementalMobDirectory) - 1] = L'\0';
    }
    if(!GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_TEMP_MPR", mpr,
                                DWORD(sizeof(mpr) / sizeof(mpr[0]))) ||
       !GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_TEMP_MOB", mob,
                                DWORD(sizeof(mob) / sizeof(mob[0]))) ||
       !GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_SOURCE_ZONE", sourceZone,
                                DWORD(sizeof(sourceZone) / sizeof(sourceZone[0]))) ||
       !GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_TARGET_EXIT", targetExit,
                                DWORD(sizeof(targetExit) / sizeof(targetExit[0]))) ||
       !GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_LIVE_SAVES", g_liveSaves,
                                DWORD(sizeof(g_liveSaves) / sizeof(g_liveSaves[0]))) ||
       !GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_TEST_SAVES", g_testSaves,
                                DWORD(sizeof(g_testSaves) / sizeof(g_testSaves[0]))) ||
       GetFileAttributesW(mpr) == INVALID_FILE_ATTRIBUTES ||
       GetFileAttributesW(mob) == INVALID_FILE_ATTRIBUTES)
    {
        Log(L"DIRECT_LAUNCH disabled: staged MPR/MOB environment paths are missing or unreadable");
        return false;
    }
    wchar_t* exitEnd = nullptr;
    const long parsedExit = std::wcstol(targetExit, &exitEnd, 10);
    if(exitEnd == targetExit || *exitEnd != L'\0' || parsedExit <= 0 || parsedExit > 1024)
    {
        Log(L"DIRECT_LAUNCH disabled: selected map-table entrance is invalid");
        return false;
    }
    // map.txt exposes human-facing #exit IDs beginning at one, while the
    // player-default record stores the zero-based entrance array index. Passing
    // the ID unchanged selected the following entrance and made the final one
    // fall out of range (observed in both zone1 and zone2).
    g_targetExitId = int(parsedExit);
    g_targetExit = g_targetExitId - 1;
    NormalizeSlashes(g_liveSaves);
    NormalizeSlashes(g_testSaves);
    while(std::wcslen(g_liveSaves) > 3 &&
          IsPathBoundary(g_liveSaves[std::wcslen(g_liveSaves) - 1]))
        g_liveSaves[std::wcslen(g_liveSaves) - 1] = L'\0';
    while(std::wcslen(g_testSaves) > 3 &&
          IsPathBoundary(g_testSaves[std::wcslen(g_testSaves) - 1]))
        g_testSaves[std::wcslen(g_testSaves) - 1] = L'\0';
    BOOL mprSubstitution = FALSE;
    BOOL mobSubstitution = FALSE;
    if(!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, mpr, -1,
                            g_temporaryMpr, int(sizeof(g_temporaryMpr)), nullptr,
                            &mprSubstitution) ||
       !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, mob, -1,
                            g_temporaryMob, int(sizeof(g_temporaryMob)), nullptr,
                            &mobSubstitution) ||
       !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, sourceZone, -1,
                            g_sourceZone, int(sizeof(g_sourceZone)), nullptr, nullptr) ||
       mprSubstitution || mobSubstitution)
    {
        Log(L"DIRECT_LAUNCH disabled: staged paths cannot be represented in the game ANSI code page");
        return false;
    }
    wchar_t message[1800]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"DIRECT_LAUNCH profile zone=\"%s\" entrance_id=%d entrance_index=%d isolated_saves=\"%s\" live_saves_protected=\"%s\"",
                 sourceZone, g_targetExitId, g_targetExit, g_testSaves, g_liveSaves);
    Log(message);
    return true;
}

bool QueueDirectLaunchOnCurrentThread()
{
    if(!g_zoneTransitionController)
    {
        Log(L"DIRECT_LAUNCH failed: normal main-menu transition controller was not captured");
        InterlockedExchange(&g_directLaunchState, -1);
        return false;
    }
    g_gameThreadId = GetCurrentThreadId();
    g_directLaunchHook = SetWindowsHookExW(WH_GETMESSAGE, &DirectLaunchMessageHook,
                                           g_instance, g_gameThreadId);
    if(!g_directLaunchHook)
    {
        Log(L"DIRECT_LAUNCH failed: could not install the game-thread message hook");
        InterlockedExchange(&g_directLaunchState, -1);
        return false;
    }
    if(InterlockedCompareExchange(&g_directLaunchState, 2, 1) != 1 ||
       !PostThreadMessageW(g_gameThreadId, kDirectLaunchMessage, 0, 0))
    {
        UnhookWindowsHookEx(g_directLaunchHook);
        g_directLaunchHook = nullptr;
        Log(L"DIRECT_LAUNCH failed: could not queue the game-thread launch request");
        InterlockedExchange(&g_directLaunchState, -1);
        return false;
    }
    wchar_t message[256]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"DIRECT_LAUNCH queued thread=%lu after normal main-menu load",
                 g_gameThreadId);
    Log(message);
    return true;
}

void __fastcall PlayerDefaultsResetDetour(void* self, void*)
{
    g_originalPlayerDefaultsReset(self);
    if(InterlockedCompareExchange(&g_directLaunchState, 0, 0) != 3 ||
       !g_gameModule || !g_sourceZone[0] || g_targetExit < 0)
        return;

    auto* game = reinterpret_cast<unsigned char*>(g_gameModule);
    if(self != game + kPlayerDefaultsManagerRva)
    {
        Log(L"DIRECT_LAUNCH failed: New Game reset used an unexpected player-default manager");
        InterlockedExchange(&g_directLaunchState, -1);
        return;
    }
    void* begin = *reinterpret_cast<void**>(self);
    void* allocation = begin ? *reinterpret_cast<void**>(begin) : nullptr;
    if(!allocation)
    {
        Log(L"DIRECT_LAUNCH failed: New Game reset did not create the default player record");
        InterlockedExchange(&g_directLaunchState, -1);
        return;
    }

    auto* player = reinterpret_cast<unsigned char*>(allocation) + 8;
    const auto assign = reinterpret_cast<GameStringAssignFn>(game + kGameStringAssignRva);
    assign(player + 0x68, g_sourceZone);
    *reinterpret_cast<int*>(player + 0x6C) = g_targetExit;

    wchar_t selected[256] = L"<unrepresentable>";
    MultiByteToWideChar(CP_ACP, 0, g_sourceZone, -1, selected,
                        int(sizeof(selected) / sizeof(selected[0])));
    wchar_t message[512]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"DIRECT_LAUNCH initialized New Game player zone=\"%s\" entrance_id=%d entrance_index=%d before transition",
                 selected, g_targetExitId, g_targetExit);
    Log(message);
}

int __fastcall ZoneTransitionDetour(void* self, void*, const char* zoneName,
                                    int requestedMode)
{
    SetRuntimeState(RuntimeState::Transitioning, L"zone transition entered");
    const LONG state = InterlockedCompareExchange(&g_directLaunchState, 0, 0);
    if(state == 3 && requestedMode == 0 && g_sourceZone[0])
    {
        wchar_t original[256] = L"<null>";
        wchar_t selected[256] = L"<unrepresentable>";
        if(zoneName)
            MultiByteToWideChar(CP_ACP, 0, zoneName, -1, original,
                                int(sizeof(original) / sizeof(original[0])));
        MultiByteToWideChar(CP_ACP, 0, g_sourceZone, -1, selected,
                            int(sizeof(selected) / sizeof(selected[0])));
        wchar_t message[700]{};
        const bool identityMatches = zoneName && _stricmp(zoneName, g_sourceZone) == 0;
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"DIRECT_LAUNCH New Game zone identity original=\"%s\" selected=\"%s\" mode=%d result=%s",
                     original, selected, requestedMode,
                     identityMatches ? L"verified" : L"fallback-substitution");
        Log(message);
        // The player-default hook should make these identical. Keep the
        // substitution only as a non-destructive fallback for diagnostics.
        return g_originalZoneTransition(self, g_sourceZone, requestedMode);
    }
    if(requestedMode == 2 && zoneName && _stricmp(zoneName, "MainMenu") == 0)
    {
        g_zoneTransitionController = self;
        wchar_t message[256]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"DIRECT_LAUNCH captured normal transition controller=0x%08lX thread=%lu",
                     DWORD(reinterpret_cast<uintptr_t>(self)), GetCurrentThreadId());
        Log(message);
    }
    return g_originalZoneTransition(self, zoneName, requestedMode);
}

int __fastcall MapLoadDetour(void* self, void*, const char* mprPath,
                             const char* mobPath, void* context)
{
    SetRuntimeState(RuntimeState::Transitioning, L"map loader entered");
    void* caller = _ReturnAddress();
    const auto* expectedTransitionReturn = reinterpret_cast<unsigned char*>(g_gameModule)
        + kStartZoneMapLoadReturnRva;
    const LONG stateOnEntry = InterlockedCompareExchange(&g_directLaunchState, 0, 0);
    if(stateOnEntry == 3 && InterlockedIncrement(&g_pendingMapLoadProbeCount) <= 32)
    {
        wchar_t mpr[MAX_PATH * 4] = L"<null>";
        wchar_t mob[MAX_PATH * 4] = L"<null>";
        if(mprPath)
            MultiByteToWideChar(CP_ACP, 0, mprPath, -1, mpr,
                                int(sizeof(mpr) / sizeof(mpr[0])));
        if(mobPath)
            MultiByteToWideChar(CP_ACP, 0, mobPath, -1, mob,
                                int(sizeof(mob) / sizeof(mob[0])));
        const uintptr_t callerAddress = reinterpret_cast<uintptr_t>(caller);
        const uintptr_t gameBase = reinterpret_cast<uintptr_t>(g_gameModule);
        const DWORD callerRva = callerAddress >= gameBase
            ? DWORD(callerAddress - gameBase) : 0xFFFFFFFF;
        wchar_t message[3000]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"DIRECT_LAUNCH pending loader observed caller=0x%08lX caller_rva=0x%08lX expected_rva=0x%08lX mpr=\"%s\" mob=\"%s\" self=0x%08lX context=0x%08lX thread=%lu",
                     DWORD(callerAddress), callerRva, kStartZoneMapLoadReturnRva,
                     mpr, mob, DWORD(reinterpret_cast<uintptr_t>(self)),
                     DWORD(reinterpret_cast<uintptr_t>(context)), GetCurrentThreadId());
        Log(message);
    }
    const bool redirect = g_directLaunchState == 3 &&
        caller == expectedTransitionReturn &&
        InterlockedCompareExchange(&g_directLaunchState, 4, 3) == 3;
    const char* selectedMpr = redirect ? g_temporaryMpr : mprPath;
    const char* selectedMob = redirect ? g_temporaryMob : mobPath;
    if(redirect)
    {
        wchar_t mpr[MAX_PATH * 4]{};
        wchar_t mob[MAX_PATH * 4]{};
        MultiByteToWideChar(CP_ACP, 0, selectedMpr, -1, mpr,
                            int(sizeof(mpr) / sizeof(mpr[0])));
        MultiByteToWideChar(CP_ACP, 0, selectedMob, -1, mob,
                            int(sizeof(mob) / sizeof(mob[0])));
        wchar_t message[3000]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"DIRECT_LAUNCH redirecting normal single-player transition loader mpr=\"%s\" mob=\"%s\" context=0x%08lX",
                     mpr, mob, DWORD(reinterpret_cast<uintptr_t>(context)));
        Log(message);
    }

    const int result = g_originalMapLoad(self, selectedMpr, selectedMob, context);
    if(result)
        SetRuntimeState(IsMainMenuMpr(selectedMpr) ? RuntimeState::MainMenuReady
                                                   : RuntimeState::GameplayReady,
                        IsMainMenuMpr(selectedMpr) ? L"main-menu map loader returned"
                                                   : L"gameplay map loader returned");
    else
        SetRuntimeState(RuntimeState::Unknown, L"map loader failed");
    if(redirect)
    {
        wchar_t message[256]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"DIRECT_LAUNCH staged map loader returned result=%d", result);
        Log(message);
    }
    else if(g_directLaunchState == 1 && IsMainMenuMpr(mprPath))
        QueueDirectLaunchOnCurrentThread();
    if(result && InterlockedCompareExchange(&g_pendingStateAwareReload, 0, 0)
       && g_gameThreadId)
        PostThreadMessageW(g_gameThreadId, kHotReloadMessage, 0, 0);
    return result;
}

bool ProcessHotReloadOnGameThread()
{
    if(InterlockedCompareExchange(&g_directLaunchState, 0, 0) != 6
       || !g_originalZoneTransition || !g_zoneTransitionController)
    {
        Log(L"HOT_RELOAD refused: initial staged map is not in a reloadable state");
        return false;
    }
    HotReloadControl request;
    if(!ReadHotReloadControl(&request))
    {
        Log(L"HOT_RELOAD refused: request, ownership, sequence, or payload hash validation failed");
        return false;
    }
    if(!HashMatches(request.mprPath, request.mprSha256)
       || !HashMatches(request.mobPath, request.mobSha256))
    {
        Log(L"HOT_RELOAD refused: owned generation payload does not match the staged request hashes");
        return false;
    }
    BOOL usedDefault = FALSE;
    if(!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, request.mprPath, -1,
                            g_temporaryMpr, int(sizeof(g_temporaryMpr)),
                            nullptr, &usedDefault) || usedDefault
       || !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, request.mobPath, -1,
                               g_temporaryMob, int(sizeof(g_temporaryMob)),
                               nullptr, &usedDefault) || usedDefault)
    {
        Log(L"HOT_RELOAD refused: owned generation paths cannot round-trip through the game code page");
        return false;
    }
    if(strcpy_s(g_sourceZone, request.zone) != 0)
    {
        Log(L"HOT_RELOAD refused: map-table zone identity is too long");
        return false;
    }
    g_targetExitId = request.entranceId;
    g_targetExit = request.entranceId - 1;
    // The bootstrap is generation-scoped. An entrance-only reload deliberately
    // has no bootstrap MOB, so retaining the previous generation's path would
    // replay its custom spawn (and any other bootstrap actions) on the new map.
    g_godmodeMob[0] = L'\0';
    if(request.hasBootstrap)
        wcscpy_s(g_godmodeMob, request.bootstrapMobPath);
    InterlockedExchange(&g_godmodeRedirectLogged, 0);

    RuntimeState runtimeState = CurrentRuntimeState();
    if(runtimeState == RuntimeState::Transitioning)
    {
        const DWORD stateTick = DWORD(InterlockedCompareExchange(&g_runtimeStateTick, 0, 0));
        const DWORD elapsed = GetTickCount() - stateTick;
        // A real synchronous map load owns the game thread, so this message
        // cannot run until MapLoadDetour publishes a stable state. A stale
        // Transitioning state while the message loop is responsive is the
        // observed world-map/map-screen path, which has no loader return.
        if(elapsed >= 1500)
        {
            SetRuntimeState(RuntimeState::WorldMapReady,
                            L"responsive stale transition classified as world map");
            runtimeState = RuntimeState::WorldMapReady;
        }
    }
    if(runtimeState == RuntimeState::Transitioning || runtimeState == RuntimeState::Unknown
       || runtimeState == RuntimeState::Initializing)
    {
        InterlockedExchange(&g_pendingStateAwareReload, 1);
        wchar_t deferred[512]{};
        _snwprintf_s(deferred, sizeof(deferred) / sizeof(deferred[0]), _TRUNCATE,
                     L"HOT_RELOAD deferred sequence=%llu runtime_state=%s state_sequence=%ld",
                     request.sequence, RuntimeStateName(runtimeState),
                     InterlockedCompareExchange(&g_runtimeStateSequence, 0, 0));
        Log(deferred);
        return true;
    }

    if(!CommitSaveGeneration(request))
    {
        Log(L"HOT_RELOAD refused: generation save namespace is not empty, owned, or monotonic");
        return false;
    }

    wchar_t queued[512]{};
    wchar_t zone[128]{};
    MultiByteToWideChar(CP_ACP, 0, g_sourceZone, -1, zone,
                        int(sizeof(zone) / sizeof(zone[0])));
    _snwprintf_s(queued, sizeof(queued) / sizeof(queued[0]), _TRUNCATE,
                 L"HOT_RELOAD dispatch sequence=%llu generation=%llu runtime_state=%s zone=\"%s\" entrance_id=%d thread=%lu",
                 request.sequence, request.generation, RuntimeStateName(runtimeState),
                 zone, request.entranceId, GetCurrentThreadId());
    Log(queued);

    InterlockedExchange(&g_pendingMapLoadProbeCount, 0);
    if(runtimeState != RuntimeState::MainMenuReady)
    {
        InterlockedExchange(&g_pendingStateAwareReload, 1);
        SetRuntimeState(RuntimeState::Transitioning,
                        L"hot reload normal Main Menu rebootstrap started");
        const int menuResult = g_originalZoneTransition(
            g_zoneTransitionController, "MainMenu", 2);
        wchar_t result[384]{};
        _snwprintf_s(result, sizeof(result) / sizeof(result[0]), _TRUNCATE,
                     L"HOT_RELOAD rebootstrap-to-main-menu sequence=%llu prior_state=%s transition_result=%d",
                     request.sequence, RuntimeStateName(runtimeState), menuResult);
        Log(result);
        return true;
    }

    InterlockedExchange(&g_pendingStateAwareReload, 0);
    InterlockedExchange(&g_directLaunchState, 3);
    const int transitionResult = 1;
    auto* game = reinterpret_cast<unsigned char*>(g_gameModule);
    const auto newGame = reinterpret_cast<NewGameBootstrapFn>(game + kNewGameBootstrapRva);
    Log(L"HOT_RELOAD invoking complete New Game bootstrap from MainMenuReady");
    newGame(g_zoneTransitionController);
    const bool redirected = InterlockedCompareExchange(&g_directLaunchState, 6, 4) == 4;
    if(!redirected)
    {
        InterlockedExchange(&g_directLaunchState, -1);
        if(g_hotReloadThreadPath[0])
            DeleteFileW(g_hotReloadThreadPath);
    }
    else
    {
        g_hotReloadSequence = request.sequence;
        SetRuntimeState(RuntimeState::GameplayReady, L"state-aware reload completed");
    }
    wchar_t completed[384]{};
    _snwprintf_s(completed, sizeof(completed) / sizeof(completed[0]), _TRUNCATE,
                 L"HOT_RELOAD %s sequence=%llu transition_result=%d staged_loader=%s",
                 redirected ? L"completed" : L"failed", request.sequence,
                 transitionResult, redirected ? L"observed" : L"missing");
    Log(completed);
    return redirected;
}

LRESULT CALLBACK DirectLaunchMessageHook(int code, WPARAM wParam, LPARAM lParam)
{
    MSG* message = reinterpret_cast<MSG*>(lParam);
    if(code < 0 || wParam != PM_REMOVE || !message
       || (message->message != kDirectLaunchMessage
           && message->message != kHotReloadMessage))
        return CallNextHookEx(g_directLaunchHook, code, wParam, lParam);

    const UINT requestedMessage = message->message;
    message->message = WM_NULL;
    const LRESULT next = CallNextHookEx(g_directLaunchHook, code, wParam, lParam);
    if(requestedMessage == kHotReloadMessage)
    {
        ProcessHotReloadOnGameThread();
        return next;
    }
    if(InterlockedCompareExchange(&g_directLaunchState, 3, 2) != 2)
        return next;

    auto* game = reinterpret_cast<unsigned char*>(g_gameModule);
    wchar_t bootstrapMessage[384]{};
    _snwprintf_s(bootstrapMessage, sizeof(bootstrapMessage) / sizeof(bootstrapMessage[0]), _TRUNCATE,
                 L"DIRECT_LAUNCH invoking complete New Game bootstrap entry=0x%08lX controller=0x%08lX thread=%lu",
                 DWORD(reinterpret_cast<uintptr_t>(game + kNewGameBootstrapRva)),
                 DWORD(reinterpret_cast<uintptr_t>(g_zoneTransitionController)),
                 GetCurrentThreadId());
    Log(bootstrapMessage);
    InterlockedExchange(&g_pendingMapLoadProbeCount, 0);
    const auto newGame = reinterpret_cast<NewGameBootstrapFn>(game + kNewGameBootstrapRva);
    newGame(g_zoneTransitionController);
    const bool redirected = InterlockedCompareExchange(&g_directLaunchState, 6, 4) == 4;
    if(!redirected)
        InterlockedExchange(&g_directLaunchState, -1);
    else
        PublishHotReloadThreadId();
    Log(redirected
        ? L"DIRECT_LAUNCH complete New Game bootstrap returned staged_loader=observed"
        : L"DIRECT_LAUNCH failed: complete New Game bootstrap returned without the verified staged loader call");
    return next;
}

bool InstallPlayerDefaultsResetDetour()
{
    auto* game = reinterpret_cast<unsigned char*>(g_gameModule);
    unsigned char* target = game + kPlayerDefaultsResetRva;
    if(std::memcmp(target, kPlayerDefaultsResetBytes,
                   sizeof(kPlayerDefaultsResetBytes)) != 0)
    {
        Log(L"DIRECT_LAUNCH disabled: validated player-default reset bytes do not match");
        return false;
    }
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, 16,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if(!trampoline)
    {
        Log(L"DIRECT_LAUNCH disabled: player-default reset trampoline allocation failed");
        return false;
    }
    std::memcpy(trampoline, target, sizeof(kPlayerDefaultsResetBytes));
    trampoline[sizeof(kPlayerDefaultsResetBytes)] = 0xE9;
    *reinterpret_cast<LONG*>(trampoline + sizeof(kPlayerDefaultsResetBytes) + 1) =
        LONG((target + sizeof(kPlayerDefaultsResetBytes)) -
             (trampoline + sizeof(kPlayerDefaultsResetBytes) + 5));
    g_originalPlayerDefaultsReset = reinterpret_cast<PlayerDefaultsResetFn>(trampoline);

    DWORD oldProtect = 0;
    if(!VirtualProtect(target, sizeof(kPlayerDefaultsResetBytes),
                       PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_originalPlayerDefaultsReset = nullptr;
        Log(L"DIRECT_LAUNCH disabled: player-default reset protection change failed");
        return false;
    }
    target[0] = 0xE9;
    *reinterpret_cast<LONG*>(target + 1) =
        LONG(reinterpret_cast<unsigned char*>(&PlayerDefaultsResetDetour) - (target + 5));
    target[5] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(kPlayerDefaultsResetBytes));
    DWORD ignored = 0;
    VirtualProtect(target, sizeof(kPlayerDefaultsResetBytes), oldProtect, &ignored);
    return true;
}

bool InstallMapLoadDetour()
{
    g_gameModule = GetModuleHandleW(nullptr);
    auto* game = reinterpret_cast<unsigned char*>(g_gameModule);
    if(!game || !ReadDirectLaunchPaths())
        return false;
    if(std::memcmp(game + kStartZoneTransitionRva, kStartZoneTransitionBytes,
                   sizeof(kStartZoneTransitionBytes)) != 0 ||
       std::memcmp(game + kNewGameBootstrapRva, kNewGameBootstrapBytes,
                   sizeof(kNewGameBootstrapBytes)) != 0)
    {
        Log(L"DIRECT_LAUNCH disabled: validated transition/New Game bootstrap bytes do not match");
        return false;
    }
    if(!InstallPlayerDefaultsResetDetour())
        return false;
    unsigned char* target = game + kMapLoadEntryRva;
    if(std::memcmp(target, kMapLoadEntryBytes, sizeof(kMapLoadEntryBytes)) != 0)
    {
        Log(L"DIRECT_LAUNCH disabled: validated map-loader entry bytes do not match");
        return false;
    }

    unsigned char* transitionTarget = game + kStartZoneTransitionRva;
    auto* transitionTrampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, 16,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if(!transitionTrampoline)
    {
        Log(L"DIRECT_LAUNCH disabled: zone-transition trampoline allocation failed");
        return false;
    }
    std::memcpy(transitionTrampoline, transitionTarget,
                sizeof(kStartZoneTransitionBytes));
    transitionTrampoline[sizeof(kStartZoneTransitionBytes)] = 0xE9;
    *reinterpret_cast<LONG*>(transitionTrampoline + sizeof(kStartZoneTransitionBytes) + 1) =
        LONG((transitionTarget + sizeof(kStartZoneTransitionBytes)) -
             (transitionTrampoline + sizeof(kStartZoneTransitionBytes) + 5));
    g_originalZoneTransition = reinterpret_cast<ZoneTransitionFn>(transitionTrampoline);

    DWORD transitionOldProtect = 0;
    if(!VirtualProtect(transitionTarget, sizeof(kStartZoneTransitionBytes),
                       PAGE_EXECUTE_READWRITE, &transitionOldProtect))
    {
        VirtualFree(transitionTrampoline, 0, MEM_RELEASE);
        g_originalZoneTransition = nullptr;
        Log(L"DIRECT_LAUNCH disabled: zone-transition protection change failed");
        return false;
    }
    transitionTarget[0] = 0xE9;
    *reinterpret_cast<LONG*>(transitionTarget + 1) =
        LONG(reinterpret_cast<unsigned char*>(&ZoneTransitionDetour) -
             (transitionTarget + 5));
    transitionTarget[5] = 0x90;
    transitionTarget[6] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), transitionTarget,
                          sizeof(kStartZoneTransitionBytes));
    DWORD transitionIgnored = 0;
    VirtualProtect(transitionTarget, sizeof(kStartZoneTransitionBytes),
                   transitionOldProtect, &transitionIgnored);

    const auto restoreTransition = [&]()
    {
        DWORD writable = 0;
        if(VirtualProtect(transitionTarget, sizeof(kStartZoneTransitionBytes),
                          PAGE_EXECUTE_READWRITE, &writable))
        {
            std::memcpy(transitionTarget, kStartZoneTransitionBytes,
                        sizeof(kStartZoneTransitionBytes));
            FlushInstructionCache(GetCurrentProcess(), transitionTarget,
                                  sizeof(kStartZoneTransitionBytes));
            DWORD ignored = 0;
            VirtualProtect(transitionTarget, sizeof(kStartZoneTransitionBytes),
                           writable, &ignored);
        }
        VirtualFree(transitionTrampoline, 0, MEM_RELEASE);
        g_originalZoneTransition = nullptr;
    };

    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, 16,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if(!trampoline)
    {
        restoreTransition();
        Log(L"DIRECT_LAUNCH disabled: map-loader trampoline allocation failed");
        return false;
    }
    std::memcpy(trampoline, target, sizeof(kMapLoadEntryBytes));
    trampoline[6] = 0xE9;
    *reinterpret_cast<LONG*>(trampoline + 7) =
        LONG((target + sizeof(kMapLoadEntryBytes)) - (trampoline + 11));
    g_originalMapLoad = reinterpret_cast<MapLoadFn>(trampoline);

    DWORD oldProtect = 0;
    if(!VirtualProtect(target, sizeof(kMapLoadEntryBytes), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_originalMapLoad = nullptr;
        restoreTransition();
        Log(L"DIRECT_LAUNCH disabled: map-loader protection change failed");
        return false;
    }
    target[0] = 0xE9;
    *reinterpret_cast<LONG*>(target + 1) =
        LONG(reinterpret_cast<unsigned char*>(&MapLoadDetour) - (target + 5));
    target[5] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(kMapLoadEntryBytes));
    DWORD ignored = 0;
    VirtualProtect(target, sizeof(kMapLoadEntryBytes), oldProtect, &ignored);

    InterlockedExchange(&g_directLaunchState, 1);
    wchar_t message[256]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"DIRECT_LAUNCH armed map_loader=0x%08lX new_game_bootstrap=0x%08lX mode=single-player",
                 DWORD(reinterpret_cast<uintptr_t>(target)),
                 DWORD(reinterpret_cast<uintptr_t>(game + kNewGameBootstrapRva)));
    Log(message);
    return true;
}

bool InitializeDirectLaunch()
{
    if(InterlockedCompareExchange(&g_initialized, 1, 0) != 0)
        return true;
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_BRIDGE_RESULT", g_resultPath,
                            DWORD(sizeof(g_resultPath) / sizeof(g_resultPath[0])));
    if(!g_resultPath[0])
        return false;
    InitializeCriticalSection(&g_savePathLock);
    InterlockedExchange(&g_savePathLockReady, 1);
    SetRuntimeState(RuntimeState::Initializing, L"bridge initialization");
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_HOT_RELOAD_REQUEST",
                            g_hotReloadRequestPath,
                            DWORD(sizeof(g_hotReloadRequestPath)
                                  / sizeof(g_hotReloadRequestPath[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_HOT_RELOAD_THREAD",
                            g_hotReloadThreadPath,
                            DWORD(sizeof(g_hotReloadThreadPath)
                                  / sizeof(g_hotReloadThreadPath[0])));
    if(g_hotReloadRequestPath[0] && g_hotReloadThreadPath[0])
    {
        if(GetFullPathNameW(g_hotReloadRequestPath,
                            DWORD(sizeof(g_hotReloadRoot) / sizeof(g_hotReloadRoot[0])),
                            g_hotReloadRoot, nullptr))
        {
            wchar_t* slash = std::wcsrchr(g_hotReloadRoot, L'\\');
            wchar_t* forward = std::wcsrchr(g_hotReloadRoot, L'/');
            if(!slash || (forward && forward > slash)) slash = forward;
            if(slash) *slash = L'\0';
        }
        Log(L"HOT_RELOAD transport configured; runtime requests remain game-thread and hash guarded");
    }
    const RuntimeBuild runtime = VerifyRuntime();
    Log(runtime == RuntimeBuild::BaseEi
        ? L"EI Mapper Endurance Base EI direct-map feasibility bridge initializing"
        : L"EI Mapper Endurance JMV direct single-player map launch initializing");
    if(runtime == RuntimeBuild::Unsupported)
    {
        Log(L"DIRECT_LAUNCH disabled: game.exe SHA-256 does not match an exact validated runtime manifest");
        return false;
    }
    if(runtime == RuntimeBuild::BaseEi)
    {
        wchar_t level[8]{};
        GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_BASE_PROBE_LEVEL", level,
                                DWORD(sizeof(level) / sizeof(level[0])));
        g_baseProbeLevel = _wtoi(level);
        if(g_baseProbeLevel < 1 || g_baseProbeLevel > 4)
        {
            Log(L"BASE_PROBE disabled: invalid or missing probe level");
            return false;
        }
    }
    const int patched = InstallFileObservationHooks();
    if(runtime == RuntimeBuild::BaseEi && g_baseProbeLevel == 1)
    {
        Log(patched > 0
            ? L"BASE_PROBE level=1 injection=ready observation_hooks=ready redirect=disabled"
            : L"BASE_PROBE level=1 injection=ready observation_hooks=failed redirect=disabled");
        return patched > 0;
    }
    const bool directLaunch = InstallMapLoadDetour();
    if(runtime == RuntimeBuild::BaseEi && g_baseProbeLevel == 2 && directLaunch)
    {
        // Keep the byte-guarded detours installed, but use a state which can
        // never queue or redirect a transition. Calls retain their arguments.
        InterlockedExchange(&g_directLaunchState, 10);
        Log(L"BASE_PROBE level=2 hook_signatures=ready mode=pass-through redirect=disabled; start a normal Base New Game manually");
    }
    wchar_t message[256]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"DIRECT_LAUNCH active imports=%d stack_capture=%s map_loader=%s hot_reload=main-menu-rebootstrap trigger=complete-new-game-bootstrap",
                 patched, g_captureStack ? L"available" : L"unavailable",
                 directLaunch ? L"available" : L"unavailable");
    Log(message);
    return patched > 0 && directLaunch;
}
}

extern "C" __declspec(dllexport) int __cdecl EntryBeforePatch(const char*)
{
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl EntryAfterPatch(const char*)
{
    // Starter treats non-zero as an assertion condition. Launch failures are
    // recorded in the run log and leave the runtime otherwise untouched.
    InitializeDirectLaunch();
    return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI EnduranceInitialize(void*)
{
    return InitializeDirectLaunch() ? 1u : 0u;
}

// The Base owned-process injector resolves this export by its stable protocol
// name. MSVC decorates x86 __stdcall exports unless an explicit alias is
// supplied; Starter's plugin loader does not exercise this path, which is why
// a stale/missing alias only surfaced in the Base probe.
#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:EnduranceInitialize=_EnduranceInitialize@4")
#endif

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if(reason == DLL_PROCESS_ATTACH)
    {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
