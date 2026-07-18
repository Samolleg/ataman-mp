#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>

#include <cstdio>
#include <cwchar>
#include <cstring>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

namespace {
constexpr unsigned char kExpectedGameSha256[32] = {
    0xBE, 0x0F, 0xC3, 0x30, 0x03, 0x5E, 0x03, 0x98,
    0x3C, 0xCE, 0x59, 0x00, 0x38, 0xCC, 0x7F, 0x88,
    0x1F, 0x1F, 0x5D, 0x1B, 0xCF, 0x08, 0x27, 0x17,
    0xF3, 0x17, 0x76, 0xDD, 0x4D, 0xEE, 0x94, 0xE2
};

using CreateFileAFn = HANDLE (WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                      DWORD, DWORD, HANDLE);
using CreateFileWFn = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                      DWORD, DWORD, HANDLE);
using GetFileAttributesAFn = DWORD (WINAPI*)(LPCSTR);
using GetFileAttributesWFn = DWORD (WINAPI*)(LPCWSTR);
using CaptureStackFn = USHORT (WINAPI*)(ULONG, ULONG, PVOID*, PULONG);
using MapLoadFn = int (__thiscall*)(void*, const char*, const char*, void*);
using NewGameFn = ULONG_PTR (__cdecl*)();

HMODULE g_instance = nullptr;
volatile LONG g_initialized = 0;
volatile LONG g_observationCount = 0;
wchar_t g_resultPath[MAX_PATH * 4] = {};
CreateFileAFn g_originalCreateFileA = nullptr;
CreateFileWFn g_originalCreateFileW = nullptr;
GetFileAttributesAFn g_originalGetFileAttributesA = nullptr;
GetFileAttributesWFn g_originalGetFileAttributesW = nullptr;
CaptureStackFn g_captureStack = nullptr;

constexpr DWORD kMapLoadEntryRva = 0x00195480;
constexpr DWORD kNewGameEntryRva = 0x00056140;
constexpr DWORD kNewGameMapLoadReturnRva = 0x000561F5;
constexpr UINT kDirectLaunchMessage = WM_APP + 0x45D;
constexpr unsigned char kMapLoadEntryBytes[6] = {
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00
};

MapLoadFn g_originalMapLoad = nullptr;
HMODULE g_gameModule = nullptr;
HHOOK g_directLaunchHook = nullptr;
volatile LONG g_directLaunchState = 0;
DWORD g_gameThreadId = 0;
char g_temporaryMpr[MAX_PATH * 4] = {};
char g_temporaryMob[MAX_PATH * 4] = {};

int __fastcall MapLoadDetour(void* self, void*, const char* mprPath,
                             const char* mobPath, void* context);
LRESULT CALLBACK DirectLaunchMessageHook(int code, WPARAM wParam, LPARAM lParam);

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
    return HashFileSha256(gamePath, hash) &&
           std::memcmp(hash, kExpectedGameSha256, sizeof(hash)) == 0;
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

HANDLE WINAPI HookCreateFileW(LPCWSTR path, DWORD access, DWORD share,
                              LPSECURITY_ATTRIBUTES security, DWORD creation,
                              DWORD flags, HANDLE templateFile)
{
    void* caller = _ReturnAddress();
    HANDLE result = g_originalCreateFileW(path, access, share, security, creation, flags, templateFile);
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
    HANDLE result = g_originalCreateFileA(path, access, share, security, creation, flags, templateFile);
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
    const DWORD result = g_originalGetFileAttributesW(path);
    const DWORD error = GetLastError();
    Observe(L"GetFileAttributesW", path, result, error, caller);
    SetLastError(error);
    return result;
}

DWORD WINAPI HookGetFileAttributesA(LPCSTR path)
{
    void* caller = _ReturnAddress();
    const DWORD result = g_originalGetFileAttributesA(path);
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
    g_captureStack = ntdll ? reinterpret_cast<CaptureStackFn>(
        GetProcAddress(ntdll, "RtlCaptureStackBackTrace")) : nullptr;
    if(!g_originalCreateFileA || !g_originalCreateFileW ||
       !g_originalGetFileAttributesA || !g_originalGetFileAttributesW)
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
    if(!GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_TEMP_MPR", mpr,
                                DWORD(sizeof(mpr) / sizeof(mpr[0]))) ||
       !GetEnvironmentVariableW(L"EI_MAPPER_ENDURANCE_TEMP_MOB", mob,
                                DWORD(sizeof(mob) / sizeof(mob[0]))) ||
       GetFileAttributesW(mpr) == INVALID_FILE_ATTRIBUTES ||
       GetFileAttributesW(mob) == INVALID_FILE_ATTRIBUTES)
    {
        Log(L"DIRECT_LAUNCH disabled: staged MPR/MOB environment paths are missing or unreadable");
        return false;
    }
    BOOL mprSubstitution = FALSE;
    BOOL mobSubstitution = FALSE;
    if(!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, mpr, -1,
                            g_temporaryMpr, int(sizeof(g_temporaryMpr)), nullptr,
                            &mprSubstitution) ||
       !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, mob, -1,
                            g_temporaryMob, int(sizeof(g_temporaryMob)), nullptr,
                            &mobSubstitution) || mprSubstitution || mobSubstitution)
    {
        Log(L"DIRECT_LAUNCH disabled: staged paths cannot be represented in the game ANSI code page");
        return false;
    }
    return true;
}

bool QueueDirectLaunchOnCurrentThread()
{
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

int __fastcall MapLoadDetour(void* self, void*, const char* mprPath,
                             const char* mobPath, void* context)
{
    void* caller = _ReturnAddress();
    const auto* expectedNewGameReturn = reinterpret_cast<unsigned char*>(g_gameModule)
        + kNewGameMapLoadReturnRva;
    const bool redirect = g_directLaunchState == 3 &&
        caller == expectedNewGameReturn &&
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
                     L"DIRECT_LAUNCH redirecting normal New Game loader mpr=\"%s\" mob=\"%s\" context=0x%08lX",
                     mpr, mob, DWORD(reinterpret_cast<uintptr_t>(context)));
        Log(message);
    }

    const int result = g_originalMapLoad(self, selectedMpr, selectedMob, context);
    if(redirect)
    {
        wchar_t message[256]{};
        _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                     L"DIRECT_LAUNCH staged map loader returned result=%d", result);
        Log(message);
    }
    else if(g_directLaunchState == 1 && IsMainMenuMpr(mprPath))
        QueueDirectLaunchOnCurrentThread();
    return result;
}

LRESULT CALLBACK DirectLaunchMessageHook(int code, WPARAM wParam, LPARAM lParam)
{
    MSG* message = reinterpret_cast<MSG*>(lParam);
    if(code < 0 || wParam != PM_REMOVE || !message ||
       message->message != kDirectLaunchMessage)
        return CallNextHookEx(g_directLaunchHook, code, wParam, lParam);

    message->message = WM_NULL;
    const LRESULT next = CallNextHookEx(g_directLaunchHook, code, wParam, lParam);
    HHOOK hook = g_directLaunchHook;
    g_directLaunchHook = nullptr;
    if(hook)
        UnhookWindowsHookEx(hook);
    if(InterlockedCompareExchange(&g_directLaunchState, 3, 2) != 2)
        return next;

    Log(L"DIRECT_LAUNCH invoking the normal New Game initializer on the game thread");
    const auto newGame = reinterpret_cast<NewGameFn>(
        reinterpret_cast<unsigned char*>(g_gameModule) + kNewGameEntryRva);
    const ULONG_PTR result = newGame();
    const bool redirected = InterlockedCompareExchange(&g_directLaunchState, 6, 4) == 4;
    if(!redirected)
        InterlockedExchange(&g_directLaunchState, -1);
    wchar_t resultMessage[320]{};
    _snwprintf_s(resultMessage, sizeof(resultMessage) / sizeof(resultMessage[0]), _TRUNCATE,
                 redirected
                    ? L"DIRECT_LAUNCH New Game initializer completed result=0x%08lX staged_loader=observed"
                    : L"DIRECT_LAUNCH failed: New Game initializer completed result=0x%08lX without the expected staged loader call",
                 DWORD(result));
    Log(resultMessage);
    return next;
}

bool InstallMapLoadDetour()
{
    g_gameModule = GetModuleHandleW(nullptr);
    auto* game = reinterpret_cast<unsigned char*>(g_gameModule);
    if(!game || !ReadDirectLaunchPaths())
        return false;
    unsigned char* target = game + kMapLoadEntryRva;
    if(std::memcmp(target, kMapLoadEntryBytes, sizeof(kMapLoadEntryBytes)) != 0)
    {
        Log(L"DIRECT_LAUNCH disabled: validated map-loader entry bytes do not match");
        return false;
    }

    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, 16,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if(!trampoline)
    {
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
                 L"DIRECT_LAUNCH armed map_loader=0x%08lX new_game=0x%08lX mode=single-player",
                 DWORD(reinterpret_cast<uintptr_t>(target)),
                 DWORD(reinterpret_cast<uintptr_t>(game + kNewGameEntryRva)));
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
    Log(L"EI Mapper Endurance JMV direct single-player map launch initializing");
    if(!VerifyRuntime())
    {
        Log(L"DIRECT_LAUNCH disabled: game.exe SHA-256 does not match the validated JMV runtime");
        return false;
    }
    const int patched = InstallFileObservationHooks();
    const bool directLaunch = InstallMapLoadDetour();
    wchar_t message[256]{};
    _snwprintf_s(message, sizeof(message) / sizeof(message[0]), _TRUNCATE,
                 L"DIRECT_LAUNCH active imports=%d stack_capture=%s map_loader=%s trigger=normal-new-game",
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

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if(reason == DLL_PROCESS_ATTACH)
    {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
