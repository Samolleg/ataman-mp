#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace {
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
    fwprintf(file, L"%lu\n", pid);
    fclose(file);
    return true;
}

bool injectAndInitialize(HANDLE process, const wchar_t* bridgePath,
                         const char* exportName, const wchar_t* logPath)
{
    const SIZE_T pathBytes = (wcslen(bridgePath) + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, pathBytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if(!remotePath || !WriteProcessMemory(process, remotePath, bridgePath,
                                           pathBytes, nullptr))
    {
        writeText(logPath, L"BASE_PROBE injection failed while staging bridge path");
        return false;
    }
    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE loadThread = CreateRemoteThread(process, nullptr, 0, loadLibrary,
                                            remotePath, 0, nullptr);
    if(!loadThread || WaitForSingleObject(loadThread, 15000) != WAIT_OBJECT_0)
    {
        writeText(logPath, L"BASE_PROBE injection failed in remote LoadLibraryW");
        if(loadThread) CloseHandle(loadThread);
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }
    DWORD remoteModule = 0;
    GetExitCodeThread(loadThread, &remoteModule);
    CloseHandle(loadThread);
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    if(!remoteModule)
    {
        writeText(logPath, L"BASE_PROBE bridge LoadLibraryW returned null");
        return false;
    }

    HMODULE localModule = LoadLibraryExW(bridgePath, nullptr,
                                         DONT_RESOLVE_DLL_REFERENCES);
    FARPROC localExport = localModule ? GetProcAddress(localModule, exportName) : nullptr;
    if(!localModule || !localExport)
    {
        writeText(logPath, L"BASE_PROBE bridge initialization export is missing");
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
    if(!initThread || WaitForSingleObject(initThread, 15000) != WAIT_OBJECT_0)
    {
        writeText(logPath, L"BASE_PROBE bridge initialization call failed");
        if(initThread) CloseHandle(initThread);
        return false;
    }
    DWORD initialized = 0;
    GetExitCodeThread(initThread, &initialized);
    CloseHandle(initThread);
    if(initialized != 1)
    {
        writeText(logPath, L"BASE_PROBE bridge rejected the runtime fingerprint or hook signatures");
        return false;
    }
    writeText(logPath, L"BASE_PROBE bridge injected and initialized before process resume");
    return true;
}
}

int wmain(int argc, wchar_t** argv)
{
    if(argc != 6)
        return 64;
    const wchar_t* gamePath = argv[1];
    const wchar_t* bridgePath = argv[2];
    const wchar_t* workingDirectory = argv[3];
    const wchar_t* pidPath = argv[4];
    const wchar_t* logPath = argv[5];

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
    if(!AssignProcessToJobObject(job, process.hProcess)
       || !writePid(pidPath, process.dwProcessId)
       || !injectAndInitialize(process.hProcess, bridgePath,
                               "EnduranceInitialize", logPath))
    {
        TerminateProcess(process.hProcess, 67);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        return 67;
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return int(exitCode);
}
