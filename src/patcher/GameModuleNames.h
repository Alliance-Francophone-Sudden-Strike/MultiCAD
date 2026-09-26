#pragma once

#include <string>

#include "types.h"
#include "PatchTypes.h"
#include "ScreenConfig.h"

// The game names its own two modules in the ini it boots from, and binds them by name:
//
//   [StartUp]
//   Module1=Menu_Dll.dll     ; the menu
//   Module2=Game_Dll.dll     ; the game
//
// Reading the same keys means MultiCAD doesn't depend on a dll being called "menu*" or
// "game*". A replacement front end can be called anything - RUI.dll, n2Menu_Dll.dll -
// and is still found, both on disk and when it loads. Empty when the key is missing;
// the hardcoded name parts stay as the fallback.
namespace GameModules
{
    // The ini is ANSI, and so are the module names in it.
    inline std::wstring Widen(const char* text, const int length)
    {
        if (length <= 0)
            return {};

        const int wide = MultiByteToWideChar(CP_ACP, 0, text, length, nullptr, 0);
        if (wide <= 0)
            return {};

        std::wstring out(static_cast<size_t>(wide), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text, length, out.data(), wide);

        return out;
    }

    inline std::wstring GetConfiguredName(const DllType type)
    {
        const std::string iniPath = Screen::GetIniPath();
        if (iniPath.empty())
            return {};

        const char* key = IsMenuDll(type) ? "Module1" : "Module2";

        char buffer[MAX_PATH] = { 0 };
        DWORD len = GetPrivateProfileStringA("StartUp", key, "", buffer, sizeof(buffer), iniPath.c_str());

        // GetPrivateProfileString trims blanks, but not a trailing inline comment marker.
        while (len > 0 && (buffer[len - 1] == ' ' || buffer[len - 1] == '\t'))
            --len;

        if (len == 0)
            return {};

        return Widen(buffer, static_cast<int>(len));
    }

    // HS2Engine's hybrid game dll hosts the original rather than being it: its ini
    // section names the original, which the host loads itself,
    //
    //   [HS2Engine]
    //   OriginalDll=Game_Dll.orig.dll
    //
    // so the module the game loads as Module2 is not the one to patch. True for a game
    // module whose file is not that original; the original, loaded next, still matches
    // the "game" name part and is identified and patched as usual.
    inline bool IsHs2EngineHost(const std::wstring& modulePath)
    {
        const std::string iniPath = Screen::GetIniPath();
        if (iniPath.empty())
            return false;

        char buffer[MAX_PATH] = { 0 };
        const DWORD len = GetPrivateProfileStringA("HS2Engine", "OriginalDll", "", buffer, sizeof(buffer), iniPath.c_str());
        if (len == 0)
            return false;

        const auto fileName = [](std::wstring_view path) {
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
        };
        const std::wstring original = Widen(buffer, static_cast<int>(len));
        const std::wstring_view module = fileName(modulePath);
        const std::wstring_view wanted = fileName(original);
        return module.size() != wanted.size() || _wcsnicmp(module.data(), wanted.data(), module.size()) != 0;
    }
}
