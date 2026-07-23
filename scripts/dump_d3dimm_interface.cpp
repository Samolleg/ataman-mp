#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <vector>

namespace {

bool readable(DWORD protection)
{
    if(protection & (PAGE_GUARD | PAGE_NOACCESS))
        return false;
    const DWORD base = protection & 0xffu;
    return base == PAGE_READONLY || base == PAGE_READWRITE
        || base == PAGE_WRITECOPY || base == PAGE_EXECUTE
        || base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE
        || base == PAGE_EXECUTE_WRITECOPY;
}

std::uint32_t relativeTo(HMODULE module, const void* address)
{
    return static_cast<std::uint32_t>(
        reinterpret_cast<const std::uint8_t*>(address)
        - reinterpret_cast<const std::uint8_t*>(module));
}

void* callInterface(FARPROC entry)
{
    using GetInterface = void* (__cdecl*)();
    __try
    {
        return reinterpret_cast<GetInterface>(entry)();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        std::fprintf(stderr, "GetD3DInterface raised 0x%08lX\n",
                     GetExceptionCode());
        return nullptr;
    }
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if(argc != 3 && argc != 4)
    {
        std::fwprintf(stderr,
                      L"usage: dump_d3dimm_interface DLL OUTPUT [EXPORT]\n");
        return 2;
    }

    wchar_t directory[MAX_PATH]{};
    if(!GetFullPathNameW(argv[1], MAX_PATH, directory, nullptr))
        return 3;
    wchar_t* slash = wcsrchr(directory, L'\\');
    if(slash)
    {
        *slash = L'\0';
        SetDllDirectoryW(directory);
    }

    HMODULE module = LoadLibraryW(argv[1]);
    if(!module)
    {
        std::fwprintf(stderr, L"LoadLibrary failed: %lu\n", GetLastError());
        return 4;
    }

    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        reinterpret_cast<const std::uint8_t*>(module) + dos->e_lfanew);
    const std::uint32_t imageSize = nt->OptionalHeader.SizeOfImage;
    std::vector<std::uint8_t> image(imageSize, 0);
    std::uint8_t* cursor = reinterpret_cast<std::uint8_t*>(module);
    std::uint8_t* const end = cursor + imageSize;
    while(cursor < end)
    {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(cursor, &info, sizeof(info)))
            break;
        std::uint8_t* regionEnd = static_cast<std::uint8_t*>(info.BaseAddress)
            + info.RegionSize;
        if(regionEnd > end)
            regionEnd = end;
        if(info.State == MEM_COMMIT && readable(info.Protect))
            std::memcpy(image.data() + (cursor - reinterpret_cast<std::uint8_t*>(module)),
                        cursor, regionEnd - cursor);
        cursor = regionEnd;
    }

    FILE* output = nullptr;
    if(_wfopen_s(&output, argv[2], L"wb") || !output)
        return 5;
    const bool wrote = std::fwrite(image.data(), 1, image.size(), output) == image.size();
    std::fclose(output);
    if(!wrote)
        return 6;

    char exportName[128] = "GetD3DInterface";
    if(argc == 4)
    {
        const int converted = WideCharToMultiByte(CP_ACP, 0, argv[3], -1,
                                                   exportName,
                                                   sizeof(exportName),
                                                   nullptr, nullptr);
        if(converted <= 0)
            return 7;
    }
    FARPROC entry = GetProcAddress(module, exportName);
    std::printf("module=%p image_size=%u export=%p export_rva=0x%08X\n",
                module, imageSize, entry, entry ? relativeTo(module, entry) : 0u);
    if(entry)
    {
        void* object = callInterface(entry);
        std::printf("object=%p\n", object);
        if(object)
        {
            void** vtable = *reinterpret_cast<void***>(object);
            std::printf("vtable=%p vtable_rva=0x%08X\n",
                        vtable, relativeTo(module, vtable));
            for(unsigned index = 0; index != 32; ++index)
                std::printf("slot_%02X=%p rva=0x%08X\n", index * 4,
                            vtable[index], relativeTo(module, vtable[index]));
        }
    }

    FreeLibrary(module);
    return 0;
}
