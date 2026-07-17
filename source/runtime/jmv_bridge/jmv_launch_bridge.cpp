#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>

#include <cstdio>
#include <cwchar>
#include <cstring>

namespace {
constexpr unsigned char kExpectedGameSha256[32] = {
    0xBE, 0x0F, 0xC3, 0x30, 0x03, 0x5E, 0x03, 0x98,
    0x3C, 0xCE, 0x59, 0x00, 0x38, 0xCC, 0x7F, 0x88,
    0x1F, 0x1F, 0x5D, 0x1B, 0xCF, 0x08, 0x27, 0x17,
    0xF3, 0x17, 0x76, 0xDD, 0x4D, 0xEE, 0x94, 0xE2
};
constexpr uintptr_t kGameStringCtorAddress = 0x007009DE;
constexpr uintptr_t kGameStringDtorAddress = 0x00700970;
constexpr uintptr_t kServerHandlerAddress = 0x005DF490;
constexpr unsigned char kServerHandlerPrologue[13] = {
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A,
    0xFF, 0x68, 0xC0, 0x8B, 0x72, 0x00
};

using CreateFileAFn = HANDLE (WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                      DWORD, DWORD, HANDLE);
using CreateFileWFn = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                      DWORD, DWORD, HANDLE);
using GetFileAttributesAFn = DWORD (WINAPI*)(LPCSTR);
using GetFileAttributesWFn = DWORD (WINAPI*)(LPCWSTR);

HMODULE g_instance = nullptr;
volatile LONG g_initialized = 0;
volatile LONG g_launchHandled = 0;
volatile LONG g_redirectCount = 0;
wchar_t g_expectedTemporaryMap[32] = {};
wchar_t g_sourceMap[64] = {};
wchar_t g_temporaryMpr[MAX_PATH * 4] = {};
wchar_t g_temporaryMob[MAX_PATH * 4] = {};
wchar_t g_resultPath[MAX_PATH * 4] = {};
char g_sourceMapA[64] = {};
char g_temporaryMprA[MAX_PATH * 4] = {};
char g_temporaryMobA[MAX_PATH * 4] = {};
CreateFileAFn g_originalCreateFileA = nullptr;
CreateFileWFn g_originalCreateFileW = nullptr;
GetFileAttributesAFn g_originalGetFileAttributesA = nullptr;
GetFileAttributesWFn g_originalGetFileAttributesW = nullptr;

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
    return std::memcmp(reinterpret_cast<const void*>(kServerHandlerAddress),
                       kServerHandlerPrologue, sizeof(kServerHandlerPrologue)) == 0;
}

bool IsSafeMapName(const wchar_t* map, size_t maximum)
{
    if(!map)
        return false;
    const size_t length = std::wcslen(map);
    if(length == 0 || length >= maximum)
        return false;
    for(size_t index = 0; index < length; ++index)
    {
        const wchar_t value = map[index];
        if(!((value >= L'0' && value <= L'9') ||
             (value >= L'a' && value <= L'z') || value == L'_'))
            return false;
    }
    return true;
}

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

const char* FileNamePart(const char* path)
{
    if(!path)
        return nullptr;
    const char* slash = std::strrchr(path, '\\');
    const char* forward = std::strrchr(path, '/');
    if(!slash || (forward && forward > slash))
        slash = forward;
    return slash ? slash + 1 : path;
}

const wchar_t* RedirectedPath(const wchar_t* path)
{
    const wchar_t* name = FileNamePart(path);
    if(!name)
        return path;
    wchar_t expected[96]{};
    _snwprintf_s(expected, sizeof(expected) / sizeof(expected[0]), _TRUNCATE,
                 L"%s.mpr", g_sourceMap);
    if(_wcsicmp(name, expected) == 0)
        return g_temporaryMpr;
    _snwprintf_s(expected, sizeof(expected) / sizeof(expected[0]), _TRUNCATE,
                 L"%s.mob", g_sourceMap);
    if(_wcsicmp(name, expected) == 0)
        return g_temporaryMob;
    return path;
}

const char* RedirectedPath(const char* path)
{
    const char* name = FileNamePart(path);
    if(!name)
        return path;
    char expected[96]{};
    _snprintf_s(expected, sizeof(expected), _TRUNCATE, "%s.mpr", g_sourceMapA);
    if(_stricmp(name, expected) == 0)
        return g_temporaryMprA;
    _snprintf_s(expected, sizeof(expected), _TRUNCATE, "%s.mob", g_sourceMapA);
    if(_stricmp(name, expected) == 0)
        return g_temporaryMobA;
    return path;
}

void LogRedirect(const wchar_t* kind, const wchar_t* source, const wchar_t* destination)
{
    const LONG count = InterlockedIncrement(&g_redirectCount);
    if(count > 16)
        return;
    wchar_t message[900]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"Redirected %s: %s -> %s", kind, source, destination);
    Log(message);
}

HANDLE WINAPI HookCreateFileW(LPCWSTR path, DWORD access, DWORD share,
                              LPSECURITY_ATTRIBUTES security, DWORD creation,
                              DWORD flags, HANDLE templateFile)
{
    const wchar_t* redirected = RedirectedPath(path);
    if(redirected != path)
        LogRedirect(L"CreateFileW", path, redirected);
    return g_originalCreateFileW(redirected, access, share, security, creation, flags, templateFile);
}

HANDLE WINAPI HookCreateFileA(LPCSTR path, DWORD access, DWORD share,
                              LPSECURITY_ATTRIBUTES security, DWORD creation,
                              DWORD flags, HANDLE templateFile)
{
    const char* redirected = RedirectedPath(path);
    if(redirected != path)
    {
        wchar_t sourceW[MAX_PATH * 4]{};
        wchar_t destinationW[MAX_PATH * 4]{};
        MultiByteToWideChar(CP_ACP, 0, path, -1, sourceW,
                            int(sizeof(sourceW) / sizeof(sourceW[0])));
        MultiByteToWideChar(CP_ACP, 0, redirected, -1, destinationW,
                            int(sizeof(destinationW) / sizeof(destinationW[0])));
        LogRedirect(L"CreateFileA", sourceW, destinationW);
    }
    return g_originalCreateFileA(redirected, access, share, security, creation, flags, templateFile);
}

DWORD WINAPI HookGetFileAttributesW(LPCWSTR path)
{
    const wchar_t* redirected = RedirectedPath(path);
    if(redirected != path)
        LogRedirect(L"GetFileAttributesW", path, redirected);
    return g_originalGetFileAttributesW(redirected);
}

DWORD WINAPI HookGetFileAttributesA(LPCSTR path)
{
    const char* redirected = RedirectedPath(path);
    if(redirected != path)
    {
        wchar_t sourceW[MAX_PATH * 4]{};
        wchar_t destinationW[MAX_PATH * 4]{};
        MultiByteToWideChar(CP_ACP, 0, path, -1, sourceW,
                            int(sizeof(sourceW) / sizeof(sourceW[0])));
        MultiByteToWideChar(CP_ACP, 0, redirected, -1, destinationW,
                            int(sizeof(destinationW) / sizeof(destinationW[0])));
        LogRedirect(L"GetFileAttributesA", sourceW, destinationW);
    }
    return g_originalGetFileAttributesA(redirected);
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

bool InstallFileRedirectHooks()
{
    HMODULE kernel = GetModuleHandleW(L"KERNEL32.dll");
    if(!kernel)
        return false;
    g_originalCreateFileA = reinterpret_cast<CreateFileAFn>(GetProcAddress(kernel, "CreateFileA"));
    g_originalCreateFileW = reinterpret_cast<CreateFileWFn>(GetProcAddress(kernel, "CreateFileW"));
    g_originalGetFileAttributesA = reinterpret_cast<GetFileAttributesAFn>(
        GetProcAddress(kernel, "GetFileAttributesA"));
    g_originalGetFileAttributesW = reinterpret_cast<GetFileAttributesWFn>(
        GetProcAddress(kernel, "GetFileAttributesW"));
    if(!g_originalCreateFileA || !g_originalCreateFileW ||
       !g_originalGetFileAttributesA || !g_originalGetFileAttributesW)
        return false;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                               GetCurrentProcessId());
    if(snapshot == INVALID_HANDLE_VALUE)
        return false;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int patched = 0;
    if(Module32FirstW(snapshot, &entry))
    {
        do
        {
            patched += PatchModuleImports(entry.hModule);
        }
        while(Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    wchar_t message[160]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"Installed %d temporary map file redirect imports", patched);
    Log(message);
    return patched > 0;
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

bool ReadEnvironment()
{
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_MAP", g_expectedTemporaryMap,
                            DWORD(sizeof(g_expectedTemporaryMap) / sizeof(g_expectedTemporaryMap[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_SOURCE_MAP", g_sourceMap,
                            DWORD(sizeof(g_sourceMap) / sizeof(g_sourceMap[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_TEMP_MPR", g_temporaryMpr,
                            DWORD(sizeof(g_temporaryMpr) / sizeof(g_temporaryMpr[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_TEMP_MOB", g_temporaryMob,
                            DWORD(sizeof(g_temporaryMob) / sizeof(g_temporaryMob[0])));
    GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_BRIDGE_RESULT", g_resultPath,
                            DWORD(sizeof(g_resultPath) / sizeof(g_resultPath[0])));
    if(!IsSafeMapName(g_expectedTemporaryMap, sizeof(g_expectedTemporaryMap) / sizeof(wchar_t)) ||
       !IsSafeMapName(g_sourceMap, sizeof(g_sourceMap) / sizeof(wchar_t)) ||
       !g_temporaryMpr[0] || !g_temporaryMob[0] || !g_resultPath[0])
        return false;
    if(GetFileAttributesW(g_temporaryMpr) == INVALID_FILE_ATTRIBUTES ||
       GetFileAttributesW(g_temporaryMob) == INVALID_FILE_ATTRIBUTES)
        return false;
    WideCharToMultiByte(CP_ACP, 0, g_sourceMap, -1, g_sourceMapA,
                        int(sizeof(g_sourceMapA)), nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, g_temporaryMpr, -1, g_temporaryMprA,
                        int(sizeof(g_temporaryMprA)), nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, g_temporaryMob, -1, g_temporaryMobA,
                        int(sizeof(g_temporaryMobA)), nullptr, nullptr);
    return g_sourceMapA[0] && g_temporaryMprA[0] && g_temporaryMobA[0];
}

bool InitializeBridge()
{
    if(InterlockedCompareExchange(&g_initialized, 1, 0) != 0)
        return true;
    if(!ReadEnvironment())
        return false;
    Log(L"EI Mapper Endurance JMV plugin bridge initializing");
    if(!VerifyRuntime())
    {
        Log(L"Bridge disabled: game hash or server-handler signature mismatch");
        return false;
    }
    Log(L"Bridge ready; waiting for the deferred game-thread launch timer");
    return true;
}

void CALLBACK DeferredLaunchTimer(HWND, UINT, UINT_PTR timerId, DWORD)
{
    KillTimer(nullptr, timerId);
    if(InterlockedCompareExchange(&g_launchHandled, 1, 0) != 0)
        return;
    Log(L"Deferred game-thread launch timer fired");
    if(!InstallFileRedirectHooks())
    {
        Log(L"Launch refused: no supported file imports could be redirected");
        return;
    }
    Log(L"Invoking the internal server handler with the source map identity");
    QueueServerLaunch(g_sourceMapA);
    Log(L"Engine server launch request queued");
}

bool ScheduleDeferredLaunch()
{
    if(!InitializeBridge())
        return false;
    const UINT_PTR timer = SetTimer(nullptr, 0, 1000, &DeferredLaunchTimer);
    if(!timer)
    {
        Log(L"Bridge disabled: could not create the deferred game-thread timer");
        return false;
    }
    Log(L"Deferred game-thread launch scheduled for 1000 ms after Starter patches");
    return true;
}
}

extern "C" __declspec(dllexport) int __cdecl EntryBeforePatch(const char*)
{
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl EntryAfterPatch(const char*)
{
    // Starter treats any non-zero callback result as an error/assert condition.
    // Failures are reported through the run-specific log and leave the runtime
    // unpatched while the loader still receives its required zero result.
    ScheduleDeferredLaunch();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if(reason == DLL_PROCESS_ATTACH)
    {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
