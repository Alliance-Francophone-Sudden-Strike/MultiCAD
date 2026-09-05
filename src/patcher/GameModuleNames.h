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
}
