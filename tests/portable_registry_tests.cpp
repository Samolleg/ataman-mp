#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../source/runtime/portable_registry.h"

int wmain(int argc, wchar_t** argv) {
    if(argc != 2) return 1;
    std::wstring owned;
    {
        PortableRegistry::Session session;
        SetEnvironmentVariableW(PortableRegistry::fileEnvironment, argv[1]);
        if(!session.prepare(argv[1])) return 2;
        owned = PortableRegistry::environment(PortableRegistry::keyEnvironment);
        HKEY original = nullptr;
        if(RegOpenCurrentUser(KEY_READ, &original)) return 3;
        bool active = false;
        if(!PortableRegistry::attach(active) || !active) return 4;
        HKEY settings = nullptr;
        if(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Gipat.Ru\\EI_Starter\\Settings", 0, KEY_ALL_ACCESS, &settings)) return 5;
        wchar_t mod[4096]; DWORD size = sizeof(mod), type = 0;
        if(RegQueryValueExW(settings, L"AddonPath", nullptr, &type, reinterpret_cast<BYTE*>(mod), &size) || type != REG_SZ) return 6;
        DWORD value = 123;
        if(RegSetValueExW(settings, L"AtamanProbe", 0, REG_DWORD, reinterpret_cast<BYTE*>(&value), sizeof(value))) return 7;
        RegCloseKey(settings);
        HKEY host = nullptr;
        const LONG opened = RegOpenKeyExW(original, L"Software\\Gipat.Ru\\EI_Starter\\Settings", 0, KEY_READ, &host);
        if(opened == ERROR_SUCCESS) {
            size = sizeof(value);
            const LONG found = RegQueryValueExW(host, L"AtamanProbe", nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &size);
            RegCloseKey(host);
            if(found != ERROR_FILE_NOT_FOUND) return 8;
        } else if(opened != ERROR_FILE_NOT_FOUND) return 9;
        if(RegOverridePredefKey(HKEY_CURRENT_USER, nullptr)) return 10;
        RegCloseKey(original);
    }
    HKEY leaked = nullptr;
    const LONG found = RegOpenKeyExW(HKEY_CURRENT_USER, owned.c_str(), 0, KEY_READ, &leaked);
    if(found != ERROR_FILE_NOT_FOUND) { if(leaked) RegCloseKey(leaked); return 11; }
    puts("PASS portable profile, isolated writes, host unchanged, cleanup");
    return 0;
}
