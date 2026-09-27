#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include "../portable_registry.h"

namespace {
bool stopRequested()
{
    MSG message{};
    while(PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if(message.message == WM_CLOSE || message.message == WM_QUIT)
            return true;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return false;
}
bool writeText(const wchar_t* path, const wchar_t* text)
{
    FILE* file = nullptr;
    if(_wfopen_s(&file, path, L"ab") != 0 || !file)
        return false;
    fwprintf(file, L"%s\r\n", text);
    fclose(file);
    return true;
}

bool writePid(const wchar_t* path, DWORD pid)
{
    FILE* file = nullptr;
    if(_wfopen_s(&file, path, L"wb") != 0 || !file)
        return false;
    // This file is a tiny cross-process protocol consumed by the Qt editor.
    // Keep it explicitly ASCII instead of relying on the CRT's wide-stream
    // encoding (which produced UTF-16LE without a BOM in alpha.29).
    fprintf(file, "%lu\n", static_cast<unsigned long>(pid));
    fclose(file);
    return true;
}

bool loadBridge(HANDLE process, const wchar_t* bridgePath,
                DWORD* remoteModule, const wchar_t* logPath)
{
    const SIZE_T pathBytes = (wcslen(bridgePath) + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, pathBytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if(!remotePath || !WriteProcessMemory(process, remotePath, bridgePath,
                                           pathBytes, nullptr))
    {
        writeText(logPath, L"RUNTIME_HOST injection failed while staging bridge path");
        return false;
    }
    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE loadThread = CreateRemoteThread(process, nullptr, 0, loadLibrary,
                                            remotePath, 0, nullptr);
    if(!loadThread || WaitForSingleObject(loadThread, 15000) != WAIT_OBJECT_0)
    {
        writeText(logPath, L"RUNTIME_HOST injection failed in remote LoadLibraryW");
        if(loadThread) CloseHandle(loadThread);
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }
    DWORD loadedModule = 0;
    GetExitCodeThread(loadThread, &loadedModule);
    CloseHandle(loadThread);
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    if(!loadedModule)
    {
        writeText(logPath, L"RUNTIME_HOST bridge LoadLibraryW returned null");
        return false;
    }
    *remoteModule = loadedModule;
    return true;
}

bool invokeBridgeExport(HANDLE process, DWORD remoteModule,
                        const wchar_t* bridgePath, const char* exportName,
                        const wchar_t* logPath)
{
    wchar_t phaseMessage[256]{};
    _snwprintf_s(phaseMessage, sizeof(phaseMessage) / sizeof(phaseMessage[0]),
                 _TRUNCATE, L"RUNTIME_HOST invoking bridge export %S", exportName);
    writeText(logPath, phaseMessage);
    HMODULE localModule = LoadLibraryExW(bridgePath, nullptr,
                                         DONT_RESOLVE_DLL_REFERENCES);
    FARPROC localExport = localModule ? GetProcAddress(localModule, exportName) : nullptr;
    if(!localModule || !localExport)
    {
        writeText(logPath, L"RUNTIME_HOST bridge initialization export is missing");
        if(localModule) FreeLibrary(localModule);
        return false;
    }
    const uintptr_t exportRva = reinterpret_cast<uintptr_t>(localExport)
        - reinterpret_cast<uintptr_t>(localModule);
    FreeLibrary(localModule);
    const auto remoteExport = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        uintptr_t(remoteModule) + exportRva);
    HANDLE initThread = CreateRemoteThread(process, nullptr, 0, remoteExport,
                                           nullptr, 0, nullptr);
    if(!initThread)
    {
        const DWORD error = GetLastError();
        _snwprintf_s(phaseMessage, sizeof(phaseMessage) / sizeof(phaseMessage[0]),
                     _TRUNCATE,
                     L"RUNTIME_HOST could not create bridge export thread %S error=%lu",
                     exportName, static_cast<unsigned long>(error));
        writeText(logPath, phaseMessage);
        return false;
    }
    const DWORD started = GetTickCount();
    const DWORD waitResult = WaitForSingleObject(initThread, 15000);
    if(waitResult != WAIT_OBJECT_0)
    {
        const DWORD elapsed = GetTickCount() - started;
        const DWORD error = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
        _snwprintf_s(phaseMessage, sizeof(phaseMessage) / sizeof(phaseMessage[0]),
                     _TRUNCATE,
                     L"RUNTIME_HOST bridge export %S did not complete wait=0x%08lX elapsed_ms=%lu error=%lu",
                     exportName, static_cast<unsigned long>(waitResult),
                     static_cast<unsigned long>(elapsed),
                     static_cast<unsigned long>(error));
        writeText(logPath, phaseMessage);
        CloseHandle(initThread);
        return false;
    }
    DWORD initialized = 0;
    GetExitCodeThread(initThread, &initialized);
    CloseHandle(initThread);
    if(initialized != 1)
    {
        _snwprintf_s(phaseMessage, sizeof(phaseMessage) / sizeof(phaseMessage[0]),
                     _TRUNCATE,
                     L"RUNTIME_HOST bridge export %S rejected the runtime fingerprint or hook signatures result=%lu",
                     exportName, static_cast<unsigned long>(initialized));
        writeText(logPath, phaseMessage);
        return false;
    }
    _snwprintf_s(phaseMessage, sizeof(phaseMessage) / sizeof(phaseMessage[0]),
                 _TRUNCATE, L"RUNTIME_HOST bridge export %S completed result=1",
                 exportName);
    writeText(logPath, phaseMessage);
    return true;
}

bool mainModuleBase(DWORD pid, uintptr_t* base)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                               pid);
    if(snapshot == INVALID_HANDLE_VALUE)
        return false;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const bool ok = Module32FirstW(snapshot, &entry) != FALSE;
    if(ok)
        *base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
    CloseHandle(snapshot);
    return ok;
}

bool executableFingerprint(HANDLE process, uintptr_t imageBase, uint64_t* result)
{
    IMAGE_DOS_HEADER dos{};
    SIZE_T read = 0;
    if(!ReadProcessMemory(process, reinterpret_cast<void*>(imageBase), &dos,
                          sizeof(dos), &read) || read != sizeof(dos)
       || dos.e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    IMAGE_NT_HEADERS32 nt{};
    const uintptr_t ntAddress = imageBase + uintptr_t(dos.e_lfanew);
    if(!ReadProcessMemory(process, reinterpret_cast<void*>(ntAddress), &nt,
                          sizeof(nt), &read) || read != sizeof(nt)
       || nt.Signature != IMAGE_NT_SIGNATURE)
        return false;
    const WORD sectionCount = nt.FileHeader.NumberOfSections;
    if(!sectionCount || sectionCount > 96)
        return false;
    std::vector<IMAGE_SECTION_HEADER> sections(sectionCount);
    const uintptr_t sectionsAddress = ntAddress + sizeof(DWORD)
        + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    const SIZE_T sectionBytes = sections.size() * sizeof(IMAGE_SECTION_HEADER);
    if(!ReadProcessMemory(process, reinterpret_cast<void*>(sectionsAddress),
                          sections.data(), sectionBytes, &read)
       || read != sectionBytes)
        return false;

    uint64_t hash = 1469598103934665603ull;
    std::vector<unsigned char> buffer(64 * 1024);
    bool foundExecutable = false;
    for(const IMAGE_SECTION_HEADER& section : sections)
    {
        if(!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;
        foundExecutable = true;
        SIZE_T remaining = section.Misc.VirtualSize;
        uintptr_t address = imageBase + section.VirtualAddress;
        while(remaining)
        {
            const SIZE_T chunk = remaining < buffer.size() ? remaining : buffer.size();
            if(!ReadProcessMemory(process, reinterpret_cast<void*>(address),
                                  buffer.data(), chunk, &read) || read != chunk)
                return false;
            for(SIZE_T i = 0; i < chunk; ++i)
            {
                hash ^= buffer[i];
                hash *= 1099511628211ull;
            }
            address += chunk;
            remaining -= chunk;
        }
    }
    if(!foundExecutable)
        return false;
    *result = hash;
    return true;
}

bool waitForJmvPatchQuiescence(HANDLE process, uintptr_t imageBase,
                               uint64_t initialFingerprint,
                               const wchar_t* logPath)
{
    uint64_t previous = initialFingerprint;
    bool observedMutation = false;
    const DWORD started = GetTickCount();
    DWORD lastMutation = started;
    while(DWORD(GetTickCount() - started) < 30000)
    {
        if(stopRequested())
            return false;
        if(WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
        {
            writeText(logPath, L"RUNTIME_HOST JMV process exited during runtime patch initialization");
            return false;
        }
        uint64_t current = 0;
        if(!executableFingerprint(process, imageBase, &current))
        {
            writeText(logPath, L"RUNTIME_HOST could not observe JMV executable patch state");
            return false;
        }
        if(current != previous)
        {
            previous = current;
            lastMutation = GetTickCount();
            if(current != initialFingerprint)
                observedMutation = true;
        }
        if(observedMutation && DWORD(GetTickCount() - lastMutation) >= 150)
        {
            writeText(logPath, L"RUNTIME_HOST observed completed JMV executable patch phase");
            return true;
        }
        Sleep(10);
    }
    writeText(logPath, L"RUNTIME_HOST timed out waiting for the JMV executable patch phase");
    return false;
}
}

int wmain(int argc, wchar_t** argv)
{
    if(argc != 7)
        return 64;
    const wchar_t* gamePath = argv[1];
    const wchar_t* bridgePath = argv[2];
    const wchar_t* workingDirectory = argv[3];
    const wchar_t* pidPath = argv[4];
    const wchar_t* logPath = argv[5];
    const bool deferredJmvActivation = _wcsicmp(argv[6], L"jmv") == 0;
    MSG initialMessage{};
    PeekMessageW(&initialMessage, nullptr, 0, 0, PM_NOREMOVE);

    // Do not inherit an unrelated supervisor's private registry branch.
    SetEnvironmentVariableW(PortableRegistry::keyEnvironment, nullptr);
    PortableRegistry::Session portable;
    const std::wstring profile = PortableRegistry::environment(PortableRegistry::fileEnvironment);
    if(!profile.empty() && (!deferredJmvActivation || !portable.prepare(profile.c_str())))
    {
        writeText(logPath, L"PORTABLE_PROFILE_V1 preparation failed before process creation");
        return 69;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                         &limits, sizeof(limits)))
        return 65;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + std::wstring(gamePath) + L"\"";
    if(!CreateProcessW(gamePath, &command[0], nullptr, nullptr, FALSE,
                       CREATE_SUSPENDED, nullptr, workingDirectory,
                       &startup, &process))
    {
        CloseHandle(job);
        return 66;
    }
    DWORD remoteBridge = 0;
    if(!AssignProcessToJobObject(job, process.hProcess)
       || !writePid(pidPath, process.dwProcessId)
       || !loadBridge(process.hProcess, bridgePath, &remoteBridge, logPath)
       || !invokeBridgeExport(process.hProcess, remoteBridge, bridgePath,
                              "EndurancePrepareRuntime", logPath))
    {
        TerminateProcess(process.hProcess, 67);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        return 67;
    }

    bool activated = false;
    if(deferredJmvActivation)
    {
        uintptr_t imageBase = 0;
        uint64_t initialFingerprint = 0;
        const bool quiescent = mainModuleBase(process.dwProcessId, &imageBase)
            && executableFingerprint(process.hProcess, imageBase, &initialFingerprint)
            && ResumeThread(process.hThread) != DWORD(-1)
            && waitForJmvPatchQuiescence(process.hProcess, imageBase,
                                         initialFingerprint, logPath);
        if(quiescent)
        {
            // The JMV main thread must remain runnable here. Suspending it can
            // freeze the activation thread behind the Windows loader lock even
            // after executable patch bytes have become stable. The detours are
            // signature guarded and target map-loading code that is not reached
            // until the later scripted transition.
            writeText(logPath,
                L"RUNTIME_HOST activating with JMV main thread runnable after patch quiescence");
            activated = invokeBridgeExport(process.hProcess, remoteBridge, bridgePath,
                                           "EnduranceActivateRuntime", logPath);
        }
    }
    else
    {
        activated = invokeBridgeExport(process.hProcess, remoteBridge, bridgePath,
                                       "EnduranceActivateRuntime", logPath);
        if(activated)
            ResumeThread(process.hThread);
    }
    if(!activated)
    {
        writeText(logPath, L"RUNTIME_HOST could not activate the Endurance runtime bridge safely");
        TerminateProcess(process.hProcess, 68);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        return 68;
    }
    writeText(logPath, deferredJmvActivation
        ? L"RUNTIME_HOST bridge activated after JMV patch quiescence"
        : L"RUNTIME_HOST bridge activated before Base process resume");
    CloseHandle(process.hThread);
    // QProcess::terminate posts WM_CLOSE to the supervisor thread. Pump it so
    // Stop Test can finish the child and release the private profile normally.
    while(WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0)
    {
        if(stopRequested())
        {
            TerminateProcess(process.hProcess, 0);
            WaitForSingleObject(process.hProcess, INFINITE);
            break;
        }
        MsgWaitForMultipleObjects(1, &process.hProcess, FALSE, 100, QS_ALLINPUT);
    }
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return int(exitCode);
}
