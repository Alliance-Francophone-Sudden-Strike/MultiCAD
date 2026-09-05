#pragma once

#include <string>
#include <string_view>

#include "types.h"
#include "PatchTypes.h"
#include "ScreenConfig.h"

// Optional per-dll profile override, read from the same ini as [Game] Resolution:
//
//   [Game]
//   GameProfile=SS_2
//   MenuProfile=SS_2
//
// It forces a known profile onto a dll whose hash MultiCAD doesn't recognise, which is
// the only way to patch a custom build. Patches are 5-byte jumps written at fixed RVAs
// into bytes we haven't verified, so a wrong profile crashes on the first patched call.
// Opt-in and at your own risk - see the README.
namespace ProfileOverride
{
    struct VersionName
    {
        std::string_view name;
        GameVersion      version;
    };

    // Only the versions ProfileFactory::create actually resolves. The rest of the
    // GameVersion enum has no profile behind it, so naming one here would do nothing.
    inline constexpr VersionName kVersionNames[] = {
        { "SS_V1_0",            GameVersion::SS_V1_0 },
        { "SS_V1_2",            GameVersion::SS_V1_2 },
        { "SS_GOLD_RU",         GameVersion::SS_GOLD_RU },
        { "SS_GOLD_EN",         GameVersion::SS_GOLD_EN },
        { "SS_GOLD_DE",         GameVersion::SS_GOLD_DE },
        { "SS_GOLD_FR",         GameVersion::SS_GOLD_FR },
        { "SS_2",               GameVersion::SS_2 },
        { "SS_RW_V2_3",         GameVersion::SS_RW_V2_3 },
        { "SS_RW_V2_4",         GameVersion::SS_RW_V2_4 },
        { "SS_BLACK_GOLD",      GameVersion::SS_BLACK_GOLD },
        { "SS_EUROPE_2015",     GameVersion::SS_EUROPE_2015 },
        { "SS_BLACK_SEA",       GameVersion::SS_BLACK_SEA },
        { "SS_HD_V1_1_RU",      GameVersion::SS_HD_V1_1_RU },
        { "SS_HD_V1_1_EN",      GameVersion::SS_HD_V1_1_EN },
        { "SS_GOLD_HD_1_2_RU",  GameVersion::SS_GOLD_HD_1_2_RU },
        { "SS_GOLD_HD_1_2_INT", GameVersion::SS_GOLD_HD_1_2_INT },
        { "HS_2",               GameVersion::HS_2 },
    };

    // "SS_2" -> GameVersion::SS_2. Case-insensitive, surrounding blanks ignored.
    // UNKNOWN for anything not in the table, which callers treat as "no override".
    inline GameVersion ParseGameVersionName(std::string_view name)
    {
        const auto isBlank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };

        while (!name.empty() && isBlank(name.front()))
            name.remove_prefix(1);
        while (!name.empty() && isBlank(name.back()))
            name.remove_suffix(1);

        if (name.empty())
            return GameVersion::UNKNOWN;

        for (const VersionName& entry : kVersionNames)
        {
            if (entry.name.size() != name.size())
                continue;

            size_t i = 0;
            for (; i < name.size(); ++i)
            {
                char a = name[i];
                char b = entry.name[i];
                if (a >= 'a' && a <= 'z') a = static_cast<char>(a - ('a' - 'A'));
                if (b >= 'a' && b <= 'z') b = static_cast<char>(b - ('a' - 'A'));
                if (a != b)
                    break;
            }

            if (i == name.size())
                return entry.version;
        }

        return GameVersion::UNKNOWN;
    }

    // UNKNOWN when the key is missing, empty or names a version with no profile.
    inline GameVersion GetProfileOverride(const DllType type)
    {
        const std::string iniPath = Screen::GetIniPath();
        if (iniPath.empty())
            return GameVersion::UNKNOWN;

        const char* key = IsMenuDll(type) ? "MenuProfile" : "GameProfile";

        char buffer[64] = { 0 };
        const DWORD len = GetPrivateProfileStringA("Game", key, "", buffer, sizeof(buffer), iniPath.c_str());
        if (len == 0)
            return GameVersion::UNKNOWN;

        return ParseGameVersionName(std::string_view(buffer, len));
    }
}
