#pragma once

#include <string>

#include "GameIni.h"

// A mod declares its own name and version here:
//
//   [Launcher]
//   ModName=FMRM
//   ModVer=2.1.5.4
//
// Used for the splash line and the reported mod, so both follow the mod's
// releases rather than whatever this dll was built against.
namespace ModInfo
{
    // False unless both keys are set; the caller then falls back to its own
    // default.
    inline bool FromLauncher(std::string& name, std::string& version)
    {
        name = GameIni::Read("Launcher", "ModName");
        version = GameIni::Read("Launcher", "ModVer");

        return !name.empty() && !version.empty();
    }
}
