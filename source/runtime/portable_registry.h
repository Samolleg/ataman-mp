#pragma once
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

namespace PortableRegistry {
static const wchar_t* const keyEnvironment = L"EI_MAPPER_PORTABLE_REGISTRY_KEY";
static const wchar_t* const fileEnvironment = L"EI_MAPPER_PORTABLE_PROFILE";
static const wchar_t* const prefix = L"Software\\EIMapperAtaman\\TestRuns\\";
inline std::wstring environment(const wchar_t* name) {
    wchar_t value[32768]{};
    const DWORD size = GetEnvironmentVariableW(name, value, 32768);
    return size && size < 32768 ? value : L"";
}
// Owned by the supervisor, not the editor or the game. Never opens/replaces an
// existing branch. Volatile keys also disappear after an abnormal OS shutdown.
class Session {
    std::wstring path;
    HKEY root = nullptr;
public:
    ~Session() {
        if(root) { RegDeleteTreeW(root, nullptr); RegCloseKey(root); }
        if(!path.empty()) RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str());
    }
    bool prepare(const wchar_t* filePath) {
        FILE* file = nullptr;
        if(_wfopen_s(&file, filePath, L"rb") || !file) return false;
        struct Close { FILE* f; ~Close() { fclose(f); } } close{file};
        DWORD header[2];
        if(fread(header, sizeof(header), 1, file) != 1 || header[0] != 0x31525045
           || !header[1] || header[1] > 10000) return false;
        LARGE_INTEGER ticks; QueryPerformanceCounter(&ticks);
        wchar_t suffix[96];
        swprintf_s(suffix, L"%lu-%llu", GetCurrentProcessId(), static_cast<unsigned long long>(ticks.QuadPart));
        const std::wstring candidate = std::wstring(prefix) + suffix;
        DWORD disposition = 0;
        if(RegCreateKeyExW(HKEY_CURRENT_USER, candidate.c_str(), 0, nullptr,
            REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr, &root, &disposition) != ERROR_SUCCESS) return false;
        if(disposition != REG_CREATED_NEW_KEY) { RegCloseKey(root); root = nullptr; return false; }
        path = candidate;
        for(DWORD i = 0; i < header[1]; ++i) {
            DWORD fields[4];
            if(fread(fields, sizeof(fields), 1, file) != 1 || fields[0] < 2 || fields[1] < 2
               || fields[0] > 2050 || fields[1] > 2050 || fields[0]%2 || fields[1]%2
               || fields[3] > 1024*1024) return false;
            std::vector<wchar_t> section(fields[0]/2), name(fields[1]/2);
            std::vector<BYTE> data(fields[3]);
            if(fread(section.data(), 1, fields[0], file) != fields[0]
               || fread(name.data(), 1, fields[1], file) != fields[1]
               || fread(data.data(), 1, fields[3], file) != fields[3]
               || section.back() || name.back()) return false;
            const std::wstring suffixSection(section.data());
            if(suffixSection.size()+1 != section.size() || wcslen(name.data())+1 != name.size()
               || suffixSection.find(L"..") != std::wstring::npos
               || suffixSection.find(L'/') != std::wstring::npos
               || !(suffixSection.empty() || _wcsicmp(suffixSection.c_str(), L"Settings") == 0
                    || _wcsnicmp(suffixSection.c_str(), L"Settings\\", 9) == 0
                    || _wcsicmp(suffixSection.c_str(), L"EvilIslands") == 0
                    || _wcsnicmp(suffixSection.c_str(), L"EvilIslands\\", 12) == 0)) return false;
            if(fields[2] != REG_NONE && fields[2] != REG_SZ && fields[2] != REG_EXPAND_SZ
               && fields[2] != REG_BINARY && fields[2] != REG_DWORD
               && fields[2] != REG_MULTI_SZ && fields[2] != REG_QWORD) return false;
            std::wstring target = L"Software\\Gipat.Ru\\EI_Starter";
            if(!suffixSection.empty()) target += L"\\" + suffixSection;
            HKEY key = nullptr;
            if(RegCreateKeyExW(root, target.c_str(), 0, nullptr, REG_OPTION_VOLATILE,
                KEY_ALL_ACCESS, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
            const LSTATUS result = RegSetValueExW(key, name.data(), 0, fields[2], data.data(), fields[3]);
            RegCloseKey(key);
            if(result != ERROR_SUCCESS) return false;
            // Some original engine modules still use the Nival registry namespace.
            if(_wcsnicmp(suffixSection.c_str(), L"EvilIslands\\", 12) == 0) {
                target = L"Software\\Nival Interactive\\" + suffixSection;
                if(RegCreateKeyExW(root, target.c_str(), 0, nullptr, REG_OPTION_VOLATILE,
                    KEY_ALL_ACCESS, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
                const LSTATUS copied = RegSetValueExW(key, name.data(), 0, fields[2], data.data(), fields[3]);
                RegCloseKey(key);
                if(copied != ERROR_SUCCESS) return false;
            }
        }
        if(fgetc(file) != EOF) return false;
        return SetEnvironmentVariableW(keyEnvironment, path.c_str()) != FALSE;
    }
};
inline bool attach(bool& active) {
    const std::wstring profile = environment(fileEnvironment);
    const std::wstring path = environment(keyEnvironment);
    active = false;
    if(profile.empty()) return path.empty();
    if(path.compare(0, wcslen(prefix), prefix) != 0 || path.size() <= wcslen(prefix)
       || path.find(L'\\', wcslen(prefix)) != std::wstring::npos) return false;
    HKEY key = nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_ALL_ACCESS, &key) != ERROR_SUCCESS) return false;
    // Windows routes every HKCU API (including dynamically resolved functions)
    // for this process to the run-owned tree. No shared profile lease or restore.
    const LSTATUS status = RegOverridePredefKey(HKEY_CURRENT_USER, key);
    RegCloseKey(key);
    active = status == ERROR_SUCCESS;
    return active;
}
}
