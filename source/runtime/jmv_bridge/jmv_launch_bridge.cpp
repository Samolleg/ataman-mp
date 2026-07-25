#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <cctype>
#include <cmath>
#include <intrin.h>

#include "../runtime_variant_registry.h"

#pragma intrinsic(_ReturnAddress)

namespace {
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
using RegOpenKeyAFn = LSTATUS (WINAPI*)(HKEY, LPCSTR, PHKEY);
using RegOpenKeyExAFn = LSTATUS (WINAPI*)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
using RegOpenKeyExWFn = LSTATUS (WINAPI*)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
using RegCreateKeyExAFn = LSTATUS (WINAPI*)(HKEY, LPCSTR, DWORD, LPSTR, DWORD,
                                            REGSAM, const LPSECURITY_ATTRIBUTES,
                                            PHKEY, LPDWORD);
using RegQueryValueExAFn = LSTATUS (WINAPI*)(HKEY, LPCSTR, LPDWORD, LPDWORD,
                                             LPBYTE, LPDWORD);
using RegQueryValueExWFn = LSTATUS (WINAPI*)(HKEY, LPCWSTR, LPDWORD, LPDWORD,
                                             LPBYTE, LPDWORD);
using RegCloseKeyFn = LSTATUS (WINAPI*)(HKEY);
using MapLoadFn = int (__thiscall*)(void*, const char*, const char*, void*);
using ZoneTransitionFn = int (__thiscall*)(void*, const char*, int);
using NewGameBootstrapFn = void (__thiscall*)(void*);
using PlayerDefaultsResetFn = void (__thiscall*)(void*);
using GameStringAssignFn = void (__thiscall*)(void*, const char*);
using GetZoneByNameFn = unsigned char* (__thiscall*)(void*, const char*);
using GetTextureManagerFn = void* (__thiscall*)(void*);
using AddTextureResourceFn = int (__thiscall*)(void*, const char*);
enum class RuntimeBuild { Unsupported, JmvStarter, BaseEi };

HMODULE g_instance = nullptr;
volatile LONG g_prepared = 0;
volatile LONG g_activated = 0;
RuntimeBuild g_runtimeBuild = RuntimeBuild::Unsupported;
int g_patchedImports = 0;
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
RegOpenKeyAFn g_originalRegOpenKeyA = nullptr;
RegOpenKeyExAFn g_originalRegOpenKeyExA = nullptr;
RegOpenKeyExWFn g_originalRegOpenKeyExW = nullptr;
RegCreateKeyExAFn g_originalRegCreateKeyExA = nullptr;
RegQueryValueExAFn g_originalRegQueryValueExA = nullptr;
RegQueryValueExWFn g_originalRegQueryValueExW = nullptr;
RegCloseKeyFn g_originalRegCloseKey = nullptr;
wchar_t g_selectedGameRoot[MAX_PATH * 4] = {};
wchar_t g_selectedModRoot[MAX_PATH * 4] = {};
HKEY g_pathSettingsKeys[16] = {};
HKEY g_starterSettingsKeys[16] = {};
CRITICAL_SECTION g_registryPathLock{};
volatile LONG g_registryPathLockReady = 0;
volatile LONG g_registryOverrideLogCount = 0;
volatile LONG g_modRootRedirectLogCount = 0;

constexpr DWORD kMapLoadEntryRva = 0x00195480;
constexpr DWORD kStartZoneTransitionRva = 0x0017C460;
constexpr DWORD kStartZoneMapLoadReturnRva = 0x0017A61B;
constexpr DWORD kNewGameBootstrapRva = 0x0017BD10;
constexpr DWORD kPlayerDefaultsResetRva = 0x000A5BD0;
constexpr DWORD kPlayerDefaultsManagerRva = 0x003AA5C4;
constexpr DWORD kGameStringAssignRva = 0x00300AF9;
constexpr DWORD kGetZoneByNameRva = 0x0005E850;
constexpr DWORD kGlobalMapRva = 0x0039BF00;
constexpr DWORD kGraphRequirementRva = 0x001955CC;
constexpr DWORD kWorldClientGlobalRva = 0x003AF5B8;
constexpr DWORD kWorldGetTextureManagerRva = 0x00161990;
constexpr DWORD kTextureManagerAddResourceRva = 0x00275050;
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
constexpr unsigned char kGraphRequirementBytes[33] = {
    0x8D, 0x54, 0x24, 0x24, 0x8B, 0xCE, 0x52, 0x55,
    0x57, 0xC7, 0x44, 0x24, 0x60, 0x02, 0x00, 0x00,
    0x00, 0xE8, 0x5E, 0x0B, 0x00, 0x00, 0x85, 0xC0,
    0x75, 0x07, 0x68, 0xD8, 0x0A, 0x79, 0x00, 0xEB,
    0x50
};
constexpr unsigned char kGetZoneByNameBytes[15] = {
    0x53, 0x55, 0x56, 0x8B, 0xF1, 0x57, 0x33, 0xFF,
    0x8B, 0x46, 0x1C, 0x85, 0xC0, 0x7E, 0x25
};
constexpr unsigned char kTextureManagerAddResourceBytes[8] = {
    0x6A, 0xFF, 0x68, 0xBC, 0x15, 0x73, 0x00, 0x64
};
constexpr unsigned char kWorldGetTextureManagerBytes[7] = {
    0x8B, 0x81, 0x8C, 0x01, 0x00, 0x00, 0xC3
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
char g_bootstrapZone[128] = {};
char g_bootstrapMpr[128] = {};
char g_bootstrapMob[128] = {};
int g_targetExit = -1;
int g_targetExitId = 0;
bool g_syntheticMapIdentity = false;
int g_syntheticMapSizeX = 0;
int g_syntheticMapSizeY = 0;
float g_syntheticSpawnX = 0.0f;
float g_syntheticSpawnY = 0.0f;
wchar_t g_liveSaves[MAX_PATH * 4] = {};
wchar_t g_testSaves[MAX_PATH * 4] = {};
CRITICAL_SECTION g_savePathLock{};
volatile LONG g_savePathLockReady = 0;
wchar_t g_godmodeMob[MAX_PATH * 4] = {};
wchar_t g_supplementalMobDirectory[MAX_PATH * 4] = {};
wchar_t g_mapTableSource[MAX_PATH * 4] = {};
wchar_t g_mapTableStage[MAX_PATH * 4] = {};
volatile LONG g_mapTableRedirectLogCount = 0;
wchar_t g_temporaryTextureResource[MAX_PATH * 4] = {};
volatile LONG g_temporaryTextureResourceRegistered = 0;
wchar_t g_hotReloadRequestPath[MAX_PATH * 4] = {};
wchar_t g_hotReloadThreadPath[MAX_PATH * 4] = {};
wchar_t g_hotReloadRoot[MAX_PATH * 4] = {};
ULONGLONG g_hotReloadSequence = 0;
ULONGLONG g_saveGeneration = 0;
volatile LONG g_runtimeState = LONG(RuntimeState::Initializing);
volatile LONG g_runtimeStateSequence = 0;
volatile LONG g_runtimeStateTick = 0;
volatile LONG g_pendingStateAwareReload = 0;
volatile LONG g_graphlessPayload = 0;
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
const char* FileNamePartA(const char* path);

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
    bool graphless = false;
    bool syntheticMapIdentity = false;
    int mapSizeX = 0;
    int mapSizeY = 0;
    float spawnX = 0.0f;
    float spawnY = 0.0f;
    char sourceZone[128] = {};
    char bootstrapMpr[128] = {};
    char bootstrapMob[128] = {};
    char zone[128] = {};
    char bootstrapMode[32] = {};
};

constexpr size_t kZoneNameOffset = 0;
constexpr size_t kZoneAllodOffset = 4;
constexpr size_t kZoneMobOffset = 8;
constexpr size_t kZoneTerrainOffset = 12;
constexpr size_t kZoneFigureOffset = 16;
constexpr size_t kZoneMapTextureOffset = 20;
constexpr size_t kZoneMinimapTextureOffset = 24;
constexpr size_t kZoneCameraOffset = 28;
constexpr size_t kZoneSizeXOffset = 32;
constexpr size_t kZoneSizeYOffset = 36;
constexpr size_t kZoneTypeOffset = 40;
constexpr size_t kZoneWeatherOffset = 44;
constexpr size_t kZoneSkyOffset = 48;
constexpr size_t kZoneRestrictOffset = 52;
constexpr size_t kZoneCageOffset = 68;
// SScenarioZone::exits is an MFC CArray-derived object.  Its first word is the
// CObject virtual-table pointer; m_pData and m_nSize follow that header.  The
// containing SScenarioZone is consequently 92 bytes in this 32-bit runtime.
constexpr size_t kZoneRecordSize = 92;
constexpr size_t kZoneExitsDataOffset = 76;
constexpr size_t kZoneExitsSizeOffset = 80;
constexpr size_t kZoneExitSize = 60;
constexpr size_t kExitZoneOffset = 40;

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

bool IsReadableRange(const void* address, size_t bytes)
{
    if(!address || bytes == 0)
        return false;
    const unsigned char* cursor = static_cast<const unsigned char*>(address);
    const unsigned char* end = cursor + bytes;
    if(end < cursor)
        return false;
    while(cursor < end)
    {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(cursor, &info, sizeof(info))
           || info.State != MEM_COMMIT
           || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        const DWORD readable = info.Protect & 0xFF;
        if(readable != PAGE_READONLY && readable != PAGE_READWRITE
           && readable != PAGE_WRITECOPY && readable != PAGE_EXECUTE_READ
           && readable != PAGE_EXECUTE_READWRITE
           && readable != PAGE_EXECUTE_WRITECOPY)
            return false;
        const unsigned char* regionEnd = static_cast<const unsigned char*>(info.BaseAddress)
            + info.RegionSize;
        if(regionEnd <= cursor)
            return false;
        cursor = regionEnd < end ? regionEnd : end;
    }
    return true;
}

bool ReadBoundedCString(const char* value, char* output, size_t capacity)
{
    if(!value || !output || capacity < 2 || !IsReadableRange(value, 1))
        return false;
    size_t length = 0;
    while(length + 1 < capacity)
    {
        if(!IsReadableRange(value + length, 1))
            return false;
        const char c = value[length];
        output[length++] = c;
        if(c == '\0')
            return true;
    }
    output[capacity - 1] = '\0';
    return false;
}

bool ReadGameStringField(const unsigned char* object, size_t offset,
                         char* output, size_t capacity)
{
    if(!IsReadableRange(object + offset, sizeof(char*)))
        return false;
    const char* value = *reinterpret_cast<char* const*>(object + offset);
    return ReadBoundedCString(value, output, capacity);
}

bool MatchesGameStringField(const unsigned char* object, size_t offset,
                            const char* expected)
{
    char value[128]{};
    return expected && ReadGameStringField(object, offset, value, sizeof(value))
        && _stricmp(value, expected) == 0;
}

bool ValidateZoneCandidate(unsigned char* candidate, const char* zoneName,
                           const char* expectedMpr, const char* expectedMob)
{
    if(!IsReadableRange(candidate, kZoneRecordSize)
       || !MatchesGameStringField(candidate, kZoneNameOffset, zoneName)
       || !MatchesGameStringField(candidate, kZoneTerrainOffset, expectedMpr)
       || !MatchesGameStringField(candidate, kZoneMobOffset, expectedMob))
        return false;
    char allod[128]{};
    if(!ReadGameStringField(candidate, kZoneAllodOffset, allod, sizeof(allod))
       || !allod[0])
        return false;
    const int type = *reinterpret_cast<int*>(candidate + kZoneTypeOffset);
    const int exitCount = *reinterpret_cast<int*>(candidate + kZoneExitsSizeOffset);
    auto* exits = *reinterpret_cast<unsigned char**>(candidate + kZoneExitsDataOffset);
    return type >= 0 && type <= 2 && exitCount > 0 && exitCount <= 1024
        && IsReadableRange(exits, kZoneExitSize);
}

unsigned char* FindZoneCandidate(const char* zoneName, const char* expectedMpr,
                                 const char* expectedMob)
{
    if(!g_gameModule || !zoneName || !zoneName[0])
        return nullptr;
    auto* game = reinterpret_cast<unsigned char*>(g_gameModule);
    unsigned char* lookupEntry = game + kGetZoneByNameRva;
    if(std::memcmp(lookupEntry, kGetZoneByNameBytes,
                   sizeof(kGetZoneByNameBytes)) != 0)
    {
        Log(L"SYNTHETIC_MAP refused: map-table lookup signature does not match the verified runtime");
        return nullptr;
    }

    // The map table is a fixed game-owned object in every fingerprinted runtime
    // variant supported by this bridge. Calling its guarded lookup is bounded
    // and stays on the game thread. Never walk or lock process heaps here: the
    // game allocates from worker threads while the main-menu hook is active.
    auto* globalMap = game + kGlobalMapRva;
    const auto getZoneByName = reinterpret_cast<GetZoneByNameFn>(lookupEntry);
    unsigned char* candidate = getZoneByName(globalMap, zoneName);
    return ValidateZoneCandidate(candidate, zoneName, expectedMpr, expectedMob)
        ? candidate : nullptr;
}

bool ConfigureSyntheticZone()
{
    if(!g_syntheticMapIdentity)
        return true;
    if(g_syntheticMapSizeX <= 0 || g_syntheticMapSizeY <= 0
       || !std::isfinite(g_syntheticSpawnX) || !std::isfinite(g_syntheticSpawnY)
       || !g_sourceZone[0])
        return false;
    // The launcher appends this exact random record to a run-local map.txt
    // before game.exe starts. Change numeric fields only; strings and container
    // ownership remain exactly as constructed by the game parser. Hot-reload
    // generation payloads deliberately use the generic names map.mpr/map.mob;
    // those filesystem names are redirected by MapLoadDetour and are not the
    // logical MPR/MOB names stored in the persistent synthetic map-table record.
    unsigned char* zone = FindZoneCandidate(g_sourceZone, g_sourceZone, g_sourceZone);
    if(!zone)
    {
        Log(L"SYNTHETIC_MAP refused: the preloaded private zone record was not found");
        return false;
    }
    *reinterpret_cast<int*>(zone + kZoneSizeXOffset) = g_syntheticMapSizeX;
    *reinterpret_cast<int*>(zone + kZoneSizeYOffset) = g_syntheticMapSizeY;
    auto* exit = *reinterpret_cast<unsigned char**>(zone + kZoneExitsDataOffset);
    const int exitCount = *reinterpret_cast<int*>(zone + kZoneExitsSizeOffset);
    if(exitCount != 1 || !IsReadableRange(exit, kZoneExitSize))
        return false;
    const float radius = 0.75f;
    *reinterpret_cast<float*>(exit + 0) = 0.0f;
    *reinterpret_cast<float*>(exit + 4) = g_syntheticSpawnX - radius;
    *reinterpret_cast<float*>(exit + 8) = g_syntheticSpawnY - radius;
    *reinterpret_cast<float*>(exit + 12) = g_syntheticSpawnX + radius;
    *reinterpret_cast<float*>(exit + 16) = g_syntheticSpawnY + radius;
    *reinterpret_cast<float*>(exit + 20) = g_syntheticSpawnX - radius;
    *reinterpret_cast<float*>(exit + 24) = g_syntheticSpawnY - radius;
    *reinterpret_cast<float*>(exit + 28) = g_syntheticSpawnX + radius;
    *reinterpret_cast<float*>(exit + 32) = g_syntheticSpawnY + radius;
    *reinterpret_cast<float*>(exit + 36) = 0.0f;
    g_targetExitId = 1;
    g_targetExit = 0;
    wchar_t message[768]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"SYNTHETIC_MAP configured preloaded identity=%S size=%dx%d entrance=(%.3f,%.3f) minimap=none exits=1 string_mutations=0",
                 g_sourceZone, g_syntheticMapSizeX,
                 g_syntheticMapSizeY, g_syntheticSpawnX, g_syntheticSpawnY);
    Log(message);
    return true;
}

bool RegisterTemporaryTextureResource()
{
    if(!g_temporaryTextureResource[0]
       || InterlockedCompareExchange(&g_temporaryTextureResourceRegistered, 0, 0))
        return true;
    auto* game = reinterpret_cast<unsigned char*>(g_gameModule);
    if(!game
       || std::memcmp(game + kWorldGetTextureManagerRva,
                      kWorldGetTextureManagerBytes,
                      sizeof(kWorldGetTextureManagerBytes)) != 0
       || std::memcmp(game + kTextureManagerAddResourceRva,
                      kTextureManagerAddResourceBytes,
                      sizeof(kTextureManagerAddResourceBytes)) != 0)
    {
        Log(L"TEMP_TEXTURE_RES refused: texture resource registration signature does not match");
        return false;
    }
    char resourcePath[MAX_PATH * 4]{};
    BOOL substituted = FALSE;
    if(!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                            g_temporaryTextureResource, -1,
                            resourcePath, int(sizeof(resourcePath)),
                            nullptr, &substituted) || substituted)
    {
        Log(L"TEMP_TEXTURE_RES refused: path cannot round-trip through the game code page");
        return false;
    }
    auto** worldSlot = reinterpret_cast<void**>(game + kWorldClientGlobalRva);
    if(!IsReadableRange(worldSlot, sizeof(void*)) || !*worldSlot
       || !IsReadableRange(*worldSlot, sizeof(void*)))
    {
        Log(L"TEMP_TEXTURE_RES refused: world client is not initialized");
        return false;
    }
    void* world = *worldSlot;
    // Call the fingerprinted game accessor directly.  Looking it up through a
    // reconstructed vtable index is not safe: that table is build-specific and
    // alpha.10 consequently aborted the otherwise valid staged-map bootstrap.
    const auto getTextureManager = reinterpret_cast<GetTextureManagerFn>(
        game + kWorldGetTextureManagerRva);
    void* manager = getTextureManager(world);
    if(!manager || !IsReadableRange(manager, sizeof(void*)))
    {
        Log(L"TEMP_TEXTURE_RES refused: texture manager is not initialized");
        return false;
    }
    const auto addResource = reinterpret_cast<AddTextureResourceFn>(
        game + kTextureManagerAddResourceRva);
    if(!addResource(manager, resourcePath))
    {
        Log(L"TEMP_TEXTURE_RES refused: game texture manager rejected the run-owned archive");
        return false;
    }
    InterlockedExchange(&g_temporaryTextureResourceRegistered, 1);
    wchar_t message[1800]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"TEMP_TEXTURE_RES registered path=\"%s\" profile=%s installed_archives_modified=false",
                 g_temporaryTextureResource,
                 g_runtimeBuild == RuntimeBuild::BaseEi ? L"base" : L"jmv");
    Log(message);
    return true;
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
        else if(std::strcmp(line, "bootstrapMpr") == 0)
        {
            if(strcpy_s(parsed.bootstrapMpr, value) != 0) return false;
        }
        else if(std::strcmp(line, "bootstrapMobName") == 0)
        {
            if(strcpy_s(parsed.bootstrapMob, value) != 0) return false;
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
        else if(std::strcmp(line, "graphless") == 0)
            parsed.graphless = std::strcmp(value, "1") == 0;
        else if(std::strcmp(line, "syntheticMapIdentity") == 0)
            parsed.syntheticMapIdentity = std::strcmp(value, "1") == 0;
        else if(std::strcmp(line, "mapSizeX") == 0)
            parsed.mapSizeX = std::atoi(value);
        else if(std::strcmp(line, "mapSizeY") == 0)
            parsed.mapSizeY = std::atoi(value);
        else if(std::strcmp(line, "spawnX") == 0)
            parsed.spawnX = float(std::atof(value));
        else if(std::strcmp(line, "spawnY") == 0)
            parsed.spawnY = float(std::atof(value));
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
    if(parsed.protocol != 3 || parsed.sequence == 0 || parsed.generation == 0
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
    if(parsed.syntheticMapIdentity
       && (parsed.mapSizeX <= 0 || parsed.mapSizeX > 1024
           || parsed.mapSizeY <= 0 || parsed.mapSizeY > 1024
           || !std::isfinite(parsed.spawnX) || !std::isfinite(parsed.spawnY)
           || !parsed.sourceZone[0] || !parsed.bootstrapMpr[0]
           || !parsed.bootstrapMob[0]))
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

int HexNibble(char value)
{
    if(value >= '0' && value <= '9') return value - '0';
    if(value >= 'A' && value <= 'F') return value - 'A' + 10;
    if(value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

bool HashMatchesHex(const unsigned char actual[32], const char* expected)
{
    if(!actual || !expected || std::strlen(expected) != 64)
        return false;
    for(std::size_t index = 0; index < 32; ++index)
    {
        const int high = HexNibble(expected[index * 2]);
        const int low = HexNibble(expected[index * 2 + 1]);
        if(high < 0 || low < 0
           || actual[index] != static_cast<unsigned char>((high << 4) | low))
            return false;
    }
    return true;
}

RuntimeBuild VerifyRuntime()
{
    wchar_t gamePath[MAX_PATH * 4]{};
    if(!GetModuleFileNameW(nullptr, gamePath, DWORD(sizeof(gamePath) / sizeof(gamePath[0]))))
        return RuntimeBuild::Unsupported;
    unsigned char hash[32]{};
    if(!HashFileSha256(gamePath, hash))
        return RuntimeBuild::Unsupported;
    for(std::size_t index = 0; index < ei::runtime::runtimeVariantCount(); ++index)
    {
        const ei::runtime::SRuntimeVariant& variant = ei::runtime::kRuntimeVariants[index];
        if(std::strcmp(variant.layoutId, ei::runtime::kGameLayoutV1) != 0
           || !HashMatchesHex(hash, variant.sha256))
            continue;
        return variant.profile == ei::runtime::ERuntimeProfile::Jmv
            ? RuntimeBuild::JmvStarter : RuntimeBuild::BaseEi;
    }
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

bool RewriteSelectedModRootW(LPCWSTR path, wchar_t* output, size_t capacity)
{
    if(!path || !output || !capacity || !g_selectedModRoot[0])
        return false;
    wchar_t full[MAX_PATH * 4]{};
    const DWORD length = GetFullPathNameW(path,
        DWORD(sizeof(full) / sizeof(full[0])), full, nullptr);
    if(!length || length >= sizeof(full) / sizeof(full[0]))
        return false;
    NormalizeSlashes(full);

    wchar_t selected[MAX_PATH * 4]{};
    if(wcscpy_s(selected, g_selectedModRoot) != 0)
        return false;
    NormalizeSlashes(selected);
    const size_t selectedLength = std::wcslen(selected);
    if(_wcsnicmp(full, selected, selectedLength) == 0
       && IsPathBoundary(full[selectedLength]))
        return false;

    const wchar_t marker[] = L"\\Starter\\Mods\\";
    const size_t markerLength = (sizeof(marker) / sizeof(marker[0])) - 1;
    const wchar_t* markerAt = nullptr;
    for(const wchar_t* cursor = full; *cursor; ++cursor)
        if(_wcsnicmp(cursor, marker, markerLength) == 0)
        {
            markerAt = cursor;
            break;
        }
    if(!markerAt)
        return false;

    const wchar_t* requestedMod = markerAt + markerLength;
    const wchar_t* requestedSuffix = requestedMod;
    while(*requestedSuffix && *requestedSuffix != L'\\' && *requestedSuffix != L'/')
        ++requestedSuffix;
    const wchar_t* selectedName = FileNamePart(selected);
    const size_t requestedNameLength = size_t(requestedSuffix - requestedMod);
    if(!selectedName || std::wcslen(selectedName) != requestedNameLength
       || _wcsnicmp(requestedMod, selectedName, requestedNameLength) != 0)
        return false;

    if(_snwprintf_s(output, capacity, _TRUNCATE, L"%s%s",
                    selected, requestedSuffix) < 0)
        return false;
    const LONG sequence = InterlockedIncrement(&g_modRootRedirectLogCount);
    if(sequence <= 64)
    {
        wchar_t message[1900]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"JMV_ROOT_REDIRECT seq=%ld from=\"%s\" to=\"%s\"",
                     sequence, full, output);
        Log(message);
    }
    return true;
}

bool RewriteSelectedModRootA(LPCSTR path, char* output, size_t capacity)
{
    if(!path || !output || !capacity)
        return false;
    wchar_t wide[MAX_PATH * 4]{};
    wchar_t rewritten[MAX_PATH * 4]{};
    if(!MultiByteToWideChar(CP_ACP, 0, path, -1, wide,
                            int(sizeof(wide) / sizeof(wide[0])))
       || !RewriteSelectedModRootW(wide, rewritten,
                                   sizeof(rewritten) / sizeof(rewritten[0])))
        return false;
    BOOL substituted = FALSE;
    return WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, rewritten, -1,
                               output, int(capacity), nullptr, &substituted) != 0
        && !substituted;
}

bool RewriteOwnedPathW(LPCWSTR path, wchar_t* output, size_t capacity)
{
    if(!path || !output || !capacity)
        return false;
    wchar_t modPath[MAX_PATH * 4]{};
    const wchar_t* selected = path;
    const bool modRedirected = RewriteSelectedModRootW(
        path, modPath, sizeof(modPath) / sizeof(modPath[0]));
    if(modRedirected)
        selected = modPath;
    if(RewriteSavePathW(selected, output, capacity))
        return true;
    return modRedirected && wcscpy_s(output, capacity, selected) == 0;
}

bool RewriteOwnedPathA(LPCSTR path, char* output, size_t capacity)
{
    if(!path || !output || !capacity)
        return false;
    char modPath[MAX_PATH * 4]{};
    const char* selected = path;
    const bool modRedirected = RewriteSelectedModRootA(path, modPath, sizeof(modPath));
    if(modRedirected)
        selected = modPath;
    if(RewriteSavePathA(selected, output, capacity))
        return true;
    return modRedirected && strcpy_s(output, capacity, selected) == 0;
}

bool RewriteMapTableW(LPCWSTR path, wchar_t* output, size_t capacity)
{
    if(!path || !output || !capacity || !g_mapTableSource[0]
       || !g_mapTableStage[0])
        return false;
    wchar_t full[MAX_PATH * 4]{};
    const DWORD length = GetFullPathNameW(
        path, DWORD(sizeof(full) / sizeof(full[0])), full, nullptr);
    if(!length || length >= sizeof(full) / sizeof(full[0]))
        return false;
    NormalizeSlashes(full);
    if(_wcsicmp(full, g_mapTableSource) != 0
       || wcscpy_s(output, capacity, g_mapTableStage) != 0)
        return false;
    const LONG sequence = InterlockedIncrement(&g_mapTableRedirectLogCount);
    if(sequence <= 8)
    {
        wchar_t message[1900]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"MAP_TABLE_REDIRECT seq=%ld from=\"%s\" to=\"%s\"",
                     sequence, full, output);
        Log(message);
    }
    return true;
}

bool RewriteMapTableA(LPCSTR path, char* output, size_t capacity)
{
    if(!path || !output || !capacity)
        return false;
    wchar_t wide[MAX_PATH * 4]{};
    wchar_t rewritten[MAX_PATH * 4]{};
    if(!MultiByteToWideChar(CP_ACP, 0, path, -1, wide,
                            int(sizeof(wide) / sizeof(wide[0])))
       || !RewriteMapTableW(wide, rewritten,
                            sizeof(rewritten) / sizeof(rewritten[0])))
        return false;
    BOOL substituted = FALSE;
    return WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, rewritten, -1,
                               output, int(capacity), nullptr, &substituted) != 0
        && !substituted;
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
    wchar_t owned[MAX_PATH * 4]{};
    wchar_t redirected[MAX_PATH * 4]{};
    LPCWSTR selected = (RewriteGodmodeMobW(path, owned, sizeof(owned) / sizeof(owned[0]))
        || RewriteSupplementalMobW(path, owned, sizeof(owned) / sizeof(owned[0]))
        || RewriteOwnedPathW(path, owned, sizeof(owned) / sizeof(owned[0]))) ? owned : path;
    if(RewriteMapTableW(selected, redirected,
                        sizeof(redirected) / sizeof(redirected[0])))
        selected = redirected;
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
    char owned[MAX_PATH * 4]{};
    char redirected[MAX_PATH * 4]{};
    LPCSTR selected = (RewriteGodmodeMobA(path, owned, sizeof(owned))
        || RewriteSupplementalMobA(path, owned, sizeof(owned))
        || RewriteOwnedPathA(path, owned, sizeof(owned))) ? owned : path;
    if(RewriteMapTableA(selected, redirected, sizeof(redirected)))
        selected = redirected;
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
    wchar_t owned[MAX_PATH * 4]{};
    wchar_t redirected[MAX_PATH * 4]{};
    LPCWSTR selected = (RewriteGodmodeMobW(path, owned, sizeof(owned) / sizeof(owned[0]))
        || RewriteSupplementalMobW(path, owned, sizeof(owned) / sizeof(owned[0]))
        || RewriteOwnedPathW(path, owned, sizeof(owned) / sizeof(owned[0]))) ? owned : path;
    if(RewriteMapTableW(selected, redirected,
                        sizeof(redirected) / sizeof(redirected[0])))
        selected = redirected;
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
    char owned[MAX_PATH * 4]{};
    char redirected[MAX_PATH * 4]{};
    LPCSTR selected = (RewriteGodmodeMobA(path, owned, sizeof(owned))
        || RewriteSupplementalMobA(path, owned, sizeof(owned))
        || RewriteOwnedPathA(path, owned, sizeof(owned))) ? owned : path;
    if(RewriteMapTableA(selected, redirected, sizeof(redirected)))
        selected = redirected;
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
LPCWSTR selected = RewriteOwnedPathW(path, redirected, sizeof(redirected) / sizeof(redirected[0])) ? redirected : path; \
return original arguments; }
#define REDIRECT_ONE_A(name, original, resultType, failure, parameters, arguments) \
resultType WINAPI name parameters { char redirected[MAX_PATH * 4]{}; \
LPCSTR selected = RewriteOwnedPathA(path, redirected, sizeof(redirected)) ? redirected : path; \
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
    LPCWSTR selectedFrom = RewriteOwnedPathW(from, redirectedFrom, sizeof(redirectedFrom) / sizeof(redirectedFrom[0])) ? redirectedFrom : from;
    LPCWSTR selectedTo = RewriteOwnedPathW(to, redirectedTo, sizeof(redirectedTo) / sizeof(redirectedTo[0])) ? redirectedTo : to;
    return g_originalMoveFileW(selectedFrom, selectedTo);
}
BOOL WINAPI HookMoveFileA(LPCSTR from, LPCSTR to)
{
    char redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCSTR selectedFrom = RewriteOwnedPathA(from, redirectedFrom, sizeof(redirectedFrom)) ? redirectedFrom : from;
    LPCSTR selectedTo = RewriteOwnedPathA(to, redirectedTo, sizeof(redirectedTo)) ? redirectedTo : to;
    return g_originalMoveFileA(selectedFrom, selectedTo);
}
BOOL WINAPI HookMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags)
{
    wchar_t redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCWSTR selectedFrom = RewriteOwnedPathW(from, redirectedFrom, sizeof(redirectedFrom) / sizeof(redirectedFrom[0])) ? redirectedFrom : from;
    LPCWSTR selectedTo = RewriteOwnedPathW(to, redirectedTo, sizeof(redirectedTo) / sizeof(redirectedTo[0])) ? redirectedTo : to;
    return g_originalMoveFileExW(selectedFrom, selectedTo, flags);
}
BOOL WINAPI HookMoveFileExA(LPCSTR from, LPCSTR to, DWORD flags)
{
    char redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCSTR selectedFrom = RewriteOwnedPathA(from, redirectedFrom, sizeof(redirectedFrom)) ? redirectedFrom : from;
    LPCSTR selectedTo = RewriteOwnedPathA(to, redirectedTo, sizeof(redirectedTo)) ? redirectedTo : to;
    return g_originalMoveFileExA(selectedFrom, selectedTo, flags);
}
BOOL WINAPI HookCopyFileW(LPCWSTR from, LPCWSTR to, BOOL failIfExists)
{
    wchar_t redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCWSTR selectedFrom = RewriteOwnedPathW(from, redirectedFrom, sizeof(redirectedFrom) / sizeof(redirectedFrom[0])) ? redirectedFrom : from;
    LPCWSTR selectedTo = RewriteOwnedPathW(to, redirectedTo, sizeof(redirectedTo) / sizeof(redirectedTo[0])) ? redirectedTo : to;
    return g_originalCopyFileW(selectedFrom, selectedTo, failIfExists);
}
BOOL WINAPI HookCopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists)
{
    char redirectedFrom[MAX_PATH * 4]{}, redirectedTo[MAX_PATH * 4]{};
    LPCSTR selectedFrom = RewriteOwnedPathA(from, redirectedFrom, sizeof(redirectedFrom)) ? redirectedFrom : from;
    LPCSTR selectedTo = RewriteOwnedPathA(to, redirectedTo, sizeof(redirectedTo)) ? redirectedTo : to;
    return g_originalCopyFileA(selectedFrom, selectedTo, failIfExists);
}

bool IsPathSettingsSubkey(LPCSTR subkey)
{
    if(!subkey || !subkey[0])
        return false;
    const char* suffix = "Nival Interactive\\EvilIslands\\Path Settings";
    const size_t length = std::strlen(subkey);
    const size_t suffixLength = std::strlen(suffix);
    if(length >= suffixLength && _stricmp(subkey + length - suffixLength, suffix) == 0)
        return true;
    return _stricmp(subkey, "Path Settings") == 0;
}

bool IsPathSettingsSubkey(LPCWSTR subkey)
{
    if(!subkey || !subkey[0])
        return false;
    const wchar_t* suffix = L"Nival Interactive\\EvilIslands\\Path Settings";
    const size_t length = std::wcslen(subkey);
    const size_t suffixLength = std::wcslen(suffix);
    if(length >= suffixLength && _wcsicmp(subkey + length - suffixLength, suffix) == 0)
        return true;
    return _wcsicmp(subkey, L"Path Settings") == 0;
}

bool IsStarterSettingsSubkey(LPCSTR subkey)
{
    if(!subkey || !subkey[0])
        return false;
    const char* root = "Gipat.Ru\\EI_Starter";
    const size_t length = std::strlen(subkey);
    const size_t rootLength = std::strlen(root);
    if(length >= rootLength && _strnicmp(subkey + length - rootLength,
                                        root, rootLength) == 0)
        return true;
    const char* settings = "Gipat.Ru\\EI_Starter\\Settings";
    const size_t settingsLength = std::strlen(settings);
    return length >= settingsLength
        && _stricmp(subkey + length - settingsLength, settings) == 0;
}

bool IsStarterSettingsSubkey(LPCWSTR subkey)
{
    if(!subkey || !subkey[0])
        return false;
    const wchar_t* root = L"Gipat.Ru\\EI_Starter";
    const size_t length = std::wcslen(subkey);
    const size_t rootLength = std::wcslen(root);
    if(length >= rootLength && _wcsnicmp(subkey + length - rootLength,
                                        root, rootLength) == 0)
        return true;
    const wchar_t* settings = L"Gipat.Ru\\EI_Starter\\Settings";
    const size_t settingsLength = std::wcslen(settings);
    return length >= settingsLength
        && _wcsicmp(subkey + length - settingsLength, settings) == 0;
}

void MarkTrackedKey(HKEY key, HKEY* keys, size_t count)
{
    if(!key || !keys || !InterlockedCompareExchange(&g_registryPathLockReady, 0, 0))
        return;
    EnterCriticalSection(&g_registryPathLock);
    for(size_t index = 0; index < count; ++index)
        if(keys[index] == key)
        {
            LeaveCriticalSection(&g_registryPathLock);
            return;
        }
    for(size_t index = 0; index < count; ++index)
        if(!keys[index])
        {
            keys[index] = key;
            break;
        }
    LeaveCriticalSection(&g_registryPathLock);
}

bool IsTrackedKey(HKEY key, HKEY* keys, size_t count)
{
    if(!key || !keys || !InterlockedCompareExchange(&g_registryPathLockReady, 0, 0))
        return false;
    bool found = false;
    EnterCriticalSection(&g_registryPathLock);
    for(size_t index = 0; index < count; ++index)
        if(keys[index] == key)
        {
            found = true;
            break;
        }
    LeaveCriticalSection(&g_registryPathLock);
    return found;
}

void UnmarkTrackedKey(HKEY key, HKEY* keys, size_t count)
{
    if(!key || !keys || !InterlockedCompareExchange(&g_registryPathLockReady, 0, 0))
        return;
    EnterCriticalSection(&g_registryPathLock);
    for(size_t index = 0; index < count; ++index)
        if(keys[index] == key)
            keys[index] = nullptr;
    LeaveCriticalSection(&g_registryPathLock);
}

void MarkPathSettingsKey(HKEY key)
{
    MarkTrackedKey(key, g_pathSettingsKeys,
                   sizeof(g_pathSettingsKeys) / sizeof(g_pathSettingsKeys[0]));
}

bool IsPathSettingsKey(HKEY key)
{
    return IsTrackedKey(key, g_pathSettingsKeys,
                        sizeof(g_pathSettingsKeys) / sizeof(g_pathSettingsKeys[0]));
}

void UnmarkPathSettingsKey(HKEY key)
{
    UnmarkTrackedKey(key, g_pathSettingsKeys,
                     sizeof(g_pathSettingsKeys) / sizeof(g_pathSettingsKeys[0]));
}

LSTATUS WINAPI HookRegOpenKeyA(HKEY key, LPCSTR subkey, PHKEY result)
{
    const LSTATUS status = g_originalRegOpenKeyA(key, subkey, result);
    if(status == ERROR_SUCCESS && result && IsPathSettingsSubkey(subkey))
        MarkPathSettingsKey(*result);
    if(status == ERROR_SUCCESS && result
       && (IsStarterSettingsSubkey(subkey)
           || (IsTrackedKey(key, g_starterSettingsKeys,
                            sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]))
               && subkey && _stricmp(subkey, "Settings") == 0)))
        MarkTrackedKey(*result, g_starterSettingsKeys,
                       sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]));
    return status;
}

LSTATUS WINAPI HookRegOpenKeyExA(HKEY key, LPCSTR subkey, DWORD options,
                                 REGSAM access, PHKEY result)
{
    const LSTATUS status = g_originalRegOpenKeyExA(key, subkey, options, access, result);
    if(status == ERROR_SUCCESS && result && IsPathSettingsSubkey(subkey))
        MarkPathSettingsKey(*result);
    if(status == ERROR_SUCCESS && result
       && (IsStarterSettingsSubkey(subkey)
           || (IsTrackedKey(key, g_starterSettingsKeys,
                            sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]))
               && subkey && _stricmp(subkey, "Settings") == 0)))
        MarkTrackedKey(*result, g_starterSettingsKeys,
                       sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]));
    return status;
}

LSTATUS WINAPI HookRegOpenKeyExW(HKEY key, LPCWSTR subkey, DWORD options,
                                 REGSAM access, PHKEY result)
{
    const LSTATUS status = g_originalRegOpenKeyExW(key, subkey, options, access, result);
    if(status == ERROR_SUCCESS && result && IsPathSettingsSubkey(subkey))
        MarkPathSettingsKey(*result);
    if(status == ERROR_SUCCESS && result
       && (IsStarterSettingsSubkey(subkey)
           || (IsTrackedKey(key, g_starterSettingsKeys,
                            sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]))
               && subkey && _wcsicmp(subkey, L"Settings") == 0)))
        MarkTrackedKey(*result, g_starterSettingsKeys,
                       sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]));
    return status;
}

LSTATUS WINAPI HookRegCreateKeyExA(HKEY key, LPCSTR subkey, DWORD reserved,
                                   LPSTR className, DWORD options, REGSAM access,
                                   const LPSECURITY_ATTRIBUTES security,
                                   PHKEY result, LPDWORD disposition)
{
    const LSTATUS status = g_originalRegCreateKeyExA(key, subkey, reserved, className,
        options, access, security, result, disposition);
    if(status == ERROR_SUCCESS && result && IsPathSettingsSubkey(subkey))
        MarkPathSettingsKey(*result);
    if(status == ERROR_SUCCESS && result
       && (IsStarterSettingsSubkey(subkey)
           || (IsTrackedKey(key, g_starterSettingsKeys,
                            sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]))
               && subkey && _stricmp(subkey, "Settings") == 0)))
        MarkTrackedKey(*result, g_starterSettingsKeys,
                       sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]));
    return status;
}

bool SelectedRegistryPath(LPCSTR valueName, char* output, size_t capacity)
{
    if(!valueName || !output || !capacity || !g_selectedGameRoot[0])
        return false;
    wchar_t selected[MAX_PATH * 4]{};
    if(_stricmp(valueName, "RESOURCES PATH") == 0)
        _snwprintf_s(selected, sizeof(selected) / sizeof(selected[0]), _TRUNCATE,
                     L"%s\\Res\\", g_selectedGameRoot);
    else if(_stricmp(valueName, "WORK PATH") == 0
            || _stricmp(valueName, "CD-ROM PATH") == 0)
        _snwprintf_s(selected, sizeof(selected) / sizeof(selected[0]), _TRUNCATE,
                     L"%s\\", g_selectedGameRoot);
    else
        return false;
    BOOL substituted = FALSE;
    return WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, selected, -1,
                               output, int(capacity), nullptr, &substituted) > 0
        && !substituted;
}

bool SelectedStarterPath(LPCSTR valueName, char* output, size_t capacity)
{
    if(!valueName || !output || !capacity || !g_selectedGameRoot[0])
        return false;
    wchar_t selected[MAX_PATH * 4]{};
    if(_stricmp(valueName, "AddonPath") == 0 && g_selectedModRoot[0])
        wcscpy_s(selected, g_selectedModRoot);
    else if(_stricmp(valueName, "path") == 0)
        _snwprintf_s(selected, sizeof(selected) / sizeof(selected[0]), _TRUNCATE,
                     L"%s\\Starter", g_selectedGameRoot);
    else
        return false;
    BOOL substituted = FALSE;
    return WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, selected, -1,
                               output, int(capacity), nullptr, &substituted) > 0
        && !substituted;
}

bool SelectedRegistryPath(LPCWSTR valueName, wchar_t* output, size_t capacity)
{
    if(!valueName || !output || !capacity || !g_selectedGameRoot[0])
        return false;
    if(_wcsicmp(valueName, L"RESOURCES PATH") == 0)
        return _snwprintf_s(output, capacity, _TRUNCATE,
                            L"%s\\Res\\", g_selectedGameRoot) >= 0;
    if(_wcsicmp(valueName, L"WORK PATH") == 0
       || _wcsicmp(valueName, L"CD-ROM PATH") == 0)
        return _snwprintf_s(output, capacity, _TRUNCATE,
                            L"%s\\", g_selectedGameRoot) >= 0;
    return false;
}

bool SelectedStarterPath(LPCWSTR valueName, wchar_t* output, size_t capacity)
{
    if(!valueName || !output || !capacity || !g_selectedGameRoot[0])
        return false;
    if(_wcsicmp(valueName, L"AddonPath") == 0 && g_selectedModRoot[0])
        return wcscpy_s(output, capacity, g_selectedModRoot) == 0;
    if(_wcsicmp(valueName, L"path") == 0)
        return _snwprintf_s(output, capacity, _TRUNCATE,
                            L"%s\\Starter", g_selectedGameRoot) >= 0;
    return false;
}

LSTATUS WINAPI HookRegQueryValueExA(HKEY key, LPCSTR valueName, LPDWORD reserved,
                                    LPDWORD type, LPBYTE data, LPDWORD dataSize)
{
    char selected[MAX_PATH * 4]{};
    const bool nivalOverride = IsPathSettingsKey(key)
        && SelectedRegistryPath(valueName, selected, sizeof(selected));
    const bool starterOverride = IsTrackedKey(
        key, g_starterSettingsKeys,
        sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]))
        && SelectedStarterPath(valueName, selected, sizeof(selected));
    if(!nivalOverride && !starterOverride)
        return g_originalRegQueryValueExA(key, valueName, reserved, type, data, dataSize);
    if(!dataSize)
        return ERROR_INVALID_PARAMETER;
    const DWORD required = DWORD(std::strlen(selected) + 1);
    if(type)
        *type = REG_SZ;
    if(!data || *dataSize < required)
    {
        *dataSize = required;
        return data ? ERROR_MORE_DATA : ERROR_SUCCESS;
    }
    std::memcpy(data, selected, required);
    *dataSize = required;
    if(InterlockedIncrement(&g_registryOverrideLogCount) <= 8)
    {
        wchar_t valueWide[64]{};
        wchar_t selectedWide[MAX_PATH * 4]{};
        MultiByteToWideChar(CP_ACP, 0, valueName, -1, valueWide,
                            int(sizeof(valueWide) / sizeof(valueWide[0])));
        MultiByteToWideChar(CP_ACP, 0, selected, -1, selectedWide,
                            int(sizeof(selectedWide) / sizeof(selectedWide[0])));
        wchar_t message[1800]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     starterOverride
                         ? L"STARTER_ROOT_OVERRIDE value=\"%s\" selected=\"%s\" scope=owned-process"
                         : L"GAME_ROOT_OVERRIDE value=\"%s\" selected=\"%s\" scope=owned-process",
                     valueWide, selectedWide);
        Log(message);
    }
    return ERROR_SUCCESS;
}

LSTATUS WINAPI HookRegQueryValueExW(HKEY key, LPCWSTR valueName, LPDWORD reserved,
                                    LPDWORD type, LPBYTE data, LPDWORD dataSize)
{
    wchar_t selected[MAX_PATH * 4]{};
    const bool nivalOverride = IsPathSettingsKey(key)
        && SelectedRegistryPath(valueName, selected,
                                sizeof(selected) / sizeof(selected[0]));
    const bool starterOverride = IsTrackedKey(
        key, g_starterSettingsKeys,
        sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]))
        && SelectedStarterPath(valueName, selected,
                               sizeof(selected) / sizeof(selected[0]));
    if(!nivalOverride && !starterOverride)
        return g_originalRegQueryValueExW(key, valueName, reserved, type, data, dataSize);
    if(!dataSize)
        return ERROR_INVALID_PARAMETER;
    const DWORD required = DWORD((std::wcslen(selected) + 1) * sizeof(wchar_t));
    if(type)
        *type = REG_SZ;
    if(!data || *dataSize < required)
    {
        *dataSize = required;
        return data ? ERROR_MORE_DATA : ERROR_SUCCESS;
    }
    std::memcpy(data, selected, required);
    *dataSize = required;
    if(InterlockedIncrement(&g_registryOverrideLogCount) <= 8)
    {
        wchar_t message[1800]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     starterOverride
                         ? L"STARTER_ROOT_OVERRIDE value=\"%s\" selected=\"%s\" scope=owned-process api=wide"
                         : L"GAME_ROOT_OVERRIDE value=\"%s\" selected=\"%s\" scope=owned-process api=wide",
                     valueName, selected);
        Log(message);
    }
    return ERROR_SUCCESS;
}

LSTATUS WINAPI HookRegCloseKey(HKEY key)
{
    UnmarkPathSettingsKey(key);
    UnmarkTrackedKey(key, g_starterSettingsKeys,
                     sizeof(g_starterSettingsKeys) / sizeof(g_starterSettingsKeys[0]));
    return g_originalRegCloseKey(key);
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
    if(std::strcmp(name, "RegOpenKeyA") == 0)
        return reinterpret_cast<void*>(&HookRegOpenKeyA);
    if(std::strcmp(name, "RegOpenKeyExA") == 0)
        return reinterpret_cast<void*>(&HookRegOpenKeyExA);
    if(std::strcmp(name, "RegOpenKeyExW") == 0)
        return reinterpret_cast<void*>(&HookRegOpenKeyExW);
    if(std::strcmp(name, "RegCreateKeyExA") == 0)
        return reinterpret_cast<void*>(&HookRegCreateKeyExA);
    if(std::strcmp(name, "RegQueryValueExA") == 0)
        return reinterpret_cast<void*>(&HookRegQueryValueExA);
    if(std::strcmp(name, "RegQueryValueExW") == 0)
        return reinterpret_cast<void*>(&HookRegQueryValueExW);
    if(std::strcmp(name, "RegCloseKey") == 0)
        return reinterpret_cast<void*>(&HookRegCloseKey);
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
           _stricmp(library, "KERNELBASE.dll") != 0 &&
           _stricmp(library, "ADVAPI32.dll") != 0)
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
    HMODULE advapi = GetModuleHandleW(L"ADVAPI32.dll");
    if(!kernel || !advapi)
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
    g_originalRegOpenKeyA = reinterpret_cast<RegOpenKeyAFn>(
        GetProcAddress(advapi, "RegOpenKeyA"));
    g_originalRegOpenKeyExA = reinterpret_cast<RegOpenKeyExAFn>(
        GetProcAddress(advapi, "RegOpenKeyExA"));
    g_originalRegOpenKeyExW = reinterpret_cast<RegOpenKeyExWFn>(
        GetProcAddress(advapi, "RegOpenKeyExW"));
    g_originalRegCreateKeyExA = reinterpret_cast<RegCreateKeyExAFn>(
        GetProcAddress(advapi, "RegCreateKeyExA"));
    g_originalRegQueryValueExA = reinterpret_cast<RegQueryValueExAFn>(
        GetProcAddress(advapi, "RegQueryValueExA"));
    g_originalRegQueryValueExW = reinterpret_cast<RegQueryValueExWFn>(
        GetProcAddress(advapi, "RegQueryValueExW"));
    g_originalRegCloseKey = reinterpret_cast<RegCloseKeyFn>(
        GetProcAddress(advapi, "RegCloseKey"));
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
       !g_originalCopyFileA || !g_originalCopyFileW ||
       !g_originalRegOpenKeyA || !g_originalRegOpenKeyExA || !g_originalRegOpenKeyExW ||
       !g_originalRegCreateKeyExA || !g_originalRegQueryValueExA ||
       !g_originalRegQueryValueExW ||
       !g_originalRegCloseKey)
        return 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                               GetCurrentProcessId());
    if(snapshot == INVALID_HANDLE_VALUE)
        return 0;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int patched = 0;
    int modulesVisited = 0;
    if(Module32FirstW(snapshot, &entry))
    {
        do
        {
            ++modulesVisited;
            patched += PatchModuleImports(entry.hModule);
        }
        while(Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    wchar_t summary[256]{};
    _snwprintf_s(summary, sizeof(summary) / sizeof(summary[0]), _TRUNCATE,
                 L"TRANSITION_PROBE inspected loaded modules in owned game process modules=%d patched_imports=%d",
                 modulesVisited, patched);
    Log(summary);
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
    wchar_t bootstrapZone[128]{};
    wchar_t bootstrapMpr[128]{};
    wchar_t bootstrapMob[128]{};
    wchar_t syntheticMap[8]{};
    wchar_t mapSizeX[16]{};
    wchar_t mapSizeY[16]{};
    wchar_t spawnX[64]{};
    wchar_t spawnY[64]{};
    wchar_t targetExit[16]{};
    wchar_t graphless[8]{};
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_GRAPHLESS", graphless,
                            DWORD(sizeof(graphless) / sizeof(graphless[0])));
    InterlockedExchange(&g_graphlessPayload,
                        std::wcscmp(graphless, L"1") == 0 ? 1 : 0);
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
       !GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_BOOTSTRAP_ZONE", bootstrapZone,
                                DWORD(sizeof(bootstrapZone) / sizeof(bootstrapZone[0]))) ||
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
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_MAP_TABLE_SOURCE",
                            g_mapTableSource,
                            DWORD(sizeof(g_mapTableSource)
                                  / sizeof(g_mapTableSource[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_MAP_TABLE_STAGE",
                            g_mapTableStage,
                            DWORD(sizeof(g_mapTableStage)
                                  / sizeof(g_mapTableStage[0])));
    if(g_mapTableSource[0] || g_mapTableStage[0])
    {
        wchar_t sourceFull[MAX_PATH * 4]{};
        wchar_t stageFull[MAX_PATH * 4]{};
        const DWORD sourceLength = GetFullPathNameW(
            g_mapTableSource, DWORD(sizeof(sourceFull) / sizeof(sourceFull[0])),
            sourceFull, nullptr);
        const DWORD stageLength = GetFullPathNameW(
            g_mapTableStage, DWORD(sizeof(stageFull) / sizeof(stageFull[0])),
            stageFull, nullptr);
        if(!sourceLength || sourceLength >= sizeof(sourceFull) / sizeof(sourceFull[0])
           || !stageLength || stageLength >= sizeof(stageFull) / sizeof(stageFull[0])
           || GetFileAttributesW(sourceFull) == INVALID_FILE_ATTRIBUTES
           || GetFileAttributesW(stageFull) == INVALID_FILE_ATTRIBUTES)
        {
            Log(L"DIRECT_LAUNCH disabled: private map-table source/stage paths are incomplete or unreadable");
            return false;
        }
        NormalizeSlashes(sourceFull);
        NormalizeSlashes(stageFull);
        wcscpy_s(g_mapTableSource, sourceFull);
        wcscpy_s(g_mapTableStage, stageFull);
    }
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_TEMP_TEXTURE_RES",
                            g_temporaryTextureResource,
                            DWORD(sizeof(g_temporaryTextureResource)
                                  / sizeof(g_temporaryTextureResource[0])));
    if(g_temporaryTextureResource[0])
    {
        wchar_t full[MAX_PATH * 4]{};
        const DWORD length = GetFullPathNameW(
            g_temporaryTextureResource,
            DWORD(sizeof(full) / sizeof(full[0])), full, nullptr);
        if(!length || length >= sizeof(full) / sizeof(full[0])
           || GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES)
        {
            Log(L"DIRECT_LAUNCH disabled: run-owned texture RES is unreadable");
            return false;
        }
        NormalizeSlashes(full);
        wcscpy_s(g_temporaryTextureResource, full);
    }
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_BOOTSTRAP_MPR", bootstrapMpr,
                            DWORD(sizeof(bootstrapMpr) / sizeof(bootstrapMpr[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_BOOTSTRAP_MOB", bootstrapMob,
                            DWORD(sizeof(bootstrapMob) / sizeof(bootstrapMob[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_SYNTHETIC_MAP", syntheticMap,
                            DWORD(sizeof(syntheticMap) / sizeof(syntheticMap[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_MAP_SIZE_X", mapSizeX,
                            DWORD(sizeof(mapSizeX) / sizeof(mapSizeX[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_MAP_SIZE_Y", mapSizeY,
                            DWORD(sizeof(mapSizeY) / sizeof(mapSizeY[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_SPAWN_X", spawnX,
                            DWORD(sizeof(spawnX) / sizeof(spawnX[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_SPAWN_Y", spawnY,
                            DWORD(sizeof(spawnY) / sizeof(spawnY[0])));
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
       !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, bootstrapZone, -1,
                            g_bootstrapZone, int(sizeof(g_bootstrapZone)), nullptr, nullptr) ||
       mprSubstitution || mobSubstitution)
    {
        Log(L"DIRECT_LAUNCH disabled: staged paths cannot be represented in the game ANSI code page");
        return false;
    }
    g_syntheticMapIdentity = std::wcscmp(syntheticMap, L"1") == 0;
    if(g_syntheticMapIdentity)
    {
        wchar_t* endSizeX = nullptr;
        wchar_t* endSizeY = nullptr;
        wchar_t* endSpawnX = nullptr;
        wchar_t* endSpawnY = nullptr;
        const long parsedSizeX = std::wcstol(mapSizeX, &endSizeX, 10);
        const long parsedSizeY = std::wcstol(mapSizeY, &endSizeY, 10);
        const double parsedSpawnX = std::wcstod(spawnX, &endSpawnX);
        const double parsedSpawnY = std::wcstod(spawnY, &endSpawnY);
        if(!bootstrapMpr[0] || !bootstrapMob[0]
           || endSizeX == mapSizeX || *endSizeX != L'\0'
           || endSizeY == mapSizeY || *endSizeY != L'\0'
           || endSpawnX == spawnX || *endSpawnX != L'\0'
           || endSpawnY == spawnY || *endSpawnY != L'\0'
           || parsedSizeX <= 0 || parsedSizeX > 1024
           || parsedSizeY <= 0 || parsedSizeY > 1024
           || !std::isfinite(parsedSpawnX) || !std::isfinite(parsedSpawnY)
           || !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, bootstrapMpr, -1,
                                   g_bootstrapMpr, int(sizeof(g_bootstrapMpr)), nullptr, nullptr)
           || !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, bootstrapMob, -1,
                                   g_bootstrapMob, int(sizeof(g_bootstrapMob)), nullptr, nullptr))
        {
            Log(L"DIRECT_LAUNCH disabled: synthetic-map metadata is incomplete or invalid");
            return false;
        }
        g_syntheticMapSizeX = int(parsedSizeX);
        g_syntheticMapSizeY = int(parsedSizeY);
        g_syntheticSpawnX = float(parsedSpawnX);
        g_syntheticSpawnY = float(parsedSpawnY);
    }
    wchar_t message[1800]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"DIRECT_LAUNCH profile zone=\"%s\" entrance_id=%d entrance_index=%d synthetic=%d size=%dx%d isolated_saves=\"%s\" live_saves_protected=\"%s\"",
                 sourceZone, g_targetExitId, g_targetExit,
                 g_syntheticMapIdentity ? 1 : 0, g_syntheticMapSizeX,
                 g_syntheticMapSizeY, g_testSaves, g_liveSaves);
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
    // This is a thread-local hook in the process that owns the target thread.
    // Windows documents a null module handle for that case. Older builds used
    // the bridge HMODULE and usually succeeded, but it can be rejected on an
    // otherwise valid launch. Retain the old form only as a compatibility
    // fallback and record both errors if neither route is accepted.
    SetLastError(ERROR_SUCCESS);
    g_directLaunchHook = SetWindowsHookExW(WH_GETMESSAGE, &DirectLaunchMessageHook,
                                           nullptr, g_gameThreadId);
    const DWORD localHookError = g_directLaunchHook ? ERROR_SUCCESS : GetLastError();
    DWORD moduleHookError = ERROR_SUCCESS;
    if(!g_directLaunchHook)
    {
        SetLastError(ERROR_SUCCESS);
        g_directLaunchHook = SetWindowsHookExW(WH_GETMESSAGE, &DirectLaunchMessageHook,
                                               g_instance, g_gameThreadId);
        moduleHookError = g_directLaunchHook ? ERROR_SUCCESS : GetLastError();
    }
    if(!g_directLaunchHook)
    {
        wchar_t failure[384]{};
        _snwprintf_s(failure, sizeof(failure) / sizeof(failure[0]), _TRUNCATE,
                     L"DIRECT_LAUNCH failed: could not install the game-thread message hook local_error=%lu module_error=%lu thread=%lu",
                     localHookError, moduleHookError, g_gameThreadId);
        Log(failure);
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
                 L"DIRECT_LAUNCH queued thread=%lu after normal main-menu load hook_module=%s",
                 g_gameThreadId, localHookError == ERROR_SUCCESS ? L"process-local" : L"bridge-fallback");
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

bool SetGraphRequirementBypass(bool enabled)
{
    if(!g_gameModule)
        return false;
    auto* target = reinterpret_cast<unsigned char*>(g_gameModule)
        + kGraphRequirementRva;
    unsigned char nops[sizeof(kGraphRequirementBytes)]{};
    std::memset(nops, 0x90, sizeof(nops));
    const unsigned char* expected = enabled ? kGraphRequirementBytes : nops;
    const unsigned char* replacement = enabled ? nops : kGraphRequirementBytes;
    if(std::memcmp(target, expected, sizeof(kGraphRequirementBytes)) != 0)
    {
        Log(enabled
            ? L"GRAPHLESS refused: graph-requirement bytes do not match the validated game.exe"
            : L"GRAPHLESS cleanup failed: temporary graph bypass bytes changed unexpectedly");
        return false;
    }
    DWORD oldProtect = 0;
    if(!VirtualProtect(target, sizeof(kGraphRequirementBytes),
                       PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(target, replacement, sizeof(kGraphRequirementBytes));
    FlushInstructionCache(GetCurrentProcess(), target,
                          sizeof(kGraphRequirementBytes));
    DWORD ignored = 0;
    VirtualProtect(target, sizeof(kGraphRequirementBytes), oldProtect, &ignored);
    return true;
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

    const bool graphless = redirect
        && InterlockedCompareExchange(&g_graphlessPayload, 0, 0) != 0;
    if(graphless && !SetGraphRequirementBypass(true))
    {
        SetRuntimeState(RuntimeState::Unknown,
                        L"graphless map bypass byte guard failed");
        return 0;
    }
    if(graphless)
        Log(L"GRAPHLESS temporarily bypassed the required AI-graph load for this staged map only");
    const int result = g_originalMapLoad(self, selectedMpr, selectedMob, context);
    if(graphless)
    {
        if(SetGraphRequirementBypass(false))
            Log(L"GRAPHLESS restored the original AI-graph requirement after the staged loader returned");
        else
            Log(L"GRAPHLESS WARNING: failed to restore the temporary graph-requirement bytes");
    }
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
    if(strcpy_s(g_bootstrapZone, request.sourceZone) != 0
       || (request.syntheticMapIdentity
           && (strcpy_s(g_bootstrapMpr, request.bootstrapMpr) != 0
               || strcpy_s(g_bootstrapMob, request.bootstrapMob) != 0)))
    {
        Log(L"HOT_RELOAD refused: bootstrap map identity is too long");
        return false;
    }
    g_syntheticMapIdentity = request.syntheticMapIdentity;
    g_syntheticMapSizeX = request.mapSizeX;
    g_syntheticMapSizeY = request.mapSizeY;
    g_syntheticSpawnX = request.spawnX;
    g_syntheticSpawnY = request.spawnY;
    g_targetExitId = request.entranceId;
    g_targetExit = request.entranceId - 1;
    InterlockedExchange(&g_graphlessPayload, request.graphless ? 1 : 0);
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
    if(!RegisterTemporaryTextureResource())
    {
        Log(L"HOT_RELOAD refused: run-owned texture RES could not be registered safely");
        InterlockedExchange(&g_directLaunchState, -1);
        return false;
    }
    if(!ConfigureSyntheticZone())
    {
        Log(L"HOT_RELOAD refused: synthetic map identity could not be prepared safely");
        InterlockedExchange(&g_directLaunchState, -1);
        return false;
    }
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

    if(!RegisterTemporaryTextureResource())
    {
        Log(L"DIRECT_LAUNCH failed: run-owned texture RES could not be registered safely");
        InterlockedExchange(&g_directLaunchState, -1);
        return next;
    }
    if(!ConfigureSyntheticZone())
    {
        Log(L"DIRECT_LAUNCH failed: synthetic map identity could not be prepared safely");
        InterlockedExchange(&g_directLaunchState, -1);
        return next;
    }

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
    // JMV activation runs after the host has resumed Starter long enough for
    // its executable patch phase to settle, then temporarily suspends the
    // game's main thread while the byte-guarded detours are installed. Do not
    // call loader APIs here: the suspended thread can still own the Windows
    // loader lock even though executable bytes have stopped changing. The
    // first preparation call runs before process resume and caches this handle
    // while loader ownership is deterministic.
    if(!g_gameModule)
    {
        Log(L"DIRECT_LAUNCH disabled: prepared game module is unavailable");
        return false;
    }
    auto* game = reinterpret_cast<unsigned char*>(g_gameModule);
    if(!ReadDirectLaunchPaths())
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

bool PrepareRuntime()
{
    if(InterlockedCompareExchange(&g_prepared, 1, 0) != 0)
        return true;
    g_gameModule = GetModuleHandleW(nullptr);
    if(!g_gameModule)
    {
        Log(L"DIRECT_LAUNCH disabled: could not cache the game module during preparation");
        return false;
    }
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_BRIDGE_RESULT", g_resultPath,
                            DWORD(sizeof(g_resultPath) / sizeof(g_resultPath[0])));
    if(!g_resultPath[0])
        return false;
    InitializeCriticalSection(&g_savePathLock);
    InterlockedExchange(&g_savePathLockReady, 1);
    InitializeCriticalSection(&g_registryPathLock);
    InterlockedExchange(&g_registryPathLockReady, 1);
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_GAME_ROOT", g_selectedGameRoot,
                            DWORD(sizeof(g_selectedGameRoot)
                                  / sizeof(g_selectedGameRoot[0])));
    size_t gameRootLength = std::wcslen(g_selectedGameRoot);
    while(gameRootLength > 3
          && (g_selectedGameRoot[gameRootLength - 1] == L'\\'
              || g_selectedGameRoot[gameRootLength - 1] == L'/'))
        g_selectedGameRoot[--gameRootLength] = L'\0';
    NormalizeSlashes(g_selectedGameRoot);
    if(!g_selectedGameRoot[0])
    {
        Log(L"DIRECT_LAUNCH disabled: selected game root is missing");
        return false;
    }
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_MOD_ROOT", g_selectedModRoot,
                            DWORD(sizeof(g_selectedModRoot)
                                  / sizeof(g_selectedModRoot[0])));
    size_t modRootLength = std::wcslen(g_selectedModRoot);
    while(modRootLength > 3
          && (g_selectedModRoot[modRootLength - 1] == L'\\'
              || g_selectedModRoot[modRootLength - 1] == L'/'))
        g_selectedModRoot[--modRootLength] = L'\0';
    NormalizeSlashes(g_selectedModRoot);
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
    g_runtimeBuild = VerifyRuntime();
    Log(g_runtimeBuild == RuntimeBuild::BaseEi
        ? L"EI Mapper Endurance Base EI direct-map feasibility bridge initializing"
        : L"EI Mapper Endurance JMV direct single-player map launch initializing");
    if(g_runtimeBuild == RuntimeBuild::Unsupported)
    {
        Log(L"DIRECT_LAUNCH disabled: game.exe SHA-256 does not match an exact validated runtime manifest");
        return false;
    }
    if(g_runtimeBuild == RuntimeBuild::BaseEi)
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
    g_patchedImports = InstallFileObservationHooks();
    if(g_runtimeBuild == RuntimeBuild::BaseEi && g_baseProbeLevel == 1)
    {
        Log(g_patchedImports > 0
            ? L"BASE_PROBE level=1 injection=ready observation_hooks=ready redirect=disabled"
            : L"BASE_PROBE level=1 injection=ready observation_hooks=failed redirect=disabled");
        return g_patchedImports > 0;
    }
    Log(L"DIRECT_LAUNCH path and file isolation prepared; engine detours are not active yet");
    return g_patchedImports > 0;
}

bool ActivateRuntime()
{
    if(!PrepareRuntime())
        return false;
    if(InterlockedCompareExchange(&g_activated, 1, 0) != 0)
        return true;
    if(g_runtimeBuild == RuntimeBuild::BaseEi && g_baseProbeLevel == 1)
        return true;
    Log(L"RUNTIME_ACTIVATION phase=install-map-detours begin module=cached");
    const bool directLaunch = InstallMapLoadDetour();
    Log(directLaunch
        ? L"RUNTIME_ACTIVATION phase=install-map-detours complete result=ready"
        : L"RUNTIME_ACTIVATION phase=install-map-detours complete result=failed");
    if(g_runtimeBuild == RuntimeBuild::BaseEi && g_baseProbeLevel == 2 && directLaunch)
    {
        // Keep the byte-guarded detours installed, but use a state which can
        // never queue or redirect a transition. Calls retain their arguments.
        InterlockedExchange(&g_directLaunchState, 10);
        Log(L"BASE_PROBE level=2 hook_signatures=ready mode=pass-through redirect=disabled; start a normal Base New Game manually");
    }
    wchar_t message[256]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"DIRECT_LAUNCH active imports=%d stack_capture=%s map_loader=%s hot_reload=main-menu-rebootstrap trigger=complete-new-game-bootstrap",
                 g_patchedImports, g_captureStack ? L"available" : L"unavailable",
                 directLaunch ? L"available" : L"unavailable");
    Log(message);
    return g_patchedImports > 0 && directLaunch;
}

bool InitializeDirectLaunch()
{
    return PrepareRuntime() && ActivateRuntime();
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

extern "C" __declspec(dllexport) DWORD WINAPI EndurancePrepareRuntime(void*)
{
    return PrepareRuntime() ? 1u : 0u;
}

extern "C" __declspec(dllexport) DWORD WINAPI EnduranceActivateRuntime(void*)
{
    return ActivateRuntime() ? 1u : 0u;
}

// The Base owned-process injector resolves this export by its stable protocol
// name. MSVC decorates x86 __stdcall exports unless an explicit alias is
// supplied; Starter's plugin loader does not exercise this path, which is why
// a stale/missing alias only surfaced in the Base probe.
#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:EnduranceInitialize=_EnduranceInitialize@4")
#pragma comment(linker, "/EXPORT:EndurancePrepareRuntime=_EndurancePrepareRuntime@4")
#pragma comment(linker, "/EXPORT:EnduranceActivateRuntime=_EnduranceActivateRuntime@4")
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
