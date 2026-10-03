// Runs under Wine. Windows 7 reports a dll load before the loader binds its
// import table, so ImportHooks must not take the unbound slot for the original.
#include <windows.h>
#include <cstdint>
#include <cstring>

#include "ImportHooks.h"

#undef NDEBUG
#include <cassert>

namespace
{
    BOOL WINAPI fakeClipCursor(const RECT*) { return TRUE; }

    // Minimal image: headers, no sections, user32.dll!ClipCursor imported by name.
    constexpr DWORD kDescriptor = 0x200, kNames = 0x300, kSlots = 0x340, kDllName = 0x380, kByName = 0x3A0;

    uint8_t* MakeImage()
    {
        auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        assert(image);

        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x40;

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR_MAGIC;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = { kDescriptor, 2 * sizeof(IMAGE_IMPORT_DESCRIPTOR) };

        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + kDescriptor);
        descriptor->OriginalFirstThunk = kNames;
        descriptor->Name = kDllName;
        descriptor->FirstThunk = kSlots;

        // Unbound, as the loader leaves both tables before it snaps the imports.
        reinterpret_cast<DWORD*>(image + kNames)[0] = kByName;
        reinterpret_cast<DWORD*>(image + kSlots)[0] = kByName;

        std::strcpy(reinterpret_cast<char*>(image + kDllName), "USER32.dll");
        std::strcpy(reinterpret_cast<char*>(image + kByName + 2), "ClipCursor");

        return image;
    }
}

int main()
{
    uint8_t* const image = MakeImage();
    const auto base = reinterpret_cast<uintptr_t>(image);
    auto* const slot = reinterpret_cast<void**>(image + kSlots);
    void* const hook = reinterpret_cast<void*>(&fakeClipCursor);
    void* const real = reinterpret_cast<void*>(GetProcAddress(LoadLibraryA("user32.dll"), "ClipCursor"));
    assert(real);

    // Before binding: nothing to forward to, and the slot is left to the loader.
    assert(ImportHooks::Redirect(base, "user32.dll", "ClipCursor", hook) == nullptr);
    assert(*slot == reinterpret_cast<void*>(uintptr_t{ kByName }));

    // Bound: the real export is the original and the hook takes the slot.
    *slot = real;
    assert(ImportHooks::Redirect(base, "user32.dll", "ClipCursor", hook) == real);
    assert(*slot == hook);

    VirtualFree(image, 0, MEM_RELEASE);
    return 0;
}
